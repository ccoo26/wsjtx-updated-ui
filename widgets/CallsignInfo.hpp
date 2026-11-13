#ifndef CALLSIGN_INFO_HPP_
#define CALLSIGN_INFO_HPP_

#include <QFrame>
#include <QString>

#include "Network/CallsignLookup.hpp"

class QLabel;
class QPushButton;

//
// CallsignInfo - a strip under the decode panes showing who the current
// DX station is: photo, name, location, grid, licence class, and a button
// that opens their QRZ.com page in the browser.
//
class CallsignInfo final
  : public QFrame
{
  Q_OBJECT

public:
  explicit CallsignInfo (QWidget * parent = nullptr);

  // start showing a station; country is WSJT-X's own prefix-based guess
  // shown until (or instead of) a database result
  void showCall (QString const& call, QString const& country);
  void clear ();

  void setInfo (CallsignLookup::Info const&);
  void setPhoto (QString const& call, QPixmap const&);
  void setNotFound (QString const& call, QString const& reason);

private:
  void openQrzPage ();

  QLabel * photo_;
  QLabel * title_;
  QLabel * details_;
  QLabel * source_;
  QPushButton * qrz_button_;
  QString call_;
  QString country_;
};

#endif
