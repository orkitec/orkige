/********************************************************************
	created:	Wednesday 2010/08/04 at 15:05
	filename: 	Button.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "engine_gui/Button.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(Button, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Button::Button(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width) : Widget(name, materialGroup)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/Button", "BorderPanel", name);
		this->borderPanel = (Ogre::BorderPanelOverlayElement*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)this->borderPanel->getChild(this->borderPanel->getName() + "/ButtonCaption");
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
#endif
		this->textArea->setTop(-(this->textArea->getCharHeight() / 2));

		if (width > 0)
		{
			this->overlayElement->setWidth(width);
			this->fitToContents = false;
		}
		else this->fitToContents = true;

		this->setCaption(caption);
		this->state = BS_UP;
	}
	//---------------------------------------------------------
	Button::~Button() 
	{

	}
	//---------------------------------------------------------
	const Ogre::DisplayString& Button::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void Button::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
		if (this->fitToContents) 
		{
			this->overlayElement->setWidth(OverlayUtil::getCaptionWidth(caption, this->textArea) + this->overlayElement->getHeight() - 12);
		}
	}
	//---------------------------------------------------------
	const Button::ButtonState& Button::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void Button::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->overlayElement, cursorPos, 4)) 
		{
			this->setState(BS_DOWN);
		}
	}
	//---------------------------------------------------------
	void Button::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		if (this->state == BS_DOWN)
		{
			this->setState(BS_OVER);
			GuiManager::getSingleton().getEventManager()->trigger(Event(Button::ButtonHitEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void Button::onCursorMoved(const Ogre::Vector2& cursorPos)
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
	void Button::onFocusLost()
	{
		this->setState(BS_UP);   // reset button if cursor was lost
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void Button::setState(const Button::ButtonState& bs)
	{
		if (bs == Button::BS_OVER)
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Over");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Over");
		}
		else if (bs == Button::BS_UP)
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
	OABSTRACT_IMPL(Button)
	OOBJECT_END
}