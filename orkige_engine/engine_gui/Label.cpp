/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	Label.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "engine_gui/Label.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(Label, LabelHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Label::Label(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width) : Widget(name, materialGroup)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/Label", "BorderPanel", name);
		this->textArea = (Ogre::TextAreaOverlayElement*)((Ogre::OverlayContainer*)this->overlayElement)->getChild(this->getName() + "/LabelCaption");
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
#endif
		this->setCaption(caption);
		if (width <= 0) this->fitToTray = true;
		else
		{
			this->fitToTray = false;
			this->overlayElement->setWidth(width);
		}
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& Label::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void Label::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
	}
	//---------------------------------------------------------
	bool Label::_isFitToTray()
	{
		return this->fitToTray;
	}
	//---------------------------------------------------------
	void Label::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->overlayElement, cursorPos, 3))
		{
			GuiManager::getSingleton().getEventManager()->trigger(Event(Label::LabelHitEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(Label)
	OOBJECT_END
}