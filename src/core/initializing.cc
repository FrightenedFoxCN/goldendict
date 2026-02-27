/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include <QIcon>
#include "initializing.hh"
#include <QCloseEvent>

#if defined( Q_OS_WIN32 )
#include <qt_windows.h>
#include <uxtheme.h>

WindowsStyle::WindowsStyle()
{
  style = QStyleFactory::create( "windows" );
}

WindowsStyle & WindowsStyle::instance()
{
  static WindowsStyle ws;
  return ws;
}

#endif

Initializing::Initializing( QWidget * parent, bool showOnStartup ): QDialog( parent ),
  indexingWorkersActive( 0 ),
  indexingWorkersTotal( 0 ),
  indexingCompletedCount( 0 ),
  indexingCandidatesTotal( 0 )
{
  ui.setupUi( this );
  setWindowFlags( Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                  Qt::WindowMinimizeButtonHint );

  #ifndef Q_OS_MAC
    setWindowIcon( QIcon( ":/icons/programicon.png" ) );
  #else
    setWindowIcon( QIcon( ":/icons/macicon.png" ) );
  #endif

#if defined( Q_OS_WIN32 )

  // Style "windowsvista" in Qt5 turn off progress bar animation for classic appearance
  // We use simply "windows" style instead for this case

  oldBarStyle = 0;
  oldDeferredBarStyle = 0;

  if( QSysInfo::windowsVersion() >= QSysInfo::WV_VISTA
      && ( QSysInfo::windowsVersion() & QSysInfo::WV_NT_based )
      && !IsThemeActive() )
  {
    QStyle * barStyle = WindowsStyle::instance().getStyle();

    if( barStyle )
    {
      oldBarStyle = ui.progressBar->style();
      ui.progressBar->setStyle( barStyle );
      oldDeferredBarStyle = ui.deferredProgressBar->style();
      ui.deferredProgressBar->setStyle( barStyle );
    }
  }

#endif

  if ( showOnStartup )
  {
    ui.operation->setText( tr( "Please wait..." ) );
    ui.dictionary->hide();
    ui.progressBar->hide();
    ui.deferredOperation->hide();
    ui.deferredDictionary->hide();
    ui.deferredProgressBar->hide();
    ui.workerTasks->hide();
    show();
  }
}

void Initializing::indexing( QString const & dictionaryName )
{
  updateIndexingOperationText();
  Q_UNUSED( dictionaryName );
  ui.dictionary->hide();
  if( indexingCandidatesTotal > 0 )
  {
    ui.progressBar->setRange( 0, indexingCandidatesTotal );
    ui.progressBar->setValue( qMin( indexingCompletedCount, indexingCandidatesTotal ) );
  }
  else
  {
    ui.progressBar->setRange( 0, 0 );
  }
  ui.progressBar->show();
  ui.workerTasks->show();
  ui.deferredOperation->hide();
  ui.deferredDictionary->hide();
  ui.deferredProgressBar->hide();
  adjustSize();
  show();
}

void Initializing::indexingProgress( QString const & dictionaryName, int count )
{
  indexingCompletedCount = qMax( 0, count );
  updateIndexingOperationText();
  Q_UNUSED( dictionaryName );
  ui.dictionary->hide();
  if( indexingCandidatesTotal > 0 )
  {
    ui.progressBar->setRange( 0, indexingCandidatesTotal );
    ui.progressBar->setValue( qMin( indexingCompletedCount, indexingCandidatesTotal ) );
  }
  else
  {
    ui.progressBar->setRange( 0, 0 );
  }
  ui.progressBar->show();
  ui.workerTasks->show();
  ui.deferredOperation->hide();
  ui.deferredDictionary->hide();
  ui.deferredProgressBar->hide();
  adjustSize();
  show();
}

void Initializing::indexingCandidates( int totalCandidates )
{
  indexingCandidatesTotal = qMax( 0, totalCandidates );

  if( indexingCandidatesTotal > 0 )
  {
    ui.progressBar->setRange( 0, indexingCandidatesTotal );
    ui.progressBar->setValue( qMin( indexingCompletedCount, indexingCandidatesTotal ) );
  }
  else
  {
    ui.progressBar->setRange( 0, 0 );
  }

  updateIndexingOperationText();
}

