/***************************************************************************
                         testqgslayoutitemgroup.cpp
                         -----------------------
    begin                : October 2017
    copyright            : (C) 2017 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgslayoutitemgroup.h"
#include "qgslayout.h"
#include "qgslayoutitemshape.h"
#include "qgscomposerlabel.h"
#include "qgscomposerpolygon.h"
#include "qgscomposition.h"
#include "qgsmultirenderchecker.h"
#include "qgsapplication.h"
#include "qgslogger.h"
#include "qgsproject.h"

#include <QObject>
#include <QtTest/QSignalSpy>
#include "qgstest.h"

class TestQgsLayoutItemGroup : public QObject
{
    Q_OBJECT

  public:
    TestQgsLayoutItemGroup() = default;

  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase();// will be called after the last testfunction was executed.
    void init();// will be called before each testfunction is executed.
    void cleanup();// will be called after every testfunction.
    void createGroupDirect();
    void createGroup(); //test grouping items
    void ungroup(); //test ungrouping items
    void deleteGroup(); //test deleting group works
    void groupVisibility();
    void moveGroup();
    void resizeGroup();
    void undoRedo(); //test that group/ungroup undo/redo commands don't crash

  private:

    void dumpUndoStack( const QUndoStack &, QString prefix = QString() ) const;

};

// private
void TestQgsLayoutItemGroup::dumpUndoStack( const QUndoStack &us, QString prefix ) const
{
  if ( ! prefix.isEmpty() ) prefix += QLatin1String( ": " );
  for ( int i = 0; i < us.count(); ++i )
  {
    QgsDebugMsg( QString( "%4US %1: %2%3" )
                 .arg( i ). arg( i >= us.index() ? "-" : "",
                                 us.text( i ), prefix ) );
  }
}

void TestQgsLayoutItemGroup::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsLayoutItemGroup::cleanupTestCase()
{
}

void TestQgsLayoutItemGroup::init()
{

}

void TestQgsLayoutItemGroup::cleanup()
{

}

void TestQgsLayoutItemGroup::createGroupDirect()
{
  // create group manually
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item );
  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item2 );
  QVERIFY( !item->parentGroup() );
  QVERIFY( !item->isGroupMember() );
  QVERIFY( !item2->parentGroup() );
  QVERIFY( !item2->isGroupMember() );
  QVERIFY( item->flags() & QGraphicsItem::ItemIsSelectable );

  QgsLayoutItemGroup *group = new QgsLayoutItemGroup( &l );
  l.addLayoutItem( group );
  QVERIFY( group->items().empty() );

  // add null item
  group->addItem( nullptr );
  QVERIFY( group->items().empty() );

  group->addItem( item );
  QCOMPARE( item->parentGroup(), group );
  QVERIFY( item->isGroupMember() );
  QVERIFY( !( item->flags() & QGraphicsItem::ItemIsSelectable ) ); // group items are not selectable
  QVERIFY( !item2->parentGroup() );
  QVERIFY( !item2->isGroupMember() );
  QCOMPARE( group->items().count(), 1 );
  QVERIFY( group->items().contains( item ) );
  QVERIFY( l.items().contains( item ) );
  QVERIFY( l.items().contains( item2 ) );

  group->addItem( item2 );
  QCOMPARE( item->parentGroup(), group );
  QVERIFY( item->isGroupMember() );
  QCOMPARE( item2->parentGroup(), group );
  QVERIFY( item2->isGroupMember() );
  QVERIFY( !( item2->flags() & QGraphicsItem::ItemIsSelectable ) ); // group items are not selectable
  QCOMPARE( group->items().count(), 2 );
  QVERIFY( group->items().contains( item ) );
  QVERIFY( group->items().contains( item2 ) );
  QVERIFY( l.items().contains( item ) );
  QVERIFY( l.items().contains( item2 ) );

  // manually delete an item
  QPointer< QgsLayoutItemRectangularShape > pItem( item ); // for testing deletion
  l.removeLayoutItem( item );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pItem );

  QCOMPARE( item2->parentGroup(), group );
  QVERIFY( item2->isGroupMember() );
  QCOMPARE( group->items().count(), 1 );
  QVERIFY( group->items().contains( item2 ) );

  QPointer< QgsLayoutItemRectangularShape > pItem2( item2 ); // for testing deletion
  // remove items
  group->removeItems();
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  // should still exist
  QCOMPARE( pItem2.data(), item2 );
  QVERIFY( !item2->isGroupMember() );
  QVERIFY( !item2->parentGroup() );
  QCOMPARE( item2->layout(), &l );
  QVERIFY( l.items().contains( item2 ) );
  QVERIFY( item2->flags() & QGraphicsItem::ItemIsSelectable ); // should be selectable again
}

void TestQgsLayoutItemGroup::createGroup()
{
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );

  //group items
  QList<QgsLayoutItem *> items;
  items << item << item2;
  QgsLayoutItemGroup *group = l.groupItems( items );

  //check result
  QVERIFY( group );
  QCOMPARE( group->layout(), &l );
  QCOMPARE( group->items().size(), 2 );
  QVERIFY( group->items().contains( item ) );
  QVERIFY( group->items().contains( item2 ) );
  QVERIFY( item->isGroupMember() );
  QCOMPARE( item->parentGroup(), group );
  QCOMPARE( item2->parentGroup(), group );
}

void TestQgsLayoutItemGroup::ungroup()
{
  //test ungrouping items

  QgsProject proj;
  QgsLayout l( &proj );

  //simple tests - check that we don't crash
  l.ungroupItems( nullptr ); //no item

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item );
  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item2 );

  //group items
  QList<QgsLayoutItem *> items;
  items << item << item2;
  QgsLayoutItemGroup *group = l.groupItems( items );

  // group should be in scene
  QList<QgsLayoutItemGroup *> groups;
  l.layoutItems( groups );
  QCOMPARE( groups.size(), 1 );
  QVERIFY( groups.contains( group ) );
  QCOMPARE( group->layout(), &l );

  QPointer< QgsLayoutItemGroup > pGroup( group ); // for testing deletion
  //ungroup group
  QList<QgsLayoutItem *> ungroupedItems;
  ungroupedItems = l.ungroupItems( group );

  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pGroup );

  QCOMPARE( ungroupedItems.size(), 2 );
  QVERIFY( ungroupedItems.contains( item ) );
  QVERIFY( ungroupedItems.contains( item2 ) );

  QVERIFY( !item->isGroupMember() );
  QVERIFY( !item2->isGroupMember() );
  QVERIFY( !item->parentGroup() );
  QVERIFY( !item2->parentGroup() );

  // items should still be in scene
  QCOMPARE( item->layout(), &l );
  QVERIFY( l.items().contains( item ) );
  QCOMPARE( item2->layout(), &l );
  QVERIFY( l.items().contains( item2 ) );

  //should also be no groups left in the composition

  l.layoutItems( groups );
  QCOMPARE( groups.size(), 0 );
}

void TestQgsLayoutItemGroup::deleteGroup()
{
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  QPointer< QgsLayoutItemRectangularShape > pItem( item ); // for testing deletion
  l.addLayoutItem( item );
  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );
  QPointer< QgsLayoutItemRectangularShape > pItem2( item2 ); // for testing deletion
  l.addLayoutItem( item2 );

  //group items
  QList<QgsLayoutItem *> groupItems;
  groupItems << item << item2;
  QgsLayoutItemGroup *group = l.groupItems( groupItems );
  QPointer< QgsLayoutItemGroup > pGroup( group ); // for testing deletion

  QList<QgsLayoutItem *> items;
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 );

  //test that deleting group also removes all grouped items
  l.removeLayoutItem( group );

  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pGroup );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pItem );
  QVERIFY( !pItem2 );

  l.layoutItems( items );
  QVERIFY( items.empty() );
}

void TestQgsLayoutItemGroup::groupVisibility()
{
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item );
  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item2 );

  //group items
  QList<QgsLayoutItem *> groupItems;
  groupItems << item << item2;
  QgsLayoutItemGroup *group = l.groupItems( groupItems );

  QVERIFY( item->isVisible() );
  QVERIFY( item2->isVisible() );
  QVERIFY( group->isVisible() );
  group->setVisibility( false );
  QVERIFY( !item->isVisible() );
  QVERIFY( !item2->isVisible() );
  QVERIFY( !group->isVisible() );
  group->setVisibility( true );
  QVERIFY( item->isVisible() );
  QVERIFY( item2->isVisible() );
  QVERIFY( group->isVisible() );

  l.undoStack()->stack()->undo();
  QVERIFY( !item->isVisible() );
  QVERIFY( !item2->isVisible() );
  QVERIFY( !group->isVisible() );
  l.undoStack()->stack()->undo();
  QVERIFY( item->isVisible() );
  QVERIFY( item2->isVisible() );
  QVERIFY( group->isVisible() );
  l.undoStack()->stack()->redo();
  QVERIFY( !item->isVisible() );
  QVERIFY( !item2->isVisible() );
  QVERIFY( !group->isVisible() );
  l.undoStack()->stack()->redo();
  QVERIFY( item->isVisible() );
  QVERIFY( item2->isVisible() );
  QVERIFY( group->isVisible() );
}

void TestQgsLayoutItemGroup::moveGroup()
{
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item );
  item->attemptMove( QgsLayoutPoint( 0.05, 0.09, QgsUnitTypes::LayoutMeters ) );

  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item2 );
  item2->attemptMove( QgsLayoutPoint( 2, 3, QgsUnitTypes::LayoutInches ) );

  //group items
  QList<QgsLayoutItem *> groupItems;
  groupItems << item << item2;
  QgsLayoutItemGroup *group = l.groupItems( groupItems );

  QCOMPARE( group->positionWithUnits().x(), 50.8 );
  QCOMPARE( group->positionWithUnits().y(), 76.2 );
  QCOMPARE( group->positionWithUnits().units(), QgsUnitTypes::LayoutMillimeters );

  group->attemptMove( QgsLayoutPoint( 20.8, 36.2, QgsUnitTypes::LayoutMillimeters ) );
  QCOMPARE( group->positionWithUnits().x(), 20.8 );
  QCOMPARE( group->positionWithUnits().y(), 36.2 );
  QCOMPARE( group->positionWithUnits().units(), QgsUnitTypes::LayoutMillimeters );
  QCOMPARE( item->positionWithUnits().x(), 0.02 );
  QCOMPARE( item->positionWithUnits().y(), 0.05 );
  QCOMPARE( item->positionWithUnits().units(), QgsUnitTypes::LayoutMeters );
  QGSCOMPARENEAR( item2->positionWithUnits().x(), 0.818898, 0.0001 );
  QGSCOMPARENEAR( item2->positionWithUnits().y(), 1.425197, 0.0001 );
  QCOMPARE( item2->positionWithUnits().units(), QgsUnitTypes::LayoutInches );
}

void TestQgsLayoutItemGroup::resizeGroup()
{
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item );
  item->attemptMove( QgsLayoutPoint( 0.05, 0.09, QgsUnitTypes::LayoutMeters ) );
  item->attemptResize( QgsLayoutSize( 0.1, 0.15, QgsUnitTypes::LayoutMeters ) );

  QgsLayoutItemRectangularShape *item2 = new QgsLayoutItemRectangularShape( &l );
  l.addLayoutItem( item2 );
  item2->attemptMove( QgsLayoutPoint( 2, 3, QgsUnitTypes::LayoutInches ) );
  item2->attemptResize( QgsLayoutSize( 4, 6, QgsUnitTypes::LayoutInches ) );

  //group items
  QList<QgsLayoutItem *> groupItems;
  groupItems << item << item2;
  QgsLayoutItemGroup *group = l.groupItems( groupItems );

  QCOMPARE( group->positionWithUnits().x(), 50.0 );
  QCOMPARE( group->positionWithUnits().y(), 76.2 );
  QCOMPARE( group->positionWithUnits().units(), QgsUnitTypes::LayoutMillimeters );
  QCOMPARE( group->sizeWithUnits().width(), 102.4 );
  QCOMPARE( group->sizeWithUnits().height(),  163.8 );
  QCOMPARE( group->sizeWithUnits().units(), QgsUnitTypes::LayoutMillimeters );

  group->attemptResize( QgsLayoutSize( 50.8, 76.2, QgsUnitTypes::LayoutMillimeters ) );
  QCOMPARE( group->positionWithUnits().x(), 50.0 );
  QCOMPARE( group->positionWithUnits().y(), 76.2 );
  QCOMPARE( group->positionWithUnits().units(), QgsUnitTypes::LayoutMillimeters );
  QCOMPARE( group->sizeWithUnits().width(), 50.8 );
  QCOMPARE( group->sizeWithUnits().height(),  76.2 );
  QCOMPARE( group->sizeWithUnits().units(), QgsUnitTypes::LayoutMillimeters );
  QCOMPARE( item->positionWithUnits().x(), 0.05 );
  QGSCOMPARENEAR( item->positionWithUnits().y(), 0.0826198, 0.00001 );
  QCOMPARE( item->positionWithUnits().units(), QgsUnitTypes::LayoutMeters );
  QGSCOMPARENEAR( item->sizeWithUnits().width(), 0.0496094, 0.0001 );
  QGSCOMPARENEAR( item->sizeWithUnits().height(), 0.069780, 0.0001 );
  QCOMPARE( item->sizeWithUnits().units(), QgsUnitTypes::LayoutMeters );
  QGSCOMPARENEAR( item2->positionWithUnits().x(), 1.984129, 0.0001 );
  QGSCOMPARENEAR( item2->positionWithUnits().y(), 3.000000, 0.0001 );
  QCOMPARE( item2->positionWithUnits().units(), QgsUnitTypes::LayoutInches );
  QGSCOMPARENEAR( item2->sizeWithUnits().width(), 1.98438, 0.0001 );
  QGSCOMPARENEAR( item2->sizeWithUnits().height(),  2.791209, 0.0001 );
  QCOMPARE( item2->sizeWithUnits().units(), QgsUnitTypes::LayoutInches );
}

Q_DECLARE_METATYPE( QgsLayoutItemGroup * )
Q_DECLARE_METATYPE( QgsLayoutItemRectangularShape * )
Q_DECLARE_METATYPE( QgsLayoutItem * )

void TestQgsLayoutItemGroup::undoRedo()
{
  QgsProject proj;
  QgsLayout l( &proj );

  QgsLayoutItemRectangularShape *item1 = nullptr;
  QgsLayoutItemRectangularShape *item2 = nullptr;

// int shapesAdded = 0;
// int groupsAdded = 0;
// int itemsRemoved = 0;

  qRegisterMetaType<QgsLayoutItemRectangularShape *>();
//  QSignalSpy spyPolygonAdded( &l, &QgsLayout::itemAdded );
// QCOMPARE( spyPolygonAdded.count(), 0 );

  qRegisterMetaType<QgsLayoutItemGroup *>();
//  QSignalSpy spyGroupAdded( &l, &QgsLayout::composerItemGroupAdded );
//  QCOMPARE( spyGroupAdded.count(), 0 );

  qRegisterMetaType<QgsLayoutItem *>();
//  QSignalSpy spyItemRemoved( &l, &QgsLayout::itemRemoved );
//  QCOMPARE( spyItemRemoved.count(), 0 );

  //test for crash when undo/redoing with groups
  // Set initial condition
  QUndoStack *us = l.undoStack()->stack();
  QgsDebugMsg( QString( "clearing" ) );
  us->clear();
  QgsDebugMsg( QString( "clearing completed" ) );
  QList<QgsLayoutItem *> items;
  l.layoutItems( items );
  QCOMPARE( items.size(), 0 );
  QgsDebugMsg( QString( "clear stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );

  //create some items
  item1 = new QgsLayoutItemRectangularShape( &l );
  item1->attemptMove( QgsLayoutPoint( 0.05, 0.09, QgsUnitTypes::LayoutMeters ) );
  QPointer< QgsLayoutItem > pItem1( item1 );
  QString item1Uuid = item1->uuid();
  item1->attemptResize( QgsLayoutSize( 0.1, 0.15, QgsUnitTypes::LayoutMeters ) );

  l.addLayoutItem( item1 );
//  QCOMPARE( spyPolygonAdded.count(), ++shapesAdded );
  item2 = new QgsLayoutItemRectangularShape( &l );
  QPointer< QgsLayoutItem > pItem2( item2 );
  QString item2Uuid = item2->uuid();
  item2->attemptMove( QgsLayoutPoint( 2, 3, QgsUnitTypes::LayoutMillimeters ) );
  item2->attemptResize( QgsLayoutSize( 4, 6, QgsUnitTypes::LayoutMillimeters ) );
  l.addLayoutItem( item2 );
//  QCOMPARE( spyPolygonAdded.count(), ++shapesAdded );
  l.layoutItems( items );
  QCOMPARE( items.size(), 2 ); // 2 shapes
  QgsDebugMsg( QString( "addedItems stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );
  QCOMPARE( item1->pos(), QPointF( 50, 90 ) );
  QCOMPARE( item2->pos(), QPointF( 2, 3 ) );
  //dumpUndoStack(*us, "after initial items addition");

  //group items
  items.clear();
  items << item1 << item2;
  QgsLayoutItemGroup *group = l.groupItems( items );
  QString groupUuid = group->uuid();
// QCOMPARE( spyPolygonAdded.count(), shapesAdded );
//  QCOMPARE( spyGroupAdded.count(), ++groupsAdded );
//  QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  QCOMPARE( group->items().size(), 2 );
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  QCOMPARE( item1->pos(), QPointF( 50, 90 ) );
  QCOMPARE( item2->pos(), QPointF( 2, 3 ) );
  QCOMPARE( group->pos(), QPointF( 2, 3 ) );
  //dumpUndoStack(*us, "after initial items addition");

  //move group
  QgsDebugMsg( QString( "moving group" ) );
  group->attemptMove( QgsLayoutPoint( 10.0, 20.0 ) );
// QCOMPARE( spyPolygonAdded.count(), shapesAdded );
// QCOMPARE( spyGroupAdded.count(), groupsAdded );
// QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  QgsDebugMsg( QString( "groupItems stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );
  QCOMPARE( group->items().size(), 2 );
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  QCOMPARE( item1->pos(), QPointF( 58, 107 ) );
  QCOMPARE( item2->pos(),  QPointF( 10, 20 ) );
  QCOMPARE( group->pos(),  QPointF( 10, 20 ) );

  //ungroup
  QPointer< QgsLayoutItemGroup > pGroup( group ); // for testing deletion
  QgsDebugMsg( QString( "ungrouping" ) );
  l.ungroupItems( group );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

//  QCOMPARE( spyPolygonAdded.count(), shapesAdded );
//  QCOMPARE( spyGroupAdded.count(), groupsAdded );
//  QCOMPARE( spyItemRemoved.count(), ++itemsRemoved );
  l.layoutItems( items );
  QCOMPARE( items.size(), 2 ); // 2 shapes
  QCOMPARE( item1->pos(), QPointF( 58, 107 ) );
  QCOMPARE( item2->pos(),  QPointF( 10, 20 ) );
  QVERIFY( !pGroup );
  //dumpUndoStack(*us, "after ungroup");
  // US 0: Items grouped
  // US 1: move group
  // US 2: Remove item group

  //undo (groups again) -- crashed here before #11371 got fixed
  QgsDebugMsg( QString( "undo ungrouping" ) );
  us->undo();
//  QCOMPARE( spyPolygonAdded.count(), shapesAdded );
// QCOMPARE( spyGroupAdded.count(), ++groupsAdded );
// QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  QVERIFY( item1->isGroupMember() );
  QVERIFY( item2->isGroupMember() );
  QCOMPARE( item1->parentGroup(), item2->parentGroup() );
  QCOMPARE( item1->parentGroup()->uuid(), groupUuid );
  group = item1->parentGroup();
  pGroup = group;

  QCOMPARE( group->items().size(), 2 ); // WARNING: might not be alive anymore
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  QCOMPARE( item1->pos(), QPointF( 58, 107 ) );
  QCOMPARE( item2->pos(),  QPointF( 10, 20 ) );

  QCOMPARE( group->pos(),  QPointF( 10, 20 ) );
  //dumpUndoStack(*us, "after undo ungroup");
  // US 0: Items grouped
  // US 1: move group
  // US 2: -Remove item group

  //remove group
  QgsDebugMsg( QString( "remove group" ) );
  l.removeLayoutItem( group );
// QCOMPARE( spyPolygonAdded.count(), shapesAdded );
  //QCOMPARE( spyGroupAdded.count(), groupsAdded );
  //itemsRemoved += 3; // the group and the two items
  //QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pGroup );
  QVERIFY( !pItem1 );
  QVERIFY( !pItem2 );

  l.layoutItems( items );
  QCOMPARE( items.size(), 0 ); // nothing
  QgsDebugMsg( QString( "remove stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );
  //dumpUndoStack(*us, "after remove group");
  // US 0: Items grouped
  // US 1: move group
  // US 2: Remove item group

  //undo remove group
  QgsDebugMsg( QString( "undo remove group" ) );
  us->undo();
  //shapesAdded += 2;
  //QCOMPARE( spyPolygonAdded.count(), shapesAdded );
  //QCOMPARE( spyGroupAdded.count(), ++groupsAdded );
  //QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  l.layoutItems( items );
  group = dynamic_cast< QgsLayoutItemGroup * >( l.itemByUuid( groupUuid ) );
  QVERIFY( group );

  QCOMPARE( group->items().size(), 2 );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  item1 = dynamic_cast< QgsLayoutItemRectangularShape * >( l.itemByUuid( item1Uuid ) );
  QCOMPARE( item1->parentGroup(), group );
  item2 = dynamic_cast< QgsLayoutItemRectangularShape * >( l.itemByUuid( item2Uuid ) );
  QCOMPARE( item2->parentGroup(), group );
  QgsDebugMsg( QString( "undo stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );
  //dumpUndoStack(*us, "after undo remove group");
  // US 0: Items grouped
  // US 1: move group
  // US 2: -Remove item group

  //undo move group
  QgsDebugMsg( QString( "undo move group" ) );
  us->undo();
// QCOMPARE( spyPolygonAdded.count(), shapesAdded );
// QCOMPARE( spyGroupAdded.count(), groupsAdded );
// QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  QCOMPARE( group->items().size(), 2 );
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  QCOMPARE( item1->isGroupMember(), true );
  QCOMPARE( item2->isGroupMember(), true );
  QCOMPARE( item1->pos(),  QPointF( 50, 90 ) );
  QCOMPARE( item2->pos(), QPointF( 2, 3 ) );
  QCOMPARE( group->pos(), QPointF( 2, 3 ) );
  //dumpUndoStack(*us, "after undo move group");
  // US 0: Items grouped
  // US 1: -move group
  // US 2: -Remove item group

  //undo group
  pGroup = group;

  QgsDebugMsg( QString( "undo group" ) );
  us->undo();
  //QCOMPARE( spyPolygonAdded.count(), shapesAdded );
  //QCOMPARE( spyGroupAdded.count(), groupsAdded );
  //QCOMPARE( spyItemRemoved.count(), ++itemsRemoved );
  //QCOMPARE( mGroup->items().size(), 2 ); // not important
  l.layoutItems( items );
  QCOMPARE( items.size(), 2 ); // 2 shapes
  QCOMPARE( item1->isGroupMember(), false );
  QCOMPARE( item2->isGroupMember(), false );
  QCOMPARE( item1->pos(), QPointF( 50, 90 ) );
  QCOMPARE( item2->pos(), QPointF( 2, 3 ) );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pGroup );
  //dumpUndoStack(*us, "after undo group");
  // US 0: -Items grouped
  // US 1: -move group
  // US 2: -Remove item group

  //redo group
  QgsDebugMsg( QString( "redo group" ) );
  us->redo();
  //QCOMPARE( spyPolygonAdded.count(), shapesAdded );
  //QCOMPARE( spyGroupAdded.count(), ++groupsAdded );
  //QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  QCOMPARE( item1->isGroupMember(), true );
  QCOMPARE( item2->isGroupMember(), true );
  group = dynamic_cast< QgsLayoutItemGroup * >( l.itemByUuid( groupUuid ) );
  QCOMPARE( group->pos(), QPointF( 2, 3 ) );
  //dumpUndoStack(*us, "after redo group");
  // US 0: Items grouped
  // US 1: -move group
  // US 2: -Remove item group

  //redo move group
  QgsDebugMsg( QString( "redo move group" ) );
  us->redo();
  //QCOMPARE( spyPolygonAdded.count(), shapesAdded );
  //QCOMPARE( spyGroupAdded.count(), groupsAdded );
  //QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  l.layoutItems( items );
  QCOMPARE( items.size(), 3 ); // 2 shapes, 1 group
  QCOMPARE( item1->isGroupMember(), true );
  QCOMPARE( item2->isGroupMember(), true );


  //TODO - fix!!!
  QCOMPARE( item1->pos(), QPointF( 50, 90 ) );
  QCOMPARE( item2->pos(), QPointF( 2, 3 ) );
  QCOMPARE( group->pos(), QPointF( 2, 3 ) );
  //dumpUndoStack(*us, "after redo move group");
  // US 0: Items grouped
  // US 1: move group
  // US 2: -Remove item group

  //redo remove group
  QgsDebugMsg( QString( "redo remove group" ) );
  pGroup = group;
  pItem1 = item1;
  pItem2 = item2;
  us->redo();
  //QCOMPARE( spyPolygonAdded.count(), shapesAdded );
  //QCOMPARE( spyGroupAdded.count(), groupsAdded );
  //itemsRemoved += 3; // 1 group, 2 contained items
  //QCOMPARE( spyItemRemoved.count(), itemsRemoved );
  l.layoutItems( items );
  QCOMPARE( items.size(), 0 );
  QgsApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QVERIFY( !pGroup );
  QVERIFY( !pItem1 );
  QVERIFY( !pItem2 );

  QgsDebugMsg( QString( "undo stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );
  //dumpUndoStack(*us, "after redo remove group");
  // US 0: Items grouped
  // US 1: move group
  // US 2: Remove item group

  //unwind the whole stack
  us->clear();

  QgsDebugMsg( QString( "clear stack count:%1 index:%2" ) .arg( us->count() ) .arg( us->index() ) );
}

QGSTEST_MAIN( TestQgsLayoutItemGroup )
#include "testqgslayoutitemgroup.moc"