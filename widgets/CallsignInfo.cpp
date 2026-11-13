#include "CallsignInfo.hpp"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include "moc_CallsignInfo.cpp"

namespace
{
  int const photo_size = 64;

  QPixmap rounded (QPixmap const& source)
  {
    auto const scaled = source.scaled (photo_size, photo_size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap result {photo_size, photo_size};
    result.fill (Qt::transparent);
    QPainter p {&result};
    p.setRenderHint (QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect (0, 0, photo_size, photo_size, 10, 10);
    p.setClipPath (path);
    p.drawPixmap ((photo_size - scaled.width ()) / 2, (photo_size - scaled.height ()) / 2, scaled);
    return result;
  }

  QPixmap initials (QString const& call)
  {
    // placeholder: first letters of the call on a soft tile
    QPixmap result {photo_size, photo_size};
    result.fill (Qt::transparent);
    QPainter p {&result};
    p.setRenderHint (QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect (0, 0, photo_size, photo_size, 10, 10);
    p.fillPath (path, QColor {"#3A6EA5"});
    auto font = p.font ();
    font.setPixelSize (22);
    font.setBold (true);
    p.setFont (font);
    p.setPen (Qt::white);
    p.drawText (QRect {0, 0, photo_size, photo_size}, Qt::AlignCenter, call.left (2));
    return result;
  }
}

CallsignInfo::CallsignInfo (QWidget * parent)
  : QFrame {parent}
  , photo_ {new QLabel {this}}
  , title_ {new QLabel {this}}
  , details_ {new QLabel {this}}
  , source_ {new QLabel {this}}
  , qrz_button_ {new QPushButton {tr ("QRZ Page"), this}}
{
  setObjectName ("callsign_info");
  setFrameShape (QFrame::StyledPanel);
  setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);

  photo_->setFixedSize (photo_size, photo_size);
  photo_->setStyleSheet ("background: transparent;");
  title_->setTextFormat (Qt::RichText);
  details_->setTextFormat (Qt::PlainText);
  details_->setWordWrap (true);
  source_->setStyleSheet ("color: palette(mid); font-size: 11px;");
  for (auto * l : {title_, details_, source_})
    {
      l->setTextInteractionFlags (Qt::TextSelectableByMouse);
    }
  qrz_button_->setToolTip (tr ("Open this station's QRZ.com page in your web browser"));
  qrz_button_->setFocusPolicy (Qt::NoFocus);   // never steal the keyboard while operating
  connect (qrz_button_, &QPushButton::clicked, this, &CallsignInfo::openQrzPage);

  auto * text = new QVBoxLayout;
  text->setSpacing (1);
  text->addWidget (title_);
  text->addWidget (details_);
  text->addWidget (source_);

  auto * row = new QHBoxLayout {this};
  row->setContentsMargins (8, 6, 8, 6);
  row->setSpacing (12);
  row->addWidget (photo_);
  row->addLayout (text, 1);
  row->addWidget (qrz_button_, 0, Qt::AlignVCenter);

  clear ();
}

void CallsignInfo::clear ()
{
  call_.clear ();
  country_.clear ();
  hide ();
}

void CallsignInfo::showCall (QString const& call, QString const& country)
{
  call_ = call.toUpper ();
  country_ = country;
  photo_->setPixmap (initials (call_));
  title_->setText (QString {"<span style='font-size:16px; font-weight:600;'>%1</span>"}.arg (call_.toHtmlEscaped ()));
  details_->setText (country_);
  source_->setText (tr ("Looking up…"));
  qrz_button_->setEnabled (true);
  show ();
}

void CallsignInfo::setInfo (CallsignLookup::Info const& i)
{
  if (!call_.contains (i.call)) return;   // a different station is showing now
  auto title = QString {"<span style='font-size:16px; font-weight:600;'>%1</span>"}.arg (call_.toHtmlEscaped ());
  if (i.name.size ())
    {
      title += QString {"&nbsp;&nbsp;<span style='font-size:16px;'>%1</span>"}.arg (i.name.toHtmlEscaped ());
    }
  if (i.nickname.size () && !i.name.contains (i.nickname, Qt::CaseInsensitive))
    {
      title += QString {"&nbsp;<span style='font-size:13px;'>“%1”</span>"}.arg (i.nickname.toHtmlEscaped ());
    }
  title_->setText (title);

  QStringList place;
  if (i.city.size ()) place << i.city;
  if (i.state.size ()) place << i.state;
  place << (i.country.size () ? i.country : country_);
  QStringList line {place.join (", ")};
  if (i.grid.size ()) line << i.grid.left (6);
  if (i.license_class.size ()) line << tr ("%1 class").arg (i.license_class);
  details_->setText (line.join ("  ·  "));
  source_->setText (i.source);
}

void CallsignInfo::setPhoto (QString const& call, QPixmap const& pixmap)
{
  if (!call_.contains (call) || pixmap.isNull ()) return;
  photo_->setPixmap (rounded (pixmap));
}

void CallsignInfo::setNotFound (QString const& call, QString const& reason)
{
  if (!call_.contains (call)) return;
  details_->setText (country_);
  source_->setText (reason);
}

void CallsignInfo::openQrzPage ()
{
  if (call_.isEmpty ()) return;
  QDesktopServices::openUrl (QUrl {QString {"https://www.qrz.com/db/%1"}.arg (QString::fromLatin1 (QUrl::toPercentEncoding (call_)))});
}
