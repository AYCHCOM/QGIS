/***************************************************************************
  qgsvertextool.cpp
  --------------------------------------
  Date                 : February 2017
  Copyright            : (C) 2017 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsvertextool.h"

#include "qgsadvanceddigitizingdockwidget.h"
#include "qgscurve.h"
#include "qgscurvepolygon.h"
#include "qgsgeometryutils.h"
#include "qgsgeometryvalidator.h"
#include "qgslogger.h"
#include "qgsmapcanvas.h"
#include "qgsmulticurve.h"
#include "qgsmultipoint.h"
#include "qgspointlocator.h"
#include "qgsproject.h"
#include "qgsrubberband.h"
#include "qgssettings.h"
#include "qgssnapindicator.h"
#include "qgssnappingutils.h"
#include "qgsvectorlayer.h"
#include "qgsvertexmarker.h"
#include "qgsstatusbar.h"
#include "qgisapp.h"
#include "qgsselectedfeature.h"
#include "qgsvertexeditor.h"
#include "qgsvertexentry.h"

#include <QMenu>
#include <QRubberBand>


uint qHash( const Vertex &v )
{
  return qHash( v.layer ) ^ qHash( v.fid ) ^ qHash( v.vertexId );
}

//
// geomutils - may get moved elsewhere
//


//! Find out whether vertex at the given index is an endpoint (assuming linear geometry)
static bool isEndpointAtVertexIndex( const QgsGeometry &geom, int vertexIndex )
{
  const QgsAbstractGeometry *g = geom.constGet();
  if ( const QgsCurve *curve = qgsgeometry_cast< const QgsCurve *>( g ) )
  {
    return vertexIndex == 0 || vertexIndex == curve->numPoints() - 1;
  }
  else if ( const QgsMultiCurve *multiCurve = qgsgeometry_cast<const QgsMultiCurve *>( g ) )
  {
    for ( int i = 0; i < multiCurve->numGeometries(); ++i )
    {
      QgsCurve *part = qgsgeometry_cast<QgsCurve *>( multiCurve->geometryN( i ) );
      Q_ASSERT( part );
      if ( vertexIndex < part->numPoints() )
        return vertexIndex == 0 || vertexIndex == part->numPoints() - 1;
      vertexIndex -= part->numPoints();
    }
    Q_ASSERT( false );  // should not get here
    return false;
  }
  else
  {
    QgsDebugMsg( "is_endpoint_at_vertex_index: unexpected geometry type!" );
    return false;
  }
}


//! Return index of vertex adjacent to the given endpoint. Assuming linear geometries.
int adjacentVertexIndexToEndpoint( const QgsGeometry &geom, int vertexIndex )
{
  const QgsAbstractGeometry *g = geom.constGet();
  if ( const QgsCurve *curve = qgsgeometry_cast<const QgsCurve *>( g ) )
  {
    return vertexIndex == 0 ? 1 : curve->numPoints() - 2;
  }
  else if ( const QgsMultiCurve *multiCurve = qgsgeometry_cast<const QgsMultiCurve *>( g ) )
  {
    int offset = 0;
    for ( int i = 0; i < multiCurve->numGeometries(); ++i )
    {
      const QgsCurve *part = qgsgeometry_cast<const QgsCurve *>( multiCurve->geometryN( i ) );
      Q_ASSERT( part );
      if ( vertexIndex < part->numPoints() )
        return vertexIndex == 0 ? offset + 1 : offset + part->numPoints() - 2;
      vertexIndex -= part->numPoints();
      offset += part->numPoints();
    }
  }
  else
  {
    QgsDebugMsg( "adjacent_vertex_index_to_endpoint: unexpected geometry type!" );
  }
  return -1;
}


/**
 * Determine whether a vertex is in the middle of a circular edge or not
 * (wrapper for slightly awkward API)
 */
static bool isCircularVertex( const QgsGeometry &geom, int vertexIndex )
{
  QgsVertexId vid;
  return geom.vertexIdFromVertexNr( vertexIndex, vid ) && vid.type == QgsVertexId::CurveVertex;
}


//! Create a multi-point geometry that can be used to highlight vertices of a feature
static QgsGeometry geometryToMultiPoint( const QgsGeometry &geom )
{
  QgsMultiPoint *multiPoint = new QgsMultiPoint();
  QgsGeometry outputGeom( multiPoint );
  for ( auto pointIt = geom.vertices_begin(); pointIt != geom.vertices_end(); ++pointIt )
    multiPoint->addGeometry( ( *pointIt ).clone() );
  return outputGeom;
}


//
// snapping match filters
//


//! a filter to allow just one particular feature
class OneFeatureFilter : public QgsPointLocator::MatchFilter
{
  public:
    OneFeatureFilter( const QgsVectorLayer *layer, QgsFeatureId fid )
      : layer( layer )
      , fid( fid )
    {}

    bool acceptMatch( const QgsPointLocator::Match &match ) override
    {
      return match.layer() == layer && match.featureId() == fid;
    }

  private:
    const QgsVectorLayer *layer = nullptr;
    QgsFeatureId fid;
};


//! a filter just to gather all matches at the same place
class MatchCollectingFilter : public QgsPointLocator::MatchFilter
{
  public:
    QList<QgsPointLocator::Match> matches;
    QgsVertexTool *vertextool = nullptr;

    MatchCollectingFilter( QgsVertexTool *vertextool )
      : vertextool( vertextool ) {}

    bool acceptMatch( const QgsPointLocator::Match &match ) override
    {
      if ( match.distance() > 0 )
        return false;
      matches.append( match );

      // there may be multiple points at the same location, but we get only one
      // result... the locator API needs a new method verticesInRect()
      QgsGeometry matchGeom = vertextool->cachedGeometry( match.layer(), match.featureId() );
      QgsVertexId vid;
      QgsPoint pt;
      while ( matchGeom.constGet()->nextVertex( vid, pt ) )
      {
        int vindex = matchGeom.vertexNrFromVertexId( vid );
        if ( pt.x() == match.point().x() && pt.y() == match.point().y() && vindex != match.vertexIndex() )
        {
          QgsPointLocator::Match extra_match( match.type(), match.layer(), match.featureId(),
                                              0, match.point(), vindex );
          matches.append( extra_match );
        }
      }
      return true;
    }
};

//
//
//


QgsVertexTool::QgsVertexTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock )
  : QgsMapToolAdvancedDigitizing( canvas, cadDock )
{
  setAdvancedDigitizingAllowed( false );

  mSnapIndicator.reset( new QgsSnapIndicator( canvas ) );

  mEdgeCenterMarker = new QgsVertexMarker( canvas );
  mEdgeCenterMarker->setIconType( QgsVertexMarker::ICON_CROSS );
  mEdgeCenterMarker->setColor( Qt::red );
  mEdgeCenterMarker->setPenWidth( 3 );
  mEdgeCenterMarker->setVisible( false );

  mFeatureBand = createRubberBand( QgsWkbTypes::LineGeometry );
  mFeatureBand->setVisible( false );

  QColor color = digitizingStrokeColor();
  mFeatureBandMarkers = new QgsRubberBand( canvas );
  mFeatureBandMarkers->setIcon( QgsRubberBand::ICON_CIRCLE );
  mFeatureBandMarkers->setColor( color );
  mFeatureBandMarkers->setIconSize( 8 );
  mFeatureBandMarkers->setVisible( false );

  mVertexBand = new QgsRubberBand( canvas );
  mVertexBand->setIcon( QgsRubberBand::ICON_CIRCLE );
  mVertexBand->setColor( color );
  mVertexBand->setIconSize( 15 );
  mVertexBand->setVisible( false );

  QColor color2( color );
  color2.setAlpha( color2.alpha() / 3 );
  mEdgeBand = new QgsRubberBand( canvas );
  mEdgeBand->setColor( color2 );
  mEdgeBand->setWidth( 10 );
  mEdgeBand->setVisible( false );

  mEndpointMarker = new QgsVertexMarker( canvas );
  mEndpointMarker->setIconType( QgsVertexMarker::ICON_CROSS );
  mEndpointMarker->setColor( Qt::red );
  mEndpointMarker->setPenWidth( 3 );
  mEndpointMarker->setVisible( false );
}

QgsVertexTool::~QgsVertexTool()
{
  delete mEdgeCenterMarker;
  delete mFeatureBand;
  delete mFeatureBandMarkers;
  delete mVertexBand;
  delete mEdgeBand;
  delete mEndpointMarker;
}

void QgsVertexTool::deactivate()
{
  setHighlightedVertices( QList<Vertex>() );
  removeTemporaryRubberBands();
  cleanupVertexEditor();

  mSnapIndicator->setMatch( QgsPointLocator::Match() );

  QHash< QPair<QgsVectorLayer *, QgsFeatureId>, GeometryValidation>::iterator it = mValidations.begin();
  for ( ; it != mValidations.end(); ++it )
    it->cleanup();
  mValidations.clear();

  QgsMapToolAdvancedDigitizing::deactivate();
}

void QgsVertexTool::addDragBand( const QgsPointXY &v1, const QgsPointXY &v2 )
{
  addDragStraightBand( nullptr, v1, v2, false, true, v2 );
}

void QgsVertexTool::addDragStraightBand( QgsVectorLayer *layer, QgsPointXY v0, QgsPointXY v1, bool moving0, bool moving1, const QgsPointXY &mapPoint )
{
  // if layer is not null, the input coordinates are coming in the layer's CRS rather than map CRS
  if ( layer )
  {
    v0 = toMapCoordinates( layer, v0 );
    v1 = toMapCoordinates( layer, v1 );
  }

  StraightBand b;
  b.band = createRubberBand( QgsWkbTypes::LineGeometry, true );
  b.p0 = v0;
  b.p1 = v1;
  b.moving0 = moving0;
  b.moving1 = moving1;
  b.offset0 = v0 - mapPoint;
  b.offset1 = v1 - mapPoint;

  b.band->addPoint( v0 );
  b.band->addPoint( v1 );

  mDragStraightBands << b;
}

