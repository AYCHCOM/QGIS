/***************************************************************************
                             qgsmenuheader.h
                             ---------------
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
#ifndef QGSMENUHEADER_H
#define QGSMENUHEADER_H

#include <QWidget>
#include <QWidgetAction>
#include "qgis_gui.h"
#include "qgis.h"

/** \ingroup gui
 * \class QgsMenuHeader
 * Custom widget for displaying subheaders within a QMenu in a standard style.
 * \since QGIS 3.0
 * \see QgsMenuHeaderWidgetAction()
 */
class GUI_EXPORT QgsMenuHeader : public QWidget
{
    Q_OBJECT

  public:

    /**
     * Constructor for QgsMenuHeader, showing the specified \a text.
     */
    explicit QgsMenuHeader( const QString &text, QWidget *parent SIP_TRANSFERTHIS = nullptr );

    virtual QSize minimumSizeHint() const override;
    virtual QSize sizeHint() const override;

  protected:

    void paintEvent( QPaintEvent *event ) override;

  private:
    int mMinWidth = 0;
    QString mText;

};

/** \ingroup gui
 * \class QgsMenuHeaderWidgetAction
 * Custom QWidgetAction for displaying subheaders within a QMenu in a standard style.
 * \since QGIS 3.0
 * \see QgsMenuHeader()
 */
class GUI_EXPORT QgsMenuHeaderWidgetAction: public QWidgetAction
{
    Q_OBJECT

  public:

    /**
     * Constructor for QgsMenuHeaderWidgetAction, showing the specified \a text.
     */
    QgsMenuHeaderWidgetAction( const QString &text, QObject *parent = nullptr );

};

#endif //QGSMENUHEADER_H