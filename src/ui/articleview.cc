/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "articleview.hh"
#include <map>
#include <QMessageBox>
#include <QMenu>
#include <QDesktopServices>
#include <QClipboard>
#include <QKeyEvent>
#include <QFileDialog>
#include <QEventLoop>
#include <QTimer>
#include <QSharedPointer>
#include "folding.hh"
#include "wstring_qt.hh"
#include "webmultimediadownload.hh"
#include "programs.hh"
#include "gddebug.hh"
#include <QDebug>
#include <QCryptographicHash>
#include "gestures.hh"
#include "fulltextsearch.hh"

#include <QRegularExpression>
#include "wildcard.hh"

#include <QtWebEngineCore/QWebEngineContextMenuRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebChannel>
#include <QtWebEngineCore/QWebEngineHistory>
#include <QtWebEngineCore/QWebEngineFindTextResult>
#include "webengine_schemehandler.hh"

#include "qt4x5.hh"

#include <assert.h>

#ifdef Q_OS_WIN32
#include <windows.h>

#include <QPainter>
#endif

#include <QBuffer>

#if defined( Q_OS_WIN32 ) || defined( Q_OS_WIN )
#include "speechclient.hh"
#endif

using std::map;
using std::list;

/// This class exposes only slim, minimal API to JavaScript clients in order to
/// reduce attack surface available to potentionally malicious external scripts.
class ArticleViewJsProxy: public QObject
{
  Q_OBJECT
public:
  /// Note: view becomes the parent of this proxy object.
  explicit ArticleViewJsProxy( ArticleView & view ):
    QObject( &view ), articleView( view )
  {}

  Q_INVOKABLE void onJsActiveArticleChanged( QString const & id )
  { articleView.onJsActiveArticleChanged( id ); }

private:
  ArticleView & articleView;
};

/// AccentMarkHandler class
///
/// Remove accent marks from text
/// and mirror position in normalized text to original text

class AccentMarkHandler
{
protected:
  QString normalizedString;
  QVector< int > accentMarkPos;
public:
  AccentMarkHandler()
  {}
  virtual ~AccentMarkHandler()
  {}
  static QChar accentMark()
  { return QChar( 0x301 ); }

  /// Create text without accent marks
  /// and store mark positions
  virtual void setText( QString const & baseString )
  {
    accentMarkPos.clear();
    normalizedString.clear();
    int pos = 0;
    QChar mark = accentMark();

    for( int x = 0; x < baseString.length(); x++ )
    {
      if( baseString.at( x ) == mark )
      {
        accentMarkPos.append( pos );
        continue;
      }
      normalizedString.append( baseString.at( x ) );
      pos++;
    }
  }

  /// Return text without accent marks
  QString const & normalizedText() const
  { return normalizedString; }

  /// Convert position into position in original text
  int mirrorPosition( int const & pos ) const
  {
    int newPos = pos;
    for( int x = 0; x < accentMarkPos.size(); x++ )
    {
      if( accentMarkPos.at( x ) < pos )
        newPos++;
      else
        break;
    }
    return newPos;
  }
};

/// End of DslAccentMark class

/// DiacriticsHandler class
///
/// Remove diacritics from text
/// and mirror position in normalized text to original text

class DiacriticsHandler : public AccentMarkHandler
{
public:
  DiacriticsHandler()
  {}
  ~DiacriticsHandler()
  {}

  /// Create text without diacriticss
  /// and store diacritic marks positions
  virtual void setText( QString const & baseString )
  {
    accentMarkPos.clear();
    normalizedString.clear();

    gd::wstring baseText = gd::toWString( baseString );
    gd::wstring normText;

    int pos = 0;
    normText.reserve( baseText.size() );

    gd::wchar const * nextChar = baseText.data();
    size_t consumed;

    for( size_t left = baseText.size(); left; )
    {
      if( *nextChar >= 0x10000 )
      {
        // Will be translated into surrogate pair
        normText.push_back( *nextChar );
        pos += 2;
        nextChar++; left--;
        continue;
      }

      gd::wchar ch = Folding::foldedDiacritic( nextChar, left, consumed );

      if( Folding::isCombiningMark( ch ) )
      {
        accentMarkPos.append( pos );
        nextChar++; left--;
        continue;
      }

      if( consumed > 1 )
      {
        for( size_t i = 1; i < consumed; i++ )
          accentMarkPos.append( pos );
      }

      normText.push_back( ch );
      pos += 1;
      nextChar += consumed;
      left -= consumed;
    }
    normalizedString = gd::toQString( normText );
  }
};

/// End of DiacriticsHandler class

// Helper struct to safely manage lifetime of local variables in async callbacks
struct JavaScriptResultWrapper {
  QVariant result;
  QEventLoop* loop;
};

static QVariant runJavaScriptSync( QWebEnginePage * page, QString const & script )
{
  if ( !page ) {
    return QVariant();
  }
  
  QSharedPointer< JavaScriptResultWrapper > wrapper( new JavaScriptResultWrapper() );
  wrapper->loop = nullptr;
  QEventLoop loop;
  wrapper->loop = &loop;
  QTimer timer;
  timer.setSingleShot( true );
  timer.start( 1000 );
  QObject::connect( &timer, &QTimer::timeout, &loop, [wrapper, &loop]() {
    wrapper->loop = nullptr;
    loop.quit();
  } );
  
  page->runJavaScript( script, [wrapper]( const QVariant & value ) {
    if ( wrapper->loop ) {
      wrapper->result = value;
      wrapper->loop->quit();
    }
  } );
  loop.exec();
  return wrapper->result;
}

static QString toHtmlSync( QWebEnginePage * page )
{
  if ( !page ) {
    return QString();
  }
  
  QString html;
  QEventLoop loop;
  page->toHtml( [&html, &loop]( const QString & value ) {
    html = value;
    loop.quit();
  } );
  loop.exec();
  return html;
}

static QString toPlainTextSync( QWebEnginePage * page )
{
  if ( !page ) {
    return QString();
  }
  
  QString text;
  QEventLoop loop;
  page->toPlainText( [&text, &loop]( const QString & value ) {
    text = value;
    loop.quit();
  } );
  loop.exec();
  return text;
}

static QVariant evaluateJavaScriptVariableSafe( QWebEnginePage * page, const QString & variable )
{
  return runJavaScriptSync( page,
                            QString( "( typeof( %1 ) !== 'undefined' && %1 !== undefined ) ? %1 : null;" )
                            .arg( variable ) );
}

static bool findTextSync( QWebEngineView * view, QString const & text, QWebEnginePage::FindFlags flags )
{
  bool found = false;
  QEventLoop loop;
  view->findText( text, flags, [&]( const QWebEngineFindTextResult & result ) {
    found = result.numberOfMatches() > 0;
    loop.quit();
  } );
  loop.exec();
  return found;
}

namespace {

char const * const scrollToPrefix = "gdfrom-";

bool isScrollTo( QString const & id )
{
  return id.startsWith( scrollToPrefix );
}

QString dictionaryIdFromScrollTo( QString const & scrollTo )
{
  Q_ASSERT( isScrollTo( scrollTo ) );
  const int scrollToPrefixLength = 7;
  return scrollTo.mid( scrollToPrefixLength );
}

QString searchStatusMessageNoMatches()
{
  return ArticleView::tr( "Phrase not found" );
}

QString searchStatusMessage( int activeMatch, int matchCount )
{
  Q_ASSERT( matchCount > 0 );
  Q_ASSERT( activeMatch > 0 );
  Q_ASSERT( activeMatch <= matchCount );
  return ArticleView::tr( "%1 of %2 matches" ).arg( activeMatch ).arg( matchCount );
}

} // unnamed namespace

QString ArticleView::scrollToFromDictionaryId( QString const & dictionaryId )
{
  Q_ASSERT( !isScrollTo( dictionaryId ) );
  return scrollToPrefix + dictionaryId;
}