void QgsVertexTool::addDragCircularBand( QgsVectorLayer *layer, QgsPointXY v0, QgsPointXY v1, QgsPointXY v2, bool moving0, bool moving1, bool moving2, const QgsPointXY &mapPoint )
{
  // if layer is not null, the input coordinates are coming in the layer's CRS rather than map CRS
  if ( layer )
  {
    v0 = toMapCoordinates( layer, v0 );
    v1 = toMapCoordinates( layer, v1 );
    v2 = toMapCoordinates( layer, v2 );
  }

  CircularBand b;
  b.band = createRubberBand( QgsWkbTypes::LineGeometry, true );
  b.p0 = v0;
  b.p1 = v1;
  b.p2 = v2;
  b.moving0 = moving0;
  b.moving1 = moving1;
  b.moving2 = moving2;
  b.offset0 = v0 - mapPoint;
  b.offset1 = v1 - mapPoint;
  b.offset2 = v2 - mapPoint;
  b.updateRubberBand( mapPoint );

  mDragCircularBands << b;
}

void QgsVertexTool::clearDragBands()
{
  qDeleteAll( mDragPointMarkers );
  mDragPointMarkers.clear();
  mDragPointMarkersOffset.clear();

  for ( const StraightBand &b : qgis::as_const( mDragStraightBands ) )
    delete b.band;
  mDragStraightBands.clear();

  for ( const CircularBand &b : qgis::as_const( mDragCircularBands ) )
    delete b.band;
  mDragCircularBands.clear();
}

void QgsVertexTool::cadCanvasPressEvent( QgsMapMouseEvent *e )
{
  if ( mSelectionMethod == SelectionRange )
  {
    rangeMethodPressEvent( e );
    return;
  }

  cleanupVertexEditor();

  if ( !mDraggingVertex && !mSelectedVertices.isEmpty() && !( e->modifiers() & Qt::ShiftModifier ) && !( e->modifiers() & Qt::ControlModifier ) )
  {
    // only remove highlight if not clicked on one of highlighted vertices
    bool clickedOnHighlightedVertex = false;
    QgsPointLocator::Match m = snapToEditableLayer( e );
    if ( m.hasVertex() )
    {
      for ( const Vertex &selectedVertex : qgis::as_const( mSelectedVertices ) )
      {
        if ( selectedVertex.layer == m.layer() && selectedVertex.fid == m.featureId() && selectedVertex.vertexId == m.vertexIndex() )
        {
          clickedOnHighlightedVertex = true;
          break;
        }
      }
    }

    if ( !clickedOnHighlightedVertex )
      setHighlightedVertices( QList<Vertex>() ); // reset selection
  }

  if ( e->button() == Qt::LeftButton )
  {
    if ( e->modifiers() & Qt::ControlModifier || e->modifiers() & Qt::ShiftModifier )
    {
      // shift or ctrl-click vertices to highlight without entering edit mode
      QgsPointLocator::Match m = snapToEditableLayer( e );
      if ( m.hasVertex() )
      {
        Vertex vertex( m.layer(), m.featureId(), m.vertexIndex() );

        HighlightMode mode = ModeReset;
        if ( e->modifiers() & Qt::ShiftModifier )
        {
          // Shift+Click to add vertex to highlight
          mode = ModeAdd;
        }
        else if ( e->modifiers() & Qt::ControlModifier )
        {
          // Ctrl+Click to remove vertex
          mode = ModeSubtract;
        }

        setHighlightedVertices( QList<Vertex>() << vertex, mode );
        return;
      }
    }

    // the user may have started dragging a rect to select vertices
    if ( !mDraggingVertex && !mDraggingEdge )
      mSelectionRectStartPos.reset( new QPoint( e->pos() ) );
  }

  if ( e->button() == Qt::RightButton )
  {
    if ( !mSelectionRect && !mDraggingVertex && !mDraggingEdge )
    {
      // show popup menu - if we are on top of a feature
      if ( mLastMouseMoveMatch.isValid() && mLastMouseMoveMatch.layer() )
      {
        QMenu menu;
        QAction *actionVertexEditor = menu.addAction( tr( "Vertex editor" ) );
        connect( actionVertexEditor, &QAction::triggered, this, &QgsVertexTool::showVertexEditor );
        menu.exec( mCanvas->mapToGlobal( e->pos() ) );
      }
    }
  }
}

void QgsVertexTool::cadCanvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( mSelectionMethod == SelectionRange )
  {
    rangeMethodReleaseEvent( e );
    return;
  }

  if ( mNewVertexFromDoubleClick )
  {
    QgsPointLocator::Match m( *mNewVertexFromDoubleClick );
    mNewVertexFromDoubleClick.reset();

    // dragging of edges and double clicking on edges to add vertex are slightly overlapping
    // so we need to cancel edge moving before we start dragging new vertex
    stopDragging();
    startDraggingAddVertex( m );
  }
  else if ( mSelectionRect )
  {
    // only handling of selection rect being dragged
    QgsPointXY pt0 = toMapCoordinates( *mSelectionRectStartPos );
    QgsPointXY pt1 = toMapCoordinates( e->pos() );
    QgsRectangle map_rect( pt0, pt1 );
    QList<Vertex> vertices;

    // for each editable layer, select vertices
    const auto layers = canvas()->layers();
    for ( QgsMapLayer *layer : layers )
    {
      QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
      if ( !vlayer || !vlayer->isEditable() || !vlayer->isSpatial() )
        continue;

      QgsRectangle layerRect = toLayerCoordinates( vlayer, map_rect );
      QgsFeature f;
      QgsFeatureIterator fi = vlayer->getFeatures( QgsFeatureRequest( layerRect ).setSubsetOfAttributes( QgsAttributeList() ) );
      while ( fi.nextFeature( f ) )
      {
        QgsGeometry g = f.geometry();
        for ( int i = 0; i < g.constGet()->nCoordinates(); ++i )
        {
          QgsPointXY pt = g.vertexAt( i );
          if ( layerRect.contains( pt ) )
            vertices << Vertex( vlayer, f.id(), i );
        }
      }
    }

    HighlightMode mode = ModeReset;
    if ( e->modifiers() & Qt::ShiftModifier )
      mode = ModeAdd;
    else if ( e->modifiers() & Qt::ControlModifier )
      mode = ModeSubtract;

    setHighlightedVertices( vertices, mode );

    stopSelectionRect();
  }
  else  // selection rect is not being dragged
  {
    if ( e->button() == Qt::LeftButton && !( e->modifiers() & Qt::ShiftModifier ) && !( e->modifiers() & Qt::ControlModifier ) )
    {
      // accepting action
      if ( mDraggingVertex )
      {
        QgsPointLocator::Match match = e->mapPointMatch();
        moveVertex( e->mapPoint(), &match );
      }
      else if ( mDraggingEdge )
      {
        // do not use e.mapPoint() as it may be snapped
        moveEdge( toMapCoordinates( e->pos() ) );
      }
      else
      {
        startDragging( e );
      }
    }
    else if ( e->button() == Qt::RightButton )
    {
      // cancel action
      stopDragging();
    }
  }

  mSelectionRectStartPos.reset();
}

void QgsVertexTool::cadCanvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( mSelectionMethod == SelectionRange )
  {
    rangeMethodMoveEvent( e );
    return;
  }

  if ( mDraggingVertex )
  {
    mouseMoveDraggingVertex( e );
  }
  else if ( mDraggingEdge )
  {
    mouseMoveDraggingEdge( e );
  }
  else if ( mSelectionRectStartPos )
  {
    // the user may be dragging a rect to select vertices
    if ( !mSelectionRect && ( e->pos() - *mSelectionRectStartPos ).manhattanLength() >= 10 )
    {
      startSelectionRect( *mSelectionRectStartPos );
    }
    if ( mSelectionRect )
    {
      updateSelectionRect( e->pos() );
    }
  }
  else
  {
    mouseMoveNotDragging( e );
  }
}

void QgsVertexTool::mouseMoveDraggingVertex( QgsMapMouseEvent *e )
{
  mSnapIndicator->setMatch( e->mapPointMatch() );
  mEdgeCenterMarker->setVisible( false );

  moveDragBands( e->mapPoint() );
}

void QgsVertexTool::moveDragBands( const QgsPointXY &mapPoint )
{
  for ( int i = 0; i < mDragStraightBands.count(); ++i )
  {
    StraightBand &b = mDragStraightBands[i];
    if ( b.moving0 )
      b.band->movePoint( 0, mapPoint + b.offset0 );
    if ( b.moving1 )
      b.band->movePoint( 1, mapPoint + b.offset1 );
  }

  for ( int i = 0; i < mDragCircularBands.count(); ++i )
  {
    CircularBand &b = mDragCircularBands[i];
    b.updateRubberBand( mapPoint );
  }

  // in case of moving of standalone point geometry
  for ( int i = 0; i < mDragPointMarkers.count(); ++i )
  {
    QgsVertexMarker *marker = mDragPointMarkers[i];
    QgsVector offset = mDragPointMarkersOffset[i];
    marker->setCenter( mapPoint + offset );
  }

  // make sure the temporary feature rubber band is not visible
  removeTemporaryRubberBands();
}

void QgsVertexTool::mouseMoveDraggingEdge( QgsMapMouseEvent *e )
{
  mSnapIndicator->setMatch( QgsPointLocator::Match() );
  mEdgeCenterMarker->setVisible( false );

  QgsPointXY mapPoint = toMapCoordinates( e->pos() );  // do not use e.mapPoint() as it may be snapped

  moveDragBands( mapPoint );
}

void QgsVertexTool::canvasDoubleClickEvent( QgsMapMouseEvent *e )
{
  QgsPointLocator::Match m = snapToEditableLayer( e );
  if ( !m.hasEdge() )
    return;

  mNewVertexFromDoubleClick.reset( new QgsPointLocator::Match( m ) );
}

void QgsVertexTool::removeTemporaryRubberBands()
{
  mFeatureBand->setVisible( false );
  mFeatureBandMarkers->setVisible( false );
  mFeatureBandLayer = nullptr;
  mFeatureBandFid = QgsFeatureId();
  mVertexBand->setVisible( false );
  mEdgeBand->setVisible( false );
  mEndpointMarkerCenter.reset();
  mEndpointMarker->setVisible( false );
}

