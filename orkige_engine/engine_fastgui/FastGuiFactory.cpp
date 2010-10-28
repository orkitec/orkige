/********************************************************************
	created:	Wednesday 2010/10/27 at 13:09
	filename: 	FastGuiFactory.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiFactory.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiFactory::FastGuiFactory()
	{
	}
	//---------------------------------------------------------
	FastGuiFactory::~FastGuiFactory()
	{
	}
	//---------------------------------------------------------
	woptr<FastGuiDecorWidget> FastGuiFactory::createDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z)
	{
		optr<FastGuiDecorWidget> dw;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			return dw;
		}
		dw = onew(new FastGuiDecorWidget(id, spriteName, position, size, atlas, z));
		FastGuiManager::getSingleton().addWidget(dw);
		return dw;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}