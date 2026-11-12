#include "WebRemote.hpp"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QHostAddress>

#include "moc_WebRemote.cpp"

namespace
{
  int const max_backlog = 150;              // lines kept per pane for new clients
  int const max_request = 64 * 1024;        // ignore anything bigger
  int const lockout_failures = 5;           // wrong PINs allowed ...
  int const lockout_window_secs = 60;       // ... in this many seconds
  int const lockout_secs = 60;              // then refuse for this long

  QByteArray status_text (int status)
  {
    switch (status)
      {
      case 200: return "OK";
      case 204: return "No Content";
      case 400: return "Bad Request";
      case 401: return "Unauthorized";
      case 404: return "Not Found";
      case 405: return "Method Not Allowed";
      case 413: return "Payload Too Large";
      case 429: return "Too Many Requests";
      case 503: return "Service Unavailable";
      default: return "Error";
      }
  }

  QByteArray to_json (QJsonObject const& o)
  {
    return QJsonDocument {o}.toJson (QJsonDocument::Compact);
  }
}

struct WebRemote::Request
{
  QByteArray method;
  QString path;
  QUrlQuery query;
  QByteArray body;
};

WebRemote::WebRemote (QObject * parent)
  : QObject {parent}
  , server_ {new QTcpServer {this}}
  , state_timer_ {new QTimer {this}}
  , heartbeat_timer_ {new QTimer {this}}
{
  connect (server_, &QTcpServer::newConnection, this, &WebRemote::onNewConnection);
  state_timer_->setInterval (400);
  connect (state_timer_, &QTimer::timeout, this, &WebRemote::tick);
  heartbeat_timer_->setInterval (15000);
  connect (heartbeat_timer_, &QTimer::timeout, [this] {
      for (auto const& s : streams_)
        {
          if (s) s->write (": keep-alive\n\n");
        }
    });
}

WebRemote::~WebRemote ()
{
  stop ();
}

bool WebRemote::start (quint16 port, QString const& pin)
{
  pin_ = pin.trimmed ();
  error_.clear ();
  if (pin_.isEmpty ())
    {
      stop ();
      error_ = tr ("a PIN is required");
      return false;
    }
  if (server_->isListening () && server_->serverPort () == port) return true;
  stop ();
  if (!server_->listen (QHostAddress::Any, port))
    {
      error_ = server_->errorString ();
      return false;
    }
  state_timer_->start ();
  heartbeat_timer_->start ();
  return true;
}

void WebRemote::stop ()
{
  state_timer_->stop ();
  heartbeat_timer_->stop ();
  for (auto const& s : streams_)
    {
      if (s) s->disconnectFromHost ();
    }
  streams_.clear ();
  if (server_->isListening ()) server_->close ();
}

bool WebRemote::isListening () const
{
  return server_->isListening ();
}

QStringList WebRemote::localUrls (quint16 port)
{
  QStringList urls;
  for (auto const& iface : QNetworkInterface::allInterfaces ())
    {
      auto const flags = iface.flags ();
      if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
          || (flags & QNetworkInterface::IsLoopBack)) continue;
      for (auto const& entry : iface.addressEntries ())
        {
          auto const ip = entry.ip ();
          if (ip.protocol () != QAbstractSocket::IPv4Protocol) continue;
          // skip link-local 169.254.x.x, which phones can't reach
          if ((ip.toIPv4Address () >> 16) == 0xA9FE) continue;
          urls << QString {"http://%1:%2"}.arg (ip.toString ()).arg (port);
        }
    }
  urls.removeDuplicates ();
  return urls;
}

void WebRemote::onNewConnection ()
{
  while (auto * socket = server_->nextPendingConnection ())
    {
      connect (socket, &QTcpSocket::readyRead, this, [this, socket] {onReadyRead (socket);});
      connect (socket, &QTcpSocket::disconnected, this, [this, socket] {
          buffers_.remove (socket);
          streams_.removeAll (socket);
          socket->deleteLater ();
        });
    }
}

void WebRemote::onReadyRead (QTcpSocket * socket)
{
  if (streams_.contains (socket))
    {
      socket->readAll ();       // event streams don't send anything more
      return;
    }
  auto& buffer = buffers_[socket];
  buffer += socket->readAll ();
  if (buffer.size () > max_request)
    {
      reply (socket, 413, "text/plain", "too large");
      return;
    }
  int const header_end = buffer.indexOf ("\r\n\r\n");
  if (header_end < 0) return;   // wait for the rest of the headers

  auto const lines = buffer.left (header_end).split ('\n');
  auto const request_line = lines.value (0).trimmed ().split (' ');
  if (request_line.size () < 2)
    {
      reply (socket, 400, "text/plain", "bad request");
      return;
    }
  int content_length {0};
  for (int i = 1; i < lines.size (); ++i)
    {
      auto const line = lines[i].trimmed ();
      if (line.toLower ().startsWith ("content-length:"))
        {
          content_length = line.mid (15).trimmed ().toInt ();
        }
    }
  if (content_length > max_request)
    {
      reply (socket, 413, "text/plain", "too large");
      return;
    }
  if (buffer.size () < header_end + 4 + content_length) return;   // body not all here yet

  Request request;
  request.method = request_line[0];
  QUrl const url {QString::fromUtf8 (request_line[1])};
  request.path = url.path ();
  request.query = QUrlQuery {url};
  request.body = buffer.mid (header_end + 4, content_length);
  buffers_.remove (socket);
  handle (socket, request);
}

