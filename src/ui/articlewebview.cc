/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "articlewebview.hh"
#include <QMouseEvent>
#include <QApplication>
#include "articleinspector.hh"
#include "qt4x5.hh"

#ifdef Q_OS_WIN32
#include <qt_windows.h>
#endif

ArticleWebPage::ArticleWebPage( QObject * parent ):
  QWebEnginePage( parent )
{
}

bool ArticleWebPage::acceptNavigationRequest( QUrl const & url, NavigationType type, bool isMainFrame )
{
  if ( type == QWebEnginePage::NavigationTypeLinkClicked )
  {
    emit linkClicked( url );
    return false;
  }

  return QWebEnginePage::acceptNavigationRequest( url, type, isMainFrame );
}

ArticleWebView::ArticleWebView( QWidget *parent ):
  QWebEngineView( parent ),
  inspector( NULL ),
  webPage( new ArticleWebPage( this ) ),
  midButtonPressed( false ),
  selectionBySingleClick( false ),
  showInspectorDirectly( true )
{
  setPage( webPage );
  connect( webPage, SIGNAL( linkClicked( QUrl const & ) ), this, SIGNAL( linkClicked( QUrl const & ) ) );
}

ArticleWebView::~ArticleWebView()
{
  if ( inspector )
    inspector->deleteLater();
}

void ArticleWebView::setUp( Config::Class * cfg )
{
  this->cfg = cfg;
}

void ArticleWebView::triggerPageAction( QWebEnginePage::WebAction action, bool checked )
{
  if ( action == QWebEnginePage::InspectElement )
  {
    // Get or create inspector instance for current view.
    if ( !inspector )
    {
      inspector = new ArticleInspector( cfg );
      inspector->setInspectedPage( page() );
      connect( this, SIGNAL( destroyed() ), inspector, SLOT( beforeClosed() ) );
    }

    if ( showInspectorDirectly )
    {
      showInspectorDirectly = false;
      // Bring up the inspector window and set focus
      inspector->show();
      inspector->activateWindow();
      inspector->raise();
      return;
    }
  }

  page()->triggerAction( action, checked );
}

bool ArticleWebView::event( QEvent * event )
{
  switch ( event->type() )
  {
  case QEvent::MouseButtonRelease:
  case QEvent::MouseButtonDblClick:
    showInspectorDirectly = true;
    break;

  case QEvent::ContextMenu:
    showInspectorDirectly = false;
    break;

  default:
    break;
  }

  return QWebEngineView::event( event );
}

void ArticleWebView::mousePressEvent( QMouseEvent * event )
{
  if ( event->buttons() & Qt4x5::middleButton() )
    midButtonPressed = true;

  QWebEngineView::mousePressEvent( event );

  if ( selectionBySingleClick && ( event->buttons() & Qt::LeftButton ) )
  {
    findText( "" ); // clear the selection first, if any
    QMouseEvent ev( QEvent::MouseButtonDblClick,
            event->position(),
            event->globalPosition(),
            Qt::LeftButton,
            Qt::LeftButton,
            event->modifiers() );
    QApplication::sendEvent( this, &ev );
  }
}

void ArticleWebView::mouseReleaseEvent( QMouseEvent * event )
{
  bool noMidButton = !( event->buttons() & Qt4x5::middleButton() );

  QWebEngineView::mouseReleaseEvent( event );

  if ( midButtonPressed & noMidButton )
    midButtonPressed = false;
}

void ArticleWebView::mouseDoubleClickEvent( QMouseEvent * event )
{
  QWebEngineView::mouseDoubleClickEvent( event );
  int scrollBarWidth = 0;
  int scrollBarHeight = 0;

  // emit the signal only if we are not double-clicking on scrollbars
    if ( ( event->position().x() < width() - scrollBarWidth ) &&
      ( event->position().y() < height() - scrollBarHeight ) )
  {
    emit doubleClicked( event->pos() );
  }

}

void ArticleWebView::focusInEvent( QFocusEvent * event )
{
  QWebEngineView::focusInEvent( event );

  switch( event->reason() )
  {
    case Qt::MouseFocusReason:
    case Qt::TabFocusReason:
    case Qt::BacktabFocusReason:
      page()->runJavaScript( "window.focus();" );
      break;

    default:
      break;
  }
}

void ArticleWebView::wheelEvent( QWheelEvent *ev )
{
#ifdef Q_OS_WIN32

  // Avoid wrong mouse wheel handling in QWebEngineView
  // if system preferences is set to "scroll by page"

  if( ev->modifiers() == Qt::NoModifier )
  {
    unsigned nLines;
    SystemParametersInfo( SPI_GETWHEELSCROLLLINES, 0, &nLines, 0 );
    if( nLines == WHEEL_PAGESCROLL )
    {
      const int wheelDelta = ev->angleDelta().y();
      QKeyEvent kev( QEvent::KeyPress, wheelDelta > 0 ? Qt::Key_PageUp : Qt::Key_PageDown,
                     Qt::NoModifier );
      QApplication::sendEvent( this, &kev );

      ev->accept();
      return;
    }
  }
#endif

  if ( ev->modifiers().testFlag( Qt::ControlModifier ) )
  {
     ev->ignore();
  }
  else
  {
      QWebEngineView::wheelEvent( ev );
  }

}