ArticleView::ArticleView( QWidget * parent, ArticleNetworkAccessManager & nm,
                          AudioPlayerPtr const & audioPlayer_,
                          std::vector< sptr< Dictionary::Class > > const & allDictionaries_,
                          Instances::Groups const & groups_, bool popupView_,
                          Config::Class const & cfg_,
                          QAction & openSearchAction_,
                          QAction * dictionaryBarToggled_,
                          GroupComboBox const * groupComboBox_ ):
  QFrame( parent ),
  articleNetMgr( nm ),
  audioPlayer( audioPlayer_ ),
  allDictionaries( allDictionaries_ ),
  groups( groups_ ),
  popupView( popupView_ ),
  cfg( cfg_ ),
  jsProxy( new ArticleViewJsProxy( *this ) ),
  pasteAction( this ),
  articleUpAction( this ),
  articleDownAction( this ),
  goBackAction( this ),
  goForwardAction( this ),
  selectCurrentArticleAction( this ),
  copyAsTextAction( this ),
  inspectAction( this ),
  openSearchAction( openSearchAction_ ),
  searchIsOpened( false ),
  dictionaryBarToggled( dictionaryBarToggled_ ),
  groupComboBox( groupComboBox_ ),
  ftsSearchIsOpened( false ),
  ftsSearchMatchCase( false ),
  ftsPosition( 0 )
{
  ui.setupUi( this );

  ui.definition->setUp( const_cast< Config::Class * >( &cfg ) );

  {
    static ArticleUrlSchemeHandler * schemeHandler = 0;
    static bool schemeHandlerInstalled = false;
    if ( !schemeHandler )
      schemeHandler = new ArticleUrlSchemeHandler( articleNetMgr, ui.definition );

    if ( !schemeHandlerInstalled )
    {
      QWebEngineProfile * profile = ui.definition->page()->profile();
      const QByteArray schemes[] = { "gdlookup", "bres", "gdpicture", "gdau", "gdvideo", "gico", "qrcx" };
      for ( size_t i = 0; i < sizeof( schemes ) / sizeof( schemes[ 0 ] ); ++i )
        profile->installUrlSchemeHandler( schemes[ i ], schemeHandler );

      schemeHandlerInstalled = true;
    }
  }

  goBackAction.setShortcut( QKeySequence( "Alt+Left" ) );
  ui.definition->addAction( &goBackAction );
  connect( &goBackAction, SIGNAL( triggered() ),
           this, SLOT( back() ) );

  goForwardAction.setShortcut( QKeySequence( "Alt+Right" ) );
  ui.definition->addAction( &goForwardAction );
  connect( &goForwardAction, SIGNAL( triggered() ),
           this, SLOT( forward() ) );

  ui.definition->page()->action( QWebEnginePage::Copy )->setShortcut( QKeySequence::Copy );
  ui.definition->addAction( ui.definition->page()->action( QWebEnginePage::Copy ) );

  QAction * selectAll = ui.definition->page()->action( QWebEnginePage::SelectAll );
  selectAll->setShortcut( QKeySequence::SelectAll );
  selectAll->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  ui.definition->addAction( selectAll );

  ui.definition->setContextMenuPolicy( Qt::CustomContextMenu );

  ui.definition->page()->settings()->setAttribute( QWebEngineSettings::LocalContentCanAccessRemoteUrls, true );
  ui.definition->page()->settings()->setAttribute( QWebEngineSettings::LocalContentCanAccessFileUrls, true );

  connect( ui.definition, SIGNAL( loadFinished( bool ) ),
           this, SLOT( loadFinished( bool ) ) );

  {
    QWebChannel * channel = new QWebChannel( ui.definition->page() );
    channel->registerObject( "articleview", jsProxy );
    ui.definition->page()->setWebChannel( channel );

    QWebEngineScript channelLib;
    channelLib.setName( "gd-webchannel-lib" );
    channelLib.setSourceUrl( QUrl( "qrc:/qtwebchannel/qwebchannel.js" ) );
    channelLib.setInjectionPoint( QWebEngineScript::DocumentCreation );
    channelLib.setRunsOnSubFrames( true );
    ui.definition->page()->scripts().insert( channelLib );

    QWebEngineScript channelInit;
    channelInit.setName( "gd-webchannel-init" );
    channelInit.setSourceCode(
      "(function() {"
      "  function initChannel() {"
      "    if (typeof qt !== 'undefined' && qt.webChannelTransport) {"
      "      try {"
      "        new QWebChannel(qt.webChannelTransport, function(channel) {"
      "          window.articleview = channel.objects.articleview;"
      "        });"
      "      } catch(e) {}"
      "    } else if (typeof qt === 'undefined') {"
      "      setTimeout(initChannel, 100);"
      "    }"
      "  }"
      "  if (document.readyState === 'loading') {"
      "    document.addEventListener('DOMContentLoaded', initChannel);"
      "  } else {"
      "    setTimeout(initChannel, 0);"
      "  }"
      "})();"
    );
    channelInit.setInjectionPoint( QWebEngineScript::DocumentReady );
    channelInit.setRunsOnSubFrames( true );
    ui.definition->page()->scripts().insert( channelInit );
  }

  connect( ui.definition, SIGNAL( titleChanged( QString const & ) ),
           this, SLOT( handleTitleChanged( QString const & ) ) );

  connect( ui.definition, SIGNAL( urlChanged( QUrl const & ) ),
           this, SLOT( handleUrlChanged( QUrl const & ) ) );

  connect( ui.definition, SIGNAL( customContextMenuRequested( QPoint const & ) ),
           this, SLOT( contextMenuRequested( QPoint const & ) ) );

  connect( ui.definition, SIGNAL( linkClicked( QUrl const & ) ),
           this, SLOT( linkClicked( QUrl const & ) ) );

  connect( ui.definition->page(), SIGNAL( linkHovered( const QString & ) ),
           this, SLOT( linkHovered( const QString & ) ) );

  connect( ui.definition, SIGNAL( doubleClicked( QPoint ) ),this,SLOT( doubleClicked( QPoint ) ) );

  pasteAction.setShortcut( QKeySequence::Paste  );
  ui.definition->addAction( &pasteAction );
  connect( &pasteAction, SIGNAL( triggered() ), this, SLOT( pasteTriggered() ) );

  articleUpAction.setShortcut( QKeySequence( "Alt+Up" ) );
  ui.definition->addAction( &articleUpAction );
  connect( &articleUpAction, SIGNAL( triggered() ), this, SLOT( moveOneArticleUp() ) );

  articleDownAction.setShortcut( QKeySequence( "Alt+Down" ) );
  ui.definition->addAction( &articleDownAction );
  connect( &articleDownAction, SIGNAL( triggered() ), this, SLOT( moveOneArticleDown() ) );

  ui.definition->addAction( &openSearchAction );
  connect( &openSearchAction, SIGNAL( triggered() ), this, SLOT( openSearch() ) );

  selectCurrentArticleAction.setShortcut( QKeySequence( "Ctrl+Shift+A" ));
  selectCurrentArticleAction.setText( tr( "Select Current Article" ) );
  ui.definition->addAction( &selectCurrentArticleAction );
  connect( &selectCurrentArticleAction, SIGNAL( triggered() ),
           this, SLOT( selectCurrentArticle() ) );

  copyAsTextAction.setShortcut( QKeySequence( "Ctrl+Shift+C" ) );
  copyAsTextAction.setText( tr( "Copy as text" ) );
  ui.definition->addAction( &copyAsTextAction );
  connect( &copyAsTextAction, SIGNAL( triggered() ),
           this, SLOT( copyAsText() ) );

  inspectAction.setShortcut( QKeySequence( Qt::Key_F12 ) );
  inspectAction.setText( tr( "Inspect" ) );
  ui.definition->addAction( &inspectAction );
  connect( &inspectAction, SIGNAL( triggered() ), this, SLOT( inspect() ) );

  ui.definition->installEventFilter( this );
  ui.searchFrame->installEventFilter( this );
  ui.ftsSearchFrame->installEventFilter( this );

  // Load the default blank page instantly, so there would be no flicker.

  QString contentType;
  QUrl blankPage( "gdlookup://localhost?blank=1" );

  sptr< Dictionary::DataRequest > r = articleNetMgr.getResource( blankPage,
                                                                 contentType );

  ui.definition->setHtml( QString::fromUtf8( &( r->getFullData().front() ),
                                             r->getFullData().size() ),
                          blankPage );

  expandOptionalParts = cfg.preferences.alwaysExpandOptionalParts;

  ui.definition->grabGesture( Gestures::GDPinchGestureType );
  ui.definition->grabGesture( Gestures::GDSwipeGestureType );

  // Variable name for store current selection range
  rangeVarName = QString( "sr_%1" ).arg( QString::number( (quint64)this, 16 ) );
}

// explicitly report the minimum size, to avoid
// sidebar widgets' improper resize during restore
QSize ArticleView::minimumSizeHint() const
{
  return ui.searchFrame->minimumSizeHint();
}

void ArticleView::setGroupComboBox( GroupComboBox const * g )
{
  groupComboBox = g;
}

ArticleView::~ArticleView()
{
  cleanupTemp();
  audioPlayer->stop();

  ui.definition->ungrabGesture( Gestures::GDPinchGestureType );
  ui.definition->ungrabGesture( Gestures::GDSwipeGestureType );
}

void ArticleView::showDefinition( Config::InputPhrase const & phrase, unsigned group,
                                  QString const & scrollTo,
                                  Contexts const & contexts_ )
{
  // first, let's stop the player
  audioPlayer->stop();

  QUrl req;
  Contexts contexts( contexts_ );

  req.setScheme( "gdlookup" );
  req.setHost( "localhost" );
  Qt4x5::Url::addQueryItem( req, "word", phrase.phrase );
  if ( !phrase.punctuationSuffix.isEmpty() )
    Qt4x5::Url::addQueryItem( req, "punctuation_suffix", phrase.punctuationSuffix );
  Qt4x5::Url::addQueryItem( req, "group", QString::number( group ) );
  if( cfg.preferences.ignoreDiacritics )
    Qt4x5::Url::addQueryItem( req, "ignore_diacritics", "1" );

  if ( scrollTo.size() )
    Qt4x5::Url::addQueryItem( req, "scrollto", scrollTo );

  Contexts::Iterator pos = contexts.find( "gdanchor" );
  if( pos != contexts.end() )
  {
    Qt4x5::Url::addQueryItem( req, "gdanchor", contexts[ "gdanchor" ] );
    contexts.erase( pos );
  }

  if ( contexts.size() )
  {
    QBuffer buf;

    buf.open( QIODevice::WriteOnly );

    QDataStream stream( &buf );

    stream << contexts;

    buf.close();

    Qt4x5::Url::addQueryItem( req,  "contexts", QString::fromLatin1( buf.buffer().toBase64() ) );
  }

  QString mutedDicts = getMutedForGroup( group );

  if ( mutedDicts.size() )
    Qt4x5::Url::addQueryItem( req,  "muted", mutedDicts );

  // Update headwords history
  emit sendWordToHistory( phrase.phrase );

  // Any search opened is probably irrelevant now
  closeSearch();

  // Clear highlight all button selection
  ui.highlightAllButton->setChecked(false);

  emit setExpandMode( expandOptionalParts );

  load( req );

  //QApplication::setOverrideCursor( Qt::WaitCursor );
  ui.definition->setCursor( Qt::WaitCursor );
}

void ArticleView::showDefinition( QString const & word, unsigned group,
                                  QString const & scrollTo,
                                  Contexts const & contexts_ )
{
  showDefinition( Config::InputPhrase::fromPhrase( word ), group, scrollTo, contexts_ );
}

void ArticleView::showDefinition( QString const & word, QStringList const & dictIDs,
                                  QRegularExpression const & searchRegExp, unsigned group,
                                  bool ignoreDiacritics )
{
  if( dictIDs.isEmpty() )
    return;

  // first, let's stop the player
  audioPlayer->stop();

  QUrl req;

  req.setScheme( "gdlookup" );
  req.setHost( "localhost" );
  Qt4x5::Url::addQueryItem( req, "word", word );
  Qt4x5::Url::addQueryItem( req, "dictionaries", dictIDs.join( ",") );
  Qt4x5::Url::addQueryItem( req, "regexp", searchRegExp.pattern() );
  if( !searchRegExp.patternOptions().testFlag( QRegularExpression::CaseInsensitiveOption ) )
    Qt4x5::Url::addQueryItem( req, "matchcase", "1" );
  Qt4x5::Url::addQueryItem( req, "group", QString::number( group ) );
  if( ignoreDiacritics )
    Qt4x5::Url::addQueryItem( req, "ignore_diacritics", "1" );

  // Update headwords history
  emit sendWordToHistory( word );

  // Any search opened is probably irrelevant now
  closeSearch();

  // Clear highlight all button selection
  ui.highlightAllButton->setChecked(false);

  emit setExpandMode( expandOptionalParts );

  load( req );

  //QApplication::setOverrideCursor( Qt::WaitCursor );
  ui.definition->setCursor( Qt::WaitCursor );
}

void ArticleView::showAnticipation()
{
  ui.definition->setHtml( "" );
  ui.definition->setCursor( Qt::WaitCursor );
  //QApplication::setOverrideCursor( Qt::WaitCursor );
}

void ArticleView::loadFinished( bool )
{
  QUrl url = ui.definition->url();

  QWebEnginePage * page = ui.definition->page();

  const QString expandScript =
    "var frames=document.getElementsByTagName('iframe');"
    "var were=false;"
    "for(var i=0;i<frames.length;i++){"
    " var f=frames[i];"
    " if(f.name && f.name.indexOf('gdexpandframe-')===0){"
    "  try{"
    "   var doc=f.contentDocument || (f.contentWindow?f.contentWindow.document:null);"
    "   var h=(doc && doc.body)?doc.body.scrollHeight:0;"
    "   f.style.display='block';"
    "   if(h){f.height=h;}"
    "   if(f.contentWindow && f.contentWindow.document){"
    "    f.contentWindow.document.addEventListener('click',function(ev){window.top.gdLastUrlText=ev.target.textContent;},true);"
    "    f.contentWindow.document.addEventListener('contextmenu',function(ev){window.top.gdLastUrlText=ev.target.textContent;},true);"
    "   }"
    "  }catch(e){}"
    "  were=true;"
    " }"
    "}"
    "were;";

  runJavaScriptSync( page, expandScript );

  page->runJavaScript( "gdCheckArticlesNumber();" );

  QVariantMap userData = currentHistoryUserData();
  if ( !userData.isEmpty() )
  {

    double sx = 0, sy = 0;
    bool moveToCurrentArticle = true;

    const QVariant sxValue = userData.value( "sx" );
    if ( sxValue.isValid() && sxValue.canConvert< double >() )
    {
      sx = sxValue.toDouble();
      moveToCurrentArticle = false;
    }

    const QVariant syValue = userData.value( "sy" );
    if ( syValue.isValid() && syValue.canConvert< double >() )
    {
      sy = syValue.toDouble();
      moveToCurrentArticle = false;
    }

    const QString currentArticle = userData.value( "currentArticle" ).toString();
    if( !currentArticle.isEmpty() )
    {
      setCurrentArticle( currentArticle, moveToCurrentArticle );
    }

    if ( sx != 0 || sy != 0 )
    {
      page->runJavaScript( QString( "window.scroll( %1, %2 );" ).arg( sx ).arg( sy ) );
    }
  }
  else
  {
    QString const scrollTo = Qt4x5::Url::queryItemValue( url, "scrollto" );
    if( isScrollTo( scrollTo ) )
      setCurrentArticle( scrollTo, true );
  }

  if( !Qt4x5::Url::queryItemValue( url, "gdanchor" ).isEmpty() )
  {
    QString anchor = QUrl::fromPercentEncoding( Qt4x5::Url::encodedQueryItemValue( url, "gdanchor" ) );
    QString escapedAnchor = anchor;
    escapedAnchor.replace( "\\", "\\\\" );
    escapedAnchor.replace( "\"", "\\\"" );

    QString script =
      QString( "var anchor=\"%1\";" ).arg( escapedAnchor ) +
      "var n=anchor.indexOf('_');"
      "if(n===33) n=anchor.indexOf('_', n+1); else n=0;"
      "if(n>0){"
      " var prefix=anchor.substring(0,34);"
      " var original=anchor.substring(n+1);"
      " var rx=new RegExp(prefix+'[0-9a-f]*_'+original);"
      " var els=document.querySelectorAll('a[name],a[id]');"
      " for(var i=0;i<els.length;i++){"
      "  var name=els[i].getAttribute('name')||'';"
      "  var id=els[i].getAttribute('id')||'';"
      "  var match=name.match(rx)||id.match(rx);"
      "  if(match){window.location.hash=match[0];return;}"
      " }"
      "}"
      "window.location.hash=anchor;";

    page->runJavaScript( script );
  }

  emit pageLoaded( this );

  if( Qt4x5::Url::hasQueryItem( ui.definition->url(), "regexp" ) )
    highlightFTSResults();
}

