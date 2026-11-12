#include "QrzLogbook.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>

#include <algorithm>

#include "revision_utils.hpp"

#include "moc_QrzLogbook.cpp"

namespace
{
  char const * const api_url = "https://logbook.qrz.com/api";
  int const first_retry_seconds = 60;
  int const max_retry_seconds = 15 * 60;

  // QRZ replies with a url-encoded form such as RESULT=OK&LOGID=1&COUNT=1
  QUrlQuery parse_reply (QByteArray const& body)
  {
    return QUrlQuery {QString::fromUtf8 (body).trimmed ()};
  }

  QString value (QUrlQuery const& q, QString const& name)
  {
    return q.queryItemValue (name, QUrl::FullyDecoded);
  }

  int const max_sent_remembered = 20000;

  // value of an ADIF field such as <call:4>W1AW, empty if absent
  QString adif_field (QByteArray const& adif, char const * name)
  {
    auto const lower = adif.toLower ();
    QByteArray const tag = QByteArray {"<"} + name + ':';
    int pos = 0;
    while ((pos = lower.indexOf (tag, pos)) >= 0)
      {
        // make sure e.g. <call: doesn't match <station_call...
        if (pos == 0 || lower[pos - 1] != '_') break;
        pos += tag.size ();
      }
    if (pos < 0) return {};
    int const close = lower.indexOf ('>', pos);
    if (close < 0) return {};
    bool ok {false};
    int const len = adif.mid (pos + tag.size (), close - pos - tag.size ()).split (':').first ().toInt (&ok);
    if (!ok) return {};
    return QString::fromUtf8 (adif.mid (close + 1, len)).trimmed ();
  }

  QString adif_call (QByteArray const& adif)
  {
    return adif_field (adif, "call");
  }
}

QString QrzLogbook::fingerprint (QByteArray const& adif)
{
  // the fields QRZ itself uses to detect a duplicate; time to the minute
  return QStringList {
    adif_field (adif, "call").toUpper (),
    adif_field (adif, "qso_date"),
    adif_field (adif, "time_on").left (4),
    adif_field (adif, "band").toLower (),
    adif_field (adif, "mode").toUpper ()
  }.join ('|');
}

QrzLogbook::QrzLogbook (QObject * parent)
  : QObject {parent}
  , network_ {new QNetworkAccessManager {this}}
  , retry_timer_ {new QTimer {this}}
  , retry_seconds_ {first_retry_seconds}
  , auth_failed_ {false}
{
  retry_timer_->setSingleShot (true);
  connect (retry_timer_, &QTimer::timeout, this, &QrzLogbook::sendNext);
}

void QrzLogbook::setPendingFile (QString const& path)
{
  pending_file_ = path;
  loadSent ();
  loadPending ();
}

void QrzLogbook::setApiKey (QString const& key)
{
  auto const trimmed = key.trimmed ();
  if (trimmed != key_)
    {
      key_ = trimmed;
      auth_failed_ = false;
      retry_seconds_ = first_retry_seconds;
    }
  sendNext ();   // flush anything left over from a previous session
}

bool QrzLogbook::upload (QByteArray const& adif)
{
  QByteArray record = adif.trimmed ();
  record.replace ('\r', ' ').replace ('\n', ' ');
  if (!record.toLower ().endsWith ("<eor>"))
    {
      record += " <eor>";
    }

  // never send the same QSO twice, whether it was uploaded earlier or is
  // still waiting in the queue (e.g. logged twice by accident)
  auto const fp = fingerprint (record);
  bool const duplicate = sent_.contains (fp)
    || std::any_of (queue_.cbegin (), queue_.cend (), [&fp] (QByteArray const& r) {return fingerprint (r) == fp;});
  if (duplicate)
    {
      Q_EMIT status (tr ("QRZ: %1 already uploaded, not sent again").arg (adif_call (record)), false);
      return false;
    }

  queue_ << record;
  savePending ();
  sendNext ();
  return true;
}

