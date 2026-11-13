#include "SplashScreen.hpp"

#include <QPixmap>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QCoreApplication>

#include "revision_utils.hpp"
#include "pimpl_impl.hpp"

#include "moc_SplashScreen.cpp"

class SplashScreen::impl
{
public:
  impl ()
    : checkbox_ {"Do not show this again"}
  {
    main_layout_.addStretch ();
    main_layout_.addWidget (&checkbox_, 0, Qt::AlignRight);
  }

  QVBoxLayout main_layout_;
  QCheckBox checkbox_;
};

SplashScreen::SplashScreen ()
  : QSplashScreen {QPixmap {":/splash.png"}, Qt::WindowStaysOnTopHint}
{
  setLayout (&m_->main_layout_);
  showMessage ("<h2>" + QString {QCoreApplication::applicationName () + " v" +
        QCoreApplication::applicationVersion() + " " +
        revision ()}.simplified () + "</h2>"
    "by Carson Coordes, KF0KYL &mdash; based on WSJT-X " WSJTX_BASE_VERSION " by K1JT and the WSJT Development Team.<br />"
    "This modified version is not supported by the WSJT-X team: please don't send<br />"
    "its issue reports to the WSJT-X groups.<br /><br />"
    "<img src=\":/icon_128x128.png\" />"
    "<img src=\":/gpl-v3-logo.svg\" height=\"80\" />", Qt::AlignCenter);
  connect (&m_->checkbox_, &QCheckBox::stateChanged, [this] (int s) {
      if (Qt::Checked == s) Q_EMIT disabled ();
    });
}

SplashScreen::~SplashScreen ()
{
}
