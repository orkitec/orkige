/**************************************************************
	created:	2010/08/07 at 3:29
	filename: 	IGuiObject.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/
#ifndef __IGuiObject_h__7_8_2010__3_29_30__
#define __IGuiObject_h__7_8_2010__3_29_30__

#include <core_base/Object.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_input/KeyEventData.h"
#include "engine_graphic/FrameEventData.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! abstract base for all GuiElements
	class ORKIGE_ENGINE_DLL IGuiObject : public Object
	{
		OOBJECT(IGuiObject, Object);
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! destructor
		virtual ~IGuiObject();

		//! @brief overridable message gets called when cursor is pressed
		//! @param cursorPos absolute cursor screen position
		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		//! @brief overridable message gets called when cursor is released
		//! @param cursorPos absolute cursor screen position
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		//! @brief overridable message gets called when cursor is moved
		//! @param cursorPos absolute cursor screen position
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		//! overridable message gets called when a key is pressed
		virtual bool onKeyPressed(KeyEventData const & data);
		//! overridable message gets called when a key is released
		virtual bool onKeyReleased(KeyEventData const & data);
		//! overridable gets called every frame
		virtual bool onFrameStarted(FrameEventData const & data);
	protected:
		//! protected constructor t o prevent creation
		IGuiObject(String const & id);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------
}

#endif //__IGuiObject_h__7_8_2010__3_29_30__