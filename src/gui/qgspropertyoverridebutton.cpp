/***************************************************************************
     qgspropertyoverridebutton.cpp
     -----------------------------
    Date                 : January 2017
    Copyright            : (C) 2017 by Nyall Dawson
    Email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgspropertyoverridebutton.h"

#include "qgsapplication.h"
#include "qgsexpressionbuilderdialog.h"
#include "qgsexpression.h"
#include "qgsmessageviewer.h"
#include "qgsvectorlayer.h"
#include "qgspanelwidget.h"
#include "qgspropertyassistantwidget.h"

#include <QClipboard>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QGroupBox>

QgsPropertyOverrideButton::QgsPropertyOverrideButton( QWidget *parent,
    const QgsVectorLayer *layer )
  : QToolButton( parent )
  , mVectorLayer( layer )
  , mExpressionContextGenerator( nullptr )
{
  setFocusPolicy( Qt::StrongFocus );

  // set default tool button icon properties
  setFixedSize( 30, 26 );
  setStyleSheet( QString( "QToolButton{ background: none; border: 1px solid rgba(0, 0, 0, 0%);} QToolButton:focus { border: 1px solid palette(highlight); }" ) );
  setIconSize( QSize( 24, 24 ) );
  setPopupMode( QToolButton::InstantPopup );

  connect( this, &QgsPropertyOverrideButton::activated, this, &QgsPropertyOverrideButton::checkCheckedWidgets );

  mDefineMenu = new QMenu( this );
  connect( mDefineMenu, &QMenu::aboutToShow, this, &QgsPropertyOverrideButton::aboutToShowMenu );
  connect( mDefineMenu, &QMenu::triggered, this, &QgsPropertyOverrideButton::menuActionTriggered );
  setMenu( mDefineMenu );

  mFieldsMenu = new QMenu( this );
  mActionDataTypes = new QAction( this );
  // list fields and types in submenu, since there may be many
  mActionDataTypes->setMenu( mFieldsMenu );

  mActionVariables = new QAction( tr( "Variable" ), this );
  mVariablesMenu = new QMenu( this );
  mActionVariables->setMenu( mVariablesMenu );

  mActionActive = new QAction( this );
  QFont f = mActionActive->font();
  f.setBold( true );
  mActionActive->setFont( f );

  mActionDescription = new QAction( tr( "Description..." ), this );

  mActionExpDialog = new QAction( tr( "Edit..." ), this );
  mActionExpression = nullptr;
  mActionPasteExpr = new QAction( tr( "Paste" ), this );
  mActionCopyExpr = new QAction( tr( "Copy" ), this );
  mActionClearExpr = new QAction( tr( "Clear" ), this );
  mActionAssistant = new QAction( tr( "Assistant..." ), this );
  QFont assistantFont = mActionAssistant->font();
  assistantFont.setBold( true );
  mActionAssistant->setFont( assistantFont );
  mDefineMenu->addAction( mActionAssistant );
}

void QgsPropertyOverrideButton::init( int propertyKey, const QgsProperty &property, const QgsPropertiesDefinition &definitions, const QgsVectorLayer *layer )
{
  mVectorLayer = layer;
  setToProperty( property );
  mPropertyKey = propertyKey;

  mDefinition = definitions.value( propertyKey );
  mDataTypes = mDefinition.dataType();

  mInputDescription = mDefinition.helpText();
  mFullDescription.clear();
  mUsageInfo.clear();

  // set up data types string
  mDataTypesString.clear();

  QStringList ts;
  switch ( mDataTypes )
  {
    case QgsPropertyDefinition::DataTypeBoolean:
      ts << tr( "boolean" );
      FALLTHROUGH;

    case QgsPropertyDefinition::DataTypeNumeric:
      ts << tr( "int" );
      ts << tr( "double" );
      FALLTHROUGH;

    case QgsPropertyDefinition::DataTypeString:
      ts << tr( "string" );
      break;
  }

  if ( !ts.isEmpty() )
  {
    mDataTypesString = ts.join( ", " );
    mActionDataTypes->setText( tr( "Field type: " ) + mDataTypesString );
  }

  updateFieldLists();
  updateGui();
}

void QgsPropertyOverrideButton::init( int propertyKey, const QgsAbstractPropertyCollection &collection, const QgsPropertiesDefinition &definitions, const QgsVectorLayer *layer )
{
  init( propertyKey, collection.property( propertyKey ), definitions, layer );
}


void QgsPropertyOverrideButton::updateFieldLists()
{
  mFieldNameList.clear();
  mFieldTypeList.clear();

  if ( mVectorLayer )
  {
    // store just a list of fields of unknown type or those that match the expected type
    Q_FOREACH ( const QgsField &f, mVectorLayer->fields() )
    {
      bool fieldMatch = false;
      QString fieldType;

      switch ( mDataTypes )
      {
        case QgsPropertyDefinition::DataTypeBoolean:
          fieldMatch = true;
          break;

        case QgsPropertyDefinition::DataTypeNumeric:
          fieldMatch = f.isNumeric() || f.type() == QVariant::String;
          break;

        case QgsPropertyDefinition::DataTypeString:
          fieldMatch = f.type() == QVariant::String;
          break;
      }

      switch ( f.type() )
      {
        case QVariant::String:
          fieldType = tr( "string" );
          break;
        case QVariant::Int:
          fieldType = tr( "integer" );
          break;
        case QVariant::Double:
          fieldType = tr( "double" );
          break;
        case QVariant::Bool:
          fieldType = tr( "boolean" );
          break;
        default:
          fieldType = tr( "unknown type" );
      }
      if ( fieldMatch )
      {
        mFieldNameList << f.name();
        mFieldTypeList << fieldType;
      }
    }
  }
}

QgsProperty QgsPropertyOverrideButton::toProperty() const
{
  return mProperty;
}

void QgsPropertyOverrideButton::setVectorLayer( const QgsVectorLayer *layer )
{
  mVectorLayer = layer;
}

void QgsPropertyOverrideButton::registerCheckedWidget( QWidget *widget )
{
  Q_FOREACH ( const QPointer<QWidget> &w, mCheckedWidgets )
  {
    if ( widget == w.data() )
      return;
  }
  mCheckedWidgets.append( QPointer<QWidget>( widget ) );
}

void QgsPropertyOverrideButton::mouseReleaseEvent( QMouseEvent *event )
{
  // Ctrl-click to toggle activated state
  if ( ( event->modifiers() & ( Qt::ControlModifier ) )
       || event->button() == Qt::RightButton )
  {
    setActivePrivate( !mProperty.isActive() );
    updateGui();
    emit changed();
    event->ignore();
    return;
  }

  // pass to default behavior
  QToolButton::mousePressEvent( event );
}

void QgsPropertyOverrideButton::setToProperty( const QgsProperty &property )
{
  if ( property )
  {
    switch ( property.propertyType() )
    {
      case QgsProperty::StaticProperty:
      case QgsProperty::InvalidProperty:
        break;
      case QgsProperty::FieldBasedProperty:
      {
        mFieldName = property.field();
        break;
      }
      case QgsProperty::ExpressionBasedProperty:
      {
        mExpressionString = property.expressionString();
        break;
      }
    }
  }
  else
  {
    mFieldName.clear();
    mExpressionString.clear();
  }
  mProperty = property;
  setActive( mProperty && mProperty.isActive() );
  updateGui();
}

void QgsPropertyOverrideButton::aboutToShowMenu()
{
  mDefineMenu->clear();
  // update fields so that changes made to layer's fields are reflected
  updateFieldLists();

  bool hasExp = !mExpressionString.isEmpty();
  QString ddTitle = tr( "Data defined override" );

  QAction *ddTitleAct = mDefineMenu->addAction( ddTitle );
  QFont titlefont = ddTitleAct->font();
  titlefont.setItalic( true );
  ddTitleAct->setFont( titlefont );
  ddTitleAct->setEnabled( false );

  bool addActiveAction = false;
  if ( mProperty.propertyType() == QgsProperty::ExpressionBasedProperty && hasExp )
  {
    QgsExpression exp( mExpressionString );
    // whether expression is parse-able
    addActiveAction = !exp.hasParserError();
  }
  else if ( mProperty.propertyType() == QgsProperty::FieldBasedProperty )
  {
    // whether field exists
    addActiveAction = mFieldNameList.contains( mFieldName );
  }

  if ( addActiveAction )
  {
    ddTitleAct->setText( ddTitle + " (" + ( mProperty.propertyType() == QgsProperty::ExpressionBasedProperty ? tr( "expression" ) : tr( "field" ) ) + ')' );
    mDefineMenu->addAction( mActionActive );
    mActionActive->setText( mProperty.isActive() ? tr( "Deactivate" ) : tr( "Activate" ) );
    mActionActive->setData( QVariant( mProperty.isActive() ? false : true ) );
  }

  if ( !mFullDescription.isEmpty() )
  {
    mDefineMenu->addAction( mActionDescription );
  }

  mDefineMenu->addSeparator();

  bool fieldActive = false;
  if ( !mDataTypesString.isEmpty() )
  {
    QAction *fieldTitleAct = mDefineMenu->addAction( tr( "Attribute field" ) );
    fieldTitleAct->setFont( titlefont );
    fieldTitleAct->setEnabled( false );

    mDefineMenu->addAction( mActionDataTypes );

    mFieldsMenu->clear();

    if ( !mFieldNameList.isEmpty() )
    {

      for ( int j = 0; j < mFieldNameList.count(); ++j )
      {
        QString fldname = mFieldNameList.at( j );
        QAction *act = mFieldsMenu->addAction( fldname + "    (" + mFieldTypeList.at( j ) + ')' );
        act->setData( QVariant( fldname ) );
        if ( mFieldName == fldname )
        {
          act->setCheckable( true );
          act->setChecked( mProperty.propertyType() == QgsProperty::FieldBasedProperty );
          fieldActive = mProperty.propertyType() == QgsProperty::FieldBasedProperty;
        }
      }
    }
    else
    {
      QAction *act = mFieldsMenu->addAction( tr( "No matching field types found" ) );
      act->setEnabled( false );
    }

    mDefineMenu->addSeparator();
  }

  mFieldsMenu->menuAction()->setCheckable( true );
  mFieldsMenu->menuAction()->setChecked( fieldActive && mProperty.propertyType() == QgsProperty::FieldBasedProperty && !mProperty.transformer() );

  QAction *exprTitleAct = mDefineMenu->addAction( tr( "Expression" ) );
  exprTitleAct->setFont( titlefont );
  exprTitleAct->setEnabled( false );

  mVariablesMenu->clear();
  bool variableActive = false;
  if ( mExpressionContextGenerator )
  {
    QgsExpressionContext context = mExpressionContextGenerator->createExpressionContext();
    QStringList variables = context.variableNames();
    Q_FOREACH ( const QString &variable, variables )
    {
      if ( context.isReadOnly( variable ) ) //only want to show user-set variables
        continue;
      if ( variable.startsWith( '_' ) ) //no hidden variables
        continue;

      QAction *act = mVariablesMenu->addAction( variable );
      act->setData( QVariant( variable ) );

      if ( mProperty.propertyType() == QgsProperty::ExpressionBasedProperty && hasExp && mExpressionString == '@' + variable )
      {
        act->setCheckable( true );
        act->setChecked( true );
        variableActive = true;
      }
    }
  }

  if ( mVariablesMenu->actions().isEmpty() )
  {
    QAction *act = mVariablesMenu->addAction( tr( "No variables set" ) );
    act->setEnabled( false );
  }

  mDefineMenu->addAction( mActionVariables );
  mVariablesMenu->menuAction()->setCheckable( true );
  mVariablesMenu->menuAction()->setChecked( variableActive && !mProperty.transformer() );

  if ( hasExp )
  {
    QString expString = mExpressionString;
    if ( expString.length() > 35 )
    {
      expString.truncate( 35 );
      expString.append( "..." );
    }

    expString.prepend( tr( "Current: " ) );

    if ( !mActionExpression )
    {
      mActionExpression = new QAction( expString, this );
      mActionExpression->setCheckable( true );
    }
    else
    {
      mActionExpression->setText( expString );
    }
    mDefineMenu->addAction( mActionExpression );
    mActionExpression->setChecked( mProperty.propertyType() == QgsProperty::ExpressionBasedProperty && !variableActive && !mProperty.transformer() );

    mDefineMenu->addAction( mActionExpDialog );
    mDefineMenu->addAction( mActionCopyExpr );
    mDefineMenu->addAction( mActionPasteExpr );
    mDefineMenu->addAction( mActionClearExpr );
  }
  else
  {
    mDefineMenu->addAction( mActionExpDialog );
    mDefineMenu->addAction( mActionPasteExpr );
  }

  if ( !mDefinition.name().isEmpty() && mDefinition.supportsAssistant() )
  {
    mDefineMenu->addSeparator();
    mActionAssistant->setCheckable( mProperty.transformer() );
    mActionAssistant->setChecked( mProperty.transformer() );
    mDefineMenu->addAction( mActionAssistant );
  }
}

void QgsPropertyOverrideButton::menuActionTriggered( QAction *action )
{
  if ( action == mActionActive )
  {
    setActivePrivate( mActionActive->data().toBool() );
    updateGui();
    emit changed();
  }
  else if ( action == mActionDescription )
  {
    showDescriptionDialog();
  }
  else if ( action == mActionExpDialog )
  {
    showExpressionDialog();
  }
  else if ( action == mActionExpression )
  {
    mProperty.setExpressionString( mExpressionString );
    mProperty.setTransformer( nullptr );
    setActivePrivate( true );
    updateGui();
    emit changed();
  }
  else if ( action == mActionCopyExpr )
  {
    QApplication::clipboard()->setText( mExpressionString );
  }
  else if ( action == mActionPasteExpr )
  {
    QString exprString = QApplication::clipboard()->text();
    if ( !exprString.isEmpty() )
    {
      mExpressionString = exprString;
      mProperty.setExpressionString( mExpressionString );
      mProperty.setTransformer( nullptr );
      setActivePrivate( true );
      updateGui();
      emit changed();
    }
  }
  else if ( action == mActionClearExpr )
  {
    setActivePrivate( false );
    mProperty.setStaticValue( QVariant() );
    mProperty.setTransformer( nullptr );
    mExpressionString.clear();
    updateGui();
    emit changed();
  }
  else if ( action == mActionAssistant )
  {
    showAssistant();
  }
  else if ( mFieldsMenu->actions().contains( action ) )  // a field name clicked
  {
    if ( action->isEnabled() )
    {
      if ( mFieldName != action->text() )
      {
        mFieldName = action->data().toString();
      }
      mProperty.setField( mFieldName );
      mProperty.setTransformer( nullptr );
      setActivePrivate( true );
      updateGui();
      emit changed();
    }
  }
  else if ( mVariablesMenu->actions().contains( action ) )  // a variable name clicked
  {
    if ( mExpressionString != action->text().prepend( "@" ) )
    {
      mExpressionString = action->data().toString().prepend( "@" );
    }
    mProperty.setExpressionString( mExpressionString );
    mProperty.setTransformer( nullptr );
    setActivePrivate( true );
    updateGui();
    emit changed();
  }
}

void QgsPropertyOverrideButton::showDescriptionDialog()
{
  QgsMessageViewer *mv = new QgsMessageViewer( this );
  mv->setWindowTitle( tr( "Data definition description" ) );
  mv->setMessageAsHtml( mFullDescription );
  mv->exec();
}


void QgsPropertyOverrideButton::showExpressionDialog()
{
  QgsExpressionContext context = mExpressionContextGenerator ? mExpressionContextGenerator->createExpressionContext() : QgsExpressionContext();

  QgsExpressionBuilderDialog d( const_cast<QgsVectorLayer *>( mVectorLayer ), mProperty.asExpression(), this, QStringLiteral( "generic" ), context );
  if ( d.exec() == QDialog::Accepted )
  {
    mExpressionString = d.expressionText().trimmed();
    mProperty.setExpressionString( mExpressionString );
    mProperty.setTransformer( nullptr );
    setActivePrivate( !mExpressionString.isEmpty() );
    updateGui();
    emit changed();
  }
  activateWindow(); // reset focus to parent window
}

void QgsPropertyOverrideButton::showAssistant()
{
  //first step - try to convert any existing expression to a transformer if one doesn't
  //already exist
  if ( !mProperty.transformer() )
  {
    ( void )mProperty.convertToTransformer();
  }

  QgsPanelWidget *panel = QgsPanelWidget::findParentPanel( this );
  QgsPropertyAssistantWidget *widget = new QgsPropertyAssistantWidget( panel, mDefinition, mProperty, mVectorLayer );
  widget->registerExpressionContextGenerator( mExpressionContextGenerator );
  widget->setSymbol( mSymbol ); // we only show legend preview in dialog version

  if ( panel && panel->dockMode() )
  {
    connect( widget, &QgsPropertyAssistantWidget::widgetChanged, this, [this, widget]
    {
      widget->updateProperty( this->mProperty );
      mExpressionString = this->mProperty.asExpression();
      mFieldName = this->mProperty.field();
      this->emit changed();
    } );

    connect( widget, &QgsPropertyAssistantWidget::panelAccepted, this, [ = ] { updateGui(); } );

    panel->openPanel( widget );
    return;
  }
  else
  {
    // Show the dialog version if not in a panel
    QDialog *dlg = new QDialog( this );
    QString key =  QStringLiteral( "/UI/paneldialog/%1" ).arg( widget->panelTitle() );
    QgsSettings settings;
    dlg->restoreGeometry( settings.value( key ).toByteArray() );
    dlg->setWindowTitle( widget->panelTitle() );
    dlg->setLayout( new QVBoxLayout() );
    dlg->layout()->addWidget( widget );
    QDialogButtonBox *buttonBox = new QDialogButtonBox( QDialogButtonBox::Ok );
    connect( buttonBox, SIGNAL( accepted() ), dlg, SLOT( accept() ) );
    dlg->layout()->addWidget( buttonBox );
    dlg->exec();
    settings.setValue( key, dlg->saveGeometry() );

    widget->updateProperty( mProperty );
    mExpressionString = mProperty.asExpression();
    mFieldName = mProperty.field();
    widget->acceptPanel();
    updateGui();

    emit changed();
  }
}

void QgsPropertyOverrideButton::updateGui()
{
  bool hasExp = !mExpressionString.isEmpty();
  bool hasField = !mFieldName.isEmpty();

  QIcon icon = QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefine.svg" ) );
  QString deftip = tr( "undefined" );
  if ( mProperty.propertyType() == QgsProperty::ExpressionBasedProperty && hasExp )
  {
    icon = mProperty.isActive() ? QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefineExpressionOn.svg" ) ) : QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefineExpression.svg" ) );

    QgsExpression exp( mExpressionString );
    if ( exp.hasParserError() )
    {
      icon = QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefineExpressionError.svg" ) );
      deftip = tr( "Parse error: %1" ).arg( exp.parserErrorString() );
    }
  }
  else if ( mProperty.propertyType() != QgsProperty::ExpressionBasedProperty && hasField )
  {
    icon = mProperty.isActive() ? QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefineOn.svg" ) ) : QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefine.svg" ) );

    if ( !mFieldNameList.contains( mFieldName ) && !mProperty.transformer() )
    {
      icon = QgsApplication::getThemeIcon( QStringLiteral( "/mIconDataDefineError.svg" ) );
      deftip = tr( "'%1' field missing" ).arg( mFieldName );
    }
  }

  setIcon( icon );

  // build full description for tool tip and popup dialog
  mFullDescription = tr( "<b><u>Data defined override</u></b><br>" );

  mFullDescription += tr( "<b>Active: </b>%1&nbsp;&nbsp;&nbsp;<i>(ctrl|right-click toggles)</i><br>" ).arg( mProperty.isActive() ? tr( "yes" ) : tr( "no" ) );

  if ( !mUsageInfo.isEmpty() )
  {
    mFullDescription += tr( "<b>Usage:</b><br>%1<br>" ).arg( mUsageInfo );
  }

  if ( !mInputDescription.isEmpty() )
  {
    mFullDescription += tr( "<b>Expected input:</b><br>%1<br>" ).arg( mInputDescription );
  }

  if ( !mDataTypesString.isEmpty() )
  {
    mFullDescription += tr( "<b>Valid input types:</b><br>%1<br>" ).arg( mDataTypesString );
  }

  QString deftype( "" );
  if ( deftip != tr( "undefined" ) )
  {
    deftype = QString( " (%1)" ).arg( mProperty.propertyType() == QgsProperty::ExpressionBasedProperty ? tr( "expression" ) : tr( "field" ) );
  }

  // truncate long expressions, or tool tip may be too wide for screen
  if ( deftip.length() > 75 )
  {
    deftip.truncate( 75 );
    deftip.append( "..." );
  }

  mFullDescription += tr( "<b>Current definition %1:</b><br>%2" ).arg( deftype, deftip );

  setToolTip( mFullDescription );

}

void QgsPropertyOverrideButton::setActivePrivate( bool active )
{
  if ( mProperty.isActive() != active )
  {
    mProperty.setActive( active );
    emit activated( mProperty.isActive() );
  }
}

void QgsPropertyOverrideButton::checkCheckedWidgets( bool check )
{
  // don't uncheck, only set to checked
  if ( !check )
  {
    return;
  }

  Q_FOREACH ( const QPointer< QWidget > &w, mCheckedWidgets )
  {
    QAbstractButton *btn = qobject_cast< QAbstractButton * >( w.data() );
    if ( btn && btn->isCheckable() )
    {
      btn->setChecked( true );
      continue;
    }
    QGroupBox *grpbx = qobject_cast< QGroupBox * >( w.data() );
    if ( grpbx && grpbx->isCheckable() )
    {
      grpbx->setChecked( true );
    }
  }
}

void QgsPropertyOverrideButton::setActive( bool active )
{
  if ( mProperty.isActive() != active )
  {
    mProperty.setActive( active );
    emit changed();
    emit activated( mProperty.isActive() );
  }
}

void QgsPropertyOverrideButton::registerExpressionContextGenerator( QgsExpressionContextGenerator *generator )
{
  mExpressionContextGenerator = generator;
}