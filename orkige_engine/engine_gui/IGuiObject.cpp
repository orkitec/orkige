/**************************************************************
	created:	2010/08/07 at 3:29
	filename: 	IGuiObject.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
***************************************************************/

#include "engine_gui/IGuiObject.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	IGuiObject::~IGuiObject()
	{

	}
	//---------------------------------------------------------
	void IGuiObject::onCursorPressed(Ogre::Vector2 const & cursorPos) 
	{

	}
	//---------------------------------------------------------
	void IGuiObject::onCursorReleased(Ogre::Vector2 const & cursorPos) 
	{

	}
	//---------------------------------------------------------
	void IGuiObject::onCursorMoved(Ogre::Vector2 const & cursorPos) 
	{

	}
	//---------------------------------------------------------
	bool IGuiObject::onKeyPressed(KeyEventData const & data)
	{
		return false;
	}
	//---------------------------------------------------------
	bool IGuiObject::onKeyReleased(KeyEventData const & data)
	{
		return false;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	IGuiObject::IGuiObject(String const & id) : Object(id)
	{
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(IGuiObject)
	OOBJECT_END
}