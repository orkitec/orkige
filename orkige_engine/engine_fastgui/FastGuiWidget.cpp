/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	FastGuiWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiWidget.h"
#include "engine_fastgui/FastGuiManager.h"
#include "engine_graphic/Engine.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiWidget::FastGuiWidget(String const & id, String const & atlas, uint z) : IGuiObject(id), visible(true)
	{
		this->view = FastGuiManager::getSingleton().getCreateView(atlas);
		oAssert(view.lock());
		this->layer = view.lock()->getLayer(z);
		oAssert(this->layer);
	}
	//---------------------------------------------------------
	FastGuiWidget::~FastGuiWidget()
	{
	}
	//---------------------------------------------------------
	void FastGuiWidget::centerHorizontal()
	{
		Ogre::Vector2 size = this->getSize();
		Ogre::Vector2 pos = this->getPosition();
		int screenWidth = Engine::getSingleton().getViewport()->getActualWidth();
		// floor to a whole pixel - Caption asserts on subpixel positions
		pos.x = Ogre::Math::Floor((screenWidth/2.f)-(size.x/2.f));
		this->setPosition(pos.x, pos.y);
	}
	//---------------------------------------------------------
	/*
	void FastGuiWidget::setVisibility(bool enable)
	{
		if (enable && !this->visible)
		{
			//optr<FastGuiWidget> widget = boost::static_pointer_cast<Orkige::FastGuiWidget>(this);
			//FastGuiManager::getSingleton().addWidget(widget);
		}
		else if (!enable && this->visible)
		{
			FastGuiManager::getSingleton().destroyWidget(this->getObjectID());
		}
		this->visible = enable;
	}
	//---------------------------------------------------------
	bool FastGuiWidget::getVisibility()
	{
		oAssertDesc(this->visible == FastGuiManager::getSingleton().widgetExists(this->getObjectID()), "");
		return this->visible;
	}
	*/
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiWidget)
		OFUNC(setPosition)
		OFUNC(setSize)
		OFUNC(getSize)
		OFUNC(getPosition)
		OFUNC(centerHorizontal)
		// visibility rides on the shared per-z Gorilla layer (see the jumper
		// HUD): widget:getLayer():hide()/show()/isVisible()
		OFUNC(getLayer)
	OOBJECT_END
}