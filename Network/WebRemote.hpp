#ifndef WEB_REMOTE_HPP_
#define WEB_REMOTE_HPP_

#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QList>
#include <QDateTime>
#include <QPointer>

class QTcpServer;
class QTcpSocket;
class QTimer;

//
// WebRemote - operate WSJT-X from a phone or tablet on the home network
//
// A small built-in web server (plain HTTP + Server-Sent Events, no extra
// libraries). The browser gets a touch friendly page, live decodes, the
// waterfall and station state, and sends commands back. Every request
// must carry the PIN set in Settings; repeated wrong PINs from one
// address are locked out for a minute.
//
//   GET  /                  the remote control page
//   GET  /api/check?pin=    200 if the PIN is right, 401 if not
//   GET  /events?pin=       event stream: init, state, decode, clear
//   GET  /waterfall.jpg?pin=
//   POST /api/command       {"pin": "...", "cmd": "...", ...}
//
// The server never touches the radio itself; commands are handed to
// MainWindow through the command() signal and go through exactly the
// same code paths as clicking the real buttons.
//
class WebRemote final
  : public QObject
{
  Q_OBJECT

public:
  using StateProvider = std::function<QJsonObject ()>;
  using ImageProvider = std::function<QByteArray ()>;

  explicit WebRemote (QObject * parent = nullptr);
  ~WebRemote ();

  // (re)start listening on all interfaces, returns false on failure
  bool start (quint16 port, QString const& pin);
  void stop ();
  bool isListening () const;
  QString errorString () const {return error_;}
  int clientCount () const {return streams_.size ();}

  void setStateProvider (StateProvider p) {state_provider_ = std::move (p);}
  void setWaterfallProvider (ImageProvider p) {waterfall_provider_ = std::move (p);}

  // decode pane updates: pane 0 = Band Activity, 1 = Rx Frequency
  void addLine (int pane, QString const& text, QString const& bg, QString const& fg);
  void clearPane (int pane);

  // push state right away (e.g. after a command) instead of next tick
  void stateChanged ();

  // http://<address>:<port> for each IPv4 address of this computer
  static QStringList localUrls (quint16 port);

  Q_SIGNAL void command (QJsonObject const& cmd) const;

private:
  struct Request;
  void onNewConnection ();
  void onReadyRead (QTcpSocket *);
  void handle (QTcpSocket *, Request const&);
  void reply (QTcpSocket *, int status, QByteArray const& type, QByteArray const& body,
              QList<QPair<QByteArray, QByteArray>> const& extra = {});
  bool authorised (QTcpSocket *, QString const& pin);
  void openStream (QTcpSocket *);
  void broadcast (QByteArray const& event, QByteArray const& json);
  void tick ();
  QJsonObject state () const;

  QTcpServer * server_;
  QTimer * state_timer_;
  QTimer * heartbeat_timer_;
  QString pin_;
  QString error_;
  StateProvider state_provider_;
  ImageProvider waterfall_provider_;
  QList<QPointer<QTcpSocket>> streams_;
  QHash<QTcpSocket *, QByteArray> buffers_;
  QJsonArray backlog_[2];
  QByteArray last_state_;
  struct Lockout {int failures {0}; QDateTime first; QDateTime until;};
  QHash<QString, Lockout> lockouts_;
};

#endif
