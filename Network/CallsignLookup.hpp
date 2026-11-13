#ifndef CALLSIGN_LOOKUP_HPP_
#define CALLSIGN_LOOKUP_HPP_

#include <QObject>
#include <QString>
#include <QHash>
#include <QPixmap>
#include <QDateTime>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

//
// CallsignLookup - who is this station?
//
// Looks a callsign up on QRZ.com through the QRZ XML Logbook Data
// service when a QRZ username and password are set, falling back to
// the free callook.info service (FCC data) for US callsigns. Results
// and photos are cached for the session. Lookups are asynchronous and
// only the most recent request is reported, so typing a call or
// double-clicking quickly never shows stale information.
//
class CallsignLookup final
  : public QObject
{
  Q_OBJECT

public:
  struct Info
  {
    QString call;          // the call that was looked up
    QString name;          // display name, e.g. "Fred Lloyd"
    QString nickname;
    QString city;
    QString state;
    QString county;
    QString country;
    QString grid;
    QString license_class;
    QString image_url;
    QString source;        // "QRZ.com", "QRZ.com (non-subscriber)", "FCC via callook.info"
    bool found {false};
  };

  explicit CallsignLookup (QObject * parent = nullptr);

  void setQrzLogin (QString const& username, QString const& password);

  // look up a call; results arrive via found() / notFound()
  void lookup (QString const& call);

  // check QRZ login, result via loginResult()
  void testLogin (QString const& username, QString const& password);

  Q_SIGNAL void found (Info const&) const;
  Q_SIGNAL void notFound (QString const& call, QString const& reason) const;
  Q_SIGNAL void photo (QString const& call, QPixmap const&) const;
  Q_SIGNAL void loginResult (bool ok, QString const& detail) const;

private:
  void queryQrz (QString const& call, bool after_login);
  void loginQrz (QString const& call);
  void queryCallook (QString const& call, QString const& qrz_reason);
  void queryCallook (QString const& call, QString const& qrz_reason, Info const& partial);
  void fetchPhoto (Info const&);
  void deliver (Info const&);
  QNetworkReply * get (QString const& url);

  QNetworkAccessManager * network_;
  QString username_;
  QString password_;
  QString session_key_;
  bool subscriber_ {false};
  QString wanted_;                       // most recent call asked for
  QHash<QString, Info> cache_;
  QHash<QString, QPixmap> photos_;
  QPointer<QNetworkReply> test_reply_;
};

#endif