bool WebRemote::authorised (QTcpSocket * socket, QString const& pin)
{
  auto const who = socket->peerAddress ().toString ();
  auto const now = QDateTime::currentDateTimeUtc ();
  auto& lock = lockouts_[who];
  if (lock.until.isValid () && now < lock.until)
    {
      reply (socket, 429, "application/json", R"({"error":"too many wrong PINs, wait a minute"})");
      return false;
    }
  if (pin.size () && pin == pin_)
    {
      lockouts_.remove (who);
      return true;
    }
  if (!lock.first.isValid () || lock.first.secsTo (now) > lockout_window_secs)
    {
      lock.first = now;
      lock.failures = 0;
    }
  if (++lock.failures >= lockout_failures)
    {
      lock.until = now.addSecs (lockout_secs);
    }
  reply (socket, 401, "application/json", R"({"error":"wrong PIN"})");
  return false;
}

void WebRemote::handle (QTcpSocket * socket, Request const& r)
{
  if (r.method == "GET" && (r.path == "/" || r.path == "/index.html"))
    {
      QFile page {":/webremote/index.html"};
      if (!page.open (QIODevice::ReadOnly))
        {
          reply (socket, 404, "text/plain", "page missing");
          return;
        }
      reply (socket, 200, "text/html; charset=utf-8", page.readAll ());
      return;
    }
  if (r.method == "GET" && r.path == "/manifest.json")
    {
      reply (socket, 200, "application/manifest+json",
             R"({"name":"WSJT-X Remote","short_name":"WSJT-X","display":"standalone","background_color":"#202020","theme_color":"#202020","start_url":"/"})");
      return;
    }

  auto const query_pin = r.query.queryItemValue ("pin", QUrl::FullyDecoded);

  if (r.method == "GET" && r.path == "/api/check")
    {
      if (authorised (socket, query_pin)) reply (socket, 200, "application/json", R"({"ok":true})");
      return;
    }
  if (r.method == "GET" && r.path == "/events")
    {
      if (authorised (socket, query_pin)) openStream (socket);
      return;
    }
  if (r.method == "GET" && r.path == "/waterfall.jpg")
    {
      if (!authorised (socket, query_pin)) return;
      auto const image = waterfall_provider_ ? waterfall_provider_ () : QByteArray {};
      if (image.isEmpty ())
        {
          reply (socket, 204, "text/plain", {});
        }
      else
        {
          reply (socket, 200, "image/jpeg", image, {{"Cache-Control", "no-store"}});
        }
      return;
    }
  if (r.path == "/api/command")
    {
      if (r.method != "POST")
        {
          reply (socket, 405, "text/plain", "POST only");
          return;
        }
      QJsonParseError error;
      auto const doc = QJsonDocument::fromJson (r.body, &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject ())
        {
          reply (socket, 400, "application/json", R"({"error":"bad JSON"})");
          return;
        }
      auto cmd = doc.object ();
      if (!authorised (socket, cmd.value ("pin").toString ())) return;
      cmd.remove ("pin");
      Q_EMIT command (cmd);
      reply (socket, 200, "application/json", R"({"ok":true})");
      stateChanged ();
      return;
    }
  reply (socket, 404, "text/plain", "not found");
}

void WebRemote::reply (QTcpSocket * socket, int status, QByteArray const& type, QByteArray const& body,
                       QList<QPair<QByteArray, QByteArray>> const& extra)
{
  QByteArray response = "HTTP/1.1 " + QByteArray::number (status) + ' ' + status_text (status) + "\r\n";
  response += "Content-Type: " + type + "\r\n";
  response += "Content-Length: " + QByteArray::number (body.size ()) + "\r\n";
  response += "Connection: close\r\n";
  response += "X-Content-Type-Options: nosniff\r\n";
  for (auto const& h : extra)
    {
      response += h.first + ": " + h.second + "\r\n";
    }
  response += "\r\n";
  socket->write (response);
  socket->write (body);
  socket->disconnectFromHost ();
}

void WebRemote::openStream (QTcpSocket * socket)
{
  socket->write ("HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/event-stream\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: keep-alive\r\n"
                 "X-Accel-Buffering: no\r\n\r\n"
                 "retry: 2000\n\n");
  streams_ << socket;
  QJsonObject init;
  init["state"] = state ();
  init["pane0"] = backlog_[0];
  init["pane1"] = backlog_[1];
  socket->write ("event: init\ndata: " + to_json (init) + "\n\n");
}

void WebRemote::broadcast (QByteArray const& event, QByteArray const& json)
{
  QByteArray const message = "event: " + event + "\ndata: " + json + "\n\n";
  for (auto const& s : streams_)
    {
      if (s) s->write (message);
    }
}

QJsonObject WebRemote::state () const
{
  auto s = state_provider_ ? state_provider_ () : QJsonObject {};
  s["clients"] = streams_.size ();
  return s;
}

void WebRemote::tick ()
{
  if (streams_.isEmpty ()) return;
  auto const json = to_json (state ());
  if (json != last_state_)
    {
      last_state_ = json;
      broadcast ("state", json);
    }
}

void WebRemote::stateChanged ()
{
  if (server_->isListening ()) QTimer::singleShot (50, this, &WebRemote::tick);
}

void WebRemote::addLine (int pane, QString const& text, QString const& bg, QString const& fg)
{
  if (pane < 0 || pane > 1) return;
  QJsonObject line {{"pane", pane}, {"text", text}};
  if (bg.size ()) line["bg"] = bg;
  if (fg.size ()) line["fg"] = fg;
  backlog_[pane].append (line);
  while (backlog_[pane].size () > max_backlog) backlog_[pane].removeFirst ();
  if (!streams_.isEmpty ()) broadcast ("decode", to_json (line));
}

void WebRemote::clearPane (int pane)
{
  if (pane < 0 || pane > 1) return;
  backlog_[pane] = QJsonArray {};
  if (!streams_.isEmpty ()) broadcast ("clear", to_json (QJsonObject {{"pane", pane}}));
}
