/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "loaddictionaries.hh"
#include "initializing.hh"
#include "bgl.hh"
#include "stardict.hh"
#include "lsa.hh"
#include "dsl.hh"
#include "mediawiki.hh"
#include "sounddir.hh"
#include "hunspell.hh"
#include "dictdfiles.hh"
#include "romaji.hh"
#include "russiantranslit.hh"
#include "german.hh"
#include "greektranslit.hh"
#include "belarusiantranslit.hh"
#include "website.hh"
#include "forvo.hh"
#include "programs.hh"
#include "voiceengines.hh"
#include "gddebug.hh"
#include "fsencoding.hh"
#include "xdxf.hh"
#include "sdict.hh"
#include "aard.hh"
#include "zipsounds.hh"
#include "mdx.hh"
#include "zim.hh"
#include "dictserver.hh"
#include "slob.hh"
#include "gls.hh"



#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
#include "chinese.hh"
#endif

#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QThreadPool>
#include <QSemaphore>
#include <QMutex>
#include <QMutexLocker>
#include <QMetaObject>
#include <QElapsedTimer>

#include <set>
#include <functional>
#include <exception>

using std::set;

using std::string;
using std::vector;

namespace
{

int indexingWorkerCount()
{
  int idealThreads = QThread::idealThreadCount();
  if( idealThreads < 1 )
    idealThreads = 4;

  if( idealThreads == 1 )
    return 1;

  if( idealThreads == 2 )
    return 2;

  return qMin( 8, idealThreads - 1 );
}

enum CandidateFormat
{
  CandidateBgl,
  CandidateStarDict,
  CandidateLsa,
  CandidateDsl,
  CandidateDictd,
  CandidateXdxf,
  CandidateSdict,
  CandidateAard,
  CandidateZipSounds,
  CandidateMdx,
  CandidateGls,
#ifdef MAKE_ZIM_SUPPORT
  CandidateZim,
  CandidateSlob,
#endif
  CandidateUnknown
};

CandidateFormat detectCandidateFormat( string const & fileName )
{
  QString path = FsEncoding::decode( fileName.c_str() );

  if( path.endsWith( ".dsl.dz", Qt::CaseInsensitive ) )
    return CandidateDsl;
  if( path.endsWith( ".xdxf.dz", Qt::CaseInsensitive ) )
    return CandidateXdxf;
  if( path.endsWith( ".gls.dz", Qt::CaseInsensitive ) )
    return CandidateGls;

  if( path.endsWith( ".bgl", Qt::CaseInsensitive ) )
    return CandidateBgl;
  if( path.endsWith( ".ifo", Qt::CaseInsensitive ) )
    return CandidateStarDict;
  if( path.endsWith( ".lsa", Qt::CaseInsensitive ) ||
      path.endsWith( ".dat", Qt::CaseInsensitive ) )
    return CandidateLsa;
  if( path.endsWith( ".dsl", Qt::CaseInsensitive ) )
    return CandidateDsl;
  if( path.endsWith( ".index", Qt::CaseInsensitive ) )
    return CandidateDictd;
  if( path.endsWith( ".xdxf", Qt::CaseInsensitive ) )
    return CandidateXdxf;
  if( path.endsWith( ".dct", Qt::CaseInsensitive ) )
    return CandidateSdict;
  if( path.endsWith( ".aar", Qt::CaseInsensitive ) )
    return CandidateAard;
  if( path.endsWith( ".zips", Qt::CaseInsensitive ) )
    return CandidateZipSounds;
  if( path.endsWith( ".mdx", Qt::CaseInsensitive ) )
    return CandidateMdx;
  if( path.endsWith( ".gls", Qt::CaseInsensitive ) )
    return CandidateGls;
#ifdef MAKE_ZIM_SUPPORT
  if( path.endsWith( ".zim", Qt::CaseInsensitive ) ||
      path.endsWith( ".zimaa", Qt::CaseInsensitive ) )
    return CandidateZim;
  if( path.endsWith( ".slob", Qt::CaseInsensitive ) )
    return CandidateSlob;
#endif

  return CandidateUnknown;
}

vector< sptr< Dictionary::Class > > buildDictionariesForFile( string const & fileName,
                                                              string const & indicesDir,
                                                              Dictionary::Initializing & initializing,
                                                              int maxPictureWidth,
                                                              unsigned int maxHeadwordSize,
                                                              unsigned int maxHeadwordToExpand )
{
  vector< string > singleFile( 1, fileName );

  switch( detectCandidateFormat( fileName ) )
  {
    case CandidateBgl:
      return Bgl::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateStarDict:
      return Stardict::makeDictionaries( singleFile, indicesDir, initializing, maxHeadwordToExpand );
    case CandidateLsa:
      return Lsa::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateDsl:
      return Dsl::makeDictionaries( singleFile, indicesDir, initializing, maxPictureWidth, maxHeadwordSize );
    case CandidateDictd:
      return DictdFiles::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateXdxf:
      return Xdxf::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateSdict:
      return Sdict::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateAard:
      return Aard::makeDictionaries( singleFile, indicesDir, initializing, maxHeadwordToExpand );
    case CandidateZipSounds:
      return ZipSounds::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateMdx:
      return Mdx::makeDictionaries( singleFile, indicesDir, initializing );
    case CandidateGls:
      return Gls::makeDictionaries( singleFile, indicesDir, initializing );
#ifdef MAKE_ZIM_SUPPORT
    case CandidateZim:
      return Zim::makeDictionaries( singleFile, indicesDir, initializing, maxHeadwordToExpand );
    case CandidateSlob:
      return Slob::makeDictionaries( singleFile, indicesDir, initializing, maxHeadwordToExpand );
#endif
    case CandidateUnknown:
      return vector< sptr< Dictionary::Class > >();
  }

  return vector< sptr< Dictionary::Class > >();
}

class FileIndexTask: public QRunnable
{
  QString taskName;
  string fileName;
  string indicesDir;
  Dictionary::Initializing & initializing;
  int maxPictureWidth;
  unsigned int maxHeadwordSize;
  unsigned int maxHeadwordToExpand;
  vector< sptr< Dictionary::Class > > & dictionaries;
  QMutex & dictionariesMutex;
  int & activeWorkers;
  QMutex & activeWorkersMutex;
  std::function< void( int, int ) > workersReporter;
  std::vector< QString > & workerTasks;
  std::function< void( int, int, QString const &, bool ) > workerTaskReporter;
  int totalWorkers;
  std::exception_ptr & firstException;
  QMutex & exceptionMutex;
  QSemaphore & done;

public:
  FileIndexTask( string const & fileName_,
                 string const & indicesDir_,
                 Dictionary::Initializing & initializing_,
                 int maxPictureWidth_,
                 unsigned int maxHeadwordSize_,
                 unsigned int maxHeadwordToExpand_,
                   vector< sptr< Dictionary::Class > > & dictionaries_,
                   QMutex & dictionariesMutex_,
                   int & activeWorkers_,
                   QMutex & activeWorkersMutex_,
                   std::function< void( int, int ) > workersReporter_,
                   std::vector< QString > & workerTasks_,
                   std::function< void( int, int, QString const &, bool ) > workerTaskReporter_,
                   int totalWorkers_,
                   std::exception_ptr & firstException_,
                   QMutex & exceptionMutex_,
                   QSemaphore & done_ ):
            fileName( fileName_ ),
            indicesDir( indicesDir_ ),
            initializing( initializing_ ),
            maxPictureWidth( maxPictureWidth_ ),
            maxHeadwordSize( maxHeadwordSize_ ),
            maxHeadwordToExpand( maxHeadwordToExpand_ ),
    dictionaries( dictionaries_ ),
    dictionariesMutex( dictionariesMutex_ ),
    activeWorkers( activeWorkers_ ),
    activeWorkersMutex( activeWorkersMutex_ ),
    workersReporter( workersReporter_ ),
    workerTasks( workerTasks_ ),
    workerTaskReporter( workerTaskReporter_ ),
    totalWorkers( totalWorkers_ ),
    firstException( firstException_ ),
    exceptionMutex( exceptionMutex_ ),
    done( done_ )
  {
    QString decoded = FsEncoding::decode( fileName.c_str() );
    taskName = QFileInfo( decoded ).fileName();
    if( taskName.isEmpty() )
      taskName = decoded;
  }

