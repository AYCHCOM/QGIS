/***************************************************************************
                         qgslayoutattributetablewidget.h
                         ---------------------------------
    begin                : November 2017
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

#ifndef QGSLAYOUTATTRIBUTETABLEWIDGET_H
#define QGSLAYOUTATTRIBUTETABLEWIDGET_H

#include "ui_qgslayoutattributetablewidgetbase.h"
#include "qgslayoutitemwidget.h"

class QgsLayoutItemAttributeTable;
class QgsLayoutFrame;

class QgsLayoutAttributeTableWidget: public QgsLayoutItemBaseWidget, private Ui::QgsLayoutAttributeTableWidgetBase
{
    Q_OBJECT
  public:
    QgsLayoutAttributeTableWidget( QgsLayoutFrame *frame );

  protected:

    bool setNewItem( QgsLayoutItem *item ) override;

  private:
    QgsLayoutItemAttributeTable *mTable = nullptr;
    QgsLayoutFrame *mFrame = nullptr;
    QgsLayoutItemPropertiesWidget *mItemPropertiesWidget = nullptr;

    //! Blocks / unblocks the signals of all GUI elements
    void blockAllSignals( bool b );

    void toggleSourceControls();

    void toggleAtlasSpecificControls( const bool atlasEnabled );

  private slots:
    void mRefreshPushButton_clicked();
    void mAttributesPushButton_clicked();
    void composerMapChanged( QgsLayoutItem *item );
    void mMaximumRowsSpinBox_valueChanged( int i );
    void mMarginSpinBox_valueChanged( double d );
    void mGridStrokeWidthSpinBox_valueChanged( double d );
    void mGridColorButton_colorChanged( const QColor &newColor );
    void mBackgroundColorButton_colorChanged( const QColor &newColor );
    void headerFontChanged();
    void mHeaderFontColorButton_colorChanged( const QColor &newColor );
    void contentFontChanged();
    void mContentFontColorButton_colorChanged( const QColor &newColor );
    void mDrawHorizontalGrid_toggled( bool state );
    void mDrawVerticalGrid_toggled( bool state );
    void mShowGridGroupCheckBox_toggled( bool state );
    void mShowOnlyVisibleFeaturesCheckBox_stateChanged( int state );
    void mFeatureFilterCheckBox_stateChanged( int state );
    void mFeatureFilterEdit_editingFinished();
    void mFeatureFilterButton_clicked();
    void mHeaderHAlignmentComboBox_currentIndexChanged( int index );
    void mHeaderModeComboBox_currentIndexChanged( int index );
    void mWrapStringLineEdit_editingFinished();
    void changeLayer( QgsMapLayer *layer );
    void mAddFramePushButton_clicked();
    void mResizeModeComboBox_currentIndexChanged( int index );
    void mSourceComboBox_currentIndexChanged( int index );
    void mRelationsComboBox_currentIndexChanged( int index );
    void mEmptyModeComboBox_currentIndexChanged( int index );
    void mDrawEmptyCheckBox_toggled( bool checked );
    void mEmptyMessageLineEdit_editingFinished();
    void mIntersectAtlasCheckBox_stateChanged( int state );
    void mUniqueOnlyCheckBox_stateChanged( int state );
    void mEmptyFrameCheckBox_toggled( bool checked );
    void mHideEmptyBgCheckBox_toggled( bool checked );
    void mWrapBehaviorComboBox_currentIndexChanged( int index );
    void mAdvancedCustomisationButton_clicked();

    //! Inserts a new maximum number of features into the spin box (without the spinbox emitting a signal)
    void setMaximumNumberOfFeatures( int n );

    //! Sets the GUI elements to the values of mComposerTable
    void updateGuiElements();

    void atlasToggled();

    void updateRelationsCombo();

};

#endif // QGSLAYOUTATTRIBUTETABLEWIDGET_H