QNetworkReply * QrzLogbook::post (QString const& key, QByteArray const& action, QByteArray const& adif)
{
  // build the form by hand: QUrlQuery leaves '+' unencoded, which the
  // server would turn into a space and mangle reports like +05
  QByteArray body {"KEY=" + QUrl::toPercentEncoding (key) + "&ACTION=" + action};
  if (adif.size ())
    {
      body += "&ADIF=" + QUrl::toPercentEncoding (QString::fromUtf8 (adif));
    }
  QNetworkRequest request {QUrl {api_url}};
  request.setHeader (QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  request.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
  return network_->post (request, body);
}

void QrzLogbook::sendNext ()
{
  if (upload_reply_ || queue_.isEmpty () || key_.isEmpty () || auth_failed_) return;
  if (retry_timer_->isActive ()) return;   // waiting out a back-off period
  upload_reply_ = post (key_, "INSERT", queue_.first ());
  connect (upload_reply_.data (), &QNetworkReply::finished, this, &QrzLogbook::onUploadFinished);
}

void QrzLogbook::onUploadFinished ()
{
  auto * reply = upload_reply_.data ();
  if (!reply) return;
  reply->deleteLater ();
  upload_reply_.clear ();
  if (queue_.isEmpty ()) return;

  auto const call = adif_call (queue_.first ());

  if (reply->error () != QNetworkReply::NoError)
    {
      // network trouble: keep the QSO and try again later
      Q_EMIT status (tr ("QRZ: could not reach QRZ.com (%1), %2 QSO(s) queued, will retry")
                     .arg (reply->errorString ()).arg (queue_.size ()), true);
      scheduleRetry ();
      return;
    }

  auto const result = parse_reply (reply->readAll ());
  auto const outcome = value (result, "RESULT").toUpper ();
  auto const reason = value (result, "REASON");

  if (outcome == "OK" || outcome == "REPLACE")
    {
      rememberSent (queue_.first ());
      queue_.removeFirst ();
      Q_EMIT status (tr ("QRZ: %1 uploaded to logbook").arg (call), false);
    }
  else if (outcome == "AUTH")
    {
      // bad key or no subscription, hold everything until the key changes
      auth_failed_ = true;
      Q_EMIT status (tr ("QRZ: upload refused, check your Logbook API key and subscription (%1 QSO(s) queued)")
                     .arg (queue_.size ()), true);
      savePending ();
      return;
    }
  else if (reason.contains ("duplicate", Qt::CaseInsensitive))
    {
      rememberSent (queue_.first ());
      queue_.removeFirst ();
      Q_EMIT status (tr ("QRZ: %1 already in logbook").arg (call), false);
    }
  else if (outcome == "FAIL")
    {
      // QRZ rejected this record (e.g. station callsign does not match
      // the logbook); retrying won't help, so drop it and say why
      queue_.removeFirst ();
      Q_EMIT status (tr ("QRZ: %1 not accepted: %2").arg (call, reason.isEmpty () ? tr ("unknown reason") : reason), true);
    }
  else
    {
      // unexpected reply, treat like a temporary server problem
      Q_EMIT status (tr ("QRZ: unexpected reply from server, will retry"), true);
      scheduleRetry ();
      return;
    }

  retry_seconds_ = first_retry_seconds;
  savePending ();
  sendNext ();
}

void QrzLogbook::scheduleRetry ()
{
  savePending ();
  retry_timer_->start (retry_seconds_ * 1000);
  retry_seconds_ = qMin (retry_seconds_ * 2, max_retry_seconds);
}

void QrzLogbook::testKey (QString const& key)
{
  if (test_reply_) test_reply_->abort ();
  test_reply_ = post (key.trimmed (), "STATUS");
  connect (test_reply_.data (), &QNetworkReply::finished, this, &QrzLogbook::onTestFinished);
}

void QrzLogbook::onTestFinished ()
{
  auto * reply = test_reply_.data ();
  if (!reply) return;
  reply->deleteLater ();
  test_reply_.clear ();
  if (reply->error () == QNetworkReply::OperationCanceledError) return;
  if (reply->error () != QNetworkReply::NoError)
    {
      Q_EMIT keyTestResult (false, tr ("No connection: %1").arg (reply->errorString ()));
      return;
    }
  auto const result = parse_reply (reply->readAll ());
  if (value (result, "RESULT").toUpper () == "OK")
    {
      auto const call = value (result, "CALLSIGN");
      Q_EMIT keyTestResult (true, call.isEmpty () ? tr ("Key OK") : tr ("Key OK for %1").arg (call));
    }
  else
    {
      auto const reason = value (result, "REASON");
      Q_EMIT keyTestResult (false, reason.isEmpty () ? tr ("Key not accepted") : reason);
    }
}

void QrzLogbook::loadPending ()
{
  if (pending_file_.isEmpty ()) return;
  QFile f {pending_file_};
  if (!f.open (QIODevice::ReadOnly | QIODevice::Text)) return;
  while (!f.atEnd ())
    {
      auto const line = f.readLine ().trimmed ();
      if (line.size () && !queue_.contains (line)) queue_ << line;
    }
}

void QrzLogbook::savePending () const
{
  if (pending_file_.isEmpty ()) return;
  if (queue_.isEmpty ())
    {
      QFile::remove (pending_file_);
      return;
    }
  QSaveFile f {pending_file_};
  if (!f.open (QIODevice::WriteOnly | QIODevice::Text)) return;
  for (auto const& record : queue_)
    {
      f.write (record);
      f.write ("\n");
    }
  f.commit ();
}

QString QrzLogbook::sentFile () const
{
  if (pending_file_.isEmpty ()) return {};
  QFileInfo fi {pending_file_};
  return fi.absoluteDir ().absoluteFilePath ("qrz_uploaded.txt");
}

void QrzLogbook::loadSent ()
{
  sent_.clear ();
  sent_order_.clear ();
  QFile f {sentFile ()};
  if (sentFile ().isEmpty () || !f.open (QIODevice::ReadOnly | QIODevice::Text)) return;
  while (!f.atEnd ())
    {
      auto const fp = QString::fromUtf8 (f.readLine ()).trimmed ();
      if (fp.size () && !sent_.contains (fp))
        {
          sent_.insert (fp);
          sent_order_ << fp;
        }
    }
}

void QrzLogbook::rememberSent (QByteArray const& record)
{
  auto const fp = fingerprint (record);
  if (sent_.contains (fp)) return;
  sent_.insert (fp);
  sent_order_ << fp;
  if (sentFile ().isEmpty ()) return;
  if (sent_order_.size () > max_sent_remembered)
    {
      // trim the oldest entries and rewrite the file
      while (sent_order_.size () > max_sent_remembered)
        {
          sent_.remove (sent_order_.takeFirst ());
        }
      QSaveFile f {sentFile ()};
      if (f.open (QIODevice::WriteOnly | QIODevice::Text))
        {
          f.write (sent_order_.join ('\n').toUtf8 () + '\n');
          f.commit ();
        }
    }
  else
    {
      QFile f {sentFile ()};
      if (f.open (QIODevice::Append | QIODevice::Text))
        {
          f.write (fp.toUtf8 () + '\n');
        }
    }
}
