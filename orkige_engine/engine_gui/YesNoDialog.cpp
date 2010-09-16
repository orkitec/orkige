/********************************************************************
	created:	Friday 2010/08/06 at 19:03
	filename: 	YesNoDialog.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_gui/YesNoDialog.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(YesNoDialog, YesNoDialogClosedEvent);
	IMPL_OWNED_EVENTTYPE(YesNoDialog, YesNoDialogAbortEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	YesNoDialog::YesNoDialog(DialogType type, String const & name, String const & materialGroup, Ogre::DisplayString const & caption, Ogre::DisplayString const & message, const Ogre::DisplayString& yesButtontext,const Ogre::DisplayString& noButtontext) : Dialog(type, name, materialGroup, caption, message)
	{
		this->yesButton = new Button(name + "/YesButton",materialGroup , yesButtontext, 58);

		GuiManager::getSingleton().getDialogShade()->addChild(this->yesButton->getOverlayElement());
		this->yesButton->getOverlayElement()->setVerticalAlignment(Ogre::GVA_CENTER);
		this->yesButton->getOverlayElement()->setLeft(-(this->yesButton->getOverlayElement()->getWidth() + 2));
		this->yesButton->getOverlayElement()->setTop(this->text->getOverlayElement()->getTop() + this->text->getOverlayElement()->getHeight() + 5);

		this->noButton = new Button(name + "/NoButton",materialGroup , noButtontext, 50);

		GuiManager::getSingleton().getDialogShade()->addChild(this->noButton->getOverlayElement());
		this->noButton->getOverlayElement()->setVerticalAlignment(Ogre::GVA_CENTER);
		this->noButton->getOverlayElement()->setLeft(3);
		this->noButton->getOverlayElement()->setTop(this->text->getOverlayElement()->getTop() + this->text->getOverlayElement()->getHeight() + 5);

		this->registerEvent(Button::ButtonHitEvent, &YesNoDialog::onButtonHit, this);
	}
	//---------------------------------------------------------
	YesNoDialog::~YesNoDialog()
	{
		this->yesButton->cleanup();
		this->noButton->cleanup();
		delete this->yesButton;
		delete this->noButton;
		this->yesButton = 0;
		this->noButton = 0;
	}
	//---------------------------------------------------------
	void YesNoDialog::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		this->text->onCursorPressed(cursorPos);
		this->yesButton->onCursorPressed(cursorPos);
		this->noButton->onCursorPressed(cursorPos);
	}
	//---------------------------------------------------------
	void YesNoDialog::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		this->text->onCursorReleased(cursorPos);
		this->yesButton->onCursorReleased(cursorPos);
		if(GuiManager::getSingleton().isDialogVisible()) //button may have already close dialog so we have to check for that
			this->noButton->onCursorReleased(cursorPos);
	}
	//---------------------------------------------------------
	void YesNoDialog::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		if(this->text) this->text->onCursorMoved(cursorPos);
		if(this->yesButton) this->yesButton->onCursorMoved(cursorPos);
		if(this->noButton) this->noButton->onCursorMoved(cursorPos);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool YesNoDialog::onButtonHit(Orkige::Event const & event)
	{
		optr<Button> button = event.getDataPtr<Button>();
		if(button.get() == this->yesButton)
		{
			this->getEventManager()->trigger(Event(YesNoDialog::YesNoDialogClosedEvent, oBadPointer(this)));
			GuiManager::getSingleton().closeDialog();
			return true;
		}
		else if(button.get() == this->noButton)
		{
			this->getEventManager()->trigger(Event(YesNoDialog::YesNoDialogAbortEvent, oBadPointer(this)));
			GuiManager::getSingleton().closeDialog();
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(YesNoDialog)
	OOBJECT_END
}