void Initializing::indexingWorkers( int active, int total )
{
  indexingWorkersActive = qMax( 0, active );
  indexingWorkersTotal = qMax( 0, total );

  if( indexingWorkersTotal > 0 )
  {
    if( workerTasks.size() != indexingWorkersTotal )
      workerTasks = QVector< QString >( indexingWorkersTotal );
  }
  else
  {
    workerTasks.clear();
  }

  updateIndexingOperationText();
  updateWorkerTasksText();
}

void Initializing::indexingWorkerTask( int worker, int total, QString const & taskName, bool active )
{
  int safeTotal = qMax( 0, total );
  if( safeTotal > 0 && workerTasks.size() != safeTotal )
    workerTasks = QVector< QString >( safeTotal );

  int index = worker - 1;
  if( index >= 0 && index < workerTasks.size() )
  {
    workerTasks[ index ] = active ? taskName : QString();
  }

  updateWorkerTasksText();
}

void Initializing::updateIndexingOperationText()
{
  if( indexingCandidatesTotal > 0 && indexingWorkersTotal > 0 )
    ui.operation->setText( tr( "Please wait while indexing dictionaries (%1/%2, %3/%4 workers active)" )
                           .arg( qMin( indexingCompletedCount, indexingCandidatesTotal ) )
                           .arg( indexingCandidatesTotal )
                           .arg( indexingWorkersActive )
                           .arg( indexingWorkersTotal ) );
  else if( indexingCandidatesTotal > 0 )
    ui.operation->setText( tr( "Please wait while indexing dictionaries (%1/%2)" )
                           .arg( qMin( indexingCompletedCount, indexingCandidatesTotal ) )
                           .arg( indexingCandidatesTotal ) );
  else if( indexingWorkersTotal > 0 )
    ui.operation->setText( tr( "Please wait while indexing dictionaries (%1/%2 workers active)" )
                           .arg( indexingWorkersActive )
                           .arg( indexingWorkersTotal ) );
  else
    ui.operation->setText( tr( "Please wait while indexing dictionaries" ) );
}

void Initializing::deferredInitializing( QString const & dictionaryName, int current, int total )
{
  ui.operation->setText( tr( "Dictionary indexing completed" ) );
  ui.dictionary->hide();
  ui.progressBar->setRange( 0, 1 );
  ui.progressBar->setValue( 1 );
  ui.progressBar->show();

  ui.deferredOperation->setText( tr( "Please wait while initializing dictionaries" ) );
  ui.deferredOperation->show();

  if( total > 0 )
  {
    ui.deferredProgressBar->setRange( 0, total );
    ui.deferredProgressBar->setValue( qMin( current, total ) );
  }
  else
  {
    ui.deferredProgressBar->setRange( 0, 0 );
  }

  if( dictionaryName.isEmpty() )
    ui.deferredDictionary->setText( tr( "Preparing..." ) );
  else
    ui.deferredDictionary->setText( QString( "%1 (%2/%3)" ).arg( dictionaryName ).arg( current ).arg( total ) );

  ui.deferredDictionary->show();
  ui.deferredProgressBar->show();
  ui.workerTasks->hide();
  adjustSize();
  show();
}

void Initializing::updateWorkerTasksText()
{
  if( workerTasks.isEmpty() )
  {
    ui.workerTasks->setText( tr( "Workers: idle" ) );
    return;
  }

  QStringList lines;
  for( int i = 0; i < workerTasks.size(); ++i )
  {
    QString task = workerTasks.at( i );
    if( task.isEmpty() )
      task = tr( "idle" );

    lines << tr( "Worker %1: %2" ).arg( i + 1 ).arg( task ).toHtmlEscaped();
  }

  QString html = QString::fromLatin1( "<div style=\"line-height:1.45;\">" );
  for( QStringList::const_iterator it = lines.constBegin(); it != lines.constEnd(); ++it )
    html += QString::fromLatin1( "<div style=\"margin:3px 0;\">%1</div>" ).arg( *it );
  html += QString::fromLatin1( "</div>" );

  ui.workerTasks->setText( html );
}

void Initializing::closeEvent( QCloseEvent * ev )
{
  ev->ignore();
}

void Initializing::reject()
{
}

#if defined( Q_OS_WIN32 )

Initializing::~Initializing()
{
  if( oldBarStyle )
    ui.progressBar->setStyle( oldBarStyle );
  if( oldDeferredBarStyle )
    ui.deferredProgressBar->setStyle( oldDeferredBarStyle );
}

#endif
