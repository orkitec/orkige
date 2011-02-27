/********************************************************************
	created:	Wednesday 2010/08/04 at 15:05
	filename: 	CheckBox.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_gui/CheckBox.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(CheckBox, CheckBoxToggledEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	CheckBox::CheckBox(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width) : Widget(name, materialGroup)
	{
		this->cursorOver = false;
		this->fitToContents = width <= 0;
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/CheckBox", "BorderPanel", name);
		Ogre::OverlayContainer* c = (Ogre::OverlayContainer*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)c->getChild(this->getName() + "/CheckBoxCaption");
		this->square = (Ogre::BorderPanelOverlayElement*)c->getChild(this->getName() + "/CheckBoxSquare");
		this->checkElement = this->square->getChild(this->square->getName() + "/CheckBoxX");
		this->checkElement->hide();
		this->overlayElement->setWidth(width);
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
#endif
		this->setCaption(caption);
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& CheckBox::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void CheckBox::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
		if (this->fitToContents) 
		{
			this->overlayElement->setWidth(OverlayUtil::getCaptionWidth(caption, this->textArea) + this->square->getWidth() + 23);
		}
	}
	//---------------------------------------------------------
	bool CheckBox::isChecked()
	{
		return this->checkElement->isVisible();
	}
	//---------------------------------------------------------
	void CheckBox::setChecked(bool checked, bool notifyListener)
	{
		if (checked) 
		{
			this->checkElement->show();
		}
		else 
		{
			this->checkElement->hide();
		}

		if (notifyListener)
		{
			GuiManager::getSingleton().getEventManager()->trigger(Event(CheckBox::CheckBoxToggledEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void CheckBox::toggle(bool notifyListener)
	{
		this->setChecked(!this->isChecked(), notifyListener);
	}
	//---------------------------------------------------------
	void CheckBox::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (this->cursorOver) 
		{
			this->toggle();
		}
	}
	//---------------------------------------------------------
	void CheckBox::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->square, cursorPos, 5))
		{
			if (!this->cursorOver)
			{
				this->cursorOver = true;
				this->square->setMaterialName(this->materialGroup + "/MiniTextBox/Over");
				this->square->setBorderMaterialName(this->materialGroup + "/MiniTextBox/Over");
			}
		}
		else
		{
			if (this->cursorOver)
			{
				this->cursorOver = false;
				this->square->setMaterialName(this->materialGroup + "/MiniTextBox");
				this->square->setBorderMaterialName(this->materialGroup + "/MiniTextBox");
			}
		}
	}
	//---------------------------------------------------------
	void CheckBox::onFocusLost()
	{
		this->square->setMaterialName(this->materialGroup + "/MiniTextBox");
		this->square->setBorderMaterialName(this->materialGroup + "/MiniTextBox");
		this->cursorOver = false;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(CheckBox)
	OOBJECT_END
}