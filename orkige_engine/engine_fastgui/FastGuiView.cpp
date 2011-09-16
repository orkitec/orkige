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
#include "engine_graphic/Engine.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiView::FastGuiView(Gorilla::Screen* _screen, uint _z) : screen(_screen), z(_z), screenRenderable(NULL)
	{

	}
	//---------------------------------------------------------
	FastGuiView::FastGuiView(Gorilla::ScreenRenderable* _screenRenderable, uint _z) : screen(NULL), screenRenderable(_screenRenderable), z(_z)
	{

	}
	//---------------------------------------------------------
	FastGuiView::FastGuiView(FastGuiView const & other) : screen(other.screen), screenRenderable(other.screenRenderable), z(other.z), layers(other.layers)
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
			if(this->screen)
			{
				this->screen->setVisible(false);
				this->screen->destroy(it->second);
			}
			else if(this->screenRenderable)
			{
				this->screenRenderable->setVisible(false);
				this->screenRenderable->destroy(it->second);
			}
		}
		if(this->screen)
		{
			this->screen->_destroyVertexBuffer();
			this->screen->_redrawAllIndexes();
		}
		else if(this->screenRenderable)
		{
			this->screenRenderable->_destroyVertexBuffer();
			this->screenRenderable->_redrawAllIndexes();
		}
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiView::getPosition(FastGuiView::Alignment alignment)
	{
		Ogre::Real width = 0.f;
		Ogre::Real height = 0.f;

		if(this->screen)
		{
			width = this->screen->getWidth();
			height = this->screen->getHeight();
		}
		else
		{
			width = this->screenRenderable->getBoundingBox().getSize().x;
			height = this->screenRenderable->getBoundingBox().getSize().y;
		}
		
		switch(alignment)
		{
		case VA_TOPLEFT:
			{
				return Ogre::Vector2::ZERO;
			} break;
		case VA_TOP:
			{
				return Ogre::Vector2(width/2.f, 0.f);
			} break;
		case VA_TOPRIGHT:
			{
				return Ogre::Vector2(width, 0.f);
			} break;
		case VA_LEFT:
			{
				return Ogre::Vector2(0.f, height/2.f);
			} break;
		case VA_CENTER:
			{
				return Ogre::Vector2(width/2.f, height/2.f);
			} break;
		case VA_RIGHT:
			{
				return Ogre::Vector2(width, height/2.f);
			} break;
		case VA_BOTTOMLEFT:
			{
				return Ogre::Vector2(0, height);
			} break;
		case VA_BOTTOM:
			{
				return Ogre::Vector2(width/2.f, height);
			} break;
		case VA_BOTTOMRIGHT:
			{
				return Ogre::Vector2(width, height);
			} break;
		}

		return Ogre::Vector2::ZERO;
	}
	//---------------------------------------------------------
	Ogre::Real FastGuiView::getWidth()
	{
		if(this->screen)
		{
			return this->screen->getWidth();
		}
		else
		{
			return Engine::getSingleton().getViewport()->getActualWidth();
			/*return 1920.f;*/
			return this->screenRenderable->getBoundingBox().getSize().x*100.f;
		}
	}
	//---------------------------------------------------------
	Ogre::Real FastGuiView::getHeight()
	{
		if(this->screen)
		{
			return this->screen->getHeight();
		}
		else
		{
			return Engine::getSingleton().getViewport()->getActualHeight();
			/*return 1080.f;*/
			return this->screenRenderable->getBoundingBox().getSize().y*100.f;
		}
	}
	//---------------------------------------------------------
	void FastGuiView::setVisible(bool visible)
	{
		if(this->screen)
		{
			this->screen->setVisible(visible);
		}
		else if(this->screenRenderable)
		{
			this->screenRenderable->setVisible(visible);
		}
	}
	//---------------------------------------------------------
	Gorilla::TextureAtlas* FastGuiView::getAtlas() const
	{
		if(this->screen)
		{
			return this->screen->getAtlas();
		}
		else
		{
			return this->screenRenderable->getAtlas();
		}
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