/***************************************************************************
  qgssnappinglayertreemodel.cpp - QgsSnappingLayerTreeView

 ---------------------
 begin                : 31.8.2016
 copyright            : (C) 2016 by Denis Rouzaud
 email                : denis.rouzaud@gmail.com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QComboBox>
#include <QDoubleSpinBox>

#include "qgssnappinglayertreemodel.h"

#include "qgslayertree.h"
#include "qgsmapcanvas.h"
#include "qgsproject.h"
#include "qgssnappingconfig.h"
#include "qgsvectorlayer.h"


QgsSnappingLayerDelegate::QgsSnappingLayerDelegate( QgsMapCanvas *canvas, QObject *parent )
  : QItemDelegate( parent )
  , mCanvas( canvas )
{
}

QWidget *QgsSnappingLayerDelegate::createEditor( QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index ) const
{
  Q_UNUSED( option );
  Q_UNUSED( index );

  if ( index.column() == QgsSnappingLayerTreeModel::TypeColumn )
  {
    QComboBox *w = new QComboBox( parent );
    w->addItem( QIcon( QgsApplication::getThemeIcon( "/mIconSnappingVertex.svg" ) ), QStringLiteral( "Vertex" ), QgsSnappingConfig::Vertex );
    w->addItem( QIcon( QgsApplication::getThemeIcon( "/mIconSnappingVertexAndSegment.svg" ) ), QStringLiteral( "Vertex and segment" ), QgsSnappingConfig::VertexAndSegment );
    w->addItem( QIcon( QgsApplication::getThemeIcon( "/mIconSnappingSegment.svg" ) ), QStringLiteral( "Segment" ), QgsSnappingConfig::Segment );
    return w;
  }

  if ( index.column() == QgsSnappingLayerTreeModel::ToleranceColumn )
  {
    QDoubleSpinBox *w = new QDoubleSpinBox( parent );
    QVariant val = index.model()->data( index.model()->sibling( index.row(), QgsSnappingLayerTreeModel::UnitsColumn, index ), Qt::UserRole );
    if ( val.isValid() )
    {
      QgsTolerance::UnitType units = ( QgsTolerance::UnitType )val.toInt();
      if ( units == QgsTolerance::Pixels )
      {
        w->setDecimals( 0 );
      }
      else
      {
        QgsUnitTypes::DistanceUnitType type = QgsUnitTypes::unitType( mCanvas->mapUnits() );
        w->setDecimals( type == QgsUnitTypes::Standard ? 2 : 5 );
      }
    }
    else
    {
      w->setDecimals( 5 );
    }
    return w;
  }

  if ( index.column() == QgsSnappingLayerTreeModel::UnitsColumn )
  {
    QComboBox *w = new QComboBox( parent );
    w->addItem( tr( "px" ), QgsTolerance::Pixels );
    w->addItem( QgsUnitTypes::toString( QgsProject::instance()->distanceUnits() ), QgsTolerance::ProjectUnits );
    return w;
  }

  return nullptr;
}

void QgsSnappingLayerDelegate::setEditorData( QWidget *editor, const QModelIndex &index ) const
{
  QVariant val = index.model()->data( index, Qt::UserRole );
  if ( !val.isValid() )
    return;

  if ( index.column() == QgsSnappingLayerTreeModel::TypeColumn )
  {
    QgsSnappingConfig::SnappingType type = ( QgsSnappingConfig::SnappingType )val.toInt();
    QComboBox *cb = qobject_cast<QComboBox *>( editor );
    if ( cb )
    {
      cb->setCurrentIndex( cb->findData( type ) );
    }
  }
  else if ( index.column() == QgsSnappingLayerTreeModel::ToleranceColumn )
  {
    QDoubleSpinBox *w = qobject_cast<QDoubleSpinBox *>( editor );
    if ( w )
    {
      w->setValue( val.toDouble() );
    }
  }
  else if ( index.column() == QgsSnappingLayerTreeModel::UnitsColumn )
  {
    QgsTolerance::UnitType units = ( QgsTolerance::UnitType )val.toInt();
    QComboBox *w = qobject_cast<QComboBox *>( editor );
    if ( w )
    {
      w->setCurrentIndex( w->findData( units ) );
    }
  }
}

void QgsSnappingLayerDelegate::setModelData( QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const
{
  if ( index.column() == QgsSnappingLayerTreeModel::TypeColumn ||
       index.column() == QgsSnappingLayerTreeModel::UnitsColumn )
  {
    QComboBox *w = qobject_cast<QComboBox *>( editor );
    if ( w )
    {
      model->setData( index, w->currentData(), Qt::EditRole );
    }
  }
  else if ( index.column() == QgsSnappingLayerTreeModel::ToleranceColumn )
  {
    QDoubleSpinBox *w = qobject_cast<QDoubleSpinBox *>( editor );
    if ( w )
    {
      model->setData( index, w->value(), Qt::EditRole );
    }
  }
}


QgsSnappingLayerTreeModel::QgsSnappingLayerTreeModel( QgsProject *project, QObject *parent )
  : QSortFilterProxyModel( parent )
  , mProject( project )
  , mIndividualLayerSettings( project->snappingConfig().individualLayerSettings() )
  , mLayerTreeModel( nullptr )
{
  connect( project, &QgsProject::snappingConfigChanged, this, &QgsSnappingLayerTreeModel::onSnappingSettingsChanged );
  connect( project, &QgsProject::avoidIntersectionsLayersChanged, this, &QgsSnappingLayerTreeModel::onSnappingSettingsChanged );
}

QgsSnappingLayerTreeModel::~QgsSnappingLayerTreeModel()
{
}

int QgsSnappingLayerTreeModel::columnCount( const QModelIndex &parent ) const
{
  Q_UNUSED( parent );
  return 5;
}

Qt::ItemFlags QgsSnappingLayerTreeModel::flags( const QModelIndex &idx ) const
{
  if ( idx.column() == LayerColumn )
  {
    return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
  }

  QgsVectorLayer *vl = vectorLayer( idx );
  if ( !vl )
  {
    return Qt::NoItemFlags;
  }
  else
  {
    const QModelIndex layerIndex = sibling( idx.row(), LayerColumn, idx );
    if ( data( layerIndex, Qt::CheckStateRole ) == Qt::Checked )
    {
      if ( idx.column() == AvoidIntersectionColumn )
      {
        if ( vl->geometryType() == QgsWkbTypes::PolygonGeometry )
        {
          return Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable;
        }
        else
        {
          return Qt::NoItemFlags;
        }
      }
      else
      {
        return Qt::ItemIsEnabled | Qt::ItemIsEditable;
      }
    }
  }
  return Qt::NoItemFlags;
}

QModelIndex QgsSnappingLayerTreeModel::index( int row, int column, const QModelIndex &parent ) const
{
  QModelIndex newIndex = QSortFilterProxyModel::index( row, 0, parent );
  if ( column == LayerColumn )
    return newIndex;

  return createIndex( row, column, newIndex.internalId() );
}

QModelIndex QgsSnappingLayerTreeModel::parent( const QModelIndex &child ) const
{
  return QSortFilterProxyModel::parent( createIndex( child.row(), 0, child.internalId() ) );
}

QModelIndex QgsSnappingLayerTreeModel::sibling( int row, int column, const QModelIndex &idx ) const
{
  QModelIndex parent = idx.parent();
  return index( row, column, parent );
}

QgsVectorLayer *QgsSnappingLayerTreeModel::vectorLayer( const QModelIndex &idx ) const
{
  QgsLayerTreeNode *node = nullptr;
  if ( idx.column() == LayerColumn )
  {
    node = mLayerTreeModel->index2node( mapToSource( idx ) );
  }
  else
  {
    node = mLayerTreeModel->index2node( mapToSource( index( idx.row(), 0, idx.parent() ) ) );
  }

  if ( !node || !QgsLayerTree::isLayer( node ) )
    return nullptr;

  return qobject_cast<QgsVectorLayer *>( QgsLayerTree::toLayer( node )->layer() );
}

void QgsSnappingLayerTreeModel::onSnappingSettingsChanged()
{
  const QHash<QgsVectorLayer *, QgsSnappingConfig::IndividualLayerSettings> oldSettings = mIndividualLayerSettings;

  Q_FOREACH ( QgsVectorLayer *vl, oldSettings.keys() )
  {
    if ( !mProject->snappingConfig().individualLayerSettings().contains( vl ) )
    {
      beginResetModel();
      mIndividualLayerSettings = mProject->snappingConfig().individualLayerSettings();
      endResetModel();
      return;
    }
  }
  Q_FOREACH ( QgsVectorLayer *vl, mProject->snappingConfig().individualLayerSettings().keys() )
  {
    if ( !oldSettings.contains( vl ) )
    {
      beginResetModel();
      mIndividualLayerSettings = mProject->snappingConfig().individualLayerSettings();
      endResetModel();
      return;
    }
  }

  hasRowchanged( mLayerTreeModel->rootGroup(), oldSettings );
}

void QgsSnappingLayerTreeModel::hasRowchanged( QgsLayerTreeNode *node, const QHash<QgsVectorLayer *, QgsSnappingConfig::IndividualLayerSettings> &oldSettings )
{
  if ( node->nodeType() == QgsLayerTreeNode::NodeGroup )
  {
    Q_FOREACH ( QgsLayerTreeNode *child, node->children() )
    {
      hasRowchanged( child, oldSettings );
    }
  }
  else
  {
    QModelIndex idx = mapFromSource( mLayerTreeModel->node2index( node ) );
    QgsVectorLayer *vl = vectorLayer( idx );
    if ( !vl )
    {
      emit dataChanged( QModelIndex(), idx );
    }
    if ( oldSettings.value( vl ) != mProject->snappingConfig().individualLayerSettings().value( vl ) )
    {
      mIndividualLayerSettings.insert( vl, mProject->snappingConfig().individualLayerSettings().value( vl ) );
      emit dataChanged( idx, index( idx.row(), columnCount( idx ) - 1 ) );
    }
  }
}

QgsLayerTreeModel *QgsSnappingLayerTreeModel::layerTreeModel() const
{
  return mLayerTreeModel;
}

void QgsSnappingLayerTreeModel::setLayerTreeModel( QgsLayerTreeModel *layerTreeModel )
{
  mLayerTreeModel = layerTreeModel;
  QSortFilterProxyModel::setSourceModel( layerTreeModel );
}

bool QgsSnappingLayerTreeModel::filterAcceptsRow( int sourceRow, const QModelIndex &sourceParent ) const
{
  QgsLayerTreeNode *node = mLayerTreeModel->index2node( mLayerTreeModel->index( sourceRow, 0, sourceParent ) );
  return nodeShown( node );
}

bool QgsSnappingLayerTreeModel::nodeShown( QgsLayerTreeNode *node ) const
{
  if ( !node )
    return false;
  if ( node->nodeType() == QgsLayerTreeNode::NodeGroup )
  {
    Q_FOREACH ( QgsLayerTreeNode *child, node->children() )
    {
      if ( nodeShown( child ) )
      {
        return true;
      }
    }
    return false;
  }
  else
  {
    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsLayerTree::toLayer( node )->layer() );
    return layer && layer->hasGeometryType();
  }
}

QVariant QgsSnappingLayerTreeModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
  if ( orientation == Qt::Horizontal )
  {
    if ( role == Qt::DisplayRole )
    {
      switch ( section )
      {
        case 0:
          return tr( "Layer" );
        case 1:
          return tr( "Type" );
        case 2:
          return tr( "Tolerance" );
        case 3:
          return tr( "Units" );
        case 4:
          return tr( "Avoid intersection" );
        default:
          return QVariant();
      }
    }
  }
  return mLayerTreeModel->headerData( section, orientation, role );
}

QVariant QgsSnappingLayerTreeModel::data( const QModelIndex &idx, int role ) const
{
  if ( idx.column() == LayerColumn )
  {
    if ( role == Qt::CheckStateRole )
    {
      QgsVectorLayer *vl = vectorLayer( idx );
      if ( vl  && mIndividualLayerSettings.contains( vl ) )
      {
        const QgsSnappingConfig::IndividualLayerSettings ls = mIndividualLayerSettings.value( vl );
        if ( !ls.valid() )
        {
          return QVariant();
        }
        if ( ls.enabled() )
        {
          return Qt::Checked;
        }
        else
        {
          return Qt::Unchecked;
        }
      }
      else
      {
        // i.e. this is a group, analyse its children
        bool hasChecked = false, hasUnchecked = false;
        int n;
        for ( n = 0; !hasChecked || !hasUnchecked; n++ )
        {
          QVariant v = data( idx.child( n, 0 ), role );
          if ( !v.isValid() )
            break;

          switch ( v.toInt() )
          {
            case Qt::PartiallyChecked:
              // parent of partially checked child shared state
              return Qt::PartiallyChecked;

            case Qt::Checked:
              hasChecked = true;
              break;

            case Qt::Unchecked:
              hasUnchecked = true;
              break;
          }
        }

        // unchecked leaf
        if ( n == 0 )
          return Qt::Unchecked;

        // both
        if ( hasChecked &&  hasUnchecked )
          return Qt::PartiallyChecked;

        if ( hasChecked )
          return Qt::Checked;

        Q_ASSERT( hasUnchecked );
        return Qt::Unchecked;
      }
    }
    else
    {
      return mLayerTreeModel->data( mapToSource( idx ), role );
    }
  }
  else
  {
    QgsVectorLayer *vl = vectorLayer( idx );

    if ( !vl || !mIndividualLayerSettings.contains( vl ) )
    {
      return QVariant();
    }

    const QgsSnappingConfig::IndividualLayerSettings ls = mIndividualLayerSettings.value( vl );

    // type
    if ( idx.column() == TypeColumn )
    {
      if ( role == Qt::DisplayRole )
      {
        switch ( ls.type() )
        {
          case QgsSnappingConfig::Vertex:
            return tr( "vertex" );
          case QgsSnappingConfig::VertexAndSegment:
            return tr( "vertex and segment" );
          case QgsSnappingConfig::Segment:
            return tr( "segment" );
          default:
            return tr( "N/A" );
        }
      }

      if ( role == Qt::UserRole )
        return ls.type();
    }

    // tolerance
    if ( idx.column() == ToleranceColumn )
    {
      if ( role == Qt::DisplayRole )
      {
        return QString::number( ls.tolerance() );
      }

      if ( role == Qt::UserRole )
      {
        return ls.tolerance();
      }
    }

    // units
    if ( idx.column() == UnitsColumn )
    {
      if ( role == Qt::DisplayRole )
      {
        switch ( ls.units() )
        {
          case QgsTolerance::Pixels:
            return tr( "pixels" );
          case QgsTolerance::ProjectUnits:
            return QgsUnitTypes::toString( QgsProject::instance()->distanceUnits() );
          default:
            return QVariant();
        }
      }

      if ( role == Qt::UserRole )
      {
        return ls.units();
      }
    }

    // avoid intersection
    if ( idx.column() == AvoidIntersectionColumn )
    {
      if ( role == Qt::CheckStateRole && vl->geometryType() == QgsWkbTypes::PolygonGeometry )
      {
        if ( mProject->avoidIntersectionsLayers().contains( vl ) )
        {
          return Qt::Checked;
        }
        else
        {
          return Qt::Unchecked;
        }
      }
    }
  }

  return QVariant();
}

bool QgsSnappingLayerTreeModel::setData( const QModelIndex &index, const QVariant &value, int role )
{
  if ( index.column() == LayerColumn )
  {
    if ( role == Qt::CheckStateRole )
    {
      int i = 0;
      for ( i = 0; ; i++ )
      {
        QModelIndex child = index.child( i, 0 );
        if ( !child.isValid() )
          break;

        setData( child, value, role );
      }

      if ( i == 0 )
      {
        QgsVectorLayer *vl = vectorLayer( index );
        if ( !vl || !mIndividualLayerSettings.contains( vl ) )
        {
          return false;
        }
        QgsSnappingConfig::IndividualLayerSettings ls = mIndividualLayerSettings.value( vl );
        if ( !ls.valid() )
          return false;
        if ( value.toInt() == Qt::Checked )
          ls.setEnabled( true );
        else if ( value.toInt() == Qt::Unchecked )
          ls.setEnabled( false );
        else
          Q_ASSERT( "expected checked or unchecked" );

        QgsSnappingConfig config = mProject->snappingConfig();
        config.setIndividualLayerSettings( vl, ls );
        mProject->setSnappingConfig( config );
      }
      return true;
    }

    return mLayerTreeModel->setData( mapToSource( index ), value, role );
  }

  if ( index.column() == TypeColumn && role == Qt::EditRole )
  {
    QgsVectorLayer *vl = vectorLayer( index );
    if ( vl )
    {
      if ( !mIndividualLayerSettings.contains( vl ) )
        return false;

      QgsSnappingConfig::IndividualLayerSettings ls = mIndividualLayerSettings.value( vl );
      if ( !ls.valid() )
        return false;

      ls.setType( ( QgsSnappingConfig::SnappingType )value.toInt() );
      QgsSnappingConfig config = mProject->snappingConfig();
      config.setIndividualLayerSettings( vl, ls );
      mProject->setSnappingConfig( config );
      return true;
    }
  }

  if ( index.column() == ToleranceColumn && role == Qt::EditRole )
  {
    QgsVectorLayer *vl = vectorLayer( index );
    if ( vl )
    {
      if ( !mIndividualLayerSettings.contains( vl ) )
        return false;

      QgsSnappingConfig::IndividualLayerSettings ls = mIndividualLayerSettings.value( vl );
      if ( !ls.valid() )
        return false;

      ls.setTolerance( value.toDouble() );
      QgsSnappingConfig config = mProject->snappingConfig();
      config.setIndividualLayerSettings( vl, ls );
      mProject->setSnappingConfig( config );
      return true;
    }
  }

  if ( index.column() == UnitsColumn && role == Qt::EditRole )
  {
    QgsVectorLayer *vl = vectorLayer( index );
    if ( vl )
    {
      if ( !mIndividualLayerSettings.contains( vl ) )
        return false;

      QgsSnappingConfig::IndividualLayerSettings ls = mIndividualLayerSettings.value( vl );
      if ( !ls.valid() )
        return false;

      ls.setUnits( ( QgsTolerance::UnitType )value.toInt() );
      QgsSnappingConfig config = mProject->snappingConfig();
      config.setIndividualLayerSettings( vl, ls );
      mProject->setSnappingConfig( config );
      return true;
    }
  }

  if ( index.column() == AvoidIntersectionColumn && role == Qt::CheckStateRole )
  {
    QgsVectorLayer *vl = vectorLayer( index );
    if ( vl )
    {
      if ( !mIndividualLayerSettings.contains( vl ) )
        return false;

      QList<QgsVectorLayer *> avoidIntersectionsList = mProject->avoidIntersectionsLayers();

      if ( value.toInt() == Qt::Checked && !avoidIntersectionsList.contains( vl ) )
        avoidIntersectionsList.append( vl );
      else
        avoidIntersectionsList.removeAll( vl );

      mProject->setAvoidIntersectionsLayers( avoidIntersectionsList );
      return true;
    }
  }

  return false;
}