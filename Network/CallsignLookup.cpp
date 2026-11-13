#include "CallsignLookup.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include "revision_utils.hpp"

#include "moc_CallsignLookup.cpp"

namespace
{
  char const * const qrz_url = "https://xmldata.qrz.com/xml/current/";
  int const max_photo_bytes = 3 * 1024 * 1024;

  // QRZ and the FCC store names in capitals; "FRED L LLOYD" -> "Fred L Lloyd"
  QString title_case (QString const& s)
  {
    if (s.isEmpty () || s != s.toUpper ()) return s.simplified ();
    auto words = s.toLower ().simplified ().split (' ');
    for (auto& w : words)
      {
        bool up {true};
        for (int i = 0; i < w.size (); ++i)
          {
            if (up && w[i].isLetter ()) w[i] = w[i].toUpper ();
            up = !w[i].isLetter () && w[i] != '\'';   // O'BRIEN -> O'brien is close enough
          }
      }
    return words.join (' ');
  }

  // W1AW/P, VE3/W1AW, W1AW/QRP -> W1AW (the part QRZ has a page for)
  QString base_call (QString const& call)
  {
    auto const parts = call.toUpper ().trimmed ().split ('/', Qt::SkipEmptyParts);
    QString best;
    for (auto const& p : parts)
      {
        // the base call is the longest part that has a digit and a letter
        if (p.contains (QRegularExpression {"[0-9]"}) && p.contains (QRegularExpression {"[A-Z]"})
            && p.size () > best.size ())
          {
            best = p;
          }
      }
    return best.isEmpty () ? call.toUpper ().trimmed () : best;
  }

  bool is_us_call (QString const& call)
  {
    static QRegularExpression const us {"^(A[A-L]|K|N|W)[A-Z]?[0-9][A-Z]{1,3}$"};
    return us.match (call).hasMatch ();
  }

  struct QrzReply
  {
    CallsignLookup::Info info;
    QString key;
    QString error;
    QString sub_exp;
  };

  QrzReply parse_qrz (QByteArray const& xml)
  {
    QrzReply r;
    QXmlStreamReader reader {xml};
    QString section;
    QHash<QString, QString> fields;
    while (!reader.atEnd ())
      {
        reader.readNext ();
        if (reader.isStartElement ())
          {
            auto const name = reader.name ().toString ();
            if (name == "Callsign" || name == "Session")
              {
                section = name;
              }
            else if (section.size ())
              {
                auto const text = reader.readElementText ().trimmed ();
                if (section == "Session")
                  {
                    if (name == "Key") r.key = text;
                    else if (name == "Error") r.error = text;
                    else if (name == "SubExp") r.sub_exp = text;
                  }
                else
                  {
                    fields[name] = text;
                  }
              }
          }
        else if (reader.isEndElement () && reader.name () == section)
          {
            section.clear ();
          }
      }
    if (fields.contains ("call"))
      {
        auto& i = r.info;
        i.found = true;
        i.call = fields.value ("call").toUpper ();
        i.name = fields.value ("name_fmt").size () ? title_case (fields.value ("name_fmt"))
          : title_case ((fields.value ("fname") + ' ' + fields.value ("name")).trimmed ());
        i.nickname = fields.value ("nickname");
        i.city = title_case (fields.value ("addr2"));
        i.state = fields.value ("state");
        i.county = fields.value ("county");
        i.country = fields.value ("country");
        i.grid = fields.value ("grid");
        i.license_class = fields.value ("class");
        i.image_url = fields.value ("image");
      }
    return r;
  }

  // every value percent-encoded exactly once, so any password works
  QString login_url (QString const& username, QString const& password)
  {
    return QString {"%1?username=%2;password=%3;agent=%4"}.arg (qrz_url,
        QString::fromLatin1 (QUrl::toPercentEncoding (username)),
        QString::fromLatin1 (QUrl::toPercentEncoding (password)),
        QString::fromLatin1 (QUrl::toPercentEncoding (http_user_agent ())));
  }

  QString class_name (QString const& c)
  {
    auto const u = c.toUpper ();
    if (u == "E" || u == "EXTRA") return "Extra";
    if (u == "A" || u == "ADVANCED") return "Advanced";
    if (u == "G" || u == "GENERAL") return "General";
    if (u == "T" || u == "TECHNICIAN") return "Technician";
    if (u == "N" || u == "NOVICE") return "Novice";
    return title_case (c);
  }
}