QgsPointLocator::Match QgsVertexTool::snapToEditableLayer( QgsMapMouseEvent *e )
{
  QgsSnappingUtils *snapUtils = canvas()->snappingUtils();
  QgsSnappingConfig oldConfig = snapUtils->config();
  QgsPointLocator::Match m;

  QgsPointXY mapPoint = toMapCoordinates( e->pos() );
  double tol = QgsTolerance::vertexSearchRadius( canvas()->mapSettings() );

  QgsSnappingConfig config( QgsProject::instance() );
  config.setEnabled( true );
  config.setMode( QgsSnappingConfig::AdvancedConfiguration );
  config.setIntersectionSnapping( false );  // only snap to layers

  // if there is a current layer, it should have priority over other layers
  // because sometimes there may be match from multiple layers at one location
  // and selecting current layer is an easy way for the user to prioritize a layer
  if ( QgsVectorLayer *currentVlayer = currentVectorLayer() )
  {
    if ( currentVlayer->isEditable() )
    {
      const auto layers = canvas()->layers();
      for ( QgsMapLayer *layer : layers )
      {
        QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
        if ( !vlayer )
          continue;

        config.setIndividualLayerSettings( vlayer, QgsSnappingConfig::IndividualLayerSettings(
                                             vlayer == currentVlayer, QgsSnappingConfig::VertexAndSegment, tol, QgsTolerance::ProjectUnits ) );
      }

      snapUtils->setConfig( config );
      m = snapUtils->snapToMap( mapPoint );
    }
  }

  // if there is no match from the current layer, try to use any editable vector layer
  if ( !m.isValid() )
  {
    const auto layers = canvas()->layers();
    for ( QgsMapLayer *layer : layers )
    {
      QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
      if ( !vlayer )
        continue;

      config.setIndividualLayerSettings( vlayer, QgsSnappingConfig::IndividualLayerSettings(
                                           vlayer->isEditable(), QgsSnappingConfig::VertexAndSegment, tol, QgsTolerance::ProjectUnits ) );
    }

    snapUtils->setConfig( config );
    m = snapUtils->snapToMap( mapPoint );
  }

  // try to stay snapped to previously used feature
  // so the highlight does not jump around at vertices where features are joined
  if ( mLastSnap )
  {
    OneFeatureFilter filterLast( mLastSnap->layer(), mLastSnap->featureId() );
    QgsPointLocator::Match lastMatch = snapUtils->snapToMap( mapPoint, &filterLast );
    // but skip the the previously used feature if it would only snap to segment, while now we have snap to vertex
    // so that if there is a point on a line, it gets priority (as is usual with combined vertex+segment snapping)
    bool matchHasVertexLastHasEdge = m.hasVertex() && lastMatch.hasEdge();
    if ( lastMatch.isValid() && lastMatch.distance() <= m.distance() && !matchHasVertexLastHasEdge )
    {
      m = lastMatch;
    }
  }

  snapUtils->setConfig( oldConfig );

  mLastSnap.reset( new QgsPointLocator::Match( m ) );

  return m;
}

bool QgsVertexTool::isNearEndpointMarker( const QgsPointXY &mapPoint )
{
  if ( !mEndpointMarkerCenter )
    return false;

  double distMarker = std::sqrt( mEndpointMarkerCenter->sqrDist( mapPoint ) );
  double tol = QgsTolerance::vertexSearchRadius( canvas()->mapSettings() );

  QgsGeometry geom = cachedGeometryForVertex( *mMouseAtEndpoint );
  QgsPointXY vertexPointV2 = geom.vertexAt( mMouseAtEndpoint->vertexId );
  QgsPointXY vertexPoint = QgsPointXY( vertexPointV2.x(), vertexPointV2.y() );
  double distVertex = std::sqrt( vertexPoint.sqrDist( mapPoint ) );

  return distMarker < tol && distMarker < distVertex;
}

bool QgsVertexTool::isMatchAtEndpoint( const QgsPointLocator::Match &match )
{
  QgsGeometry geom = cachedGeometry( match.layer(), match.featureId() );

  if ( geom.type() != QgsWkbTypes::LineGeometry )
    return false;

  return isEndpointAtVertexIndex( geom, match.vertexIndex() );
}

QgsPointXY QgsVertexTool::positionForEndpointMarker( const QgsPointLocator::Match &match )
{
  QgsGeometry geom = cachedGeometry( match.layer(), match.featureId() );

  QgsPointXY pt0 = geom.vertexAt( adjacentVertexIndexToEndpoint( geom, match.vertexIndex() ) );
  QgsPointXY pt1 = geom.vertexAt( match.vertexIndex() );

  pt0 = toMapCoordinates( match.layer(), pt0 );
  pt1 = toMapCoordinates( match.layer(), pt1 );

  double dx = pt1.x() - pt0.x();
  double dy = pt1.y() - pt0.y();
  double dist = 15 * canvas()->mapSettings().mapUnitsPerPixel();
  double angle = std::atan2( dy, dx );  // to the top: angle=0, to the right: angle=90, to the left: angle=-90
  double x = pt1.x() + std::cos( angle ) * dist;
  double y = pt1.y() + std::sin( angle ) * dist;
  return QgsPointXY( x, y );
}

void QgsVertexTool::mouseMoveNotDragging( QgsMapMouseEvent *e )
{
  if ( mMouseAtEndpoint )
  {
    // check if we are still at the endpoint, i.e. whether to keep showing
    // the endpoint indicator - or go back to snapping to editable layers
    QgsPointXY mapPoint = toMapCoordinates( e->pos() );
    if ( isNearEndpointMarker( mapPoint ) )
    {
      mEndpointMarker->setColor( Qt::red );
      mEndpointMarker->update();
      // make it clear this would add endpoint, not move the vertex
      mVertexBand->setVisible( false );
      return;
    }
  }

  // do not use snap from mouse event, use our own with any editable layer
  QgsPointLocator::Match m = snapToEditableLayer( e );

  mLastMouseMoveMatch = m;

  // possibility to move a vertex
  if ( m.type() == QgsPointLocator::Vertex )
  {
    updateVertexBand( m );

    // if we are at an endpoint, let's show also the endpoint indicator
    // so user can possibly add a new vertex at the end
    if ( isMatchAtEndpoint( m ) )
    {
      mMouseAtEndpoint.reset( new Vertex( m.layer(), m.featureId(), m.vertexIndex() ) );
      mEndpointMarkerCenter.reset( new QgsPointXY( positionForEndpointMarker( m ) ) );
      mEndpointMarker->setCenter( *mEndpointMarkerCenter );
      mEndpointMarker->setColor( Qt::gray );
      mEndpointMarker->setVisible( true );
      mEndpointMarker->update();
    }
    else
    {
      mMouseAtEndpoint.reset();
      mEndpointMarkerCenter.reset();
      mEndpointMarker->setVisible( false );
    }
  }
  else
  {
    mVertexBand->setVisible( false );
    mMouseAtEndpoint.reset();
    mEndpointMarkerCenter.reset();
    mEndpointMarker->setVisible( false );
  }

  // possibility to create new vertex here - or to move the edge
  if ( m.type() == QgsPointLocator::Edge )
  {
    QgsPointXY mapPoint = toMapCoordinates( e->pos() );
    bool isCircularEdge = false;

    QgsPointXY p0, p1;
    m.edgePoints( p0, p1 );

    QgsGeometry geom = cachedGeometry( m.layer(), m.featureId() );
    if ( isCircularVertex( geom, m.vertexIndex() ) )
    {
      // circular edge at the first vertex
      isCircularEdge = true;
      QgsPointXY pX = toMapCoordinates( m.layer(), geom.vertexAt( m.vertexIndex() - 1 ) );
      QgsPointSequence points;
      QgsGeometryUtils::segmentizeArc( QgsPoint( pX ), QgsPoint( p0 ), QgsPoint( p1 ), points );
      mEdgeBand->reset();
      for ( const QgsPoint &pt : qgis::as_const( points ) )
        mEdgeBand->addPoint( pt );
    }
    else if ( isCircularVertex( geom, m.vertexIndex() + 1 ) )
    {
      // circular edge at the second vertex
      isCircularEdge = true;
      QgsPointXY pX = toMapCoordinates( m.layer(), geom.vertexAt( m.vertexIndex() + 2 ) );
      QgsPointSequence points;
      QgsGeometryUtils::segmentizeArc( QgsPoint( p0 ), QgsPoint( p1 ), QgsPoint( pX ), points );
      mEdgeBand->reset();
      for ( const QgsPoint &pt : qgis::as_const( points ) )
        mEdgeBand->addPoint( pt );
    }
    else
    {
      // straight edge
      QgsPolylineXY points;
      points << p0 << p1;
      mEdgeBand->setToGeometry( QgsGeometry::fromPolylineXY( points ), nullptr );
    }

    QgsPointXY edgeCenter;
    bool isNearCenter = matchEdgeCenterTest( m, mapPoint, &edgeCenter );
    mEdgeCenterMarker->setCenter( edgeCenter );
    mEdgeCenterMarker->setColor( isNearCenter ? Qt::red : Qt::gray );
    mEdgeCenterMarker->setVisible( !isCircularEdge );  // currently not supported for circular edges
    mEdgeCenterMarker->update();

    mEdgeBand->setVisible( !isNearCenter );
  }
  else
  {
    mEdgeCenterMarker->setVisible( false );
    mEdgeBand->setVisible( false );
  }

  updateFeatureBand( m );
}

void QgsVertexTool::updateVertexBand( const QgsPointLocator::Match &m )
{
  if ( m.hasVertex() && m.layer() )
  {
    mVertexBand->setToGeometry( QgsGeometry::fromPointXY( m.point() ), nullptr );
    mVertexBand->setVisible( true );
    bool isCircular = false;
    if ( m.layer() )
    {
      isCircular = isCircularVertex( cachedGeometry( m.layer(), m.featureId() ), m.vertexIndex() );
    }

    mVertexBand->setIcon( isCircular ? QgsRubberBand::ICON_FULL_DIAMOND : QgsRubberBand::ICON_CIRCLE );
  }
  else
  {
    mVertexBand->setVisible( false );
  }
}

