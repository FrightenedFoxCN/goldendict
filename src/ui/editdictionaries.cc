/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "editdictionaries.hh"
#include "loaddictionaries.hh"
#include "dictinfo.hh"
#include "mainwindow.hh"
#include <QMessageBox>

using std::vector;

EditDictionaries::EditDictionaries( QWidget * parent, Config::Class & cfg_,
                                    vector< sptr< Dictionary::Class > > & dictionaries_,
                                    Instances::Groups & groupInstances_,
                                    QNetworkAccessManager & dictNetMgr_ ):
  QDialog( parent, Qt::WindowSystemMenuHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint ),
  cfg( cfg_ ),
  dictionaries( dictionaries_ ),
  groupInstances( groupInstances_ ),
  dictNetMgr( dictNetMgr_ ),
  origCfg( cfg ),
  sources( this, cfg ),
  orderAndProps( new OrderAndProps( this, cfg, cfg.dictionaryOrder, cfg.inactiveDictionaries,
                                    dictionaries ) ),
  labelsWidget( new LabelsWidget( this, cfg, dictionaries ) ),
  dictionariesChanged( false ),
  groupsChanged( false ),
  lastCurrentTab( 0 )
, helpWindow( 0 )
, helpAction( this )
{
  origCfg.dictionaryOrder = orderAndProps->getCurrentDictionaryOrder();
  origCfg.inactiveDictionaries = orderAndProps->getCurrentInactiveDictionaries();
  origCfg.dictionaryLabels = cfg.dictionaryLabels;

  ui.setupUi( this );

  setWindowIcon( QIcon(":/icons/book.svg") );

  ui.tabs->clear();

  ui.tabs->addTab( &sources, QIcon(":/icons/reload.svg"), tr( "&Sources" ) );
  ui.tabs->addTab( orderAndProps.get(), QIcon(":/icons/book.svg"), tr( "&Dictionaries" ) );
  ui.tabs->addTab( labelsWidget.get(), QIcon(":/icons/bookcase.svg"), tr( "&Labels" ) );

  connect( ui.buttons, SIGNAL( clicked( QAbstractButton * ) ),
           this, SLOT( buttonBoxClicked( QAbstractButton * ) ) );

  connect( &sources, SIGNAL( rescan() ), this, SLOT( rescanSources() ) );

  connect( orderAndProps.get(), SIGNAL( showDictionaryHeadwords( QString const & ) ),
           this, SIGNAL( showDictionaryHeadwords( QString const & ) ) );

  connect( labelsWidget.get(), SIGNAL( labelsChanged() ),
           this, SLOT( labelsPanelChanged() ) );
  connect( orderAndProps.get(), SIGNAL( labelsChanged() ),
           this, SLOT( orderLabelsChanged() ) );

  connect( ui.buttons, SIGNAL( helpRequested() ),
           this, SLOT( helpRequested() ) );

  helpAction.setShortcut( QKeySequence( "F1" ) );
  helpAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );

  connect( &helpAction, SIGNAL( triggered() ),
           this, SLOT( helpRequested() ) );

  addAction( &helpAction );

}

void EditDictionaries::editGroup( unsigned id )
{
  (void)id;
  ui.tabs->setCurrentIndex( 1 );
}

void EditDictionaries::save()
{
  Config::Group newOrder = orderAndProps->getCurrentDictionaryOrder();
  Config::Group newInactive = orderAndProps->getCurrentInactiveDictionaries();
  bool labelsChanged = ( origCfg.dictionaryLabels != cfg.dictionaryLabels );

  if ( isSourcesChanged() )
    acceptChangedSources( false );

  if ( origCfg.dictionaryOrder != newOrder || origCfg.inactiveDictionaries != newInactive ||
       labelsChanged )
  {
    groupsChanged = true;
    cfg.dictionaryOrder = newOrder;
    cfg.inactiveDictionaries = newInactive;
  }
}

void EditDictionaries::accept()
{
  save();
  QDialog::accept();
}

void EditDictionaries::on_tabs_currentChanged( int index )
{
  if ( index == -1 || !isVisible() )
    return; // Sent upon the construction/destruction

  if ( !lastCurrentTab && index )
  {
    // We're switching away from the Sources tab -- if its contents were
    // changed, we need to either apply or reject now.

    if ( isSourcesChanged() )
    {
      ui.tabs->setCurrentIndex( 0 );

      QMessageBox question( QMessageBox::Question, tr( "Sources changed" ),
                            tr( "Some sources were changed. Would you like to accept the changes?" ),
                            QMessageBox::NoButton, this );

      QPushButton * accept = question.addButton( tr( "Accept" ), QMessageBox::AcceptRole );

      question.addButton( tr( "Cancel" ), QMessageBox::RejectRole );

      question.exec();

      if ( question.clickedButton() == accept )
      {
        acceptChangedSources( true );
        
        lastCurrentTab = index;
        ui.tabs->setCurrentIndex( index );
      }
      else
      {
        // Prevent tab from switching
        lastCurrentTab = 0;
        return;
      }
    }
  }
  lastCurrentTab = index;
}
void EditDictionaries::rescanSources()
{
  acceptChangedSources( true );
}

