/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiView.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiView.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	//---------------------------------------------------------------
	FastGuiView::FastGuiView(Gorilla::Screen* _screen) : screen(_screen) 
	{

	}
	//---------------------------------------------------------
	FastGuiView::~FastGuiView()
	{
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiView::getPosition(FastGuiView::Alignment alignment)
	{
		switch(alignment)
		{
		case VA_TOPLEFT:
			{
				return Ogre::Vector2::ZERO;
			} break;
		case VA_TOP:
			{
				return Ogre::Vector2(screen->getWidth()/2.f, 0.f);
			} break;
		case VA_TOPRIGHT:
			{
				return Ogre::Vector2(screen->getWidth(), 0.f);
			} break;
		case VA_LEFT:
			{
				return Ogre::Vector2(0.f, screen->getHeight()/2.f);
			} break;
		case VA_CENTER:
			{
				return Ogre::Vector2(screen->getWidth()/2.f, screen->getHeight()/2.f);
			} break;
		case VA_RIGHT:
			{
				return Ogre::Vector2(screen->getWidth(), screen->getHeight()/2.f);
			} break;
		case VA_BOTTOMLEFT:
			{
				return Ogre::Vector2(0, screen->getHeight());
			} break;
		case VA_BOTTOM:
			{
				return Ogre::Vector2(screen->getWidth()/2.f, screen->getHeight());
			} break;
		case VA_BOTTOMRIGHT:
			{
				return Ogre::Vector2(screen->getWidth(), screen->getHeight());
			} break;
		}

		return Ogre::Vector2::ZERO;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiView)
	OOBJECT_END
}