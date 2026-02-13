/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#ifndef __ORDERANDPROPS_HH_INCLUDED__
#define __ORDERANDPROPS_HH_INCLUDED__

#include "ui_orderandprops.h"
#include "groups_widgets.hh"
#include <QSortFilterProxyModel>

class OrderAndProps: public QWidget
{
  Q_OBJECT

public:

  OrderAndProps( QWidget * parent, Config::Class & cfg,
                 Config::Group const & dictionaryOrder,
                 Config::Group const & inactiveDictionaries,
                 std::vector< sptr< Dictionary::Class > > const & allDictionaries );

  Config::Group getCurrentDictionaryOrder() const;
  Config::Group getCurrentInactiveDictionaries() const;

private slots:

  void dictionarySelectionChanged( const QItemSelection &current );
  void inactiveDictionarySelectionChanged( const QItemSelection &current );
  void contextMenuRequested( const QPoint & pos );
  void filterChanged( QString const & filterText );
  void labelFilterChanged( int index );
  void dictListFocused();
  void inactiveDictListFocused();
  void showDictNumbers();

private:

  Ui::OrderAndProps ui;
  Config::Class & cfg;

  void disableDictionaryDescription();
  void describeDictionary( DictListWidget *, QModelIndex const & );
  void updateDictionaryLabelsDisplay( QString const & dictId );
  void updateLabelFilterOptions();

signals:
  void showDictionaryHeadwords( QString const & dictId );
};

#endif
