/********************************************************************
	created:	Friday 2010/08/06 at 19:03
	filename: 	OkDialog.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_gui/OkDialog.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(OkDialog, OkDialogClosedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	OkDialog::OkDialog(DialogType type, String const & name, String const & materialGroup, Ogre::DisplayString const & caption, Ogre::DisplayString const & message, Ogre::DisplayString const & okButtonText) : Dialog(type, name, materialGroup, caption, message)
	{
		this->okButton = new Button(name + "/OkButton", materialGroup, okButtonText, 60);
		GuiManager::getSingleton().getDialogShade()->addChild(this->okButton->getOverlayElement());
		this->okButton->getOverlayElement()->setVerticalAlignment(Ogre::GVA_CENTER);
		this->okButton->getOverlayElement()->setLeft(-(this->okButton->getOverlayElement()->getWidth() / 2));
		this->okButton->getOverlayElement()->setTop(this->text->getOverlayElement()->getTop() + this->text->getOverlayElement()->getHeight() + 5);

		this->registerEvent(Button::ButtonHitEvent, &OkDialog::onButtonHit, this);
	}
	//---------------------------------------------------------
	OkDialog::~OkDialog()
	{
		this->okButton->cleanup();
		delete this->okButton;
		this->okButton = 0;
	}
	//---------------------------------------------------------
	void OkDialog::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		this->text->onCursorPressed(cursorPos);
		this->okButton->onCursorPressed(cursorPos);
	}
	//---------------------------------------------------------
	void OkDialog::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		this->text->onCursorReleased(cursorPos);
		this->okButton->onCursorReleased(cursorPos);
	}
	//---------------------------------------------------------
	void OkDialog::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		this->text->onCursorMoved(cursorPos);
		this->okButton->onCursorMoved(cursorPos);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool OkDialog::onButtonHit(Orkige::Event const & event)
	{
		optr<Button> button = event.getDataPtr<Button>();
		if(button.get() == this->okButton)
		{
			this->getEventManager()->trigger(Event(OkDialog::OkDialogClosedEvent, oBadPointer(this)));
			GuiManager::getSingleton().closeDialog();
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(OkDialog)
	OOBJECT_END
}