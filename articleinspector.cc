#include "articleinspector.hh"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

#include <QHBoxLayout>
#include <algorithm>

using std::list;

list< ArticleInspector * > ArticleInspector::openedInspectors;

ArticleInspector::ArticleInspector( Config::Class * cfg, QWidget* parent ) :
  QWidget( parent ),
  cfg( cfg ),
  view( new QWebEngineView( this ) ),
  devToolsPage( new QWebEnginePage( this ) )
{
  if ( cfg == NULL )
    throw exInit();

  QHBoxLayout * layout = new QHBoxLayout( this );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->addWidget( view );
  view->setPage( devToolsPage );
}

ArticleInspector::~ArticleInspector()
{
}

void ArticleInspector::setInspectedPage( QWebEnginePage * page )
{
  inspectedPage = page;
  if ( inspectedPage )
    inspectedPage->setDevToolsPage( devToolsPage );
}

void ArticleInspector::beforeClosed()
{
  list< ArticleInspector * >::iterator itemIter = std::find( openedInspectors.begin(),
                                                             openedInspectors.end(), this );
  if ( itemIter != openedInspectors.end() )
  {
    openedInspectors.erase( itemIter );
    // Save geometry of the recent closed inspector window
    QByteArray geometry = saveGeometry();
    cfg->inspectorGeometry = geometry;
  }
}

void ArticleInspector::showEvent( QShowEvent * event )
{
  if ( openedInspectors.empty() )
  {
    // Restore geometry from config, if no inspector opened
    restoreGeometry( cfg->inspectorGeometry );
  }
  else
  {
    // Load geometry from first inspector opened
    ArticleInspector * p = openedInspectors.front();
    setGeometry( p->geometry() );
  }

  if( std::find( openedInspectors.begin(), openedInspectors.end(), this ) == openedInspectors.end() )
    openedInspectors.push_back( this );

  QWidget::showEvent( event );
}

#elif QT_VERSION >= 0x040600

#include <algorithm>

using std::list;

list< ArticleInspector * > ArticleInspector::openedInspectors;

ArticleInspector::ArticleInspector( Config::Class * cfg, QWidget* parent ) :
  QWebInspector( parent ),
  cfg( cfg )
{
  if ( cfg == NULL )
    throw exInit();
}

ArticleInspector::~ArticleInspector()
{
}

void ArticleInspector::beforeClosed()
{
  list< ArticleInspector * >::iterator itemIter = std::find( openedInspectors.begin(),
                                                             openedInspectors.end(), this );
  if ( itemIter != openedInspectors.end() )
  {
    openedInspectors.erase( itemIter );
    // Save geometry of the recent closed inspector window
    QByteArray geometry = saveGeometry();
    cfg->inspectorGeometry = geometry;
  }
}

void ArticleInspector::showEvent( QShowEvent * event )
{
  if ( openedInspectors.empty() )
  {
    // Restore geometry from config, if no inspector opened
    restoreGeometry( cfg->inspectorGeometry );
  }
  else
  {
    // Load geometry from first inspector opened
    ArticleInspector * p = openedInspectors.front();
    setGeometry( p->geometry() );
  }

  if( std::find( openedInspectors.begin(), openedInspectors.end(), this ) == openedInspectors.end() )
    openedInspectors.push_back( this );

  QWebInspector::showEvent( event );
}

#endif // QT_VERSION
