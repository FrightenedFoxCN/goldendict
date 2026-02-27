/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#ifndef __LOADDICTIONARIES_HH_INCLUDED__
#define __LOADDICTIONARIES_HH_INCLUDED__

#include "initializing.hh"
#include "config.hh"
#include "dictionary.hh"

#include <QThread>
#include <QNetworkAccessManager>
#include <QMutex>
#include <set>

/// Use loadDictionaries() function below -- this is a helper thread class
class LoadDictionaries: public QThread, public Dictionary::Initializing
{
  Q_OBJECT

  QStringList nameFilters;
  Config::Paths const & paths;
  Config::DictionaryFiles const & dictionaryFiles;
  Config::SoundDirs const & soundDirs;
  Config::Hunspell const & hunspell;
  Config::Transliteration const & transliteration;
  std::vector< sptr< Dictionary::Class > > dictionaries;
  std::string exceptionText;
  int maxPictureWidth;
  unsigned int maxHeadwordSize;
  unsigned int maxHeadwordToExpand;
  std::set< std::string > queuedFiles;
  QMutex indexingCountMutex;
  int indexingCount;

public:

  LoadDictionaries( Config::Class const & cfg );

  virtual void run();

  std::vector< sptr< Dictionary::Class > > const & getDictionaries() const
  { return dictionaries; }

  /// Empty string means to exception occurred
  std::string const & getExceptionText() const
  { return exceptionText; }

signals:

  void indexingDictionarySignal( QString const & dictionaryName );
  void indexingProgressSignal( QString const & dictionaryName, int count );
  void indexingCandidatesSignal( int totalCandidates );
  void indexingWorkersSignal( int active, int total );
  void indexingWorkerTaskSignal( int worker, int total, QString const & taskName, bool active );
  void deferredInitializingSignal( QString const & dictionaryName, int current, int total );

public:

  virtual void indexingDictionary( std::string const & dictionaryName ) noexcept;
  void indexingProgress( QString const & dictionaryName, int count ) noexcept;
  void indexingCandidates( int totalCandidates ) noexcept;
  void indexingWorkers( int active, int total ) noexcept;
  void indexingWorkerTask( int worker, int total, QString const & taskName, bool active ) noexcept;
  void deferredInitializing( QString const & dictionaryName, int current, int total ) noexcept;

private:

  void handlePath( Config::Path const & );
  void handleFiles( std::vector< std::string > const & files );
  void queueDiscoveredFile( std::string const & fileName );
  void createDictionaries( std::vector< std::string > const & files );
  std::set< std::string > explicitFiles;
};

/// Loads all dictionaries mentioned in the configuration passed, into the
/// supplied array. When necessary, a window would pop up describing the process.
/// If showInitially is passed as true, the window will always popup.
/// If doDeferredInit is true (default), doDeferredInit() is done on all
/// dictionaries at the end.
void loadDictionaries( QWidget * parent, bool showInitially,
                       Config::Class const & cfg,
                       std::vector< sptr< Dictionary::Class > > &,
                       QNetworkAccessManager & dictNetMgr,
                       bool doDeferredInit = true );

/// Runs deferredInit() on all the given dictionaries. Useful when
/// loadDictionaries() was previously called with doDeferredInit = false.
void doDeferredInit( std::vector< sptr< Dictionary::Class > > & );
#endif

