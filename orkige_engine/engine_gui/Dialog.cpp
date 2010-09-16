/**************************************************************
	created:	2010/08/07 at 3:36
	filename: 	Dialog.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_gui/Dialog.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Dialog::~Dialog()
	{
		this->text->cleanup();
		delete this->text;
		this->text = 0;
	}
	//---------------------------------------------------------
	Dialog::DialogType Dialog::getDialogType()
	{
		return this->dialogType;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	Dialog::Dialog(DialogType type, String const & name, String const & materialGroup, Ogre::DisplayString const & caption, Ogre::DisplayString const & message) : IGuiObject(name), dialogType(type)
	{
		this->text = new TextBox(name + "/DialogBox", materialGroup, caption, 300, 208);
		this->text->setText(message);
		GuiManager::getSingleton().getDialogShade()->addChild(this->text->getOverlayElement());
		this->text->getOverlayElement()->setVerticalAlignment(Ogre::GVA_CENTER);
		this->text->getOverlayElement()->setLeft(-(this->text->getOverlayElement()->getWidth() / 2));
		this->text->getOverlayElement()->setTop(-(this->text->getOverlayElement()->getHeight() / 2));
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(Dialog)
	OOBJECT_END
}