void QgsVertexTool::updateFeatureBand( const QgsPointLocator::Match &m )
{
  // highlight feature
  if ( m.isValid() && m.layer() )
  {
    if ( mFeatureBandLayer == m.layer() && mFeatureBandFid == m.featureId() )
      return;  // skip regeneration of rubber band if not needed
    QgsGeometry geom = cachedGeometry( m.layer(), m.featureId() );
    mFeatureBandMarkers->setToGeometry( geometryToMultiPoint( geom ), m.layer() );
    mFeatureBandMarkers->setVisible( true );
    if ( QgsWkbTypes::isCurvedType( geom.wkbType() ) )
      geom = QgsGeometry( geom.constGet()->segmentize() );
    mFeatureBand->setToGeometry( geom, m.layer() );
    mFeatureBand->setVisible( true );
    mFeatureBandLayer = m.layer();
    mFeatureBandFid = m.featureId();
  }
  else
  {
    mFeatureBand->setVisible( false );
    mFeatureBandMarkers->setVisible( false );
    mFeatureBandLayer = nullptr;
    mFeatureBandFid = QgsFeatureId();
  }
}

void QgsVertexTool::keyPressEvent( QKeyEvent *e )
{
  if ( !mDraggingVertex && !mDraggingEdge && e->key() == Qt::Key_R && e->modifiers() & Qt::ShiftModifier )
  {
    startRangeVertexSelection();
    return;
  }
  if ( mSelectionMethod == SelectionRange && e->key() == Qt::Key_Escape )
  {
    stopRangeVertexSelection();
    return;
  }

  if ( !mDraggingVertex && mSelectedVertices.count() == 0 )
    return;

  if ( e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace )
  {
    e->ignore();  // Override default shortcut management
    deleteVertex();
  }
  else if ( e->key() == Qt::Key_Escape )
  {
    if ( mDraggingVertex )
      stopDragging();
  }
  else if ( e->key() == Qt::Key_Less || e->key() == Qt::Key_Comma )
  {
    highlightAdjacentVertex( -1 );
  }
  else if ( e->key() == Qt::Key_Greater || e->key() == Qt::Key_Period )
  {
    highlightAdjacentVertex( + 1 );
  }
}

QgsGeometry QgsVertexTool::cachedGeometry( const QgsVectorLayer *layer, QgsFeatureId fid )
{
  if ( !mCache.contains( layer ) )
  {
    connect( layer, &QgsVectorLayer::geometryChanged, this, &QgsVertexTool::onCachedGeometryChanged );
    connect( layer, &QgsVectorLayer::featureDeleted, this, &QgsVertexTool::onCachedGeometryDeleted );
    // TODO: also clear cache when layer is deleted
  }

  QHash<QgsFeatureId, QgsGeometry> &layerCache = mCache[layer];
  if ( !layerCache.contains( fid ) )
  {
    QgsFeature f;
    layer->getFeatures( QgsFeatureRequest( fid ).setSubsetOfAttributes( QgsAttributeList() ) ).nextFeature( f );
    layerCache[fid] = f.geometry();
  }

  return layerCache[fid];
}

QgsGeometry QgsVertexTool::cachedGeometryForVertex( const Vertex &vertex )
{
  return cachedGeometry( vertex.layer, vertex.fid );
}

void QgsVertexTool::onCachedGeometryChanged( QgsFeatureId fid, const QgsGeometry &geom )
{
  QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( sender() );
  Q_ASSERT( mCache.contains( layer ) );
  QHash<QgsFeatureId, QgsGeometry> &layerCache = mCache[layer];
  if ( layerCache.contains( fid ) )
    layerCache[fid] = geom;

  // refresh highlighted vertices - their position may have changed
  setHighlightedVertices( mSelectedVertices );

  // re-run validation for the feature
  validateGeometry( layer, fid );
}

void QgsVertexTool::onCachedGeometryDeleted( QgsFeatureId fid )
{
  QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( sender() );
  Q_ASSERT( mCache.contains( layer ) );
  QHash<QgsFeatureId, QgsGeometry> &layerCache = mCache[layer];
  if ( layerCache.contains( fid ) )
    layerCache.remove( fid );

  // refresh highlighted vertices - some may have been deleted
  setHighlightedVertices( mSelectedVertices );
}


void QgsVertexTool::showVertexEditor()
{
  QgsPointLocator::Match m = mLastMouseMoveMatch;
  if ( !m.isValid() || !m.layer() )
    return;

  mSelectedFeature.reset( new QgsSelectedFeature( m.featureId(), m.layer(), mCanvas ) );
  mVertexEditor.reset( new QgsVertexEditor( m.layer(), mSelectedFeature.get(), mCanvas ) );
  QgisApp::instance()->addDockWidget( Qt::LeftDockWidgetArea, mVertexEditor.get() );
  connect( mVertexEditor.get(), &QgsVertexEditor::deleteSelectedRequested, this, &QgsVertexTool::deleteVertexEditorSelection );
  connect( mSelectedFeature.get()->vlayer(), &QgsVectorLayer::featureDeleted, this, [ = ]( QgsFeatureId id ) { if ( mSelectedFeature.get()->featureId() == id ) cleanupVertexEditor(); } );
}

void QgsVertexTool::cleanupVertexEditor()
{
  mSelectedFeature.reset();
  mVertexEditor.reset();
}

static int _firstSelectedVertex( QgsSelectedFeature &selectedFeature )
{
  QList<QgsVertexEntry *> &vertexMap = selectedFeature.vertexMap();
  for ( int i = 0, n = vertexMap.size(); i < n; ++i )
  {
    if ( vertexMap[i]->isSelected() )
    {
      return i;
    }
  }
  return -1;
}

static void _safeSelectVertex( QgsSelectedFeature &selectedFeature, int vertexNr )
{
  int n = selectedFeature.vertexMap().size();
  selectedFeature.selectVertex( ( vertexNr + n ) % n );
}

void QgsVertexTool::deleteVertexEditorSelection()
{
  if ( !mSelectedFeature )
    return;

  int firstSelectedIndex = _firstSelectedVertex( *mSelectedFeature );
  if ( firstSelectedIndex == -1 )
    return;

  // make a list of selected vertices
  QList<Vertex> vertices;
  QList<QgsVertexEntry *> &selFeatureVertices = mSelectedFeature->vertexMap();
  QgsVectorLayer *layer = mSelectedFeature->vlayer();
  QgsFeatureId fid = mSelectedFeature->featureId();
  QgsGeometry geometry = cachedGeometry( layer, fid );
  for ( QgsVertexEntry *vertex : qgis::as_const( selFeatureVertices ) )
  {
    if ( vertex->isSelected() )
    {
      int vertexIndex = geometry.vertexNrFromVertexId( vertex->vertexId() );
      if ( vertexIndex != -1 )
        vertices.append( Vertex( layer, fid, vertexIndex ) );
    }
  }

  // now select the vertices and delete them...
  setHighlightedVertices( vertices );
  deleteVertex();

  if ( !mSelectedFeature->geometry()->isNull() )
  {
    int nextVertexToSelect = firstSelectedIndex;
    if ( mSelectedFeature->geometry()->type() == QgsWkbTypes::LineGeometry )
    {
      // for lines we don't wrap around vertex selection when deleting vertices from end of line
      nextVertexToSelect = std::min( nextVertexToSelect, mSelectedFeature->geometry()->constGet()->nCoordinates() - 1 );
    }

    _safeSelectVertex( *mSelectedFeature, nextVertexToSelect );
  }
  mSelectedFeature->vlayer()->triggerRepaint();
}


void QgsVertexTool::startDragging( QgsMapMouseEvent *e )
{
  QgsPointXY mapPoint = toMapCoordinates( e->pos() );
  if ( isNearEndpointMarker( mapPoint ) )
  {
    startDraggingAddVertexAtEndpoint( mapPoint );
    return;
  }

  QgsPointLocator::Match m = snapToEditableLayer( e );
  if ( !m.isValid() )
    return;

  // activate advanced digitizing dock
  setAdvancedDigitizingAllowed( true );

  // adding a new vertex instead of moving a vertex
  if ( m.hasEdge() )
  {
    // only start dragging if we are near edge center
    mapPoint = toMapCoordinates( e->pos() );
    bool isNearCenter = matchEdgeCenterTest( m, mapPoint );
    if ( isNearCenter )
      startDraggingAddVertex( m );
    else
      startDraggingEdge( m, mapPoint );
  }
  else   // vertex
  {
    startDraggingMoveVertex( m );
  }
}

