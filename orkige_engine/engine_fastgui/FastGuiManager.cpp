/********************************************************************
	created:	Tuesday 2010/10/26 at 18:25
	filename: 	FastGuiManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
	
	purpose:	
*********************************************************************/

#include "engine_fastgui/FastGuiManager.h"
#include "engine_graphic/Engine.h"

namespace Orkige
{
	IMPL_OSINGLETON(FastGuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiManager::FastGuiManager()
	{
		this->silverback = onew(new Gorilla::Silverback());
/*
		this->silverback->loadAtlas("fastgui_default", Engine::getSingleton().getViewort());
		this->defaultScreen = this->silverback->createScreen(Engine::getSingleton().getViewort(), "fastgui_default");
		this->defaultScreen->setOrientation(Ogre::OrientationMode::OR_DEGREE_270);*/

	}
	//---------------------------------------------------------
	FastGuiManager::~FastGuiManager()
	{

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiManager)
	OOBJECT_END
}