void ArticleView::handleTitleChanged( QString const & title )
{
  if( !title.isEmpty() ) // Qt 5.x WebKit raise signal titleChanges(QString()) while navigation within page
    emit titleChanged( this, title );
}

void ArticleView::handleUrlChanged( QUrl const & url )
{
  QIcon icon;

  unsigned group = getGroup( url );

  if ( group )
  {
    // Find the group's instance corresponding to the fragment value
    for( unsigned x = 0; x < groups.size(); ++x )
      if ( groups[ x ].id == group )
      {
        // Found it

        icon = groups[ x ].makeIcon();
        break;
      }
  }

  emit iconChanged( this, icon );
}

unsigned ArticleView::getGroup( QUrl const & url )
{
  if ( url.scheme() == "gdlookup" && Qt4x5::Url::hasQueryItem( url, "group" ) )
    return Qt4x5::Url::queryItemValue( url, "group" ).toUInt();

  return 0;
}

QStringList ArticleView::getArticlesList()
{
  return evaluateJavaScriptVariableSafe( ui.definition->page(), "gdArticleContents" )
      .toString().trimmed().split( ' ', Qt4x5::skipEmptyParts() );
}

QString ArticleView::getActiveArticleId()
{
  QString currentArticle = getCurrentArticle();
  if ( !isScrollTo( currentArticle ) )
    return QString(); // Incorrect id

  return dictionaryIdFromScrollTo( currentArticle );
}

QString ArticleView::getCurrentArticle()
{
  QVariant v = evaluateJavaScriptVariableSafe( ui.definition->page(), "gdCurrentArticle" );

  if ( v.typeId() == QMetaType::QString )
    return v.toString();
  else
    return QString();
}

void ArticleView::jumpToDictionary( QString const & id, bool force )
{
  QString targetArticle = scrollToFromDictionaryId( id );

  // jump only if neceessary, or when forced
  if ( force || targetArticle != getCurrentArticle() )
  {
    setCurrentArticle( targetArticle, true );
  }
}

bool ArticleView::setCurrentArticle( QString const & id, bool moveToIt )
{
  if ( !isScrollTo( id ) )
    return false; // Incorrect id

  if ( !ui.definition->isVisible() )
    return false; // No action on background page, scrollIntoView there don't work

  QString const dictionaryId = dictionaryIdFromScrollTo( id );
  if( !getArticlesList().contains( dictionaryId ) )
    return false;

  if ( moveToIt )
  {
    ui.definition->page()->runJavaScript(
      QString(
        "(function(){"
        "var el=document.getElementById('%1');"
        "if(el){el.scrollIntoView({behavior:'smooth',block:'start'});}"
        "})();" )
        .arg( id ) );
  }

  ui.definition->page()->runJavaScript(
    QString( "gdMakeArticleActive( '%1' );" ).arg( dictionaryId ) );

  return true;
}

void ArticleView::selectCurrentArticle()
{
  ui.definition->page()->runJavaScript(
        QString( "gdSelectArticle( '%1' );" ).arg( getActiveArticleId() ) );
}

bool ArticleView::isFramedArticle( QString const & ca )
{
  if ( ca.isEmpty() )
    return false;

  return runJavaScriptSync( ui.definition->page(),
               QString( "!!document.getElementById('gdexpandframe-%1');" )
                                          .arg( dictionaryIdFromScrollTo( ca ) ) ).toBool();
}

bool ArticleView::isExternalLink( QUrl const & url )
{
  return url.scheme() == "http" || url.scheme() == "https" ||
         url.scheme() == "ftp" || url.scheme() == "mailto" ||
         url.scheme() == "file";
}

void ArticleView::tryMangleWebsiteClickedUrl( QUrl & url, Contexts & contexts )
{
  // Don't try mangling audio urls, even if they are from the framed websites

  if( ( url.scheme() == "http" || url.scheme() == "https" )
      && ! Dictionary::WebMultimediaDownload::isAudioUrl( url ) )
  {
    // Maybe a link inside a website was clicked?

    QString ca = getCurrentArticle();

    if ( isFramedArticle( ca ) )
    {
      QVariant result = evaluateJavaScriptVariableSafe( ui.definition->page(), "gdLastUrlText" );

      if ( result.typeId() == QMetaType::QString )
      {
        // Looks this way
        contexts[ dictionaryIdFromScrollTo( ca ) ] = QString::fromLatin1( url.toEncoded() );

        QUrl target;

        QString queryWord = result.toString();

        // Empty requests are treated as no request, so we work this around by
        // adding a space.
        if ( queryWord.isEmpty() )
          queryWord = " ";

        target.setScheme( "gdlookup" );
        target.setHost( "localhost" );
        target.setPath( "/" + queryWord );

        url = target;
      }
    }
  }
}

void ArticleView::updateCurrentArticleFromCurrentFrame()
{
  // QWebEngine does not expose frames; the active article is updated via JS.
}

QString ArticleView::currentHistoryKey() const
{
  QWebEngineHistoryItem item = ui.definition->history()->currentItem();
  if ( !item.isValid() )
    return QString();

  return item.url().toString( QUrl::FullyEncoded );
}

QVariantMap ArticleView::currentHistoryUserData() const
{
  const QString key = currentHistoryKey();
  if ( key.isEmpty() )
    return QVariantMap();

  return historyUserDataByUrl.value( key );
}

void ArticleView::setCurrentHistoryUserData( const QVariantMap & userData )
{
  const QString key = currentHistoryKey();
  if ( key.isEmpty() )
    return;

  historyUserDataByUrl.insert( key, userData );
}

void ArticleView::saveHistoryUserData()
{
  // OPTIMIZATION: This function is called frequently (on navigation) and must be
  // non-blocking. All JavaScript evaluations are now async.
  
  QVariantMap userData = currentHistoryUserData();
  const QString historyKey = currentHistoryKey();
  
  if ( historyKey.isEmpty() )
    return;
  
  // Mark that we're collecting data for this key asynchronously
  // Use previous value as fallback - it's good enough for history restoration
  setCurrentHistoryUserData( userData );
  
  QWebEnginePage * page = ui.definition->page();
  if ( !page )
    return;
  
  // Retrieve ALL JS data in a single async call to minimize overhead
  // This is much faster than multiple separate calls
  page->runJavaScript(
    "[{"
    "  article: (typeof(gdCurrentArticle) !== 'undefined' && gdCurrentArticle !== undefined) ? gdCurrentArticle : null,"
    "  sx: window.scrollX,"
    "  sy: window.scrollY"
    "}][0];",
    [this, historyKey]( const QVariant & result ) {
      if ( result.typeId() != QMetaType::QVariantMap )
        return;
      
      QVariantMap data = result.toMap();
      QVariantMap userData = historyUserDataByUrl.value( historyKey );
      
      if ( data.contains( "article" ) )
        userData[ "currentArticle" ] = data[ "article" ];
      if ( data.contains( "sx" ) )
        userData[ "sx" ] = data[ "sx" ].toDouble();
      if ( data.contains( "sy" ) )
        userData[ "sy" ] = data[ "sy" ].toDouble();
      
      historyUserDataByUrl.insert( historyKey, userData );
    }
  );
}

void ArticleView::load( QUrl const & url )
{
  saveHistoryUserData();
  ui.definition->load( url );
}

void ArticleView::cleanupTemp()
{
  QSet< QString >::iterator it = desktopOpenedTempFiles.begin();
  while( it != desktopOpenedTempFiles.end() )
  {
    if( QFile::remove( *it ) )
      it = desktopOpenedTempFiles.erase( it );
    else
      ++it;
  }
}

bool ArticleView::handleF3( QObject * /*obj*/, QEvent * ev )
{
  if ( ev->type() == QEvent::ShortcutOverride
       || ev->type() == QEvent::KeyPress )
  {
    QKeyEvent * ke = static_cast<QKeyEvent *>( ev );
    if ( ke->key() == Qt::Key_F3 && isSearchOpened() ) {
      if ( !ke->modifiers() )
      {
        if( ev->type() == QEvent::KeyPress )
          on_searchNext_clicked();

        ev->accept();
        return true;
      }

      if ( ke->modifiers() == Qt::ShiftModifier )
      {
        if( ev->type() == QEvent::KeyPress )
          on_searchPrevious_clicked();

        ev->accept();
        return true;
      }
    }
    if ( ke->key() == Qt::Key_F3 && ftsSearchIsOpened )
    {
      if ( !ke->modifiers() )
      {
        if( ev->type() == QEvent::KeyPress )
          on_ftsSearchNext_clicked();

        ev->accept();
        return true;
      }

      if ( ke->modifiers() == Qt::ShiftModifier )
      {
        if( ev->type() == QEvent::KeyPress )
          on_ftsSearchPrevious_clicked();

        ev->accept();
        return true;
      }
    }
  }

  return false;
}