void QgsVertexTool::startDraggingMoveVertex( const QgsPointLocator::Match &m )
{
  Q_ASSERT( m.hasVertex() );

  QgsGeometry geom = cachedGeometry( m.layer(), m.featureId() );

  // start dragging of snapped point of current layer
  mDraggingVertex.reset( new Vertex( m.layer(), m.featureId(), m.vertexIndex() ) );
  mDraggingVertexType = MovingVertex;
  mDraggingExtraVertices.clear();
  mDraggingExtraVerticesOffset.clear();

  setHighlightedVerticesVisible( false );  // hide any extra highlight of vertices until we are done with moving

  QgsPointXY origDraggingVertexPoint = geom.vertexAt( mDraggingVertex->vertexId );

  // if there are other highlighted vertices, they should be dragged as well with their offset
  for ( const Vertex &v : qgis::as_const( mSelectedVertices ) )
  {
    if ( v != *mDraggingVertex )
    {
      QgsPointXY origPointV = cachedGeometryForVertex( v ).vertexAt( v.vertexId );
      QgsPointXY origPointLayer = origDraggingVertexPoint;
      if ( v.layer->crs() != mDraggingVertex->layer->crs() )  // reproject if necessary
        origPointLayer = toLayerCoordinates( v.layer, toMapCoordinates( m.layer(), origDraggingVertexPoint ) );
      QgsVector offset = origPointV - origPointLayer;

      mDraggingExtraVertices << v;
      mDraggingExtraVerticesOffset << offset;
    }
  }

  cadDockWidget()->setPoints( QList<QgsPointXY>() << m.point() << m.point() );

  if ( QgsProject::instance()->topologicalEditing() )
  {
    // support for topo editing - find extra features
    // that have coincident point with the vertex being dragged
    const auto layers = canvas()->layers();
    for ( QgsMapLayer *layer : layers )
    {
      QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
      if ( !vlayer || !vlayer->isEditable() )
        continue;

      const auto snappedVertices = layerVerticesSnappedToPoint( vlayer, m.point() );
      for ( const QgsPointLocator::Match &otherMatch : snappedVertices )
      {
        if ( otherMatch.layer() == m.layer() &&
             otherMatch.featureId() == m.featureId() &&
             otherMatch.vertexIndex() == m.vertexIndex() )
          continue;

        // start dragging of snapped point of current layer
        mDraggingExtraVertices << Vertex( otherMatch.layer(), otherMatch.featureId(), otherMatch.vertexIndex() );
        mDraggingExtraVerticesOffset << QgsVector(); // topo vertices have the same position
      }
    }
  }

  // now build drag rubber bands for extra vertices

  QSet<Vertex> movingVertices;
  movingVertices << *mDraggingVertex;
  for ( const Vertex &v : qgis::as_const( mDraggingExtraVertices ) )
    movingVertices << v;

  QgsPointXY dragVertexMapPoint = m.point();

  buildDragBandsForVertices( movingVertices, dragVertexMapPoint );
}

void QgsVertexTool::buildDragBandsForVertices( const QSet<Vertex> &movingVertices, const QgsPointXY &dragVertexMapPoint )
{
  QSet<Vertex> verticesInStraightBands;  // always the vertex with lower index

  // set of middle vertices that are already in a circular rubber band
  // i.e. every circular band is defined by its middle circular vertex
  QSet<Vertex> verticesInCircularBands;

  for ( const Vertex &v : qgis::as_const( movingVertices ) )
  {
    int v0idx, v1idx;
    QgsGeometry geom = cachedGeometry( v.layer, v.fid );
    QgsPointXY pt = geom.vertexAt( v.vertexId );

    geom.adjacentVertices( v.vertexId, v0idx, v1idx );

    if ( v0idx != -1 && v1idx != -1 && isCircularVertex( geom, v.vertexId ) )
    {
      // the vertex is in the middle of a curved segment
      if ( !verticesInCircularBands.contains( v ) )
      {
        addDragCircularBand( v.layer,
                             geom.vertexAt( v0idx ),
                             pt,
                             geom.vertexAt( v1idx ),
                             movingVertices.contains( Vertex( v.layer, v.fid, v0idx ) ),
                             true,
                             movingVertices.contains( Vertex( v.layer, v.fid, v1idx ) ),
                             dragVertexMapPoint );
        verticesInCircularBands << v;
      }

      // skip the rest - no need for further straight of circular bands for this vertex
      // because our circular rubber band spans both towards left and right
      continue;
    }

    if ( v0idx != -1 )
    {
      // there is another vertex to the left - let's build a rubber band for it
      Vertex v0( v.layer, v.fid, v0idx );
      if ( isCircularVertex( geom, v0idx ) )
      {
        // circular segment to the left
        if ( !verticesInCircularBands.contains( v0 ) )
        {
          addDragCircularBand( v.layer,
                               geom.vertexAt( v0idx - 1 ),
                               geom.vertexAt( v0idx ),
                               pt,
                               movingVertices.contains( Vertex( v.layer, v.fid, v0idx - 1 ) ),
                               movingVertices.contains( Vertex( v.layer, v.fid, v0idx ) ),
                               true,
                               dragVertexMapPoint );
          verticesInCircularBands << v0;
        }
      }
      else
      {
        // straight segment to the left
        if ( !verticesInStraightBands.contains( v0 ) )
        {
          addDragStraightBand( v.layer,
                               geom.vertexAt( v0idx ),
                               pt,
                               movingVertices.contains( v0 ),
                               true,
                               dragVertexMapPoint );
          verticesInStraightBands << v0;
        }
      }
    }

    if ( v1idx != -1 )
    {
      // there is another vertex to the right - let's build a rubber band for it
      Vertex v1( v.layer, v.fid, v1idx );
      if ( isCircularVertex( geom, v1idx ) )
      {
        // circular segment to the right
        if ( !verticesInCircularBands.contains( v1 ) )
        {
          addDragCircularBand( v.layer,
                               pt,
                               geom.vertexAt( v1idx ),
                               geom.vertexAt( v1idx + 1 ),
                               true,
                               movingVertices.contains( v1 ),
                               movingVertices.contains( Vertex( v.layer, v.fid, v1idx + 1 ) ),
                               dragVertexMapPoint );
          verticesInCircularBands << v1;
        }
      }
      else
      {
        // straight segment to the right
        if ( !verticesInStraightBands.contains( v ) )
        {
          addDragStraightBand( v.layer,
                               pt,
                               geom.vertexAt( v1idx ),
                               true,
                               movingVertices.contains( v1 ),
                               dragVertexMapPoint );
          verticesInStraightBands << v;
        }
      }
    }

    if ( v0idx == -1 && v1idx == -1 )
    {
      // this is a standalone point - we need to use a marker for it
      // to give some feedback to the user

      QgsPointXY ptMapPoint = toMapCoordinates( v.layer, pt );
      QgsVertexMarker *marker = new QgsVertexMarker( mCanvas );
      marker->setIconType( QgsVertexMarker::ICON_X );
      marker->setColor( Qt::red );
      marker->setPenWidth( 3 );
      marker->setVisible( true );
      marker->setCenter( ptMapPoint );
      mDragPointMarkers << marker;
      mDragPointMarkersOffset << ( ptMapPoint - dragVertexMapPoint );
    }
  }
}

QList<QgsPointLocator::Match> QgsVertexTool::layerVerticesSnappedToPoint( QgsVectorLayer *layer, const QgsPointXY &mapPoint )
{
  MatchCollectingFilter myfilter( this );
  QgsPointLocator *loc = canvas()->snappingUtils()->locatorForLayer( layer );
  loc->nearestVertex( mapPoint, 0, &myfilter );
  return myfilter.matches;
}

void QgsVertexTool::startDraggingAddVertex( const QgsPointLocator::Match &m )
{
  Q_ASSERT( m.hasEdge() );

  // activate advanced digitizing dock
  setAdvancedDigitizingAllowed( true );

  mDraggingVertex.reset( new Vertex( m.layer(), m.featureId(), m.vertexIndex() + 1 ) );
  mDraggingVertexType = AddingVertex;
  mDraggingExtraVertices.clear();
  mDraggingExtraVerticesOffset.clear();

  QgsGeometry geom = cachedGeometry( m.layer(), m.featureId() );

  // TODO: handles rings correctly?
  QgsPointXY v0 = geom.vertexAt( m.vertexIndex() );
  QgsPointXY v1 = geom.vertexAt( m.vertexIndex() + 1 );

  QgsPointXY map_v0 = toMapCoordinates( m.layer(), v0 );
  QgsPointXY map_v1 = toMapCoordinates( m.layer(), v1 );

  if ( v0.x() != 0 || v0.y() != 0 )
    addDragBand( map_v0, m.point() );
  if ( v1.x() != 0 || v1.y() != 0 )
    addDragBand( map_v1, m.point() );

  cadDockWidget()->setPoints( QList<QgsPointXY>() << m.point() << m.point() );
}

void QgsVertexTool::startDraggingAddVertexAtEndpoint( const QgsPointXY &mapPoint )
{
  Q_ASSERT( mMouseAtEndpoint );

  // activate advanced digitizing dock
  setAdvancedDigitizingAllowed( true );

  mDraggingVertex.reset( new Vertex( mMouseAtEndpoint->layer, mMouseAtEndpoint->fid, mMouseAtEndpoint->vertexId ) );
  mDraggingVertexType = AddingEndpoint;
  mDraggingExtraVertices.clear();
  mDraggingExtraVerticesOffset.clear();

  QgsGeometry geom = cachedGeometry( mMouseAtEndpoint->layer, mMouseAtEndpoint->fid );
  QgsPointXY v0 = geom.vertexAt( mMouseAtEndpoint->vertexId );
  QgsPointXY map_v0 = toMapCoordinates( mMouseAtEndpoint->layer, v0 );

  addDragBand( map_v0, mapPoint );

  // setup CAD dock previous points to endpoint and the previous point
  QgsPointXY pt0 = geom.vertexAt( adjacentVertexIndexToEndpoint( geom, mMouseAtEndpoint->vertexId ) );
  QgsPointXY pt1 = geom.vertexAt( mMouseAtEndpoint->vertexId );

  cadDockWidget()->setPoints( QList<QgsPointXY>() << pt0 << pt1 << pt1 );
}

void QgsVertexTool::startDraggingEdge( const QgsPointLocator::Match &m, const QgsPointXY &mapPoint )
{
  Q_ASSERT( m.hasEdge() );

  // activate advanced digitizing
  setAdvancedDigitizingAllowed( true );

  mDraggingEdge = true;
  mDraggingExtraVertices.clear();
  mDraggingExtraVerticesOffset.clear();

  QgsGeometry geom = cachedGeometry( m.layer(), m.featureId() );

  QSet<Vertex> movingVertices;
  movingVertices << Vertex( m.layer(), m.featureId(), m.vertexIndex() );
  movingVertices << Vertex( m.layer(), m.featureId(), m.vertexIndex() + 1 );

  // add an extra vertex if it is circular edge - so that we move the whole edge and not just one part of it
  if ( isCircularVertex( geom, m.vertexIndex() ) )
  {
    movingVertices << Vertex( m.layer(), m.featureId(), m.vertexIndex() - 1 );
  }
  else if ( isCircularVertex( geom, m.vertexIndex() + 1 ) )
  {
    movingVertices << Vertex( m.layer(), m.featureId(), m.vertexIndex() + 2 );
  }

  buildDragBandsForVertices( movingVertices, mapPoint );

  QgsPointXY layerPoint = toLayerCoordinates( m.layer(), mapPoint );

  for ( const Vertex &v : qgis::as_const( movingVertices ) )
  {
    mDraggingExtraVertices << v;
    mDraggingExtraVerticesOffset << ( geom.vertexAt( v.vertexId ) - QgsPoint( layerPoint ) );
  }

  cadDockWidget()->setPoints( QList<QgsPointXY>() << m.point() << m.point() );
}