  virtual void run()
  {
    int active = 0;
    int slot = -1;
    {
      QMutexLocker lock( &activeWorkersMutex );
      activeWorkers++;
      active = activeWorkers;
      for( int i = 0; i < totalWorkers; ++i )
      {
        if( workerTasks[ i ].isEmpty() )
        {
          workerTasks[ i ] = taskName;
          slot = i;
          break;
        }
      }
      if( slot < 0 && totalWorkers > 0 )
      {
        slot = 0;
        workerTasks[ slot ] = taskName;
      }
    }

    if( workersReporter )
      workersReporter( active, totalWorkers );
    if( workerTaskReporter && slot >= 0 )
      workerTaskReporter( slot + 1, totalWorkers, taskName, true );

    try
    {
      vector< sptr< Dictionary::Class > > result =
        buildDictionariesForFile( fileName, indicesDir, initializing,
                                  maxPictureWidth, maxHeadwordSize,
                                  maxHeadwordToExpand );
      if( !result.empty() )
      {
        QMutexLocker lock( &dictionariesMutex );
        dictionaries.insert( dictionaries.end(), result.begin(), result.end() );
      }
    }
    catch( ... )
    {
      QMutexLocker lock( &exceptionMutex );
      if( !firstException )
        firstException = std::current_exception();
    }

    {
      QMutexLocker lock( &activeWorkersMutex );
      activeWorkers--;
      active = activeWorkers;
      if( slot >= 0 && slot < totalWorkers )
        workerTasks[ slot ].clear();
    }

    if( workersReporter )
      workersReporter( active, totalWorkers );
    if( workerTaskReporter && slot >= 0 )
      workerTaskReporter( slot + 1, totalWorkers, QString(), false );

    done.release();
  }
};

class DeferredInitTask: public QRunnable
{
  sptr< Dictionary::Class > dictionary;
  QSemaphore & done;
  int & completed;
  int total;
  QMutex & completedMutex;
  std::exception_ptr & firstException;
  QMutex & exceptionMutex;
  std::function< void( QString const &, int, int ) > progressReporter;

public:
  DeferredInitTask( sptr< Dictionary::Class > const & dictionary_,
                    QSemaphore & done_,
                    int & completed_,
                    int total_,
                    QMutex & completedMutex_,
                    std::exception_ptr & firstException_,
                    QMutex & exceptionMutex_,
                    std::function< void( QString const &, int, int ) > progressReporter_ ):
    dictionary( dictionary_ ),
    done( done_ ),
    completed( completed_ ),
    total( total_ ),
    completedMutex( completedMutex_ ),
    firstException( firstException_ ),
    exceptionMutex( exceptionMutex_ ),
    progressReporter( progressReporter_ )
  {}

