/***************************************************************************
                          qgslayoutitemundocommand.cpp
                          ------------------------
    begin                : July 2017
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

#include "qgslayoutitemundocommand.h"
#include "qgslayoutitem.h"
#include "qgsreadwritecontext.h"
#include "qgslayout.h"
#include "qgsproject.h"

///@cond PRIVATE
QgsLayoutItemUndoCommand::QgsLayoutItemUndoCommand( QgsLayoutItem *item, const QString &text, int id, QUndoCommand *parent )
  : QgsAbstractLayoutUndoCommand( text, id, parent )
  , mItemUuid( item->uuid() )
  , mLayout( item->layout() )
  , mItemType( item->type() )
{

}

bool QgsLayoutItemUndoCommand::mergeWith( const QUndoCommand *command )
{
  if ( command->id() == 0 )
    return false;

  const QgsLayoutItemUndoCommand *c = dynamic_cast<const QgsLayoutItemUndoCommand *>( command );
  if ( !c )
  {
    return false;
  }

  setAfterState( c->afterState() );
  return true;
}

void QgsLayoutItemUndoCommand::saveState( QDomDocument &stateDoc ) const
{
  stateDoc.clear();
  QDomElement documentElement = stateDoc.createElement( QStringLiteral( "ItemState" ) );

  QgsLayoutItem *item = mLayout->itemByUuid( mItemUuid );
  Q_ASSERT_X( item, "QgsLayoutItemUndoCommand::saveState", "could not retrieve item for saving state" );

  item->writeXml( documentElement, stateDoc, QgsReadWriteContext() );
  stateDoc.appendChild( documentElement );
}

void QgsLayoutItemUndoCommand::restoreState( QDomDocument &stateDoc )
{
  // find item by uuid...
  QgsLayoutItem *item = mLayout->itemByUuid( mItemUuid );
  if ( !item )
  {
    // uh oh - it's been deleted! we need to create a new instance
    item = recreateItem( mItemType, mLayout );
  }

  item->readXml( stateDoc.documentElement().firstChild().toElement(), stateDoc, QgsReadWriteContext() );
  mLayout->project()->setDirty( true );
}

QgsLayoutItem *QgsLayoutItemUndoCommand::recreateItem( int itemType, QgsLayout *layout )
{
  QgsLayoutItem *item = QgsApplication::layoutItemRegistry()->createItem( itemType, layout );
  mLayout->addLayoutItem( item );
  return item;
}

QString QgsLayoutItemUndoCommand::itemUuid() const
{
  return mItemUuid;
}

QgsLayout *QgsLayoutItemUndoCommand::layout() const
{
  return mLayout;
}


//
// QgsLayoutItemDeleteUndoCommand
//

QgsLayoutItemDeleteUndoCommand::QgsLayoutItemDeleteUndoCommand( QgsLayoutItem *item, const QString &text, int id, QUndoCommand *parent )
  : QgsLayoutItemUndoCommand( item, text, id, parent )
{
  saveBeforeState();
}

bool QgsLayoutItemDeleteUndoCommand::mergeWith( const QUndoCommand * )
{
  return false;
}

void QgsLayoutItemDeleteUndoCommand::redo()
{
  if ( mFirstRun )
  {
    mFirstRun = false;
    return;
  }

  QgsLayoutItem *item = layout()->itemByUuid( itemUuid() );
  Q_ASSERT_X( item, "QgsLayoutItemDeleteUndoCommand::redo", "could not find item to re-delete!" );

  layout()->removeItem( item );
  item->deleteLater();
}

///@endcond