void QgsVertexTool::stopDragging()
{
  // deactivate advanced digitizing
  setAdvancedDigitizingAllowed( false );
  cadDockWidget()->clear();  // clear cad points and release locks

  mDraggingVertex.reset();
  mDraggingVertexType = NotDragging;
  mDraggingEdge = false;
  clearDragBands();

  setHighlightedVerticesVisible( true );  // highlight can be shown again

  mSnapIndicator->setMatch( QgsPointLocator::Match() );
}

QgsPointXY QgsVertexTool::matchToLayerPoint( const QgsVectorLayer *destLayer, const QgsPointXY &mapPoint, const QgsPointLocator::Match *match )
{
  // try to use point coordinates in the original CRS if it is the same
  if ( match && match->hasVertex() && match->layer() && match->layer()->crs() == destLayer->crs() )
  {
    QgsFeature f;
    QgsFeatureIterator fi = match->layer()->getFeatures( QgsFeatureRequest( match->featureId() ).setSubsetOfAttributes( QgsAttributeList() ) );
    if ( fi.nextFeature( f ) )
      return f.geometry().vertexAt( match->vertexIndex() );
  }

  // fall back to reprojection of the map point to layer point if they are not the same CRS
  return toLayerCoordinates( destLayer, mapPoint );
}

void QgsVertexTool::moveEdge( const QgsPointXY &mapPoint )
{
  stopDragging();

  VertexEdits edits;
  addExtraVerticesToEdits( edits, mapPoint );

  applyEditsToLayers( edits );
}

void QgsVertexTool::moveVertex( const QgsPointXY &mapPoint, const QgsPointLocator::Match *mapPointMatch )
{
  // deactivate advanced digitizing
  setAdvancedDigitizingAllowed( false );

  QgsVectorLayer *dragLayer = mDraggingVertex->layer;
  QgsFeatureId dragFid = mDraggingVertex->fid;
  int dragVertexId = mDraggingVertex->vertexId;
  bool addingVertex = mDraggingVertexType == AddingVertex || mDraggingVertexType == AddingEndpoint;
  bool addingAtEndpoint = mDraggingVertexType == AddingEndpoint;
  QgsGeometry geom = cachedGeometryForVertex( *mDraggingVertex );
  stopDragging();

  QgsPointXY layerPoint = matchToLayerPoint( dragLayer, mapPoint, mapPointMatch );

  QgsVertexId vid;
  if ( !geom.vertexIdFromVertexNr( dragVertexId, vid ) )
  {
    QgsDebugMsg( "invalid vertex index" );
    return;
  }

  QgsAbstractGeometry *geomTmp = geom.constGet()->clone();

  // add/move vertex
  if ( addingVertex )
  {
    if ( addingAtEndpoint && vid.vertex != 0 )  // appending?
      vid.vertex++;

    QgsPoint pt( layerPoint );
    if ( QgsWkbTypes::hasZ( dragLayer->wkbType() ) )
      pt.addZValue( defaultZValue() );

    if ( !geomTmp->insertVertex( vid, pt ) )
    {
      QgsDebugMsg( "append vertex failed!" );
      return;
    }
  }
  else
  {
    if ( !geomTmp->moveVertex( vid, QgsPoint( layerPoint ) ) )
    {
      QgsDebugMsg( "move vertex failed!" );
      return;
    }
  }

  geom.set( geomTmp );

  VertexEdits edits; // dict { layer : { fid : geom } }
  edits[dragLayer][dragFid] = geom;

  addExtraVerticesToEdits( edits, mapPoint, dragLayer, layerPoint );

  applyEditsToLayers( edits );

  if ( QgsProject::instance()->topologicalEditing() && mapPointMatch->hasEdge() && mapPointMatch->layer() )
  {
    // topo editing: add vertex to existing segments when moving/adding a vertex to such segment.
    // this requires that the snapping match is to a segment and the segment layer's CRS
    // is the same (otherwise we would need to reproject the point and it will not be coincident)
    const auto editKeys = edits.keys();
    for ( QgsVectorLayer *layer : editKeys )
    {
      if ( layer->crs() == mapPointMatch->layer()->crs() )
      {
        layer->addTopologicalPoints( layerPoint );
      }
    }
  }

  setHighlightedVertices( mSelectedVertices );  // update positions of existing highlighted vertices
  setHighlightedVerticesVisible( true );  // time to show highlighted vertices again
}


void QgsVertexTool::addExtraVerticesToEdits( QgsVertexTool::VertexEdits &edits, const QgsPointXY &mapPoint, QgsVectorLayer *dragLayer, const QgsPointXY &layerPoint )
{
  Q_ASSERT( mDraggingExtraVertices.count() == mDraggingExtraVerticesOffset.count() );
  // add moved vertices from other layers
  for ( int i = 0; i < mDraggingExtraVertices.count(); ++i )
  {
    const Vertex &topo = mDraggingExtraVertices[i];
    const QgsVector &offset = mDraggingExtraVerticesOffset[i];

    QHash<QgsFeatureId, QgsGeometry> &layerEdits = edits[topo.layer];
    QgsGeometry topoGeom;
    if ( layerEdits.contains( topo.fid ) )
      topoGeom = QgsGeometry( edits[topo.layer][topo.fid] );
    else
      topoGeom = QgsGeometry( cachedGeometryForVertex( topo ) );

    QgsPointXY point;
    if ( dragLayer && topo.layer->crs() == dragLayer->crs() )
      point = layerPoint;  // this point may come from exact match so it may be more precise
    else
      point = toLayerCoordinates( topo.layer, mapPoint );

    if ( offset.x() || offset.y() )
    {
      point += offset;
    }

    if ( !topoGeom.moveVertex( point.x(), point.y(), topo.vertexId ) )
    {
      QgsDebugMsg( "[topo] move vertex failed!" );
      continue;
    }
    edits[topo.layer][topo.fid] = topoGeom;
  }
}


void QgsVertexTool::applyEditsToLayers( QgsVertexTool::VertexEdits &edits )
{
  QHash<QgsVectorLayer *, QHash<QgsFeatureId, QgsGeometry> >::iterator it = edits.begin();
  for ( ; it != edits.end(); ++it )
  {
    QgsVectorLayer *layer = it.key();
    QHash<QgsFeatureId, QgsGeometry> &layerEdits = it.value();
    layer->beginEditCommand( tr( "Moved vertex" ) );
    QHash<QgsFeatureId, QgsGeometry>::iterator it2 = layerEdits.begin();
    for ( ; it2 != layerEdits.end(); ++it2 )
      layer->changeGeometry( it2.key(), it2.value() );
    layer->endEditCommand();
    layer->triggerRepaint();
  }
}


