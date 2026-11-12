// -*- Mode: C++ -*-
#ifndef WIN11_THEME_HPP_
#define WIN11_THEME_HPP_

#include <QObject>
#include <QString>
#include <QFont>

class QApplication;
class QWidget;

//
// Win11Theme - Windows 11 (Fluent-inspired) look for WSJT-X.
//
// Provides light and dark Qt style sheets plus matching palettes, and
// on Windows asks DWM for a dark title bar and rounded corners on every
// top-level window. Only presentation is touched; no radio, audio,
// timing or decoder behaviour depends on anything in here.
//
class Win11Theme final
  : public QObject
{
  Q_OBJECT

public:
  // install Fusion style and the window event filter, call once at start up
  static void install (QApplication *);

  // apply light or dark theme, extra is appended (e.g. a user style sheet)
  static void apply (bool dark, QFont const& font, QString const& extra = QString {});

  static bool isDark () {return dark_;}

  // re-apply title bar attributes to one window
  static void updateTitleBar (QWidget *);

  // colours used by code that styles individual widgets
  static QString controlBackground ();  // normal button face
  static QString controlBorder ();      // normal button border
  static QString inputBackground ();    // line edit / combo face

protected:
  bool eventFilter (QObject *, QEvent *) override;

private:
  explicit Win11Theme (QObject * parent) : QObject {parent} {}
  static QString styleSheet (bool dark);
  static bool dark_;
};

#endif
