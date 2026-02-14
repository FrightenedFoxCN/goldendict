/* This file is (c) 2026
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#ifndef __LABELS_WIDGET_HH_INCLUDED__
#define __LABELS_WIDGET_HH_INCLUDED__

#include "config.hh"
#include "dictionary.hh"
#include "ui_labels.h"

#include <QMap>
#include <QSet>
#include <QHash>
#include <QStringList>

class LabelsWidget: public QWidget
{
  Q_OBJECT

public:
  LabelsWidget( QWidget * parent,
                Config::Class & cfg,
                std::vector< sptr< Dictionary::Class > > const & dictionaries );

  void refreshFromConfig();

signals:
  void labelsChanged();

private slots:
  void addLabel();
  void renameLabel();
  void removeLabel();
  void addToLabel();
  void removeFromLabel();
  void labelSelectionChanged();
  void availableDoubleClicked( const QModelIndex & index );
  void assignedDoubleClicked( const QModelIndex & index );

private:
  Ui::LabelsWidget ui;
  Config::Class & cfg;
  std::vector< sptr< Dictionary::Class > > const & dictionaries;

  QMap< QString, QString > labelDisplayByKey;
  QMap< QString, QSet< QString > > labelDictsByKey;
  QStringList labelOrderKeys;
  QHash< QString, sptr< Dictionary::Class > > dictById;

  void buildDictIndex();
  void rebuildLabelOrder();
  void rebuildLabelsList( QString const & keyToSelect );
  void refreshAvailableDisplay();
  void updateAssignedList();
  void updateButtons();
  void updateLabelFromAssignedList( QString const & key );
  void syncAllToConfig();
  QString currentLabelKey() const;
  QString normalizedLabelText( QString const & input ) const;
  QString labelKeyFor( QString const & label ) const;
};

#endif
