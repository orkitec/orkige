/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiView.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiView.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_util/foreach.h>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiView::FastGuiView(Gorilla::Screen* _screen, uint _z) : screen(_screen), z(_z)
	{

	}
	//---------------------------------------------------------
	FastGuiView::FastGuiView(FastGuiView const & other) : screen(other.screen), z(other.z), layers(other.layers)
	{

	}
	//---------------------------------------------------------
	FastGuiView::~FastGuiView()
	{
		for(std::map<uint, Gorilla::Layer*>::iterator it = this->layers.begin(), itend = this->layers.end(); it != itend; it++)
		{
			it->second->destroyAllCaptions();
			it->second->destroyAllLineLists();
			it->second->destroyAllMarkupTexts();
			it->second->destroyAllPolygons();
			it->second->destroyAllQuadLists();
			it->second->destroyAllRectangles();

			it->second->hide();
			this->screen->hide();

			this->screen->destroy(it->second);
		}
		this->screen->_destroyVertexBuffer();
		this->screen->_redrawAllIndexes();
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