void EditDictionaries::labelsPanelChanged()
{
  orderAndProps->refreshLabels();
}

void EditDictionaries::orderLabelsChanged()
{
  labelsWidget->refreshFromConfig();
}

void EditDictionaries::buttonBoxClicked( QAbstractButton * button )
{
  if (ui.buttons->buttonRole(button) == QDialogButtonBox::ApplyRole) {
    if ( isSourcesChanged() ) {
      acceptChangedSources( true );
    }
    save();
  }
}

bool EditDictionaries::isSourcesChanged() const
{
  return sources.getPaths() != cfg.paths ||
         sources.getDictionaryFiles() != cfg.dictionaryFiles ||
         sources.getSoundDirs() != cfg.soundDirs ||
         sources.getHunspell() != cfg.hunspell ||
         sources.getTransliteration() != cfg.transliteration ||
         sources.getForvo() != cfg.forvo ||
         sources.getMediaWikis() != cfg.mediawikis ||
         sources.getWebSites() != cfg.webSites ||
         sources.getDictServers() != cfg.dictServers ||
         sources.getPrograms() != cfg.programs ||
         sources.getVoiceEngines() != cfg.voiceEngines;
}

void EditDictionaries::acceptChangedSources( bool rebuildGroups )
{
  dictionariesChanged = true;
  Config::Group savedOrder = orderAndProps->getCurrentDictionaryOrder();
  Config::Group savedInactive = orderAndProps->getCurrentInactiveDictionaries();

  cfg.paths = sources.getPaths();
  cfg.dictionaryFiles = sources.getDictionaryFiles();
  cfg.soundDirs = sources.getSoundDirs();
  cfg.hunspell = sources.getHunspell();
  cfg.transliteration = sources.getTransliteration();
  cfg.forvo = sources.getForvo();
  cfg.mediawikis = sources.getMediaWikis();
  cfg.webSites = sources.getWebSites();
  cfg.dictServers = sources.getDictServers();
  cfg.programs = sources.getPrograms();
  cfg.voiceEngines = sources.getVoiceEngines();

  groupInstances.clear(); // Those hold pointers to dictionaries, we need to
                          // free them.

  ui.tabs->setUpdatesEnabled( false );
  ui.tabs->removeTab( 1 );
  orderAndProps.reset();

  loadDictionaries( this, true, cfg, dictionaries, dictNetMgr );

  bool noOrderEdits = ( origCfg.dictionaryOrder == savedOrder );

  if ( noOrderEdits )
    savedOrder = cfg.dictionaryOrder;

  Instances::updateNames( savedOrder, dictionaries );

  bool noInactiveEdits = ( origCfg.inactiveDictionaries == savedInactive );

  if ( noInactiveEdits )
    savedInactive  = cfg.inactiveDictionaries;

  Instances::updateNames( savedInactive, dictionaries );

  if ( rebuildGroups )
  {
    orderAndProps = new OrderAndProps( this, cfg, savedOrder, savedInactive, dictionaries );
    ui.tabs->insertTab( 1, orderAndProps.get(), QIcon(":/icons/book.svg"), tr( "&Dictionaries" ) );
    ui.tabs->setUpdatesEnabled( true );

    connect( orderAndProps.get(), SIGNAL( labelsChanged() ),
             this, SLOT( orderLabelsChanged() ) );

    if ( noOrderEdits )
      origCfg.dictionaryOrder = orderAndProps->getCurrentDictionaryOrder();

    if ( noInactiveEdits )
      origCfg.inactiveDictionaries = orderAndProps->getCurrentInactiveDictionaries();

    origCfg.dictionaryLabels = cfg.dictionaryLabels;
  }

  labelsWidget->refreshFromConfig();
}

void EditDictionaries::helpRequested()
{
  if( !helpWindow )
  {
    MainWindow * mainWindow = qobject_cast< MainWindow * >( parentWidget() );
    if( mainWindow )
      mainWindow->closeGDHelp();

    helpWindow = new Help::HelpWindow( this, cfg );

    if( helpWindow )
    {
      #ifdef Q_OS_MAC
        helpWindow->setWindowFlags( Qt::Dialog );
      #else
        helpWindow->setWindowFlags( Qt::Window );
      #endif

      connect( helpWindow, SIGNAL( needClose() ),
               this, SLOT( closeHelp() ) );
      helpWindow->showHelpFor( "Manage dictionaries" );
      helpWindow->show();
      #ifdef Q_OS_MAC
        helpWindow->activateWindow();
      #endif
    }
  }
  else
  {
    if( !helpWindow->isVisible() )
      helpWindow->show();

    helpWindow->activateWindow();
  }
}

void EditDictionaries::closeHelp()
{
  if( helpWindow )
    helpWindow->hide();
}
