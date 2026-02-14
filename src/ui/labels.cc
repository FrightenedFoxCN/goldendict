/* This file is (c) 2026
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "labels.hh"
#include "ui_labels.h"
#include "groups_widgets.hh"

#include <algorithm>
#include <QInputDialog>
#include <QMessageBox>

LabelsWidget::LabelsWidget( QWidget * parent,
                            Config::Class & cfg_,
                            std::vector< sptr< Dictionary::Class > > const & dictionaries_ ):
  QWidget( parent ),
  cfg( cfg_ ),
  dictionaries( dictionaries_ )
{
  ui.setupUi( this );

  ui.availableDictionaries->setAsSource();
  ui.availableDictionaries->setDictionaryLabels( &cfg.dictionaryLabels );
  ui.assignedDictionaries->setDictionaryLabels( &cfg.dictionaryLabels );

  ui.searchLine->applyTo( ui.availableDictionaries );
  addAction( ui.searchLine->getFocusAction() );

  connect( ui.addLabel, SIGNAL( clicked() ), this, SLOT( addLabel() ) );
  connect( ui.renameLabel, SIGNAL( clicked() ), this, SLOT( renameLabel() ) );
  connect( ui.removeLabel, SIGNAL( clicked() ), this, SLOT( removeLabel() ) );

  connect( ui.addDictsToLabel, SIGNAL( clicked() ), this, SLOT( addToLabel() ) );
  connect( ui.removeDictsFromLabel, SIGNAL( clicked() ), this, SLOT( removeFromLabel() ) );

  connect( ui.labelsList, SIGNAL( itemSelectionChanged() ),
           this, SLOT( labelSelectionChanged() ) );

  connect( ui.availableDictionaries, SIGNAL( doubleClicked( QModelIndex ) ),
           this, SLOT( availableDoubleClicked( QModelIndex ) ) );
  connect( ui.assignedDictionaries, SIGNAL( doubleClicked( QModelIndex ) ),
           this, SLOT( assignedDoubleClicked( QModelIndex ) ) );

  refreshFromConfig();
}

void LabelsWidget::refreshFromConfig()
{
  QString currentKey = currentLabelKey();

  buildDictIndex();
  labelDisplayByKey.clear();
  labelDictsByKey.clear();

  for( Config::DictionaryLabels::const_iterator it = cfg.dictionaryLabels.begin();
       it != cfg.dictionaryLabels.end(); ++it )
  {
    QString dictId = it.key();
    QStringList labels = it.value();
    for( QStringList::const_iterator labelIt = labels.begin(); labelIt != labels.end(); ++labelIt )
    {
      QString display = labelIt->trimmed();
      if ( display.isEmpty() )
        continue;

      QString key = labelKeyFor( display );
      if ( !labelDisplayByKey.contains( key ) )
        labelDisplayByKey.insert( key, display );

      labelDictsByKey[ key ].insert( dictId );
    }
  }

  rebuildLabelOrder();
  rebuildLabelsList( currentKey );

  ui.availableDictionaries->populate( dictionaries );
  refreshAvailableDisplay();
}

void LabelsWidget::addLabel()
{
  bool ok = false;
  QString text = QInputDialog::getText( this,
                                        tr( "Add label" ),
                                        tr( "Label name:" ),
                                        QLineEdit::Normal,
                                        "",
                                        &ok );
  if ( !ok )
    return;

  QString normalized = normalizedLabelText( text );
  if ( normalized.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Invalid label" ), tr( "Label name cannot be empty." ) );
    return;
  }

  QString key = labelKeyFor( normalized );
  if ( labelDisplayByKey.contains( key ) )
  {
    labelDisplayByKey[ key ] = normalized;
    rebuildLabelOrder();
    rebuildLabelsList( key );
    syncAllToConfig();
    refreshAvailableDisplay();
    emit labelsChanged();
    return;
  }

  labelDisplayByKey.insert( key, normalized );
  labelDictsByKey.insert( key, QSet< QString >() );
  rebuildLabelOrder();
  rebuildLabelsList( key );
}

void LabelsWidget::renameLabel()
{
  QString currentKey = currentLabelKey();
  if ( currentKey.isEmpty() )
    return;

  QString currentName = labelDisplayByKey.value( currentKey );

  bool ok = false;
  QString text = QInputDialog::getText( this,
                                        tr( "Rename label" ),
                                        tr( "New label name:" ),
                                        QLineEdit::Normal,
                                        currentName,
                                        &ok );
  if ( !ok )
    return;

  QString normalized = normalizedLabelText( text );
  if ( normalized.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Invalid label" ), tr( "Label name cannot be empty." ) );
    return;
  }

  QString newKey = labelKeyFor( normalized );

  if ( newKey == currentKey )
  {
    labelDisplayByKey[ currentKey ] = normalized;
    rebuildLabelOrder();
    rebuildLabelsList( currentKey );
    syncAllToConfig();
    refreshAvailableDisplay();
    emit labelsChanged();
    return;
  }

  if ( labelDisplayByKey.contains( newKey ) )
  {
    QMessageBox::warning( this, tr( "Duplicate label" ),
                          tr( "A label with this name already exists." ) );
    return;
  }

  labelDisplayByKey.insert( newKey, normalized );
  labelDictsByKey.insert( newKey, labelDictsByKey.value( currentKey ) );
  labelDisplayByKey.remove( currentKey );
  labelDictsByKey.remove( currentKey );

  rebuildLabelOrder();
  syncAllToConfig();
  rebuildLabelsList( newKey );
  refreshAvailableDisplay();
  emit labelsChanged();
}

void LabelsWidget::removeLabel()
{
  QString currentKey = currentLabelKey();
  if ( currentKey.isEmpty() )
    return;

  QString labelName = labelDisplayByKey.value( currentKey );
  if ( QMessageBox::question( this, tr( "Remove label" ),
         tr( "Are you sure you want to remove the label <b>%1</b>?" ).arg( labelName ),
         QMessageBox::Yes, QMessageBox::Cancel ) != QMessageBox::Yes )
    return;

  labelDisplayByKey.remove( currentKey );
  labelDictsByKey.remove( currentKey );

  rebuildLabelOrder();
  syncAllToConfig();
  rebuildLabelsList( QString() );
  refreshAvailableDisplay();
  emit labelsChanged();
}

void LabelsWidget::addToLabel()
{
  QString key = currentLabelKey();
  if ( key.isEmpty() )
    return;

  ui.assignedDictionaries->getModel()->addSelectedUniqueFromModel(
    ui.availableDictionaries->selectionModel() );

  updateLabelFromAssignedList( key );
  syncAllToConfig();
  refreshAvailableDisplay();
  emit labelsChanged();
}

void LabelsWidget::removeFromLabel()
{
  QString key = currentLabelKey();
  if ( key.isEmpty() )
    return;

  ui.assignedDictionaries->getModel()->removeSelectedRows(
    ui.assignedDictionaries->selectionModel() );

  updateLabelFromAssignedList( key );
  syncAllToConfig();
  refreshAvailableDisplay();
  emit labelsChanged();
}

void LabelsWidget::labelSelectionChanged()
{
  updateAssignedList();
  updateButtons();
}

void LabelsWidget::availableDoubleClicked( const QModelIndex & index )
{
  (void)index;
  addToLabel();
}

void LabelsWidget::assignedDoubleClicked( const QModelIndex & index )
{
  (void)index;
  removeFromLabel();
}

void LabelsWidget::buildDictIndex()
{
  dictById.clear();
  for( unsigned i = 0; i < dictionaries.size(); ++i )
  {
    sptr< Dictionary::Class > dict = dictionaries[ i ];
    if ( dict )
    {
      QString id = QString::fromUtf8( dict->getId().c_str() );
      dictById.insert( id, dict );
    }
  }
}

void LabelsWidget::rebuildLabelOrder()
{
  labelOrderKeys = labelDisplayByKey.keys();
  std::sort( labelOrderKeys.begin(), labelOrderKeys.end(),
             [this]( QString const & a, QString const & b ) {
               return labelDisplayByKey.value( a ).localeAwareCompare(
                        labelDisplayByKey.value( b ) ) < 0;
             } );
}

void LabelsWidget::rebuildLabelsList( QString const & keyToSelect )
{
  ui.labelsList->clear();
  for( QStringList::const_iterator it = labelOrderKeys.begin(); it != labelOrderKeys.end(); ++it )
  {
    QString display = labelDisplayByKey.value( *it );
    QListWidgetItem * item = new QListWidgetItem( display );
    item->setData( Qt::UserRole, *it );
    ui.labelsList->addItem( item );
  }

  int toSelect = -1;
  if ( !keyToSelect.isEmpty() )
  {
    for ( int i = 0; i < ui.labelsList->count(); ++i )
    {
      QListWidgetItem * item = ui.labelsList->item( i );
      if ( item && item->data( Qt::UserRole ).toString() == keyToSelect )
      {
        toSelect = i;
        break;
      }
    }
  }
  if ( toSelect < 0 && ui.labelsList->count() > 0 )
    toSelect = 0;

  if ( toSelect >= 0 )
    ui.labelsList->setCurrentRow( toSelect );

  updateAssignedList();
  updateButtons();
}

void LabelsWidget::refreshAvailableDisplay()
{
  DictListModel * model = ui.availableDictionaries->getModel();
  int rows = model->rowCount( QModelIndex() );
  if ( rows > 0 )
  {
    QModelIndex first = model->index( 0, 0 );
    QModelIndex last = model->index( rows - 1, 0 );
    emit model->dataChanged( first, last );
  }
}

void LabelsWidget::updateAssignedList()
{
  QString key = currentLabelKey();
  std::vector< sptr< Dictionary::Class > > assigned;

  if ( !key.isEmpty() )
  {
    QSet< QString > dictIds = labelDictsByKey.value( key );
    for ( QSet< QString >::const_iterator it = dictIds.begin(); it != dictIds.end(); ++it )
    {
      if ( dictById.contains( *it ) )
        assigned.push_back( dictById.value( *it ) );
    }
  }

  ui.assignedDictionaries->populate( assigned, dictionaries );
}

void LabelsWidget::updateButtons()
{
  bool hasLabel = !currentLabelKey().isEmpty();
  ui.renameLabel->setEnabled( hasLabel );
  ui.removeLabel->setEnabled( hasLabel );
  ui.addDictsToLabel->setEnabled( hasLabel );
  ui.removeDictsFromLabel->setEnabled( hasLabel );
}

void LabelsWidget::updateLabelFromAssignedList( QString const & key )
{
  if ( key.isEmpty() )
    return;

  QSet< QString > dictIds;
  std::vector< sptr< Dictionary::Class > > const & assigned =
    ui.assignedDictionaries->getCurrentDictionaries();

  for ( unsigned i = 0; i < assigned.size(); ++i )
  {
    sptr< Dictionary::Class > dict = assigned[ i ];
    if ( dict )
    {
      QString id = QString::fromUtf8( dict->getId().c_str() );
      dictIds.insert( id );
    }
  }

  labelDictsByKey[ key ] = dictIds;
}

void LabelsWidget::syncAllToConfig()
{
  QMap< QString, QStringList > labelsForDict;

  for ( QStringList::const_iterator it = labelOrderKeys.begin(); it != labelOrderKeys.end(); ++it )
  {
    QString key = *it;
    QString label = labelDisplayByKey.value( key );
    if ( label.isEmpty() )
      continue;

    QSet< QString > dictIds = labelDictsByKey.value( key );
    if ( dictIds.isEmpty() )
      continue;

    for ( QSet< QString >::const_iterator dictIt = dictIds.begin(); dictIt != dictIds.end(); ++dictIt )
      labelsForDict[ *dictIt ].push_back( label );
  }

  cfg.dictionaryLabels.clear();
  for ( QMap< QString, QStringList >::const_iterator it = labelsForDict.begin();
        it != labelsForDict.end(); ++it )
    cfg.dictionaryLabels.insert( it.key(), it.value() );
}

QString LabelsWidget::currentLabelKey() const
{
  QListWidgetItem * item = ui.labelsList->currentItem();
  if ( !item )
    return QString();
  return item->data( Qt::UserRole ).toString();
}

QString LabelsWidget::normalizedLabelText( QString const & input ) const
{
  return input.trimmed();
}

QString LabelsWidget::labelKeyFor( QString const & label ) const
{
  return label.toCaseFolded();
}