  virtual void run()
  {
    try
    {
      dictionary->deferredInit();
    }
    catch( ... )
    {
      QMutexLocker lock( &exceptionMutex );
      if( !firstException )
        firstException = std::current_exception();
    }

    int current;
    {
      QMutexLocker lock( &completedMutex );
      completed++;
      current = completed;
    }

    if( progressReporter )
      progressReporter( QString::fromUtf8( dictionary->getName().c_str() ), current, total );

    done.release();
  }
};

void runDeferredInitParallel( std::vector< sptr< Dictionary::Class > > & dictionaries,
                              std::function< void( QString const &, int, int ) > progressReporter )
{
  QElapsedTimer timer;
  timer.start();

  int const total = dictionaries.size();
  if( !total )
    return;

  if( progressReporter )
    progressReporter( QString(), 0, total );

  QThreadPool pool;
  pool.setMaxThreadCount( indexingWorkerCount() );

  QSemaphore done;
  QMutex completedMutex;
  QMutex exceptionMutex;
  std::exception_ptr firstException;
  int completed = 0;

  for( int i = 0; i < total; ++i )
  {
    pool.start( new DeferredInitTask( dictionaries[ i ], done, completed, total,
                                      completedMutex, firstException, exceptionMutex,
                                      progressReporter ) );
  }

  done.acquire( total );

  gdDebug( "Deferred init completed for %d dictionaries in %lld ms\n",
           total,
           static_cast< long long >( timer.elapsed() ) );

  if( firstException )
    std::rethrow_exception( firstException );
}

}

