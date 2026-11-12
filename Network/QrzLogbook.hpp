#ifndef QRZ_LOGBOOK_HPP_
#define QRZ_LOGBOOK_HPP_

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

//
// QrzLogbook - uploads logged QSOs to a QRZ.com Logbook
//
// Uses the QRZ Logbook API (https://logbook.qrz.com/api) with the
// per-logbook API key from My Logbook > Settings on QRZ.com.
//
// QSOs are queued and sent one at a time. If QRZ cannot be reached the
// QSO stays in the queue (saved to disk when a pending file is set) and
// is retried with back-off, so nothing is lost when the internet is down.
// Results are reported through the status() signal rather than modal
// message boxes so operating is never interrupted.
//
class QrzLogbook final
  : public QObject
{
  Q_OBJECT

public:
  explicit QrzLogbook (QObject * parent = nullptr);

  // file used to keep un-sent QSOs across restarts, empty for none
  void setPendingFile (QString const& path);

  // set or change the API key; an empty key pauses uploads
  void setApiKey (QString const& key);

  // queue one ADIF record (with or without a trailing <eor>); returns
  // false if the same QSO was already sent or is already waiting
  bool upload (QByteArray const& adif);

  // call/date/time/band/mode key used to spot duplicate QSOs
  static QString fingerprint (QByteArray const& adif);

  // number of QSOs waiting to be sent
  int pending () const {return queue_.size ();}

  // check a key with the STATUS action, result via keyTestResult()
  void testKey (QString const& key);

  Q_SIGNAL void status (QString const& message, bool error) const;
  Q_SIGNAL void keyTestResult (bool ok, QString const& detail) const;

private:
  void sendNext ();
  void onUploadFinished ();
  void onTestFinished ();
  void scheduleRetry ();
  void loadPending ();
  void savePending () const;
  void loadSent ();
  void rememberSent (QByteArray const& record);
  QString sentFile () const;
  QNetworkReply * post (QString const& key, QByteArray const& action, QByteArray const& adif = {});

  QNetworkAccessManager * network_;
  QTimer * retry_timer_;
  QString key_;
  QString pending_file_;
  QList<QByteArray> queue_;
  QSet<QString> sent_;          // fingerprints of QSOs already in the QRZ logbook
  QStringList sent_order_;      // same, oldest first, for trimming the file
  QPointer<QNetworkReply> upload_reply_;
  QPointer<QNetworkReply> test_reply_;
  int retry_seconds_;
  bool auth_failed_;
};

#endif