bool ArticleView::eventFilter( QObject * obj, QEvent * ev )
{
  if( ev->type() == QEvent::Gesture )
  {
    Gestures::GestureResult result;
    QPoint pt;

    bool handled = Gestures::handleGestureEvent( obj, ev, result, pt );

    if( handled )
    {
      if( result == Gestures::ZOOM_IN )
        zoomIn();
      else
      if( result == Gestures::ZOOM_OUT )
        zoomOut();
      else
      if( result == Gestures::SWIPE_LEFT )
        back();
      else
      if( result == Gestures::SWIPE_RIGHT )
        forward();
      else
      if( result == Gestures::SWIPE_UP || result == Gestures::SWIPE_DOWN )
      {
        int delta = result == Gestures::SWIPE_UP ? -120 : 120;
        QWidget *widget = static_cast< QWidget * >( obj );

        QWidget *child = widget->childAt( widget->mapFromGlobal( pt ) );
        if( child )
          widget = child;

        QWheelEvent whev( widget->mapFromGlobal( pt ), pt, QPoint(), QPoint( 0, delta ),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
        qApp->sendEvent( widget, &whev );
      }
    }

    return handled;
  }

  if( ev->type() == QEvent::MouseMove )
  {
    if( Gestures::isFewTouchPointsPresented() )
    {
      ev->accept();
      return true;
    }
  }

  if ( handleF3( obj, ev ) )
  {
    return true;
  }

  if ( obj == ui.definition )
  {
    if ( ev->type() == QEvent::MouseButtonPress ) {
      QMouseEvent * event = static_cast< QMouseEvent * >( ev );
      if ( event->button() == Qt::XButton1 ) {
        back();
        return true;
      }
      if ( event->button() == Qt::XButton2 ) {
        forward();
        return true;
      }
    }
    else
    if ( ev->type() == QEvent::KeyPress )
    {
      QKeyEvent * keyEvent = static_cast< QKeyEvent * >( ev );

      if ( keyEvent->modifiers() &
           ( Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier ) )
        return false; // A non-typing modifier is pressed

      if( keyEvent->key() == Qt::Key_Backspace )
        return !canGoBack();  // Prevent QWebView navigation to first (empty) page

      if ( keyEvent->key() == Qt::Key_Space ||
           keyEvent->key() == Qt::Key_Backspace ||
           keyEvent->key() == Qt::Key_Tab ||
           keyEvent->key() == Qt::Key_Backtab ||
           keyEvent->key() == Qt::Key_Return ||
           keyEvent->key() == Qt::Key_Enter )
        return false; // Those key have other uses than to start typing

      QString text = keyEvent->text();

      if ( text.size() )
      {
        emit typingEvent( text );
        return true;
      }
    }
  }
  else
    return QFrame::eventFilter( obj, ev );

  return false;
}

QString ArticleView::getMutedForGroup( unsigned group )
{
  if ( dictionaryBarToggled && dictionaryBarToggled->isChecked() )
  {
    // Dictionary bar is active -- mute the muted dictionaries
    Instances::Group const * groupInstance = groups.findGroup( group );

    // Find muted dictionaries for current group
    Config::Group const * grp = cfg.getGroup( group );
    Config::MutedDictionaries const * mutedDictionaries;
    if( group == Instances::Group::AllGroupId )
      mutedDictionaries = popupView ? &cfg.popupMutedDictionaries : &cfg.mutedDictionaries;
    else if( grp )
      mutedDictionaries = popupView ? &grp->popupMutedDictionaries : &grp->mutedDictionaries;
    else
      mutedDictionaries = popupView ? &cfg.popupMutedDictionaries : &cfg.mutedDictionaries;
    if( !mutedDictionaries )
      return QString();

    QStringList mutedDicts;

    if ( groupInstance )
    {
      for( unsigned x = 0; x < groupInstance->dictionaries.size(); ++x )
      {
        QString id = QString::fromStdString(
                       groupInstance->dictionaries[ x ]->getId() );

        if ( mutedDictionaries->contains( id ) )
          mutedDicts.append( id );
      }
    }

    if ( mutedDicts.size() )
      return mutedDicts.join( "," );
  }

  return QString();
}

void ArticleView::linkHovered( const QString & link )
{
  QString msg;
  QUrl url(link);

  if ( url.scheme() == "bres" )
  {
    msg = tr( "Resource" );
  }
  else
  if ( url.scheme() == "gdau" || Dictionary::WebMultimediaDownload::isAudioUrl( url ) )
  {
    msg = tr( "Audio" );
  }
  else
  if ( url.scheme() == "gdtts" )
  {
    msg = tr( "TTS Voice" );
  }
  else
  if ( url.scheme() == "gdpicture" )
  {
    msg = tr( "Picture" );
  }
  else
  if ( url.scheme() == "gdvideo" )
  {
    if ( url.path().isEmpty() )
    {
      msg = tr( "Video" );
    }
    else
    {
      QString path = url.path();
      if ( path.startsWith( '/' ) )
      {
        path = path.mid( 1 );
      }
      msg = tr( "Video: %1" ).arg( path );
    }
  }
  else
  if (url.scheme() == "gdlookup" || url.scheme().compare( "bword" ) == 0)
  {
    QString def = url.path();
    if (def.startsWith("/"))
    {
      def = def.mid( 1 );
    }

    if( Qt4x5::Url::hasQueryItem( url, "dict" ) )
    {
      // Link to other dictionary
      QString dictName( Qt4x5::Url::queryItemValue( url, "dict" ) );
      if( !dictName.isEmpty() )
        msg = tr( "Definition from dictionary \"%1\": %2" ).arg( dictName ).arg( def );
    }

    if( msg.isEmpty() )
    {
      if( def.isEmpty() && url.hasFragment() )
        msg = '#' + url.fragment(); // this must be a citation, footnote or backlink
      else
        msg = tr( "Definition: %1").arg( def );
    }
  }
  else
  {
    msg = link;
  }

  emit statusBarMessage( msg );
}

void ArticleView::linkClicked( QUrl const & url_ )
{
  Qt::KeyboardModifiers kmod = QApplication::keyboardModifiers();

  // Lock jump on links while Alt key is pressed
  if( kmod & Qt::AltModifier )
    return;

  const bool openInNewTab = !popupView &&
                            ( ui.definition->isMidButtonPressed() ||
                              ( kmod & ( Qt::ControlModifier | Qt::ShiftModifier ) ) );

  QTimer::singleShot( 0, this, [this, url_, openInNewTab]() {
    updateCurrentArticleFromCurrentFrame();

    QUrl url( url_ );
    Contexts contexts;

    tryMangleWebsiteClickedUrl( url, contexts );

    // OPTIMIZATION: Fetch current article asynchronously instead of blocking
    QWebEnginePage * page = ui.definition->page();
    if ( !page )
      return;
    
    page->runJavaScript(
      "(typeof(gdCurrentArticle) !== 'undefined' && gdCurrentArticle !== undefined) ? gdCurrentArticle : null;",
      [this, url, contexts, openInNewTab]( const QVariant & articleVar ) {
        QString currentArticle = articleVar.toString();
        
        if ( openInNewTab )
        {
          // Mid button or Control/Shift is currently pressed - open the link in new tab
          emit openLinkInNewTab( url, ui.definition->url(), currentArticle, contexts );
        }
        else
        {
          openLink( url, ui.definition->url(), currentArticle, contexts );
        }
      }
    );
  } );
}

void ArticleView::openLink( QUrl const & url, QUrl const & ref,
                            QString const & scrollTo,
                            Contexts const & contexts_ )
{
  qDebug() << "clicked" << url;

  Contexts contexts( contexts_ );

  if( url.scheme().compare( "gdpicture" ) == 0 )
    load( url );
  else
  if ( url.scheme().compare( "bword" ) == 0 )
  {
    if( Qt4x5::Url::hasQueryItem( ref, "dictionaries" ) )
    {
      QStringList dictsList = Qt4x5::Url::queryItemValue( ref, "dictionaries" )
                                          .split( ",", Qt4x5::skipEmptyParts() );

      showDefinition( url.path(), dictsList, QRegularExpression(), getGroup( ref ), false );
    }
    else
      showDefinition( url.path(),
                      getGroup( ref ), scrollTo, contexts );
  }
  else
  if ( url.scheme() == "gdlookup" ) // Plain html links inherit gdlookup scheme
  {
    if ( url.hasFragment() )
    {
      QUrl baseUrl( url );
      baseUrl.setFragment( QString() );
      QUrl currentUrl( ui.definition->url() );
      currentUrl.setFragment( QString() );

      if ( baseUrl == currentUrl )
      {
        QString fragment = url.fragment( QUrl::FullyEncoded );
        ui.definition->page()->runJavaScript(
          QString(
            "(function(){"
            "var id=\"%1\";"
            "var el=document.getElementById(id)||document.getElementsByName(id)[0];"
            "if(el){el.scrollIntoView({behavior:'smooth',block:'start'});return;}"
            "window.location.hash=id;"
            "})();" )
            .arg( fragment ) );
        return;
      }

      ui.definition->page()->runJavaScript(
        QString( "window.location = \"%1\"" ).arg( QString::fromUtf8( url.toEncoded() ) ) );
    }
    else
    {
      if( Qt4x5::Url::hasQueryItem( ref, "dictionaries" ) )
      {
        // Specific dictionary group from full-text search
        QStringList dictsList = Qt4x5::Url::queryItemValue( ref, "dictionaries" )
                                            .split( ",", Qt4x5::skipEmptyParts() );

        showDefinition( url.path().mid( 1 ), dictsList, QRegularExpression(), getGroup( ref ), false );
        return;
      }

      QString newScrollTo( scrollTo );
      if( Qt4x5::Url::hasQueryItem( url, "dict" ) )
      {
        // Link to other dictionary
        QString dictName( Qt4x5::Url::queryItemValue( url, "dict" ) );
        for( unsigned i = 0; i < allDictionaries.size(); i++ )
        {
          if( dictName.compare( QString::fromUtf8( allDictionaries[ i ]->getName().c_str() ) ) == 0 )
          {
            newScrollTo = scrollToFromDictionaryId( QString::fromUtf8( allDictionaries[ i ]->getId().c_str() ) );
            break;
          }
        }
      }

      if( Qt4x5::Url::hasQueryItem( url, "gdanchor" ) )
        contexts[ "gdanchor" ] = Qt4x5::Url::queryItemValue( url, "gdanchor" );

      showDefinition( url.path().mid( 1 ),
                      getGroup( ref ), newScrollTo, contexts );
    }
  }
  else
  if ( url.scheme() == "bres" || url.scheme() == "gdau" || url.scheme() == "gdvideo" ||
       Dictionary::WebMultimediaDownload::isAudioUrl( url ) )
  {
    // Download it

    // Clear any pending ones

    resourceDownloadRequests.clear();

    resourceDownloadUrl = url;

    if ( Dictionary::WebMultimediaDownload::isAudioUrl( url ) )
    {
      sptr< Dictionary::DataRequest > req =
        new Dictionary::WebMultimediaDownload( url, articleNetMgr );

      resourceDownloadRequests.push_back( req );

      connect( req.get(), SIGNAL( finished() ),
               this, SLOT( resourceDownloadFinished() ) );
    }
    else
    if ( url.scheme() == "gdau" && url.host() == "search" )
    {
      // Since searches should be limited to current group, we just do them
      // here ourselves since otherwise we'd need to pass group id to netmgr
      // and it should've been having knowledge of the current groups, too.

      unsigned currentGroup = getGroup( ref );

      std::vector< sptr< Dictionary::Class > > const * activeDicts = 0;

      if ( groups.size() )
      {
        for( unsigned x = 0; x < groups.size(); ++x )
          if ( groups[ x ].id == currentGroup )
          {
            activeDicts = &( groups[ x ].dictionaries );
            break;
          }
      }
      else
        activeDicts = &allDictionaries;

      if ( activeDicts )
      {
        unsigned preferred = UINT_MAX;
        if( url.hasFragment() )
        {
          // Find sound in the preferred dictionary
          QString preferredName = Qt4x5::Url::fragment( url );
          try
          {
            for( unsigned x = 0; x < activeDicts->size(); ++x )
            {
              if( preferredName.compare( QString::fromUtf8( (*activeDicts)[ x ]->getName().c_str() ) ) == 0 )
              {
                preferred = x;
                sptr< Dictionary::DataRequest > req =
                  (*activeDicts)[ x ]->getResource(
                    url.path().mid( 1 ).toUtf8().data() );

                resourceDownloadRequests.push_back( req );

                if ( !req->isFinished() )
                {
                  // Queued loading
                  connect( req.get(), SIGNAL( finished() ),
                           this, SLOT( resourceDownloadFinished() ) );
                }
                else
                {
                  // Immediate loading
                  if( req->dataSize() > 0 )
                  {
                    // Resource already found, stop next search
                    resourceDownloadFinished();
                    return;
                  }
                }
                break;
              }
            }
          }
          catch( std::exception & e )
          {
            emit statusBarMessage(
                  tr( "ERROR: %1" ).arg( e.what() ),
                  10000, QPixmap( ":/icons/error.svg" ) );
          }
        }
        for( unsigned x = 0; x < activeDicts->size(); ++x )
        {
          try
          {
            if( x == preferred )
              continue;

            sptr< Dictionary::DataRequest > req =
              (*activeDicts)[ x ]->getResource(
                url.path().mid( 1 ).toUtf8().data() );

            resourceDownloadRequests.push_back( req );

            if ( !req->isFinished() )
            {
              // Queued loading
              connect( req.get(), SIGNAL( finished() ),
                       this, SLOT( resourceDownloadFinished() ) );
            }
            else
            {
              // Immediate loading
              if( req->dataSize() > 0 )
              {
                // Resource already found, stop next search
                break;
              }
            }
          }
          catch( std::exception & e )
          {
            emit statusBarMessage(
                  tr( "ERROR: %1" ).arg( e.what() ),
                  10000, QPixmap( ":/icons/error.svg" ) );
          }
        }
      }
    }
    else
    {
      // Normal resource download
      QString contentType;

      sptr< Dictionary::DataRequest > req =
        articleNetMgr.getResource( url, contentType );

      if ( !req.get() )
      {
        // Request failed, fail
      }
      else
      if ( req->isFinished() && req->dataSize() >= 0 )
      {
        // Have data ready, handle it
        resourceDownloadRequests.push_back( req );
        resourceDownloadFinished();

        return;
      }
      else
      if ( !req->isFinished() )
      {
        // Queue to be handled when done

        resourceDownloadRequests.push_back( req );

        connect( req.get(), SIGNAL( finished() ),
                 this, SLOT( resourceDownloadFinished() ) );
      }
    }

    if ( resourceDownloadRequests.empty() ) // No requests were queued
    {
      QMessageBox::critical( this, "GoldenDict", tr( "The referenced resource doesn't exist." ) );
      return;
    }
    else
      resourceDownloadFinished(); // Check any requests finished already
  }
  else
  if ( url.scheme() == "gdprg" )
  {
    // Program. Run it.
    QString id( url.host() );

    for( Config::Programs::const_iterator i = cfg.programs.begin();
         i != cfg.programs.end(); ++i )
    {
      if ( i->id == id )
      {
        // Found the corresponding program.
        Programs::RunInstance * req = new Programs::RunInstance;

        connect( req, SIGNAL(finished(QByteArray,QString)),
                 req, SLOT( deleteLater() ) );

        QString error;

        // Delete the request if it fails to start
        if ( !req->start( *i, url.path().mid( 1 ), error ) )
        {
          delete req;

          QMessageBox::critical( this, "GoldenDict",
                                 error );
        }

        return;
      }
    }

    // Still here? No such program exists.
    QMessageBox::critical( this, "GoldenDict",
                           tr( "The referenced audio program doesn't exist." ) );
  }
  else
  if ( url.scheme() == "gdtts" )
  {
// TODO: Port TTS
#if defined( Q_OS_WIN32 ) || defined( Q_OS_WIN )
    // Text to speech
    QString md5Id = Qt4x5::Url::queryItemValue( url, "engine" );
    QString text( url.path().mid( 1 ) );

    for ( Config::VoiceEngines::const_iterator i = cfg.voiceEngines.begin();
          i != cfg.voiceEngines.end(); ++i )
    {
      QString itemMd5Id = QString( QCryptographicHash::hash(
                                     i->id.toUtf8(),
                                     QCryptographicHash::Md5 ).toHex() );

      if ( itemMd5Id == md5Id )
      {
        SpeechClient * speechClient = new SpeechClient( *i, this );
        connect( speechClient, SIGNAL( finished() ), speechClient, SLOT( deleteLater() ) );
        speechClient->tell( text );
        break;
      }
    }
#endif
  }
  else
  if ( isExternalLink( url ) )
  {
    // Use the system handler for the conventional external links
    QDesktopServices::openUrl( url );
  }
}

ResourceToSaveHandler * ArticleView::saveResource( const QUrl & url, const QString & fileName )
{
  return saveResource( url, ui.definition->url(), fileName );
}

ResourceToSaveHandler * ArticleView::saveResource( const QUrl & url, const QUrl & ref, const QString & fileName )
{
  ResourceToSaveHandler * handler = new ResourceToSaveHandler( this, fileName );
  sptr< Dictionary::DataRequest > req;

  if( url.scheme() == "bres" || url.scheme() == "gico" || url.scheme() == "gdau" || url.scheme() == "gdvideo" )
  {
    if ( url.host() == "search" )
    {
      // Since searches should be limited to current group, we just do them
      // here ourselves since otherwise we'd need to pass group id to netmgr
      // and it should've been having knowledge of the current groups, too.

      unsigned currentGroup = getGroup( ref );

      std::vector< sptr< Dictionary::Class > > const * activeDicts = 0;

      if ( groups.size() )
      {
        for( unsigned x = 0; x < groups.size(); ++x )
          if ( groups[ x ].id == currentGroup )
          {
            activeDicts = &( groups[ x ].dictionaries );
            break;
          }
      }
      else
        activeDicts = &allDictionaries;

      if ( activeDicts )
      {
        unsigned preferred = UINT_MAX;
        if( url.hasFragment() && url.scheme() == "gdau" )
        {
          // Find sound in the preferred dictionary
          QString preferredName = Qt4x5::Url::fragment( url );
          for( unsigned x = 0; x < activeDicts->size(); ++x )
          {
            try
            {
              if( preferredName.compare( QString::fromUtf8( (*activeDicts)[ x ]->getName().c_str() ) ) == 0 )
              {
                preferred = x;
                sptr< Dictionary::DataRequest > req =
                  (*activeDicts)[ x ]->getResource(
                    url.path().mid( 1 ).toUtf8().data() );

                handler->addRequest( req );

                if( req->isFinished() && req->dataSize() > 0 )
                {
                  handler->downloadFinished();
                  return handler;
                }
                break;
              }
            }
            catch( std::exception & e )
            {
              gdWarning( "getResource request error (%s) in \"%s\"\n", e.what(),
                         (*activeDicts)[ x ]->getName().c_str() );
            }
          }
        }
        for( unsigned x = 0; x < activeDicts->size(); ++x )
        {
          try
          {
            if( x == preferred )
              continue;

            req = (*activeDicts)[ x ]->getResource(
                    Qt4x5::Url::path( url ).mid( 1 ).toUtf8().data() );

            handler->addRequest( req );

            if( req->isFinished() && req->dataSize() > 0 )
            {
              // Resource already found, stop next search
              break;
            }
          }
          catch( std::exception & e )
          {
            gdWarning( "getResource request error (%s) in \"%s\"\n", e.what(),
                       (*activeDicts)[ x ]->getName().c_str() );
          }
        }
      }
    }
    else
    {
      // Normal resource download
      QString contentType;
      req = articleNetMgr.getResource( url, contentType );

      if( req.get() )
      {
        handler->addRequest( req );
      }
    }
  }
  else
  {
    req = new Dictionary::WebMultimediaDownload( url, articleNetMgr );

    handler->addRequest( req );
  }

  if ( handler->isEmpty() ) // No requests were queued
  {
    emit statusBarMessage(
          tr( "ERROR: %1" ).arg( tr( "The referenced resource doesn't exist." ) ),
          10000, QPixmap( ":/icons/error.svg" ) );
  }

  // Check already finished downloads
  handler->downloadFinished();

  return handler;
}

void ArticleView::updateMutedContents()
{
  QUrl currentUrl = ui.definition->url();

  if ( currentUrl.scheme() != "gdlookup" )
    return; // Weird url -- do nothing

  unsigned group = getGroup( currentUrl );

  if ( !group )
    return; // No group in url -- do nothing

  QString mutedDicts = getMutedForGroup( group );

  if ( Qt4x5::Url::queryItemValue( currentUrl, "muted" ) != mutedDicts )
  {
    // The list has changed -- update the url

    Qt4x5::Url::removeQueryItem( currentUrl, "muted" );

    if ( mutedDicts.size() )
    Qt4x5::Url::addQueryItem( currentUrl, "muted", mutedDicts );

    load( currentUrl );

    //QApplication::setOverrideCursor( Qt::WaitCursor );
    ui.definition->setCursor( Qt::WaitCursor );
  }
}

bool ArticleView::canGoBack()
{
  // First entry in a history is always an empty page,
  // so we skip it.
  return ui.definition->history()->currentItemIndex() > 1;
}

bool ArticleView::canGoForward()
{
  return ui.definition->history()->canGoForward();
}

void ArticleView::setSelectionBySingleClick( bool set )
{
  ui.definition->setSelectionBySingleClick( set );
}

void ArticleView::back()
{
  // Don't allow navigating back to page 0, which is usually the initial
  // empty page
  if ( canGoBack() )
  {
    saveHistoryUserData();
    ui.definition->back();
  }
}

void ArticleView::forward()
{
  saveHistoryUserData();
  ui.definition->forward();
}

void ArticleView::reload()
{
  QVariantMap userData = currentHistoryUserData();

  // OPTIMIZATION: Save current article asynchronously to avoid blocking on reload
  // Remove saved window position. Reloading occurs in response to changes that
  // may affect content height, so restoring the current window position can cause
  // uncontrolled jumps. Scrolling to the current article (i.e. jumping to the top
  // of it) is simple, reliable and predictable, if not ideal.
  userData[ "sx" ].clear();
  userData[ "sy" ].clear();

  setCurrentHistoryUserData( userData );

  // Asynchronously fetch and save current article
  QWebEnginePage * page = ui.definition->page();
  if ( page ) {
    const QString historyKey = currentHistoryKey();
    page->runJavaScript(
      "(typeof(gdCurrentArticle) !== 'undefined' && gdCurrentArticle !== undefined) ? gdCurrentArticle : null;",
      [this, historyKey]( const QVariant & articleVar ) {
        if ( !historyKey.isEmpty() ) {
          QVariantMap userData = historyUserDataByUrl.value( historyKey );
          userData[ "currentArticle" ] = articleVar.toString();
          historyUserDataByUrl.insert( historyKey, userData );
        }
      }
    );
  }

  ui.definition->reload();
}

bool ArticleView::hasSound()
{
  QVariant v = runJavaScriptSync( ui.definition->page(), "gdAudioLinks.first" );
  if ( v.typeId() == QMetaType::QString )
    return !v.toString().isEmpty();
  return false;
}

void ArticleView::playSound()
{
  QVariant v;
  QString soundScript;

  v = runJavaScriptSync( ui.definition->page(), "gdAudioLinks[gdAudioLinks.current]" );

  if ( v.typeId() == QMetaType::QString )
    soundScript = v.toString();

  // fallback to the first one
  if ( soundScript.isEmpty() )
  {
    v = runJavaScriptSync( ui.definition->page(), "gdAudioLinks.first" );
    if ( v.typeId() == QMetaType::QString )
      soundScript = v.toString();
  }

  if ( !soundScript.isEmpty() )
    openLink( QUrl::fromEncoded( soundScript.toUtf8() ), ui.definition->url() );
}

QString ArticleView::toHtml()
{
  return toHtmlSync( ui.definition->page() );
}

QString ArticleView::getTitle()
{
  return ui.definition->page()->title();
}

Config::InputPhrase ArticleView::getPhrase() const
{
  const QUrl url = ui.definition->url();
  return Config::InputPhrase( Qt4x5::Url::queryItemValue( url, "word" ),
                              Qt4x5::Url::queryItemValue( url, "punctuation_suffix" ) );
}

void ArticleView::print( QPrinter * printer ) const
{
  ui.definition->print( printer );
}

void ArticleView::contextMenuRequested( QPoint const & pos )
{
  QWebEngineContextMenuRequest * request = ui.definition->lastContextMenuRequest();
  if ( !request )
    return;

  updateCurrentArticleFromCurrentFrame();

  QMenu menu( this );

  QAction * followLink = 0;
  QAction * followLinkExternal = 0;
  QAction * followLinkNewTab = 0;
  QAction * lookupSelection = 0;
  QAction * lookupSelectionGr = 0;
  QAction * lookupSelectionNewTab = 0;
  QAction * lookupSelectionNewTabGr = 0;
  QAction * maxDictionaryRefsAction = 0;
  QAction * addWordToHistoryAction = 0;
  QAction * addHeaderToHistoryAction = 0;
  QAction * sendWordToInputLineAction = 0;
  QAction * saveImageAction = 0;
  QAction * saveSoundAction = 0;

  QUrl targetUrl( request->linkUrl() );
  Contexts contexts;

  tryMangleWebsiteClickedUrl( targetUrl, contexts );

  if ( !request->linkUrl().isEmpty() )
  {
    if ( !isExternalLink( targetUrl ) )
    {
      followLink = new QAction( tr( "&Open Link" ), &menu );
      menu.addAction( followLink );

      if ( !popupView )
      {
        followLinkNewTab = new QAction( QIcon( ":/icons/addtab.svg" ),
                                        tr( "Open Link in New &Tab" ), &menu );
        menu.addAction( followLinkNewTab );
      }
    }

    if ( isExternalLink( request->linkUrl() ) )
    {
      followLinkExternal = new QAction( tr( "Open Link in &External Browser" ), &menu );
      menu.addAction( followLinkExternal );
      menu.addAction( ui.definition->page()->action( QWebEnginePage::CopyLinkToClipboard ) );
    }
  }

  QUrl imageUrl;
  if( !popupView && request->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage )
  {
    imageUrl = request->mediaUrl();
    if( !imageUrl.isEmpty() )
    {
      menu.addAction( ui.definition->page()->action( QWebEnginePage::CopyImageToClipboard ) );
      saveImageAction = new QAction( tr( "Save &image..." ), &menu );
      menu.addAction( saveImageAction );
    }
  }

  if( !popupView && ( targetUrl.scheme() == "gdau"
                      || Dictionary::WebMultimediaDownload::isAudioUrl( targetUrl ) ) )
  {
    saveSoundAction = new QAction( tr( "Save s&ound..." ), &menu );
    menu.addAction( saveSoundAction );
  }

  QString selectedText = request->selectedText();
  QString text = selectedText.trimmed();

  if ( text.size() && text.size() < 60 )
  {
    if( text.isRightToLeft() )
    {
      text.insert( 0, (ushort)0x202E );
      text.append( (ushort)0x202C );
    }

    lookupSelection = new QAction( tr( "&Look up \"%1\"" ).
                                   arg( text ),
                                   &menu );
    menu.addAction( lookupSelection );

    if ( !popupView )
    {
      lookupSelectionNewTab = new QAction( QIcon( ":/icons/addtab.svg" ),
                                           tr( "Look up \"%1\" in &New Tab" ).
                                           arg( text ),
                                           &menu );
      menu.addAction( lookupSelectionNewTab );

      sendWordToInputLineAction = new QAction( tr( "Send \"%1\" to input line" ).
                                               arg( text ),
                                               &menu );
      menu.addAction( sendWordToInputLineAction );
    }

    addWordToHistoryAction = new QAction( tr( "&Add \"%1\" to history" ).
                                          arg( text ),
                                          &menu );
    menu.addAction( addWordToHistoryAction );

    Instances::Group const * altGroup =
      ( groupComboBox && groupComboBox->getCurrentGroup() !=  getGroup( ui.definition->url() )  ) ?
        groups.findGroup( groupComboBox->getCurrentGroup() ) : 0;

    if ( altGroup )
    {
      QIcon icon = altGroup->icon.size() ? QIcon( ":/flags/" + altGroup->icon ) :
                   QIcon();

      lookupSelectionGr = new QAction( icon, tr( "Look up \"%1\" in %2" ).
                                       arg( text ).
                                       arg( altGroup->name ), &menu );
      menu.addAction( lookupSelectionGr );

      if ( !popupView )
      {
        lookupSelectionNewTabGr = new QAction( QIcon( ":/icons/addtab.svg" ),
                                               tr( "Look up \"%1\" in %2 in &New Tab" ).
                                               arg( text ).
                                               arg( altGroup->name ), &menu );
        menu.addAction( lookupSelectionNewTabGr );
      }
    }
  }

  if( text.isEmpty() && !cfg.preferences.storeHistory)
  {
    QString txt = ui.definition->title();
    if( txt.size() > 60 )
      txt = txt.left( 60 ) + "...";

    addHeaderToHistoryAction = new QAction( tr( "&Add \"%1\" to history" ).
                                            arg( txt ),
                                            &menu );
    menu.addAction( addHeaderToHistoryAction );
  }

  if ( selectedText.size() )
  {
    menu.addAction( ui.definition->page()->action( QWebEnginePage::Copy ) );
    menu.addAction( &copyAsTextAction );
  }
  else
  {
    menu.addAction( &selectCurrentArticleAction );
    menu.addAction( ui.definition->page()->action( QWebEnginePage::SelectAll ) );
  }

  map< QAction *, QString > tableOfContents;

  QStringList ids = getArticlesList();

  if ( !menu.isEmpty() && ids.size() )
    menu.addSeparator();

  unsigned refsAdded = 0;
  bool maxDictionaryRefsReached = false;

  for( QStringList::const_iterator i = ids.constBegin(); i != ids.constEnd();
       ++i, ++refsAdded )
  {
    for( unsigned x = allDictionaries.size(); x--; )
    {
      if ( allDictionaries[ x ]->getId() == i->toUtf8().data() )
      {
        QAction * action = 0;
        if ( refsAdded == cfg.preferences.maxDictionaryRefsInContextMenu )
        {
          maxDictionaryRefsAction = new QAction( ".........", &menu );
          action = maxDictionaryRefsAction;
          maxDictionaryRefsReached = true;
        }
        else
        {
          action = new QAction(
                  allDictionaries[ x ]->getIcon(),
                  QString::fromUtf8( allDictionaries[ x ]->getName().c_str() ),
                  &menu );
          action->setIconVisibleInMenu( true );
        }
        menu.addAction( action );

        tableOfContents[ action ] = *i;

        break;
      }
    }
    if( maxDictionaryRefsReached )
      break;
  }

  menu.addSeparator();
  menu.addAction( &inspectAction );

  if ( !menu.isEmpty() )
  {
    connect( this, SIGNAL( closePopupMenu() ), &menu, SLOT( close() ) );
    QAction * result = menu.exec( ui.definition->mapToGlobal( pos ) );

    if ( !result )
      return;

    if ( result == followLink )
      openLink( targetUrl, ui.definition->url(), getCurrentArticle(), contexts );
    else
    if ( result == followLinkExternal )
      QDesktopServices::openUrl( request->linkUrl() );
    else
    if ( result == lookupSelection )
      showDefinition( selectedText, getGroup( ui.definition->url() ), getCurrentArticle() );
    else
    if ( result == lookupSelectionGr && groupComboBox )
      showDefinition( selectedText, groupComboBox->getCurrentGroup(), QString() );
    else
    if ( result == addWordToHistoryAction )
      emit forceAddWordToHistory( selectedText );
    if ( result == addHeaderToHistoryAction )
      emit forceAddWordToHistory( ui.definition->title() );
    else
    if( result == sendWordToInputLineAction )
      emit sendWordToInputLine( selectedText );
    else
    if ( !popupView && result == followLinkNewTab )
      emit openLinkInNewTab( targetUrl, ui.definition->url(), getCurrentArticle(), contexts );
    else
    if ( !popupView && result == lookupSelectionNewTab )
      emit showDefinitionInNewTab( selectedText, getGroup( ui.definition->url() ),
                                   getCurrentArticle(), Contexts() );
    else
    if ( !popupView && result == lookupSelectionNewTabGr && groupComboBox )
      emit showDefinitionInNewTab( selectedText, groupComboBox->getCurrentGroup(),
                                   QString(), Contexts() );
    else
    if( result == saveImageAction || result == saveSoundAction )
    {
      QUrl url = ( result == saveImageAction ) ? imageUrl : targetUrl;
      QString savePath;
      QString fileName;

      if ( cfg.resourceSavePath.isEmpty() )
        savePath = QDir::homePath();
      else
      {
        savePath = QDir::fromNativeSeparators( cfg.resourceSavePath );
        if ( !QDir( savePath ).exists() )
          savePath = QDir::homePath();
      }

      QString name = Qt4x5::Url::path( url ).section( '/', -1 );

      if ( result == saveSoundAction )
      {
        if ( name.indexOf( '.' ) < 0 )
          name += ".wav";

        fileName = savePath + "/" + name;
        fileName = QFileDialog::getSaveFileName( parentWidget(), tr( "Save sound" ),
                                                 fileName,
                                                 tr( "Sound files (*.wav *.ogg *.oga *.mp3 *.mp4 *.aac *.flac *.mid *.wv *.ape);;All files (*.*)" ) );
      }
      else
      {
        if ( name[ 0 ] == '\x1E' )
          name.remove( 0, 1 );
        if ( name.length() && name[ name.length() - 1 ] == '\x1F' )
          name.chop( 1 );

        fileName = savePath + "/" + name;
        fileName = QFileDialog::getSaveFileName( parentWidget(), tr( "Save image" ),
                                                 fileName,
                                                 tr( "Image files (*.bmp *.jpg *.png *.tif);;All files (*.*)" ) );
      }

      if ( !fileName.isEmpty() )
      {
        QFileInfo fileInfo( fileName );
        emit storeResourceSavePath( QDir::toNativeSeparators( fileInfo.absoluteDir().absolutePath() ) );
        saveResource( url, ui.definition->url(), fileName );
      }
    }
    else
    {
      if ( !popupView && result == maxDictionaryRefsAction )
        emit showDictsPane();

      QString id = tableOfContents[ result ];

      if ( id.size() )
        setCurrentArticle( scrollToFromDictionaryId( id ), true );
    }
  }
}

void ArticleView::resourceDownloadFinished()
{
  if ( resourceDownloadRequests.empty() )
    return; // Stray signal

  // Find any finished resources
  for( list< sptr< Dictionary::DataRequest > >::iterator i =
       resourceDownloadRequests.begin(); i != resourceDownloadRequests.end(); )
  {
    if ( (*i)->isFinished() )
    {
      if ( (*i)->dataSize() >= 0 )
      {
        // Ok, got one finished, all others are irrelevant now

        vector< char > const & data = (*i)->getFullData();

        if ( resourceDownloadUrl.scheme() == "gdau" ||
             Dictionary::WebMultimediaDownload::isAudioUrl( resourceDownloadUrl ) )
        {
          // Audio data
          connect( audioPlayer.data(), SIGNAL( error( QString ) ), this, SLOT( audioPlayerError( QString ) ), Qt::UniqueConnection );
          QString errorMessage = audioPlayer->play( data.data(), data.size() );
          if( !errorMessage.isEmpty() )
            QMessageBox::critical( this, "GoldenDict", tr( "Failed to play sound file: %1" ).arg( errorMessage ) );
        }
        else
        {
          // Create a temporary file
          // Remove the ones previously used, if any
          cleanupTemp();
          QString fileName;

          {
            QTemporaryFile tmp(
              QDir::temp().filePath( "XXXXXX-" + resourceDownloadUrl.path().section( '/', -1 ) ), this );

            if ( !tmp.open() || (size_t) tmp.write( &data.front(), data.size() ) != data.size() )
            {
              QMessageBox::critical( this, "GoldenDict", tr( "Failed to create temporary file." ) );
              return;
            }

            tmp.setAutoRemove( false );

            desktopOpenedTempFiles.insert( fileName = tmp.fileName() );
          }

          if ( !QDesktopServices::openUrl( QUrl::fromLocalFile( fileName ) ) )
            QMessageBox::critical( this, "GoldenDict",
                                   tr( "Failed to auto-open resource file, try opening manually: %1." ).arg( fileName ) );
        }

        // Ok, whatever it was, it's finished. Remove this and any other
        // requests and finish.

        resourceDownloadRequests.clear();

        return;
      }
      else
      {
        // This one had no data. Erase it.
        resourceDownloadRequests.erase( i++ );
      }
    }
    else // Unfinished, wait.
      break;
  }

  if ( resourceDownloadRequests.empty() )
  {
    emit statusBarMessage(
          tr( "WARNING: %1" ).arg( tr( "The referenced resource failed to download." ) ),
          10000, QPixmap( ":/icons/error.svg" ) );
  }
}

void ArticleView::audioPlayerError( QString const & message )
{
  emit statusBarMessage( tr( "WARNING: Audio Player: %1" ).arg( message ),
                         10000, QPixmap( ":/icons/error.svg" ) );
}

void ArticleView::pasteTriggered()
{
  Config::InputPhrase phrase = cfg.preferences.sanitizeInputPhrase( QApplication::clipboard()->text() );

  if ( phrase.isValid() )
  {
    unsigned groupId = getGroup( ui.definition->url() );
    if ( groupId == 0 )
    {
      // We couldn't figure out the group out of the URL,
      // so let's try the currently selected group.
      groupId = groupComboBox->getCurrentGroup();
    }
    showDefinition( phrase, groupId, getCurrentArticle() );
  }
}

void ArticleView::moveOneArticleUp()
{
  QString current = getCurrentArticle();

  if ( current.size() )
  {
    QStringList lst = getArticlesList();

    int idx = lst.indexOf( dictionaryIdFromScrollTo( current ) );

    if ( idx != -1 )
    {
      --idx;

      if ( idx < 0 )
        idx = lst.size() - 1;

      setCurrentArticle( scrollToFromDictionaryId( lst[ idx ] ), true );
    }
  }
}

void ArticleView::moveOneArticleDown()
{
  QString current = getCurrentArticle();

  if ( current.size() )
  {
    QStringList lst = getArticlesList();

    int idx = lst.indexOf( dictionaryIdFromScrollTo( current ) );

    if ( idx != -1 )
    {
      idx = ( idx + 1 ) % lst.size();

      setCurrentArticle( scrollToFromDictionaryId( lst[ idx ] ), true );
    }
  }
}

void ArticleView::openSearch()
{
  if( !isVisible() )
    return;

  if( ftsSearchIsOpened )
    closeSearch();

  if ( !searchIsOpened )
  {
    ui.searchFrame->show();
    ui.searchText->setText( getTitle() );
    searchIsOpened = true;
  }

  ui.searchText->setFocus();
  ui.searchText->selectAll();

  // Clear any current selection
  if ( ui.definition->selectedText().size() )
  {
    ui.definition->page()->runJavaScript( "window.getSelection().removeAllRanges();" );
  }

  if ( ui.searchText->property( "noResults" ).toBool() )
  {
    ui.searchText->setProperty( "noResults", false );

    // Reload stylesheet
    reloadStyleSheet();
  }
}

void ArticleView::on_searchPrevious_clicked()
{
  if ( searchIsOpened )
    performFindOperation( false, true );
}

void ArticleView::on_searchNext_clicked()
{
  if ( searchIsOpened )
    performFindOperation( false, false );
}

void ArticleView::on_searchText_textEdited()
{
  performFindOperation( true, false );
}

void ArticleView::on_searchText_returnPressed()
{
  on_searchNext_clicked();
}

void ArticleView::on_searchCloseButton_clicked()
{
  closeSearch();
}

void ArticleView::on_searchCaseSensitive_clicked()
{
  performFindOperation( true, false );
}

void ArticleView::on_highlightAllButton_clicked()
{
  performFindOperation( false, false, true );
}

void ArticleView::onJsActiveArticleChanged(QString const & id)
{
  if ( !isScrollTo( id ) )
    return; // Incorrect id

  emit activeArticleChanged( this, dictionaryIdFromScrollTo( id ) );
}

void ArticleView::doubleClicked( QPoint pos )
{
  // OPTIMIZATION: Check for image click and get current article asynchronously
  // This avoids blocking the UI waiting for JavaScript results
  const QString script =
    QString( "var el=document.elementFromPoint(%1,%2);var url='';"
             "if(el && el.tagName && el.tagName.toLowerCase()==='img'){url=el.src;}"
             "[{ imageUrl: url, article: (typeof(gdCurrentArticle) !== 'undefined' ? gdCurrentArticle : null) }][0];" )
      .arg( pos.x() ).arg( pos.y() );
  
  QWebEnginePage * page = ui.definition->page();
  if ( !page )
    return;
  
  // Execute both checks asynchronously to avoid UI blocking
  page->runJavaScript( script, [this]( const QVariant & result ) {
    if ( result.typeId() != QMetaType::QVariantMap )
      return;
    
    QVariantMap data = result.toMap();
    QString imageUrlStr = data[ "imageUrl" ].toString();
    QString currentArticle = data[ "article" ].toString();
    
    QUrl imageUrl = QUrl::fromUserInput( imageUrlStr );

    if( imageUrl.isValid() && !imageUrl.isEmpty() )
    {
      // Double click on image; download it and transfer to external program

      // Clear any pending ones
      resourceDownloadRequests.clear();

      resourceDownloadUrl = imageUrl;
      sptr< Dictionary::DataRequest > req;

      if ( imageUrl.scheme() == "http" || imageUrl.scheme() == "https" || imageUrl.scheme() == "ftp" )
      {
        // Web resource
        req = new Dictionary::WebMultimediaDownload( imageUrl, articleNetMgr );
      }
      else
      if ( imageUrl.scheme() == "bres" || imageUrl.scheme() == "gdpicture" )
      {
        // Local resource
        QString contentType;
        req = articleNetMgr.getResource( imageUrl, contentType );
      }
      else
      {
        // Unsupported scheme
        gdWarning( "Unsupported url scheme \"%s\" to download image\n", imageUrl.scheme().toUtf8().data() );
        return;
      }

      if ( !req.get() )
      {
        // Request failed, fail
        gdWarning( "Can't create request to download image \"%s\"\n", imageUrl.toString().toUtf8().data() );
        return;
      }

      if ( req->isFinished() && req->dataSize() >= 0 )
      {
        // Have data ready, handle it
        resourceDownloadRequests.push_back( req );
        resourceDownloadFinished();
        return;
      }
      else
      if ( !req->isFinished() )
      {
        // Queue to be handled when done
        resourceDownloadRequests.push_back( req );
        connect( req.get(), SIGNAL( finished() ), this, SLOT( resourceDownloadFinished() ) );
      }
      if ( resourceDownloadRequests.empty() ) // No requests were queued
      {
        gdWarning( "The referenced resource \"%s\" doesn't exist\n", imageUrl.toString().toUtf8().data() ) ;
        return;
      }
      else
        resourceDownloadFinished(); // Check any requests finished already

      return;
    }

    // We might want to initiate translation of the selected word

    if ( cfg.preferences.doubleClickTranslates )
    {
      QString selectedText = ui.definition->selectedText();

      // Fast path: check size first (cheaper than Folding)
      if ( selectedText.size() > 0 && selectedText.size() < 60 &&
           Folding::applyWhitespaceOnly( gd::toWString( selectedText ) ).size() )
      {
        // Initiate translation - use the asynchronously-fetched currentArticle
        Qt::KeyboardModifiers kmod = QApplication::keyboardModifiers();
        if (kmod & (Qt::ControlModifier | Qt::ShiftModifier))
        { // open in new tab
          emit showDefinitionInNewTab( selectedText, getGroup( ui.definition->url() ),
                                       currentArticle, Contexts() );
        }
        else
        {
          QUrl const & ref = ui.definition->url();

          if( Qt4x5::Url::hasQueryItem( ref, "dictionaries" ) )
          {
            QStringList dictsList = Qt4x5::Url::queryItemValue(ref, "dictionaries" )
                                                .split( ",", Qt4x5::skipEmptyParts() );
            showDefinition( selectedText, dictsList, QRegularExpression(), getGroup( ref ), false );
          }
          else
            showDefinition( selectedText, getGroup( ref ), currentArticle );
        }
      }
    }
  } );
}


void ArticleView::performFindOperation( bool restart, bool backwards, bool checkHighlight )
{
  QString text = ui.searchText->text();

  if ( restart || checkHighlight )
  {
    if( restart ) {
      // Anyone knows how we reset the search position?
      // For now we resort to this hack:
      if ( ui.definition->selectedText().size() )
      {
        ui.definition->page()->runJavaScript( "window.getSelection().removeAllRanges();" );
      }
    }

    QWebEnginePage::FindFlags f;

    if ( ui.searchCaseSensitive->isChecked() )
      f |= QWebEnginePage::FindCaseSensitively;

    findTextSync( ui.definition, "", f );

    if( ui.highlightAllButton->isChecked() )
      findTextSync( ui.definition, text, f );

    if( checkHighlight )
      return;
  }

  QWebEnginePage::FindFlags f;

  if ( ui.searchCaseSensitive->isChecked() )
    f |= QWebEnginePage::FindCaseSensitively;

  if ( backwards )
    f |= QWebEnginePage::FindBackward;

  bool setMark = text.size() && !findTextSync( ui.definition, text, f );

  if ( ui.searchText->property( "noResults" ).toBool() != setMark )
  {
    ui.searchText->setProperty( "noResults", setMark );

    // Reload stylesheet
    reloadStyleSheet();
  }
}

void ArticleView::reloadStyleSheet()
{
  for( QWidget * w = parentWidget(); w; w = w->parentWidget() )
  {
    if ( w->styleSheet().size() )
    {
      w->setStyleSheet( w->styleSheet() );
      break;
    }
  }
}


bool ArticleView::closeSearch()
{
  if ( searchIsOpened )
  {
    ui.searchFrame->hide();
    ui.definition->setFocus();
    searchIsOpened = false;

    return true;
  }
  else
  if( ftsSearchIsOpened )
  {
    allMatches.clear();
    uniqueMatches.clear();
    ftsPosition = 0;
    ftsSearchIsOpened = false;

    ui.ftsSearchFrame->hide();
    ui.definition->setFocus();

    findTextSync( ui.definition, "", QWebEnginePage::FindFlags() );

    return true;
  }
  else
    return false;
}

bool ArticleView::isSearchOpened()
{
  return searchIsOpened;
}

void ArticleView::showEvent( QShowEvent * ev )
{
  QFrame::showEvent( ev );

  if ( !searchIsOpened )
    ui.searchFrame->hide();

  if( !ftsSearchIsOpened )
    ui.ftsSearchFrame->hide();
}

void ArticleView::receiveExpandOptionalParts( bool expand )
{
  if( expandOptionalParts != expand )
    switchExpandOptionalParts();
}

void ArticleView::switchExpandOptionalParts()
{
  expandOptionalParts = !expandOptionalParts;
  emit setExpandMode( expandOptionalParts );
  reload();
}

void ArticleView::copyAsText()
{
  QString text = ui.definition->selectedText();
  if( !text.isEmpty() )
    QApplication::clipboard()->setText( text );
}

void ArticleView::inspect()
{
  ui.definition->triggerPageAction( QWebEnginePage::InspectElement );
}

void ArticleView::highlightFTSResults()
{
  closeSearch();

  const QUrl & url = ui.definition->url();

  QString regString = Qt4x5::Url::queryItemValue( url, "regexp" );
  if( regString.isEmpty() )
    return;
  const bool ignoreDiacritics = Qt4x5::Url::hasQueryItem( url, "ignore_diacritics" );
  if( ignoreDiacritics )
    regString = gd::toQString( Folding::applyDiacriticsOnly( gd::toWString( regString ) ) );
  else
    regString = regString.remove( AccentMarkHandler::accentMark() );

  QRegularExpression regexp;
  if( Qt4x5::Url::hasQueryItem( url, "wildcards" ) )
    regexp.setPattern( wildcardsToRegexp( regString ) );
  else
    regexp.setPattern( regString );

  QRegularExpression::PatternOptions patternOptions = QRegularExpression::DotMatchesEverythingOption
                                                      | QRegularExpression::UseUnicodePropertiesOption
                                                      | QRegularExpression::MultilineOption
                                                      | QRegularExpression::InvertedGreedinessOption;
  if( !Qt4x5::Url::hasQueryItem( url, "matchcase" ) )
    patternOptions |= QRegularExpression::CaseInsensitiveOption;
  regexp.setPatternOptions( patternOptions );

  if( regexp.pattern().isEmpty() || !regexp.isValid() )
    return;

  sptr< AccentMarkHandler > marksHandler = ignoreDiacritics ?
                                           new DiacriticsHandler : new AccentMarkHandler;

  // Clear any current selection
  if ( ui.definition->selectedText().size() )
  {
    ui.definition->page()->runJavaScript( "window.getSelection().removeAllRanges();" );
  }

  QString pageText = toPlainTextSync( ui.definition->page() );
  marksHandler->setText( pageText );

  QRegularExpressionMatchIterator it = regexp.globalMatch( marksHandler->normalizedText() );
  while( it.hasNext() )
  {
    QRegularExpressionMatch match = it.next();

    // Mirror pos and matched length to original string
    int pos = match.capturedStart();
    int spos = marksHandler->mirrorPosition( pos );
    int matched = marksHandler->mirrorPosition( pos + match.capturedLength() ) - spos;

    // Add mark pos (if presented)
    while( spos + matched < pageText.length()
           && pageText[ spos + matched ].category() == QChar::Mark_NonSpacing )
      matched++;

    if( matched > FTS::MaxMatchLengthForHighlightResults )
    {
      gdWarning( "ArticleView::highlightFTSResults(): Too long match - skipped (matched length %lld, allowed %i)",
             static_cast<long long>( match.capturedLength() ),
             FTS::MaxMatchLengthForHighlightResults );
    }
    else
      allMatches.append( pageText.mid( spos, matched ) );
  }

  ftsSearchMatchCase = Qt4x5::Url::hasQueryItem( url, "matchcase" );

  QWebEnginePage::FindFlags flags;

  if( ftsSearchMatchCase )
    flags |= QWebEnginePage::FindCaseSensitively;

  if( allMatches.isEmpty() )
    ui.ftsSearchStatusLabel->setText( searchStatusMessageNoMatches() );
  else
  {
    highlightAllFtsOccurences( flags );
    if( findTextSync( ui.definition, allMatches.at( 0 ), flags ) )
    {
        ui.definition->page()->runJavaScript( QString( "%1=window.getSelection().getRangeAt(0);" )
                                   .arg( rangeVarName ) );
    }
    Q_ASSERT( ftsPosition == 0 );
    ui.ftsSearchStatusLabel->setText( searchStatusMessage( 1, allMatches.size() ) );
  }

  ui.ftsSearchFrame->show();
  ui.ftsSearchPrevious->setEnabled( false );
  ui.ftsSearchNext->setEnabled( allMatches.size()>1 );

  ftsSearchIsOpened = true;
}

void ArticleView::highlightAllFtsOccurences( QWebEnginePage::FindFlags flags )
{
  // Usually allMatches contains mostly duplicates. Thus searching for each element of
  // allMatches to highlight them takes a long time => collect unique elements into a
  // set and search for them instead.
  // Don't use QList::toSet() or QSet's range constructor because they reserve space
  // for QList::size() elements, whereas the final QSet size is likely 1 or 2.
  QSet< QString > uniqueMatches;
  for( int x = 0; x < allMatches.size(); ++x )
  {
    QString const & match = allMatches.at( x );
    // Consider words that differ only in case equal if the search is case-insensitive.
    uniqueMatches.insert( ftsSearchMatchCase ? match : match.toLower() );
  }

  for( QSet< QString >::const_iterator it = uniqueMatches.constBegin(); it != uniqueMatches.constEnd(); ++it )
    findTextSync( ui.definition, *it, flags );
}

void ArticleView::performFtsFindOperation( bool backwards )
{
  if( !ftsSearchIsOpened )
    return;

  if( allMatches.isEmpty() )
  {
    ui.ftsSearchStatusLabel->setText( searchStatusMessageNoMatches() );
    ui.ftsSearchNext->setEnabled( false );
    ui.ftsSearchPrevious->setEnabled( false );
    return;
  }

  QWebEnginePage::FindFlags flags;

  if( ftsSearchMatchCase )
    flags |= QWebEnginePage::FindCaseSensitively;


  // Restore saved highlighted selection
    ui.definition->page()->runJavaScript(
      QString( "var sel=window.getSelection();sel.removeAllRanges();sel.addRange(%1);" )
           .arg( rangeVarName ) );

  bool res;
  if( backwards )
  {
    if( ftsPosition > 0 )
    {
      res = findTextSync( ui.definition, allMatches.at( ftsPosition - 1 ),
              flags | QWebEnginePage::FindBackward );
      ftsPosition -= 1;
    }
    else
      res = findTextSync( ui.definition, allMatches.at( ftsPosition ),
              flags | QWebEnginePage::FindBackward );

    ui.ftsSearchPrevious->setEnabled( res );
    if( !ui.ftsSearchNext->isEnabled() )
      ui.ftsSearchNext->setEnabled( res );
  }
  else
  {
    if( ftsPosition < allMatches.size() - 1 )
    {
      res = findTextSync( ui.definition, allMatches.at( ftsPosition + 1 ), flags );
      ftsPosition += 1;
    }
    else
      res = findTextSync( ui.definition, allMatches.at( ftsPosition ), flags );

    ui.ftsSearchNext->setEnabled( res );
    if( !ui.ftsSearchPrevious->isEnabled() )
      ui.ftsSearchPrevious->setEnabled( res );
  }

  ui.ftsSearchStatusLabel->setText( searchStatusMessage( ftsPosition + 1, allMatches.size() ) );

  // Store new highlighted selection
    ui.definition->page()->runJavaScript(
      QString( "%1=window.getSelection().getRangeAt(0);" )
           .arg( rangeVarName ) );
}

void ArticleView::on_ftsSearchPrevious_clicked()
{
  performFtsFindOperation( true );
}

void ArticleView::on_ftsSearchNext_clicked()
{
  performFtsFindOperation( false );
}

#ifdef Q_OS_WIN32

void ArticleView::readTag( const QString & from, QString & to, int & count )
{
    QChar ch, prev_ch;
    bool inQuote = false, inDoublequote = false;

    to.append( ch = prev_ch = from[ count++ ] );
    while( count < from.size() )
    {
        ch = from[ count ];
        if( ch == '>' && !( inQuote || inDoublequote ) )
        {
            to.append( ch );
            break;
        }
        if( ch == '\'' )
            inQuote = !inQuote;
        if( ch == '\"' )
            inDoublequote = !inDoublequote;
        to.append( prev_ch = ch );
        count++;
    }
}

QString ArticleView::insertSpans( QString const & html )
{
    QChar ch;
    QString newContent;
    bool inSpan = false, escaped = false;

    /// Enclose every word in string (exclude tags) with <span></span>

    for( int i = 0; i < html.size(); i++ )
    {
        ch = html[ i ];
        if( ch == '&' )
        {
            escaped = true;
            if( inSpan )
            {
                newContent.append( "</span>" );
                inSpan = false;
            }
            newContent.append( ch );
            continue;
        }

        if( ch == '<' ) // Skip tag
        {
            escaped = false;
            if( inSpan )
            {
                newContent.append( "</span>" );
                inSpan = false;
            }
            readTag( html, newContent, i );
            continue;
        }

        if( escaped )
        {
            if( ch == ';' )
                escaped = false;
            newContent.append( ch );
            continue;
        }

        if( !inSpan && ( ch.isLetterOrNumber() || ch.isLowSurrogate() ) )
        {
            newContent.append( "<span>");
            inSpan = true;
        }

        if( inSpan && !( ch.isLetterOrNumber() || ch.isLowSurrogate() ) )
        {
            newContent.append( "</span>");
            inSpan = false;
        }

        if( ch.isLowSurrogate() )
        {
            newContent.append( ch );
            ch = html[ ++i ];
        }

        newContent.append( ch );
        if( ch == '-' && !( html[ i + 1 ] == ' ' || ( i > 0 && html[ i - 1 ] == ' ' ) ) )
            newContent.append( "<span style=\"font-size:0pt\"> </span>" );
    }
    if( inSpan )
        newContent.append( "</span>" );
    return newContent;
}

QString ArticleView::wordAtPoint( int x, int y )
{
  if( popupView )
    return QString();

  QPoint pos = mapFromGlobal( QPoint( x, y ) );
  QString script = QString(
    "var x=%1,y=%2;"
    "var range=null;"
    "if(document.caretRangeFromPoint){range=document.caretRangeFromPoint(x,y);}"
    "else if(document.caretPositionFromPoint){"
    " var p=document.caretPositionFromPoint(x,y);"
    " if(p){range=document.createRange();range.setStart(p.offsetNode,p.offset);range.setEnd(p.offsetNode,p.offset);}" 
    "}"
    "if(!range || !range.startContainer) return '';"
    "var node=range.startContainer;"
    "if(node.nodeType!==3){"
    " if(node.childNodes && node.childNodes.length>range.startOffset){node=node.childNodes[range.startOffset];}"
    "}"
    "if(!node || node.nodeType!==3) return '';"
    "var text=node.textContent;"
    "var offset=range.startOffset;"
    "var left=offset;"
    "var right=offset;"
    "while(left>0 && /\\w/.test(text[left-1])) left--;"
    "while(right<text.length && /\\w/.test(text[right])) right++;"
    "return text.substring(left,right);" )
    .arg( pos.x() ).arg( pos.y() );

  return runJavaScriptSync( ui.definition->page(), script ).toString();
}

#endif

ResourceToSaveHandler::ResourceToSaveHandler(ArticleView * view, QString const & fileName ) :
  QObject( view ),
  fileName( fileName ),
  alreadyDone( false )
{
  connect( this, SIGNAL( statusBarMessage( QString, int, QPixmap ) ),
           view, SIGNAL( statusBarMessage( QString, int, QPixmap ) ) );
}

void ResourceToSaveHandler::addRequest( sptr<Dictionary::DataRequest> req )
{
  if( !alreadyDone )
  {
    downloadRequests.push_back( req );

    connect( req.get(), SIGNAL( finished() ),
             this, SLOT( downloadFinished() ) );
  }
}

void ResourceToSaveHandler::downloadFinished()
{
  if ( downloadRequests.empty() )
    return; // Stray signal

  // Find any finished resources
  for( list< sptr< Dictionary::DataRequest > >::iterator i =
       downloadRequests.begin(); i != downloadRequests.end(); )
  {
    if ( (*i)->isFinished() )
    {
      if ( (*i)->dataSize() >= 0 && !alreadyDone )
      {
        QByteArray resourceData;
        vector< char > const & data = (*i)->getFullData();
        resourceData = QByteArray( data.data(), data.size() );

        // Write data to file

        if ( !fileName.isEmpty() )
        {
          QFileInfo fileInfo( fileName );
          QDir().mkpath( fileInfo.absoluteDir().absolutePath() );

          QFile file( fileName );
          if ( file.open( QFile::WriteOnly ) )
          {
            file.write( resourceData.data(), resourceData.size() );
            file.close();
          }

          if ( file.error() )
          {
            emit statusBarMessage(
                  tr( "ERROR: %1" ).arg( tr( "Resource saving error: " ) + file.errorString() ),
                  10000, QPixmap( ":/icons/error.svg" ) );
          }
        }
        alreadyDone = true;

        // Clear other requests

        downloadRequests.clear();
        break;
      }
      else
      {
        // This one had no data. Erase it.
        downloadRequests.erase( i++ );
      }
    }
    else // Unfinished, wait.
      break;
  }

  if ( downloadRequests.empty() )
  {
    if( !alreadyDone )
    {
      emit statusBarMessage(
            tr( "WARNING: %1" ).arg( tr( "The referenced resource failed to download." ) ),
            10000, QPixmap( ":/icons/error.svg" ) );
    }
    emit done();
    deleteLater();
  }
}

#include "articleview.moc"