LoadDictionaries::LoadDictionaries( Config::Class const & cfg ):
  paths( cfg.paths ), dictionaryFiles( cfg.dictionaryFiles ), soundDirs( cfg.soundDirs ), hunspell( cfg.hunspell ),
  transliteration( cfg.transliteration ),
  exceptionText( "Load did not finish" ), // Will be cleared upon success
  maxPictureWidth( cfg.maxPictureWidth ),
  maxHeadwordSize( cfg.maxHeadwordSize ),
  maxHeadwordToExpand( cfg.maxHeadwordsToExpand ),
  indexingCount( 0 )
{
  // Populate name filters

  nameFilters << "*.bgl" << "*.ifo" << "*.lsa" << "*.dat"
              << "*.dsl" << "*.dsl.dz"  << "*.index" << "*.xdxf"
              << "*.xdxf.dz" << "*.dct" << "*.aar" << "*.zips"
              << "*.mdx" << "*.gls" << "*.gls.dz"
#ifdef MAKE_ZIM_SUPPORT
              << "*.zim" << "*.zimaa" << "*.slob"
#endif
;
}

void LoadDictionaries::run()
{
  {
    QMutexLocker locker( &indexingCountMutex );
    indexingCount = 0;
  }

  QElapsedTimer totalTimer;
  totalTimer.start();

  try
  {
    queuedFiles.clear();

    QElapsedTimer scanTimer;
    scanTimer.start();

    std::vector< string > explicitList;

    for( Config::DictionaryFiles::const_iterator i = dictionaryFiles.begin();
         i != dictionaryFiles.end(); ++i )
    {
      if ( i->isEmpty() )
        continue;

      QFileInfo info( *i );
      if ( !info.exists() || !info.isFile() )
        continue;

      string encoded = FsEncoding::encode( QDir::toNativeSeparators( info.absoluteFilePath() ) );
      if ( explicitFiles.insert( encoded ).second )
        explicitList.push_back( encoded );
    }

    if ( !explicitList.empty() )
      handleFiles( explicitList );

    for( Config::Paths::const_iterator i = paths.begin(); i != paths.end(); ++i )
      handlePath( *i );

    if( !queuedFiles.empty() )
    {
      vector< string > allCandidates;
      allCandidates.reserve( queuedFiles.size() );
      for( std::set< string >::const_iterator it = queuedFiles.begin(); it != queuedFiles.end(); ++it )
        allCandidates.push_back( *it );

      indexingCandidates( static_cast< int >( allCandidates.size() ) );
      createDictionaries( allCandidates );
    }
    else
    {
      indexingCandidates( 0 );
    }

    // Make soundDirs
    {
      vector< sptr< Dictionary::Class > > soundDirDictionaries =
        SoundDir::makeDictionaries( soundDirs, FsEncoding::encode( Config::getIndexDir() ), *this );

      dictionaries.insert( dictionaries.end(), soundDirDictionaries.begin(),
                           soundDirDictionaries.end() );
    }

    // Make hunspells
    {
      vector< sptr< Dictionary::Class > > hunspellDictionaries =
        HunspellMorpho::makeDictionaries( hunspell );

      dictionaries.insert( dictionaries.end(), hunspellDictionaries.begin(),
                           hunspellDictionaries.end() );
    }

    gdDebug( "LoadDictionaries::run finished: %u dictionaries in %lld ms (scan/index stage %lld ms)\n",
             static_cast< unsigned >( dictionaries.size() ),
             static_cast< long long >( totalTimer.elapsed() ),
             static_cast< long long >( scanTimer.elapsed() ) );

    exceptionText.clear();
  }
  catch( std::exception & e )
  {
    exceptionText = e.what();
  }
}

