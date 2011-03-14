/********************************************************************
	created:	Monday 2010/10/11
	filename:	DragDropButton.cpp
	author:		philipp.engelhard
	notice:		
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include <core_event/GlobalEventManager.h>
#include <core_game/GameObjectManager.h>
#include <engine_graphic/Engine.h>
#include "DragDropButton.h"
#include "engine_gui/GuiManager.h"

using namespace Orkige;

namespace CC
{
	IMPL_OWNED_EVENTTYPE(DragDropButton, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DragDropButton::DragDropButton(
		const Orkige::String& name,
		const Orkige::String& materialGroup,
		const Orkige::String& templateName,
		const Ogre::DisplayString& caption,
		Ogre::Real width)
	:
	Widget(name, materialGroup),
	isDragging(false),
	pickedObject(0),
	dragee(0),
	imageOverlayContainer(0),
	imageToCursorOffset(),
	pickEvent(NULL)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(materialGroup + "/" + templateName, "", name);
		this->borderPanel = (Ogre::BorderPanelOverlayElement*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)this->borderPanel->getChild(this->borderPanel->getName() + "/DragDropButtonCaption");
		this->imageOverlay = (Ogre::OverlayElement*)this->borderPanel->getChild(this->borderPanel->getName() + "/DragDropButtonImage");

#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
#endif

		this->textArea->setTop(-(this->textArea->getCharHeight() / 2));

		this->fitToContents = false;

		this->setCaption(caption);
		this->state = BS_UP;

		this->imageOverlayContainer = this->imageOverlay->getParent();
		this->imageOverlay->setMetricsMode(Ogre::GMM_PIXELS);
	}
	//---------------------------------------------------------
	DragDropButton::~DragDropButton()
	{
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& DragDropButton::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void DragDropButton::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
		if (this->fitToContents) 
		{
			this->overlayElement->setWidth(OverlayUtil::getCaptionWidth(caption, this->textArea) + this->overlayElement->getHeight() - 12);
		}
	}
	//---------------------------------------------------------
	const DragDropButton::ButtonState& DragDropButton::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void DragDropButton::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->overlayElement, cursorPos, 4)) 
		{
			this->setState(BS_DOWN);
		}
	}
	//---------------------------------------------------------
	void DragDropButton::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		if (this->state == BS_DOWN)
		{
			this->setState(BS_OVER);
			GuiManager::getSingleton().getEventManager()->trigger(Event(::Orkige::Button::ButtonHitEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void DragDropButton::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->overlayElement, cursorPos, 4))
		{
			if (this->state == BS_UP) this->setState(BS_OVER);
		}
		else
		{
			if (this->state != BS_UP) this->setState(BS_UP);
		}
	}
	//---------------------------------------------------------
	void DragDropButton::onFocusLost()
	{
		this->setState(BS_UP);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DragDropButton::setState(const DragDropButton::ButtonState& bs)
	{
		if (bs == DragDropButton::BS_OVER)
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Over");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Over");
		}
		else if (bs == DragDropButton::BS_UP)
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Up");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Up");
		}
		else
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Down");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Down");
		}

		this->state = bs;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(DragDropButton)
	OOBJECT_END
}