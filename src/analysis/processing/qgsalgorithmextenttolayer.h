/***************************************************************************
                         qgsalgorithmextenttolayer.h
                         ---------------------
    begin                : April 2017
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

#ifndef QGSALGORITHMEXTENTTOLAYER_H
#define QGSALGORITHMEXTENTTOLAYER_H

#define SIP_NO_FILE

#include "qgis.h"
#include "qgsprocessingalgorithm.h"

///@cond PRIVATE

/**
 * Native extent to layer algorithm.
 */
class QgsExtentToLayerAlgorithm : public QgsProcessingAlgorithm
{

  public:

    QgsExtentToLayerAlgorithm() = default;
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QString name() const override;
    QString displayName() const override { return QObject::tr( "Create layer from extent" ); }
    virtual QStringList tags() const override { return QObject::tr( "extent,layer,polygon,create,new" ).split( ',' ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString shortHelpString() const override;
    QgsExtentToLayerAlgorithm *createInstance() const override SIP_FACTORY;

  protected:

    virtual QVariantMap processAlgorithm( const QVariantMap &parameters,
                                          QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;

};

///@endcond PRIVATE

#endif // QGSALGORITHMEXTENTTOLAYER_H

