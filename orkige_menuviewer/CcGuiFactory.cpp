/********************************************************************
	created:	Monday 2010/10/11
	filename: 	CcGuiFactory.cpp
	author:		philipp.engelhard
	notice:		
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include "cc_gui/CcGuiFactory.h"
#include "engine_gui/GuiManager.h"

namespace CC
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	CcGuiFactory::CcGuiFactory()
	{

	}
	//---------------------------------------------------------
	CcGuiFactory::~CcGuiFactory()
	{

	}

	//---------------------------------------------------------
	DragDropButton* CcGuiFactory::createDragDropButton(
		Orkige::Widget::TrayLocation trayLoc,
		Orkige::String const & name,
		Orkige::String const & materialGroup,
		Orkige::String const & templateName,
		Orkige::String const & caption,
		Ogre::Real width)
	{
		DragDropButton* button = new DragDropButton(name, materialGroup, templateName, caption, width);
		Orkige::GuiManager::getSingleton().moveWidgetToTray(button, trayLoc);
		return button;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}