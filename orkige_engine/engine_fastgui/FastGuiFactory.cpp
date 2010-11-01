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
		optr<FastGuiDecorWidget> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiDecorWidget(id, spriteName, position, size, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<FastGuiLabel> FastGuiFactory::createLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z)
	{
		optr<FastGuiLabel> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiLabel(id, defaultGlyphIndex, text, position, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	woptr<FastGuiButton> FastGuiFactory::createButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z)
	{
		optr<FastGuiButton> widget;

		if(FastGuiManager::getSingleton().widgetExists(id))
		{
			oAssertDesc(!FastGuiManager::getSingleton().widgetExists(id), "Widget with id: " << id << "already exists!");
			return widget;
		}
		widget = onew(new FastGuiButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z));
		FastGuiManager::getSingleton().addWidget(widget);
		return widget;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}