void QgsVertexTool::deleteVertex()
{
  QSet<Vertex> toDelete;
  if ( !mSelectedVertices.isEmpty() )
  {
    toDelete = QSet<Vertex>::fromList( mSelectedVertices );
  }
  else
  {
    bool addingVertex = mDraggingVertexType == AddingVertex || mDraggingVertexType == AddingEndpoint;
    toDelete << *mDraggingVertex;
    toDelete += QSet<Vertex>::fromList( mDraggingExtraVertices );

    if ( addingVertex )
    {
      stopDragging();
      return;   // just cancel the vertex
    }
  }

  stopDragging();
  setHighlightedVertices( QList<Vertex>() ); // reset selection

  if ( QgsProject::instance()->topologicalEditing() )
  {
    // if topo editing is enabled, delete all the vertices that are on the same location
    QSet<Vertex> topoVerticesToDelete;
    for ( const Vertex &vertexToDelete : qgis::as_const( toDelete ) )
    {
      QgsPointXY layerPt = cachedGeometryForVertex( vertexToDelete ).vertexAt( vertexToDelete.vertexId );
      QgsPointXY mapPt = toMapCoordinates( vertexToDelete.layer, layerPt );
      const auto snappedVertices = layerVerticesSnappedToPoint( vertexToDelete.layer, mapPt );
      for ( const QgsPointLocator::Match &otherMatch : snappedVertices )
      {
        Vertex otherVertex( otherMatch.layer(), otherMatch.featureId(), otherMatch.vertexIndex() );
        if ( toDelete.contains( otherVertex ) || topoVerticesToDelete.contains( otherVertex ) )
          continue;

        topoVerticesToDelete.insert( otherVertex );
      }
    }

    toDelete.unite( topoVerticesToDelete );
  }

  // switch from a plain list to dictionary { layer: { fid: [vertexNr1, vertexNr2, ...] } }
  QHash<QgsVectorLayer *, QHash<QgsFeatureId, QList<int> > > toDeleteGrouped;
  for ( const Vertex &vertex : qgis::as_const( toDelete ) )
  {
    toDeleteGrouped[vertex.layer][vertex.fid].append( vertex.vertexId );
  }

  // de-duplicate vertices in linear rings - if there is the first vertex selected,
  // then also the last vertex will be selected - but we want just one out of the pair
  QHash<QgsVectorLayer *, QHash<QgsFeatureId, QList<int> > >::iterator itX = toDeleteGrouped.begin();
  for ( ; itX != toDeleteGrouped.end(); ++itX )
  {
    QgsVectorLayer *layer = itX.key();
    QHash<QgsFeatureId, QList<int> > &featuresDict = itX.value();

    QHash<QgsFeatureId, QList<int> >::iterator it2 = featuresDict.begin();
    for ( ; it2 != featuresDict.end(); ++it2 )
    {
      QgsFeatureId fid = it2.key();
      QList<int> &vertexIds = it2.value();
      if ( vertexIds.count() >= 2 && layer->geometryType() == QgsWkbTypes::PolygonGeometry )
      {
        QSet<int> duplicateVertexIndices;
        QgsGeometry geom = cachedGeometry( layer, fid );
        for ( int i = 0; i < vertexIds.count(); ++i )
        {
          QgsVertexId vid;
          if ( geom.vertexIdFromVertexNr( vertexIds[i], vid ) )
          {
            int ringVertexCount = geom.constGet()->vertexCount( vid.part, vid.ring );
            if ( vid.vertex == ringVertexCount - 1 )
            {
              // this is the last vertex of the ring - remove the first vertex from the list
              duplicateVertexIndices << geom.vertexNrFromVertexId( QgsVertexId( vid.part, vid.ring, 0 ) );
            }
          }
        }
        // now delete the duplicities
        for ( int duplicateVertexIndex : qgis::as_const( duplicateVertexIndices ) )
          vertexIds.removeOne( duplicateVertexIndex );
      }
    }
  }

  // main for cycle to delete all selected vertices
  QHash<QgsVectorLayer *, QHash<QgsFeatureId, QList<int> > >::iterator it = toDeleteGrouped.begin();
  for ( ; it != toDeleteGrouped.end(); ++it )
  {
    QgsVectorLayer *layer = it.key();
    QHash<QgsFeatureId, QList<int> > &featuresDict = it.value();

    layer->beginEditCommand( tr( "Deleted vertex" ) );
    bool success = true;

    QHash<QgsFeatureId, QList<int> >::iterator it2 = featuresDict.begin();
    for ( ; it2 != featuresDict.end(); ++it2 )
    {
      QgsFeatureId fid = it2.key();
      QList<int> &vertexIds = it2.value();

      bool res = QgsVectorLayer::Success;
      std::sort( vertexIds.begin(), vertexIds.end(), std::greater<int>() );
      for ( int vertexId : vertexIds )
      {
        if ( res != QgsVectorLayer::EmptyGeometry )
          res = layer->deleteVertex( fid, vertexId );
        if ( res != QgsVectorLayer::EmptyGeometry && res != QgsVectorLayer::Success )
        {
          QgsDebugMsg( QString( "failed to delete vertex %1 %2 %3!" ).arg( layer->name() ).arg( fid ).arg( vertexId ) );
          success = false;
        }
      }

      if ( res == QgsVectorLayer::EmptyGeometry )
      {
        emit messageEmitted( tr( "Geometry has been cleared. Use the add part tool to set geometry for this feature." ) );
      }
    }

    if ( success )
    {
      layer->endEditCommand();
      layer->triggerRepaint();
    }
    else
      layer->destroyEditCommand();
  }

  // make sure the temporary feature rubber band is not visible
  removeTemporaryRubberBands();

  // pre-select next vertex for deletion if we are deleting just one vertex
  if ( toDelete.count() == 1 )
  {
    const Vertex &vertex = *toDelete.constBegin();
    QgsGeometry geom( cachedGeometryForVertex( vertex ) );
    int vertexId = vertex.vertexId;

    // if next vertex is not available, use the previous one
    if ( geom.vertexAt( vertexId ) == QgsPoint() )
      vertexId -= 1;

    if ( geom.vertexAt( vertexId ) != QgsPoint() )
    {
      QList<Vertex> vertices_new;
      vertices_new << Vertex( vertex.layer, vertex.fid, vertexId );
      setHighlightedVertices( vertices_new );
    }
  }
}

void QgsVertexTool::setHighlightedVertices( const QList<Vertex> &listVertices, HighlightMode mode )
{
  if ( mode == ModeReset )
  {
    qDeleteAll( mSelectedVerticesMarkers );
    mSelectedVerticesMarkers.clear();
    mSelectedVertices.clear();
  }
  else if ( mode == ModeSubtract )
  {
    // need to clear vertex markers, and rebuild later. We have no way to link
    // a marker to a vertex in order to remove one-by-one
    qDeleteAll( mSelectedVerticesMarkers );
    mSelectedVerticesMarkers.clear();
  }

  auto createMarkerForVertex = [ = ]( const Vertex & vertex )->bool
  {
    QgsGeometry geom = cachedGeometryForVertex( vertex );
    QgsVertexId vid;
    if ( !geom.vertexIdFromVertexNr( vertex.vertexId, vid ) )
      return false;  // vertex may not exist anymore

    QgsVertexMarker *marker = new QgsVertexMarker( canvas() );
    marker->setIconType( QgsVertexMarker::ICON_CIRCLE );
    marker->setPenWidth( 3 );
    marker->setColor( Qt::blue );
    marker->setFillColor( Qt::blue );
    marker->setCenter( toMapCoordinates( vertex.layer, geom.vertexAt( vertex.vertexId ) ) );
    mSelectedVerticesMarkers.append( marker );
    return true;
  };

  for ( const Vertex &vertex : listVertices )
  {
    if ( mode == ModeAdd && mSelectedVertices.contains( vertex ) )
    {
      continue;
    }
    else if ( mode == ModeSubtract )
    {
      mSelectedVertices.removeAll( vertex );
      continue;
    }

    if ( !createMarkerForVertex( vertex ) )
      continue;  // vertex may not exist anymore

    mSelectedVertices.append( vertex );
  }

  if ( mode == ModeSubtract )
  {
    // rebuild markers for remaining selection
    for ( const Vertex &vertex : qgis::as_const( mSelectedVertices ) )
    {
      createMarkerForVertex( vertex );
    }
  }
}

void QgsVertexTool::setHighlightedVerticesVisible( bool visible )
{
  for ( QgsVertexMarker *marker : qgis::as_const( mSelectedVerticesMarkers ) )
    marker->setVisible( visible );
}

void QgsVertexTool::highlightAdjacentVertex( double offset )
{
  if ( mSelectedVertices.isEmpty() )
    return;

  Vertex vertex = mSelectedVertices[0];  // simply use the first one

  QgsGeometry geom = cachedGeometryForVertex( vertex );

  // try to wrap around polygon rings
  int newVertexId, v0idx, v1idx;
  geom.adjacentVertices( vertex.vertexId, v0idx, v1idx );
  if ( offset == -1 && v0idx != -1 )
    newVertexId = v0idx;
  else if ( offset == 1 && v1idx != -1 )
    newVertexId = v1idx;
  else
    newVertexId = vertex.vertexId + offset;

  QgsPointXY pt = geom.vertexAt( newVertexId );
  if ( pt != QgsPointXY() )
    vertex = Vertex( vertex.layer, vertex.fid, newVertexId );
  setHighlightedVertices( QList<Vertex>() << vertex );
  zoomToVertex( vertex );  // make sure the vertex is visible
}

void QgsVertexTool::startSelectionRect( const QPoint &point0 )
{
  Q_ASSERT( !mSelectionRect );
  mSelectionRect.reset( new QRect() );
  mSelectionRect->setTopLeft( point0 );
  mSelectionRectItem = new QRubberBand( QRubberBand::Rectangle, canvas() );
}

void QgsVertexTool::updateSelectionRect( const QPoint &point1 )
{
  Q_ASSERT( mSelectionRect );
  mSelectionRect->setBottomRight( point1 );
  mSelectionRectItem->setGeometry( mSelectionRect->normalized() );
  mSelectionRectItem->show();
}

void QgsVertexTool::stopSelectionRect()
{
  Q_ASSERT( mSelectionRect );
  mSelectionRectItem->deleteLater();
  mSelectionRectItem = nullptr;
  mSelectionRect.reset();
}

bool QgsVertexTool::matchEdgeCenterTest( const QgsPointLocator::Match &m, const QgsPointXY &mapPoint, QgsPointXY *edgeCenterPtr )
{
  QgsPointXY p0, p1;
  m.edgePoints( p0, p1 );

  QgsGeometry geom = cachedGeometry( m.layer(), m.featureId() );
  if ( isCircularVertex( geom, m.vertexIndex() ) || isCircularVertex( geom, m.vertexIndex() + 1 ) )
    return false;  // currently not supported for circular edges

  QgsRectangle visible_extent = canvas()->mapSettings().visibleExtent();
  if ( !visible_extent.contains( p0 ) || !visible_extent.contains( p1 ) )
  {
    // clip line segment to the extent so the mid-point marker is always visible
    QgsGeometry extentGeom = QgsGeometry::fromRect( visible_extent );
    QgsGeometry lineGeom = QgsGeometry::fromPolylineXY( QgsPolylineXY() << p0 << p1 );
    lineGeom = extentGeom.intersection( lineGeom );
    QgsPolylineXY polyline = lineGeom.asPolyline();
    Q_ASSERT( polyline.count() == 2 );
    p0 = polyline[0];
    p1 = polyline[1];
  }

  QgsPointXY edgeCenter( ( p0.x() + p1.x() ) / 2, ( p0.y() + p1.y() ) / 2 );
  if ( edgeCenterPtr )
    *edgeCenterPtr = edgeCenter;

  double distFromEdgeCenter = std::sqrt( mapPoint.sqrDist( edgeCenter ) );
  double tol = QgsTolerance::vertexSearchRadius( canvas()->mapSettings() );
  bool isNearCenter = distFromEdgeCenter < tol;
  return isNearCenter;
}

void QgsVertexTool::CircularBand::updateRubberBand( const QgsPointXY &mapPoint )
{
  QgsPointSequence points;
  QgsPointXY v0 = moving0 ? mapPoint + offset0 : p0;
  QgsPointXY v1 = moving1 ? mapPoint + offset1 : p1;
  QgsPointXY v2 = moving2 ? mapPoint + offset2 : p2;
  QgsGeometryUtils::segmentizeArc( QgsPoint( v0 ), QgsPoint( v1 ), QgsPoint( v2 ), points );
  // it would be useful to have QgsRubberBand::setPoints() call
  band->reset();
  for ( const QgsPoint &p : qgis::as_const( points ) )
    band->addPoint( p );
}