CallsignLookup::CallsignLookup (QObject * parent)
  : QObject {parent}
  , network_ {new QNetworkAccessManager {this}}
{
}

void CallsignLookup::setQrzLogin (QString const& username, QString const& password)
{
  if (username.trimmed () != username_ || password != password_)
    {
      username_ = username.trimmed ();
      password_ = password;
      session_key_.clear ();
      cache_.clear ();   // results may improve with a (different) login
    }
}

QNetworkReply * CallsignLookup::get (QString const& url)
{
  QNetworkRequest request {QUrl::fromEncoded (url.toUtf8 ())};
  request.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
#if QT_VERSION >= QT_VERSION_CHECK (5, 9, 0)
  request.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
  return network_->get (request);
}

void CallsignLookup::lookup (QString const& call)
{
  auto const base = base_call (call);
  wanted_ = base;
  if (base.size () < 3) return;
  if (cache_.contains (base))
    {
      deliver (cache_.value (base));
      return;
    }
  if (username_.size () && password_.size ())
    {
      queryQrz (base, false);
    }
  else
    {
      queryCallook (base, {});
    }
}

void CallsignLookup::loginQrz (QString const& call)
{
  auto * reply = get (login_url (username_, password_));
  connect (reply, &QNetworkReply::finished, this, [this, reply, call] {
      reply->deleteLater ();
      if (reply->error () != QNetworkReply::NoError)
        {
          queryCallook (call, tr ("QRZ.com not reachable"));
          return;
        }
      auto const r = parse_qrz (reply->readAll ());
      if (r.key.isEmpty ())
        {
          queryCallook (call, r.error.size () ? tr ("QRZ.com: %1").arg (r.error) : tr ("QRZ.com login failed"));
          return;
        }
      session_key_ = r.key;
      subscriber_ = r.sub_exp.size () && !r.sub_exp.contains ("non-subscriber", Qt::CaseInsensitive);
      queryQrz (call, true);
    });
}

void CallsignLookup::queryQrz (QString const& call, bool after_login)
{
  if (session_key_.isEmpty ())
    {
      if (after_login)
        {
          queryCallook (call, tr ("QRZ.com login failed"));
        }
      else
        {
          loginQrz (call);
        }
      return;
    }
  auto * reply = get (QString {"%1?s=%2;callsign=%3"}.arg (qrz_url, session_key_, QString::fromLatin1 (QUrl::toPercentEncoding (call))));
  connect (reply, &QNetworkReply::finished, this, [this, reply, call, after_login] {
      reply->deleteLater ();
      if (reply->error () != QNetworkReply::NoError)
        {
          queryCallook (call, tr ("QRZ.com not reachable"));
          return;
        }
      auto r = parse_qrz (reply->readAll ());
      if (r.info.found)
        {
          if (r.key.size ()) session_key_ = r.key;
          if (r.sub_exp.size ()) subscriber_ = !r.sub_exp.contains ("non-subscriber", Qt::CaseInsensitive);
          r.info.source = subscriber_ ? tr ("QRZ.com") : tr ("QRZ.com (non-subscriber, limited data)");
          if (r.info.call.isEmpty ()) r.info.call = call;
          // non-subscribers get very little; fill in from the FCC for US calls
          if (!subscriber_ && r.info.city.isEmpty () && is_us_call (call))
            {
              queryCallook (call, {}, r.info);
              return;
            }
          cache_[call] = r.info;
          deliver (r.info);
          return;
        }
      if (r.key.isEmpty () && !after_login)
        {
          // session expired or invalid: log in again once
          session_key_.clear ();
          loginQrz (call);
          return;
        }
      queryCallook (call, r.error.startsWith ("Not found", Qt::CaseInsensitive)
                    ? tr ("Not on QRZ.com") : (r.error.size () ? tr ("QRZ.com: %1").arg (r.error) : QString {}));
    });
}

void CallsignLookup::queryCallook (QString const& call, QString const& qrz_reason)
{
  queryCallook (call, qrz_reason, Info {});
}

