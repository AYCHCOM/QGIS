/***************************************************************************
                         qgsalgorithmpointonsurface.cpp
                         ------------------------
    begin                : March 2018
    copyright            : (C) 2018 by Nyall Dawson
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

#include "qgsalgorithmpointonsurface.h"
#include "qgsgeometrycollection.h"

///@cond PRIVATE

QString QgsPointOnSurfaceAlgorithm::name() const
{
  return QStringLiteral( "pointonsurface" );
}

QString QgsPointOnSurfaceAlgorithm::displayName() const
{
  return QObject::tr( "Point on surface" );
}

QStringList QgsPointOnSurfaceAlgorithm::tags() const
{
  return QObject::tr( "centroid,inside,within" ).split( ',' );
}

QString QgsPointOnSurfaceAlgorithm::group() const
{
  return QObject::tr( "Vector geometry" );
}

QString QgsPointOnSurfaceAlgorithm::groupId() const
{
  return QStringLiteral( "vectorgeometry" );
}

QString QgsPointOnSurfaceAlgorithm::outputName() const
{
  return QObject::tr( "Point" );
}

QString QgsPointOnSurfaceAlgorithm::shortHelpString() const
{
  return QObject::tr( "Returns a point guaranteed to lie on the surface of a geometry." );
}

QgsPointOnSurfaceAlgorithm *QgsPointOnSurfaceAlgorithm::createInstance() const
{
  return new QgsPointOnSurfaceAlgorithm();
}

void QgsPointOnSurfaceAlgorithm::initParameters( const QVariantMap & )
{
  std::unique_ptr< QgsProcessingParameterBoolean> allParts = qgis::make_unique< QgsProcessingParameterBoolean >(
        QStringLiteral( "ALL_PARTS" ),
        QObject::tr( "Create point on surface for each part" ),
        false );
  allParts->setIsDynamic( true );
  allParts->setDynamicPropertyDefinition( QgsPropertyDefinition( QStringLiteral( "All parts" ), QObject::tr( "Create point on surface for each part" ), QgsPropertyDefinition::Boolean ) );
  allParts->setDynamicLayerParameterName( QStringLiteral( "INPUT" ) );
  addParameter( allParts.release() );
}

bool QgsPointOnSurfaceAlgorithm::prepareAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback * )
{
  mAllParts = parameterAsBool( parameters, QStringLiteral( "ALL_PARTS" ), context );
  mDynamicAllParts = QgsProcessingParameters::isDynamic( parameters, QStringLiteral( "ALL_PARTS" ) );
  if ( mDynamicAllParts )
    mAllPartsProperty = parameters.value( QStringLiteral( "ALL_PARTS" ) ).value< QgsProperty >();

  return true;
}

QgsFeatureList QgsPointOnSurfaceAlgorithm::processFeature( const QgsFeature &f, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
  QgsFeatureList list;
  QgsFeature feature = f;
  if ( feature.hasGeometry() )
  {
    QgsGeometry geom = feature.geometry();

    bool allParts = mAllParts;
    if ( mDynamicAllParts )
      allParts = mAllPartsProperty.valueAsBool( context.expressionContext(), allParts );

    if ( allParts && geom.isMultipart() )
    {
      const QgsGeometryCollection *geomCollection = static_cast<const QgsGeometryCollection *>( geom.constGet() );

      for ( int i = 0; i < geomCollection->partCount(); ++i )
      {
        QgsGeometry partGeometry( geomCollection->geometryN( i )->clone() );
        QgsGeometry outputGeometry = partGeometry.pointOnSurface();
        if ( !outputGeometry )
        {
          feedback->pushInfo( QObject::tr( "Error calculating point on surface for feature %1 part %2: %3" ).arg( feature.id() ).arg( i ).arg( outputGeometry.lastError() ) );
        }
        feature.setGeometry( outputGeometry );
        list << feature;
      }
    }
    else
    {
      QgsGeometry outputGeometry = feature.geometry().pointOnSurface();
      if ( !outputGeometry )
      {
        feedback->pushInfo( QObject::tr( "Error calculating point on surface for feature %1: %2" ).arg( feature.id() ).arg( outputGeometry.lastError() ) );
      }
      feature.setGeometry( outputGeometry );
      list << feature;
    }
  }
  else
  {
    list << feature;
  }
  return list;
}

///@endcond