void QgsVertexTool::validationErrorFound( const QgsGeometry::Error &e )
{
  QgsGeometryValidator *validator = qobject_cast<QgsGeometryValidator *>( sender() );
  if ( !validator )
    return;

  QHash< QPair<QgsVectorLayer *, QgsFeatureId>, GeometryValidation>::iterator it = mValidations.begin();
  for ( ; it != mValidations.end(); ++it )
  {
    GeometryValidation &validation = *it;
    if ( validation.validator == validator )
    {
      validation.addError( e );
      break;
    }
  }
}

void QgsVertexTool::validationFinished()
{
  QgsGeometryValidator *validator = qobject_cast<QgsGeometryValidator *>( sender() );
  if ( !validator )
    return;

  QHash< QPair<QgsVectorLayer *, QgsFeatureId>, GeometryValidation>::iterator it = mValidations.begin();
  for ( ; it != mValidations.end(); ++it )
  {
    GeometryValidation &validation = *it;
    if ( validation.validator == validator )
    {
      QgsStatusBar *sb = QgisApp::instance()->statusBarIface();
      sb->showMessage( tr( "Validation finished (%n error(s) found).", "number of geometry errors", validation.errorMarkers.size() ) );
      if ( validation.errorMarkers.isEmpty() )
      {
        // not needed anymore (no markers to keep displayed)
        validation.cleanup();
        mValidations.remove( it.key() );
      }
      break;
    }
  }
}

void QgsVertexTool::GeometryValidation::start( QgsGeometry &geom, QgsVertexTool *t, QgsVectorLayer *l )
{
  tool = t;
  layer = l;
  QgsGeometry::ValidationMethod method = QgsGeometry::ValidatorQgisInternal;
  QgsSettings settings;
  if ( settings.value( QStringLiteral( "qgis/digitizing/validate_geometries" ), 1 ).toInt() == 2 )
    method = QgsGeometry::ValidatorGeos;

  validator = new QgsGeometryValidator( geom, nullptr, method );
  connect( validator, &QgsGeometryValidator::errorFound, tool, &QgsVertexTool::validationErrorFound );
  connect( validator, &QThread::finished, tool, &QgsVertexTool::validationFinished );
  validator->start();
}

void QgsVertexTool::GeometryValidation::addError( QgsGeometry::Error e )
{
  if ( !errors.isEmpty() )
    errors += '\n';
  errors += e.what();

  if ( e.hasWhere() )
  {
    QgsVertexMarker *marker = new QgsVertexMarker( tool->canvas() );
    marker->setCenter( tool->canvas()->mapSettings().layerToMapCoordinates( layer, e.where() ) );
    marker->setIconType( QgsVertexMarker::ICON_X );
    marker->setColor( Qt::green );
    marker->setZValue( marker->zValue() + 1 );
    marker->setPenWidth( 2 );
    marker->setToolTip( e.what() );
    errorMarkers << marker;
  }

  QgsStatusBar *sb = QgisApp::instance()->statusBarIface();
  sb->showMessage( e.what() );
  sb->setToolTip( errors );
}

void QgsVertexTool::GeometryValidation::cleanup()
{
  if ( validator )
  {
    validator->stop();
    validator->wait();
    validator->deleteLater();
    validator = nullptr;
  }

  qDeleteAll( errorMarkers );
  errorMarkers.clear();
}

void QgsVertexTool::validateGeometry( QgsVectorLayer *layer, QgsFeatureId featureId )
{
  QgsSettings settings;
  if ( settings.value( QStringLiteral( "qgis/digitizing/validate_geometries" ), 1 ).toInt() == 0 )
    return;

  QPair<QgsVectorLayer *, QgsFeatureId> id( layer, featureId );
  if ( mValidations.contains( id ) )
  {
    mValidations[id].cleanup();
    mValidations.remove( id );
  }

  GeometryValidation validation;
  QgsGeometry geom = cachedGeometry( layer, featureId );
  validation.start( geom, this, layer );
  mValidations.insert( id, validation );
}

void QgsVertexTool::zoomToVertex( const Vertex &vertex )
{
  QgsPointXY newCenter = cachedGeometryForVertex( vertex ).vertexAt( vertex.vertexId );
  QgsPointXY mapPoint = mCanvas->mapSettings().layerToMapCoordinates( vertex.layer, newCenter );
  QPolygonF ext = mCanvas->mapSettings().visiblePolygon();
  if ( !ext.containsPoint( mapPoint.toQPointF(), Qt::OddEvenFill ) )
  {
    mCanvas->setCenter( mapPoint );
    mCanvas->refresh();
  }
}

QList<Vertex> QgsVertexTool::verticesInRange( QgsVectorLayer *layer, QgsFeatureId fid, int vertexId0, int vertexId1, bool longWay )
{
  QgsGeometry geom = cachedGeometry( layer, fid );

  if ( vertexId0 > vertexId1 )
    std::swap( vertexId0, vertexId1 );

  // check it is the same part and ring
  QgsVertexId vid0, vid1;
  geom.vertexIdFromVertexNr( vertexId0, vid0 );
  geom.vertexIdFromVertexNr( vertexId1, vid1 );
  if ( vid0.part != vid1.part || vid0.ring != vid1.ring )
    return QList<Vertex>();

  // check whether we are in a linear ring
  int vertexIdTmp = vertexId0 - 1;
  QgsVertexId vidTmp;
  while ( geom.vertexIdFromVertexNr( vertexIdTmp, vidTmp ) &&
          vidTmp.part == vid0.part && vidTmp.ring == vid0.ring )
    --vertexIdTmp;
  int startVertexIndex = vertexIdTmp + 1;

  vertexIdTmp = vertexId1 + 1;
  while ( geom.vertexIdFromVertexNr( vertexIdTmp, vidTmp ) &&
          vidTmp.part == vid0.part && vidTmp.ring == vid0.ring )
    ++vertexIdTmp;
  int endVertexIndex = vertexIdTmp - 1;

  QList<Vertex> lst;

  if ( geom.vertexAt( startVertexIndex ) == geom.vertexAt( endVertexIndex ) )
  {
    // closed curve - we need to find out which way around the curve is shorter
    double lengthTotal = 0, length0to1 = 0;
    QgsPoint ptOld = geom.vertexAt( startVertexIndex );
    for ( int i = startVertexIndex + 1; i <= endVertexIndex; ++i )
    {
      QgsPoint pt( geom.vertexAt( i ) );
      double len = ptOld.distance( pt );
      lengthTotal += len;
      if ( i > vertexId0 && i <= vertexId1 )
        length0to1 += len;
      ptOld = pt;
    }

    bool use0to1 = length0to1 < lengthTotal / 2;
    if ( longWay )
      use0to1 = !use0to1;
    for ( int i = startVertexIndex; i <= endVertexIndex; ++i )
    {
      bool isPickedVertex = i == vertexId0 || i == vertexId1;
      bool is0to1 = i > vertexId0 && i < vertexId1;
      if ( isPickedVertex || is0to1 == use0to1 )
        lst.append( Vertex( layer, fid, i ) );
    }
  }
  else
  {
    // curve that is not closed
    for ( int i = vertexId0; i <= vertexId1; ++i )
    {
      lst.append( Vertex( layer, fid, i ) );
    }
  }
  return lst;
}

void QgsVertexTool::rangeMethodPressEvent( QgsMapMouseEvent *e )
{
  // nothing to do here for now...
  Q_UNUSED( e );
}

void QgsVertexTool::rangeMethodReleaseEvent( QgsMapMouseEvent *e )
{
  if ( e->button() == Qt::RightButton )
  {
    stopRangeVertexSelection();
    return;
  }
  else if ( e->button() == Qt::LeftButton )
  {
    if ( mRangeSelectionFirstVertex )
    {
      // pick final vertex, make selection and switch back to normal selection
      QgsPointLocator::Match m = snapToEditableLayer( e );
      if ( m.hasVertex() )
      {
        if ( m.layer() == mRangeSelectionFirstVertex->layer && m.featureId() == mRangeSelectionFirstVertex->fid )
        {
          QList<Vertex> lst = verticesInRange( m.layer(), m.featureId(), mRangeSelectionFirstVertex->vertexId, m.vertexIndex(), e->modifiers() & Qt::ControlModifier );
          setHighlightedVertices( lst );

          mSelectionMethod = SelectionNormal;
        }
      }
    }
    else
    {
      // pick first vertex
      QgsPointLocator::Match m = snapToEditableLayer( e );
      if ( m.hasVertex() )
      {
        mRangeSelectionFirstVertex.reset( new Vertex( m.layer(), m.featureId(), m.vertexIndex() ) );
        setHighlightedVertices( QList<Vertex>() << *mRangeSelectionFirstVertex );
      }
    }
  }
}

void QgsVertexTool::rangeMethodMoveEvent( QgsMapMouseEvent *e )
{
  if ( e->buttons() )
    return;  // only with no buttons pressed

  QgsPointLocator::Match m = snapToEditableLayer( e );

  updateFeatureBand( m );
  updateVertexBand( m );

  if ( !m.hasVertex() )
  {
    QList<Vertex> lst;
    if ( mRangeSelectionFirstVertex )
      lst << *mRangeSelectionFirstVertex;
    setHighlightedVertices( lst );
    return;
  }

  if ( mRangeSelectionFirstVertex )
  {
    // pick temporary final vertex and highlight vertices
    if ( m.layer() == mRangeSelectionFirstVertex->layer && m.featureId() == mRangeSelectionFirstVertex->fid )
    {
      QList<Vertex> lst = verticesInRange( m.layer(), m.featureId(), mRangeSelectionFirstVertex->vertexId, m.vertexIndex(), e->modifiers() & Qt::ControlModifier );
      setHighlightedVertices( lst );
    }
  }
}


void QgsVertexTool::startRangeVertexSelection()
{
  mSelectionMethod = SelectionRange;
  setHighlightedVertices( QList<Vertex>() );
  mRangeSelectionFirstVertex.reset();
}

void QgsVertexTool::stopRangeVertexSelection()
{
  mSelectionMethod = SelectionNormal;
  setHighlightedVertices( QList<Vertex>() );
}