void CallsignLookup::queryCallook (QString const& call, QString const& qrz_reason, Info const& partial)
{
  if (!is_us_call (call))
    {
      Q_EMIT notFound (call, qrz_reason.size () ? qrz_reason : tr ("Add a QRZ.com login in Settings for name and location"));
      return;
    }
  auto * reply = get (QString {"https://callook.info/%1/json"}.arg (call));
  connect (reply, &QNetworkReply::finished, this, [this, reply, call, qrz_reason, partial] {
      reply->deleteLater ();
      auto const doc = reply->error () == QNetworkReply::NoError
        ? QJsonDocument::fromJson (reply->readAll ()).object () : QJsonObject {};
      if (doc.value ("status").toString () != "VALID")
        {
          if (partial.found)
            {
              // keep what QRZ did give us (name, country)
              cache_[call] = partial;
              deliver (partial);
            }
          else if (reply->error () != QNetworkReply::NoError)
            {
              Q_EMIT notFound (call, qrz_reason.size () ? qrz_reason : tr ("Lookup service not reachable"));
            }
          else
            {
              Q_EMIT notFound (call, tr ("No licence record found"));
            }
          return;
        }
      Info i;
      i.found = true;
      i.call = call;
      i.name = title_case (doc.value ("name").toString ());
      // address line2 looks like "NEWINGTON, CT 06111"
      auto const line2 = doc.value ("address").toObject ().value ("line2").toString ();
      static QRegularExpression const city_state {"^(.*),\\s*([A-Z]{2})\\b"};
      auto const m = city_state.match (line2);
      if (m.hasMatch ())
        {
          i.city = title_case (m.captured (1));
          i.state = m.captured (2);
        }
      i.country = "United States";
      i.grid = doc.value ("location").toObject ().value ("gridsquare").toString ();
      i.license_class = class_name (doc.value ("current").toObject ().value ("operClass").toString ());
      i.source = partial.found ? tr ("QRZ.com (limited) + FCC licence data") : tr ("FCC licence data");
      if (i.name.isEmpty ()) i.name = partial.name;
      cache_[call] = i;
      deliver (i);
    });
}

void CallsignLookup::deliver (Info const& info)
{
  if (info.call != wanted_) return;   // user has moved on to another station
  Info shown = info;
  shown.license_class = class_name (info.license_class);
  Q_EMIT found (shown);
  fetchPhoto (info);
}

void CallsignLookup::fetchPhoto (Info const& info)
{
  if (info.image_url.isEmpty ()) return;
  if (photos_.contains (info.call))
    {
      Q_EMIT photo (info.call, photos_.value (info.call));
      return;
    }
  auto * reply = get (info.image_url);
  reply->setReadBufferSize (max_photo_bytes);
  auto const call = info.call;
  connect (reply, &QNetworkReply::finished, this, [this, reply, call] {
      reply->deleteLater ();
      if (reply->error () != QNetworkReply::NoError) return;
      auto const bytes = reply->read (max_photo_bytes);
      QPixmap pixmap;
      if (!pixmap.loadFromData (bytes)) return;
      photos_[call] = pixmap;
      if (call == wanted_) Q_EMIT photo (call, pixmap);
    });
}

void CallsignLookup::testLogin (QString const& username, QString const& password)
{
  if (test_reply_) test_reply_->abort ();
  test_reply_ = get (login_url (username.trimmed (), password));
  auto * reply = test_reply_.data ();
  connect (reply, &QNetworkReply::finished, this, [this, reply] {
      reply->deleteLater ();
      if (reply->error () == QNetworkReply::OperationCanceledError) return;
      if (reply->error () != QNetworkReply::NoError)
        {
          Q_EMIT loginResult (false, tr ("No connection: %1").arg (reply->errorString ()));
          return;
        }
      auto const r = parse_qrz (reply->readAll ());
      if (r.key.isEmpty ())
        {
          Q_EMIT loginResult (false, r.error.size () ? r.error : tr ("Login failed"));
          return;
        }
      if (r.sub_exp.contains ("non-subscriber", Qt::CaseInsensitive) || r.sub_exp.isEmpty ())
        {
          Q_EMIT loginResult (true, tr ("Login OK (no XML subscription: limited data)"));
        }
      else
        {
          Q_EMIT loginResult (true, tr ("Login OK, subscription until %1").arg (r.sub_exp));
        }
    });
}
