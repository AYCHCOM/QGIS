/***************************************************************************
  qgsalgorithmintersection.cpp
  ---------------------
  Date                 : April 2018
  Copyright            : (C) 2018 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsalgorithmintersection.h"

#include "qgsgeometrycollection.h"
#include "qgsgeometryengine.h"
#include "qgsoverlayutils.h"

///@cond PRIVATE


QString QgsIntersectionAlgorithm::name() const
{
  return QStringLiteral( "intersection" );
}

QString QgsIntersectionAlgorithm::displayName() const
{
  return QObject::tr( "Intersection" );
}

QString QgsIntersectionAlgorithm::group() const
{
  return QObject::tr( "Vector overlay" );
}

QString QgsIntersectionAlgorithm::groupId() const
{
  return QStringLiteral( "vectoroverlay" );
}

QString QgsIntersectionAlgorithm::shortHelpString() const
{
  return QObject::tr( "This algorithm extracts the overlapping portions of features in the Input and Overlay layers. Features in the Overlay layer are assigned the attributes of the overlapping features from both the Input and Overlay layers." )
         + QStringLiteral( "\n\n" )
         + QObject::tr( "Optionally, the rotation can occur around a preset point. If not set the rotation occurs around each feature's centroid." );
}

QgsProcessingAlgorithm *QgsIntersectionAlgorithm::createInstance() const
{
  return new QgsIntersectionAlgorithm();
}

void QgsIntersectionAlgorithm::initAlgorithm( const QVariantMap & )
{
  addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
  addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "OVERLAY" ), QObject::tr( "Intersection layer" ) ) );

  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "INPUT_FIELDS" ),
                  QObject::tr( "Input fields to keep (leave empty to keep all fields)" ), QVariant(),
                  QStringLiteral( "INPUT" ), QgsProcessingParameterField::Any, true, true ) );
  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "OVERLAY_FIELDS" ),
                  QObject::tr( "Intersect fields to keep (leave empty to keep all fields)" ), QVariant(),
                  QStringLiteral( "OVERLAY" ), QgsProcessingParameterField::Any, true, true ) );

  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Intersection" ) ) );

}


QVariantMap QgsIntersectionAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
  std::unique_ptr< QgsFeatureSource > sourceA( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
  if ( !sourceA )
    throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

  std::unique_ptr< QgsFeatureSource > sourceB( parameterAsSource( parameters, QStringLiteral( "OVERLAY" ), context ) );
  if ( !sourceB )
    throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "OVERLAY" ) ) );

  QgsWkbTypes::Type geomType = QgsWkbTypes::multiType( sourceA->wkbType() );

  const QStringList fieldsA = parameterAsFields( parameters, QStringLiteral( "INPUT_FIELDS" ), context );
  const QStringList fieldsB = parameterAsFields( parameters, QStringLiteral( "OVERLAY_FIELDS" ), context );

  QList<int> fieldIndicesA = QgsProcessingUtils::fieldNamesToIndices( fieldsA, sourceA->fields() );
  QList<int> fieldIndicesB = QgsProcessingUtils::fieldNamesToIndices( fieldsB, sourceB->fields() );

  QgsFields outputFields = QgsProcessingUtils::combineFields(
                             QgsProcessingUtils::indicesToFields( fieldIndicesA, sourceA->fields() ),
                             QgsProcessingUtils::indicesToFields( fieldIndicesB, sourceB->fields() ) );

  QString dest;
  std::unique_ptr< QgsFeatureSink > sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest, outputFields, geomType, sourceA->sourceCrs() ) );
  if ( !sink )
    throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

  QVariantMap outputs;
  outputs.insert( QStringLiteral( "OUTPUT" ), dest );

  int count = 0;
  int total = sourceA->featureCount();

  QgsOverlayUtils::intersection( *sourceA.get(), *sourceB.get(), *sink.get(), context, feedback, count, total, fieldIndicesA, fieldIndicesB );

  return outputs;
}

///@endcond