void LoadDictionaries::handlePath( Config::Path const & path )
{
  QDir dir( path.path );

  QFileInfoList entries = dir.entryInfoList( nameFilters, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot );

  for( QFileInfoList::const_iterator i = entries.constBegin();
       i != entries.constEnd(); ++i )
  {
    QString fullName = i->absoluteFilePath();

    if ( path.recursive && i->isDir() )
    {
      // Make sure the path doesn't look like with dsl resources
      if ( !fullName.endsWith( ".dsl.files", Qt::CaseInsensitive ) &&
           !fullName.endsWith( ".dsl.dz.files", Qt::CaseInsensitive ) )
        handlePath( Config::Path( fullName, true ) );
    }

    if ( !i->isDir() )
    {
      string encoded = FsEncoding::encode( QDir::toNativeSeparators( fullName ) );
      if ( explicitFiles.find( encoded ) == explicitFiles.end() )
        queueDiscoveredFile( encoded );
    }
  }
}

void LoadDictionaries::handleFiles( std::vector< string > const & files )
{
  for( vector< string >::const_iterator it = files.begin(); it != files.end(); ++it )
    queueDiscoveredFile( *it );
}

void LoadDictionaries::queueDiscoveredFile( std::string const & fileName )
{
  queuedFiles.insert( fileName );
}

void LoadDictionaries::createDictionaries( std::vector< string > const & allFiles )
{
  QElapsedTimer timer;
  timer.start();

  string const indicesDir = FsEncoding::encode( Config::getIndexDir() );

  QThreadPool pool;
  pool.setMaxThreadCount( indexingWorkerCount() );

  QSemaphore done;
  QMutex dictionariesMutex;
  QMutex workersMutex;
  QMutex exceptionMutex;
  std::exception_ptr firstException;
  int activeWorkers = 0;
  int totalWorkers = pool.maxThreadCount();
  if( totalWorkers < 1 )
    totalWorkers = 1;
  std::vector< QString > workerTasks( totalWorkers );

  indexingWorkers( 0, totalWorkers );

  auto startTask = [&]( string const & fileName )
  {
    pool.start( new FileIndexTask( fileName, indicesDir, *this,
                                   maxPictureWidth, maxHeadwordSize, maxHeadwordToExpand,
                                   dictionaries, dictionariesMutex,
                                   activeWorkers, workersMutex,
                                   [this]( int active, int total )
                                   {
                                     indexingWorkers( active, total );
                                   },
                                   workerTasks,
                                   [this]( int worker, int total, QString const & name, bool active )
                                   {
                                     indexingWorkerTask( worker, total, name, active );
                                   },
                                   totalWorkers,
                                   firstException, exceptionMutex, done ) );
  };

  for( vector< string >::const_iterator it = allFiles.begin(); it != allFiles.end(); ++it )
    startTask( *it );

  if( !allFiles.empty() )
    done.acquire( allFiles.size() );

  if( firstException )
    std::rethrow_exception( firstException );

  indexingWorkers( 0, totalWorkers );
  for( int i = 0; i < totalWorkers; ++i )
    indexingWorkerTask( i + 1, totalWorkers, QString(), false );

  gdDebug( "Per-file indexing over %u candidate files finished in %lld ms\n",
           static_cast< unsigned >( allFiles.size() ),
           static_cast< long long >( timer.elapsed() ) );
}

void LoadDictionaries::indexingDictionary( string const & dictionaryName ) noexcept
{
  QString name = QString::fromUtf8( dictionaryName.c_str() );

  int count;
  {
    QMutexLocker locker( &indexingCountMutex );
    indexingCount++;
    count = indexingCount;
  }

  emit indexingDictionarySignal( name );
  emit indexingProgressSignal( name, count );
}

