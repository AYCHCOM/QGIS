/***************************************************************************
                         qgsprocessingmodeloutput.h
                         --------------------------
    begin                : June 2017
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

#ifndef QGSPROCESSINGMODELOUTPUT_H
#define QGSPROCESSINGMODELOUTPUT_H

#include "qgis_core.h"
#include "qgis.h"
#include "qgsprocessingmodelcomponent.h"

///@cond NOT_STABLE

/**
 * Represents a final output created by the model.
 * \since QGIS 3.0
 * \ingroup core
 */
class CORE_EXPORT QgsProcessingModelOutput : public QgsProcessingModelComponent
{
  public:

    /**
     * Constructor for QgsProcessingModelOutput with the specified \a name and \a description.
     */
    QgsProcessingModelOutput( const QString &name = QString(), const QString &description = QString() );

    /**
     * Returns the model output name.
     * \see setName()
     */
    QString name() const { return mName; }

    /**
     * Sets the model output \a name.
     * \see name()
     */
    void setName( const QString &name ) { mName = name; }

    /**
     * Returns the child algorithm ID from which this output is generated.
     * \see setChildId()
     */
    QString childId() const { return mChildId; }

    /**
     * Sets the child algorithm \a id from which this output is generated.
     * \see childId()
     */
    void setChildId( const QString &id ) { mChildId = id; }

    /**
     * Returns the child algorithm output name from which this output is generated.
     * \see setOutputName()
     */
    QString childOutputName() const { return mOutputName; }

    /**
     * Sets the child algorithm output \a name from which this output is generated.
     * \see outputName()
     */
    void setChildOutputName( const QString &name ) { mOutputName = name; }

    /**
     * Saves this output to a QVariant.
     * \see loadVariant()
     */
    QVariant toVariant() const;

    /**
     * Loads this output from a QVariantMap.
     * \see toVariant()
     */
    bool loadVariant( const QVariantMap &map );

  private:

    QString mName;
    QString mChildId;
    QString mOutputName;
};

///@endcond

#endif // QGSPROCESSINGMODELOUTPUT_H