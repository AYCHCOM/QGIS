# -*- coding: utf-8 -*-
"""QGIS Unit tests for QgsTextFormatWidget.

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""
__author__ = 'Nyall Dawson'
__date__ = '2016-09'
__copyright__ = 'Copyright 2016, The QGIS Project'
# This will get replaced with a git SHA1 when you do a git archive
__revision__ = '$Format:%H$'

import qgis  # NOQA
import os

from qgis.core import (QgsTextBufferSettings,
                       QgsTextBackgroundSettings,
                       QgsTextShadowSettings,
                       QgsTextFormat,
                       QgsUnitTypes,
                       QgsMapUnitScale)
from qgis.gui import (QgsTextFormatWidget, QgsTextFormatDialog)
from qgis.PyQt.QtGui import (QColor, QPainter, QFont, QImage, QBrush, QPen)
from qgis.PyQt.QtCore import (Qt, QSizeF, QPointF, QRectF, QDir)
from qgis.testing import unittest, start_app
from utilities import getTestFont, svgSymbolsPath

start_app()


class PyQgsTextFormatWidget(unittest.TestCase):

    def createBufferSettings(self):
        s = QgsTextBufferSettings()
        s.setEnabled(True)
        s.setSize(5)
        s.setSizeUnit(QgsUnitTypes.RenderPixels)
        s.setSizeMapUnitScale(QgsMapUnitScale(1, 2))
        s.setColor(QColor(255, 0, 0))
        s.setFillBufferInterior(True)
        s.setOpacity(0.5)
        s.setJoinStyle(Qt.RoundJoin)
        s.setBlendMode(QPainter.CompositionMode_Difference)
        return s

    def checkBufferSettings(self, s):
        """ test QgsTextBufferSettings """
        self.assertTrue(s.enabled())
        self.assertEqual(s.size(), 5)
        self.assertEqual(s.sizeUnit(), QgsUnitTypes.RenderPixels)
        self.assertEqual(s.sizeMapUnitScale(), QgsMapUnitScale(1, 2))
        self.assertEqual(s.color(), QColor(255, 0, 0))
        self.assertTrue(s.fillBufferInterior())
        self.assertEqual(s.opacity(), 0.5)
        self.assertEqual(s.joinStyle(), Qt.RoundJoin)
        self.assertEqual(s.blendMode(), QPainter.CompositionMode_Difference)

    def createBackgroundSettings(self):
        s = QgsTextBackgroundSettings()
        s.setEnabled(True)
        s.setType(QgsTextBackgroundSettings.ShapeEllipse)
        s.setSvgFile('svg.svg')
        s.setSizeType(QgsTextBackgroundSettings.SizeFixed)
        s.setSize(QSizeF(1, 2))
        s.setSizeUnit(QgsUnitTypes.RenderPixels)
        s.setSizeMapUnitScale(QgsMapUnitScale(1, 2))
        s.setRotationType(QgsTextBackgroundSettings.RotationFixed)
        s.setRotation(45)
        s.setOffset(QPointF(3, 4))
        s.setOffsetUnit(QgsUnitTypes.RenderMapUnits)
        s.setOffsetMapUnitScale(QgsMapUnitScale(5, 6))
        s.setRadii(QSizeF(11, 12))
        s.setRadiiUnit(QgsUnitTypes.RenderPixels)
        s.setRadiiMapUnitScale(QgsMapUnitScale(15, 16))
        s.setFillColor(QColor(255, 0, 0))
        s.setBorderColor(QColor(0, 255, 0))
        s.setOpacity(0.5)
        s.setJoinStyle(Qt.RoundJoin)
        s.setBlendMode(QPainter.CompositionMode_Difference)
        s.setBorderWidth(7)
        s.setBorderWidthUnit(QgsUnitTypes.RenderMapUnits)
        s.setBorderWidthMapUnitScale(QgsMapUnitScale(QgsMapUnitScale(25, 26)))
        return s

    def checkBackgroundSettings(self, s):
        """ test QgsTextBackgroundSettings """
        self.assertTrue(s.enabled())
        self.assertEqual(s.type(), QgsTextBackgroundSettings.ShapeEllipse)
        self.assertEqual(s.svgFile(), 'svg.svg')
        self.assertEqual(s.sizeType(), QgsTextBackgroundSettings.SizeFixed)
        self.assertEqual(s.size(), QSizeF(1, 2))
        self.assertEqual(s.sizeUnit(), QgsUnitTypes.RenderPixels)
        self.assertEqual(s.sizeMapUnitScale(), QgsMapUnitScale(1, 2))
        self.assertEqual(s.rotationType(), QgsTextBackgroundSettings.RotationFixed)
        self.assertEqual(s.rotation(), 45)
        self.assertEqual(s.offset(), QPointF(3, 4))
        self.assertEqual(s.offsetUnit(), QgsUnitTypes.RenderMapUnits)
        self.assertEqual(s.offsetMapUnitScale(), QgsMapUnitScale(5, 6))
        self.assertEqual(s.radii(), QSizeF(11, 12))
        self.assertEqual(s.radiiUnit(), QgsUnitTypes.RenderPixels)
        self.assertEqual(s.radiiMapUnitScale(), QgsMapUnitScale(15, 16))
        self.assertEqual(s.fillColor(), QColor(255, 0, 0))
        self.assertEqual(s.borderColor(), QColor(0, 255, 0))
        self.assertEqual(s.opacity(), 0.5)
        self.assertEqual(s.joinStyle(), Qt.RoundJoin)
        self.assertEqual(s.blendMode(), QPainter.CompositionMode_Difference)
        self.assertEqual(s.borderWidth(), 7)
        self.assertEqual(s.borderWidthUnit(), QgsUnitTypes.RenderMapUnits)
        self.assertEqual(s.borderWidthMapUnitScale(), QgsMapUnitScale(25, 26))

    def createShadowSettings(self):
        s = QgsTextShadowSettings()
        s.setEnabled(True)
        s.setShadowPlacement(QgsTextShadowSettings.ShadowBuffer)
        s.setOffsetAngle(45)
        s.setOffsetDistance(75)
        s.setOffsetUnit(QgsUnitTypes.RenderMapUnits)
        s.setOffsetMapUnitScale(QgsMapUnitScale(5, 6))
        s.setOffsetGlobal(True)
        s.setBlurRadius(11)
        s.setBlurRadiusUnit(QgsUnitTypes.RenderMapUnits)
        s.setBlurRadiusMapUnitScale(QgsMapUnitScale(15, 16))
        s.setBlurAlphaOnly(True)
        s.setColor(QColor(255, 0, 0))
        s.setOpacity(0.5)
        s.setScale(123)
        s.setBlendMode(QPainter.CompositionMode_Difference)
        return s

    def checkShadowSettings(self, s):
        """ test QgsTextShadowSettings """
        self.assertTrue(s.enabled())
        self.assertEqual(s.shadowPlacement(), QgsTextShadowSettings.ShadowBuffer)
        self.assertEqual(s.offsetAngle(), 45)
        self.assertEqual(s.offsetDistance(), 75)
        self.assertEqual(s.offsetUnit(), QgsUnitTypes.RenderMapUnits)
        self.assertEqual(s.offsetMapUnitScale(), QgsMapUnitScale(5, 6))
        self.assertTrue(s.offsetGlobal())
        self.assertEqual(s.blurRadius(), 11)
        self.assertEqual(s.blurRadiusUnit(), QgsUnitTypes.RenderMapUnits)
        self.assertEqual(s.blurRadiusMapUnitScale(), QgsMapUnitScale(15, 16))
        self.assertTrue(s.blurAlphaOnly())
        self.assertEqual(s.color(), QColor(255, 0, 0))
        self.assertEqual(s.opacity(), 0.5)
        self.assertEqual(s.scale(), 123)
        self.assertEqual(s.blendMode(), QPainter.CompositionMode_Difference)

    def createFormatSettings(self):
        s = QgsTextFormat()
        s.setBuffer(self.createBufferSettings())
        s.setBackground(self.createBackgroundSettings())
        s.setShadow(self.createShadowSettings())
        s.setFont(getTestFont())
        s.setNamedStyle('Roman')
        s.setSize(5)
        s.setSizeUnit(QgsUnitTypes.RenderPoints)
        s.setSizeMapUnitScale(QgsMapUnitScale(1, 2))
        s.setColor(QColor(255, 0, 0))
        s.setOpacity(0.5)
        s.setBlendMode(QPainter.CompositionMode_Difference)
        s.setLineHeight(5)
        return s

    def checkTextFormat(self, s):
        """ test QgsTextFormat """
        self.checkBufferSettings(s.buffer())
        self.checkShadowSettings(s.shadow())
        self.checkBackgroundSettings(s.background())
        self.assertEqual(s.font().family(), 'QGIS Vera Sans')
        self.assertEqual(s.namedStyle(), 'Roman')
        self.assertEqual(s.size(), 5)
        self.assertEqual(s.sizeUnit(), QgsUnitTypes.RenderPoints)
        self.assertEqual(s.sizeMapUnitScale(), QgsMapUnitScale(1, 2))
        self.assertEqual(s.color(), QColor(255, 0, 0))
        self.assertEqual(s.opacity(), 0.5)
        self.assertEqual(s.blendMode(), QPainter.CompositionMode_Difference)
        self.assertEqual(s.lineHeight(), 5)

    def testSettings(self):
        # test that widget correctly sets and returns matching settings
        s = self.createFormatSettings()
        w = QgsTextFormatWidget(s)
        self.checkTextFormat(w.format())

    def testDialogSettings(self):
        # test that dialog correctly sets and returns matching settings
        s = self.createFormatSettings()
        d = QgsTextFormatDialog(s)
        self.checkTextFormat(d.format())

if __name__ == '__main__':
    unittest.main()