void LoadDictionaries::indexingProgress( QString const & dictionaryName, int count ) noexcept
{
  emit indexingProgressSignal( dictionaryName, count );
}

void LoadDictionaries::indexingCandidates( int totalCandidates ) noexcept
{
  emit indexingCandidatesSignal( totalCandidates );
}

void LoadDictionaries::indexingWorkers( int active, int total ) noexcept
{
  emit indexingWorkersSignal( active, total );
}

void LoadDictionaries::indexingWorkerTask( int worker,
                                           int total,
                                           QString const & taskName,
                                           bool active ) noexcept
{
  emit indexingWorkerTaskSignal( worker, total, taskName, active );
}

void LoadDictionaries::deferredInitializing( QString const & dictionaryName,
                                             int current,
                                             int total ) noexcept
{
  emit deferredInitializingSignal( dictionaryName, current, total );
}


void loadDictionaries( QWidget * parent, bool showInitially,
                       Config::Class const & cfg,
                       std::vector< sptr< Dictionary::Class > > & dictionaries,
                       QNetworkAccessManager & dictNetMgr,
                       bool doDeferredInit_ )
{
  QElapsedTimer totalTimer;
  totalTimer.start();

  dictionaries.clear();

  ::Initializing init( parent, showInitially );

  // Start a thread to load all the dictionaries

  LoadDictionaries loadDicts( cfg );

  QObject::connect( &loadDicts, SIGNAL( indexingDictionarySignal( QString const & ) ),
                    &init, SLOT( indexing( QString const & ) ) );
  QObject::connect( &loadDicts, SIGNAL( indexingProgressSignal( QString const &, int ) ),
                    &init, SLOT( indexingProgress( QString const &, int ) ) );
  QObject::connect( &loadDicts, SIGNAL( indexingCandidatesSignal( int ) ),
                    &init, SLOT( indexingCandidates( int ) ) );
  QObject::connect( &loadDicts, SIGNAL( indexingWorkersSignal( int, int ) ),
                    &init, SLOT( indexingWorkers( int, int ) ) );
  QObject::connect( &loadDicts, SIGNAL( indexingWorkerTaskSignal( int, int, QString const &, bool ) ),
                    &init, SLOT( indexingWorkerTask( int, int, QString const &, bool ) ) );
  QObject::connect( &loadDicts, SIGNAL( deferredInitializingSignal( QString const &, int, int ) ),
                    &init, SLOT( deferredInitializing( QString const &, int, int ) ) );

  QEventLoop localLoop;

  QObject::connect( &loadDicts, SIGNAL( finished() ),
                    &localLoop, SLOT( quit() ) );

  loadDicts.start();

  localLoop.exec();

  loadDicts.wait();

  if ( loadDicts.getExceptionText().size() )
  {
    QMessageBox::critical( parent, QCoreApplication::translate( "LoadDictionaries", "Error loading dictionaries" ),
                           QString::fromUtf8( loadDicts.getExceptionText().c_str() ) );

    return;
  }

  dictionaries = loadDicts.getDictionaries();

  ///// We create transliterations synchronously since they are very simple

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  // Make Chinese conversion
  {
    vector< sptr< Dictionary::Class > > chineseDictionaries =
      Chinese::makeDictionaries( cfg.transliteration.chinese );

    dictionaries.insert( dictionaries.end(), chineseDictionaries.begin(),
                         chineseDictionaries.end() );
  }
#endif

  // Make Romaji
  {
    vector< sptr< Dictionary::Class > > romajiDictionaries =
      Romaji::makeDictionaries( cfg.transliteration.romaji );

    dictionaries.insert( dictionaries.end(), romajiDictionaries.begin(),
                         romajiDictionaries.end() );
  }

  // Make Russian transliteration
  if ( cfg.transliteration.enableRussianTransliteration )
    dictionaries.push_back( RussianTranslit::makeDictionary() );

  // Make German transliteration
  if ( cfg.transliteration.enableGermanTransliteration )
    dictionaries.push_back( GermanTranslit::makeDictionary() );

  // Make Greek transliteration
  if ( cfg.transliteration.enableGreekTransliteration )
    dictionaries.push_back( GreekTranslit::makeDictionary() );

  // Make Belarusian transliteration
  if ( cfg.transliteration.enableBelarusianTransliteration )
  {
    vector< sptr< Dictionary::Class > > dicts = BelarusianTranslit::makeDictionaries();
    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  ///// We create MediaWiki dicts synchronously, since they use netmgr

  {
    vector< sptr< Dictionary::Class > > dicts =
      MediaWiki::makeDictionaries( loadDicts, cfg.mediawikis, dictNetMgr );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  ///// WebSites are very simple, no need to create them asynchronously
  {
    vector< sptr< Dictionary::Class > > dicts =
      WebSite::makeDictionaries( cfg.webSites, dictNetMgr );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  //// Forvo dictionaries

  {
    vector< sptr< Dictionary::Class > > dicts =
      Forvo::makeDictionaries( loadDicts, cfg.forvo, dictNetMgr );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  //// Programs
  {
    vector< sptr< Dictionary::Class > > dicts =
      Programs::makeDictionaries( cfg.programs );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  //// Text to Speech
  {
    vector< sptr< Dictionary::Class > > dicts =
      VoiceEngines::makeDictionaries( cfg.voiceEngines );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  {
    vector< sptr< Dictionary::Class > > dicts =
      DictServer::makeDictionaries( cfg.dictServers );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  GD_DPRINTF( "Load done\n" );

  // Remove any stale index files

  set< string > ids;
  std::pair< std::set< string >::iterator, bool > ret;

  for( unsigned x = dictionaries.size(); x--; )
  {
    ret = ids.insert( dictionaries[ x ]->getId() );
    if( !ret.second )
    {
      gdWarning( "Duplicate dictionary ID found: ID=%s, name=\"%s\", path=\"%s\"",
                 dictionaries[ x ]->getId().c_str(),
                 dictionaries[ x ]->getName().c_str(),
                 dictionaries[ x ]->getDictionaryFilenames().empty() ?
                   "" : dictionaries[ x ]->getDictionaryFilenames()[ 0 ].c_str()
                );
    }
  }

  QDir indexDir( Config::getIndexDir() );

  QStringList allIdxFiles = indexDir.entryList( QDir::Files );

  for( QStringList::const_iterator i = allIdxFiles.constBegin();
       i != allIdxFiles.constEnd(); ++i )
  {
    if ( ids.find( FsEncoding::encode( *i ) ) == ids.end()
         && i->size() == 32 )
      indexDir.remove( *i );
    else
    if ( i->endsWith( "_FTS" )
         && i->size() == 36
         && ids.find( FsEncoding::encode( i->left( 32 ) ) ) == ids.end() )
      indexDir.remove( *i );
  }

  // Run deferred inits

  if ( doDeferredInit_ )
  {
    QElapsedTimer deferredTimer;
    deferredTimer.start();

    runDeferredInitParallel( dictionaries,
      [&]( QString const & dictionaryName, int current, int total )
      {
        QMetaObject::invokeMethod( &loadDicts, "deferredInitializing", Qt::QueuedConnection,
                                   Q_ARG( QString, dictionaryName ),
                                   Q_ARG( int, current ),
                                   Q_ARG( int, total ) );
      } );

    gdDebug( "loadDictionaries deferred init time: %lld ms\n",
             static_cast< long long >( deferredTimer.elapsed() ) );
  }

  gdDebug( "loadDictionaries total time: %lld ms\n",
           static_cast< long long >( totalTimer.elapsed() ) );
}

void doDeferredInit( std::vector< sptr< Dictionary::Class > > & dictionaries )
{
  runDeferredInitParallel( dictionaries, std::function< void( QString const &, int, int ) >() );
}
