#ifndef __PREFERENCES_HH_INCLUDED__
#define __PREFERENCES_HH_INCLUDED__

#include <QDialog>
#include "config.hh"
#include "helpwindow.hh"
#include "ui_preferences.h"

class QLineEdit;
class QSpinBox;

/// Preferences dialog -- allows changing various program options.
class Preferences: public QDialog
{
  Q_OBJECT

  int prevInterfaceLanguage;

  Help::HelpWindow * helpWindow;
  Config::Class & cfg;
  QAction helpAction;

public:

  Preferences( QWidget * parent, Config::Class & cfg_ );
  ~Preferences()
  { if( helpWindow ) delete helpWindow; }

  Config::Preferences getPreferences();

private:

  Ui::Preferences ui;

  void setColorField( QLineEdit * lineEdit, QString const & colorText );
  void pickColor( QLineEdit * lineEdit, QString const & title );
  void clearColor( QLineEdit * lineEdit );
  void pickFont( QLineEdit * lineEdit, QSpinBox * sizeSpin, QString const & title );
  void clearFont( QLineEdit * lineEdit, QSpinBox * sizeSpin );

private slots:

  void enableScanPopupToggled( bool );
  void enableScanPopupModifiersToggled( bool );
  void showScanFlagToggled( bool b );
  void on_scanPopupUnpinnedWindowFlags_currentIndexChanged( int index );

  void wholeAltClicked( bool );
  void wholeCtrlClicked( bool );
  void wholeShiftClicked( bool );

  void sideAltClicked( bool );
  void sideCtrlClicked( bool );
  void sideShiftClicked( bool );

  void on_enableMainWindowHotkey_toggled( bool checked );
  void on_enableClipboardHotkey_toggled( bool checked );

  void on_buttonBox_accepted();

  void on_useExternalPlayer_toggled( bool enabled );

  void customProxyToggled( bool );
  void on_maxNetworkCacheSize_valueChanged( int value );

  void on_collapseBigArticles_toggled( bool checked );
  void on_limitInputPhraseLength_toggled( bool checked );

  void on_articleFontPick_clicked();
  void on_articleFontClear_clicked();
  void on_articleTextColorPick_clicked();
  void on_articleTextColorClear_clicked();
  void on_articleBackgroundColorPick_clicked();
  void on_articleBackgroundColorClear_clicked();
  void on_articleLinkColorPick_clicked();
  void on_articleLinkColorClear_clicked();
  void on_uiFontPick_clicked();
  void on_uiFontClear_clicked();
  void on_uiTextColorPick_clicked();
  void on_uiTextColorClear_clicked();
  void on_uiBackgroundColorPick_clicked();
  void on_uiBackgroundColorClear_clicked();

  void helpRequested();
  void closeHelp();

  // Theme override handlers
  void on_articleFontPick_clicked();
  void on_articleFontClear_clicked();
  void on_articleTextColorPick_clicked();
  void on_articleTextColorClear_clicked();
  void on_articleBackgroundColorPick_clicked();
  void on_articleBackgroundColorClear_clicked();
  void on_articleLinkColorPick_clicked();
  void on_articleLinkColorClear_clicked();
  void on_uiFontPick_clicked();
  void on_uiFontClear_clicked();
  void on_uiTextColorPick_clicked();
  void on_uiTextColorClear_clicked();
  void on_uiBackgroundColorPick_clicked();
  void on_uiBackgroundColorClear_clicked();

private:
  void setColorFieldColor( QLineEdit * field, const QColor & color );
};

#endif
