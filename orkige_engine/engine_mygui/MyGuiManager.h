/**************************************************************
	created:	2011/11/03 at 1:15
	filename: 	MyGuiManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __MyGuiManager_h__3_11_2011__1_15_01__
#define __MyGuiManager_h__3_11_2011__1_15_01__

#include <core_module/OrkigePrerequisites.h>
#include "engine_input/InputManager.h"
#include <core_event/EventHandler.h>
#include <MyGUI.h>
#include <MyGUI_OgrePlatform.h>

namespace Orkige
{
	class MyGuiManager : public Singleton<MyGuiManager>, public Interface, public EventHandler
	{
		OOBJECT(MyGuiManager, Interface);
		DECL_OSINGLETON(MyGuiManager);
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		optr<MyGUI::Gui> myGui; //!< MyGUI instance
		optr<MyGUI::OgrePlatform> ogrePlatform; //!< MyGUI Ogre Renderer
	private:
		//--- Methods -----------------------------------------
	public:
		//! @see MyGUI::Gui::initialise
		MyGuiManager(Orkige::String const & _core = "MyGUI_Core.xml", unsigned short activeViewport = 0);
		virtual ~MyGuiManager();
		//! enable key and mouse events
		void enableInputEvents();
		//! disable key and mouse events
		void disableInputEvents();
		//! get Base MyGUI Gui System
		inline woptr<MyGUI::Gui> getGui();
		//! get MyGUI Ogre Renderer
		inline woptr<MyGUI::OgrePlatform> getOgrePlatform();
	protected:
		//! process key pressed events
		bool onKeyPressed(Orkige::Event const & event);
		//! process key released events
		bool onKeyReleased(Orkige::Event const & event);

		//! Processes mouse button down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMousePressed(Orkige::Event const & event);
		//! Processes mouse button up events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseReleased(Orkige::Event const & event);
		//! Updates cursor position. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseMoved(Orkige::Event const & event);

		//! Processes touch down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchPressed(Orkige::Event const & event);
		//! Processes touch up events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchReleased(Orkige::Event const & event);
		//! Processes touch move events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchMoved(Orkige::Event const & event);
	private:
	};
	//---------------------------------------------------------
	inline woptr<MyGUI::Gui> MyGuiManager::getGui()
	{
		return this->myGui;
	}
	//---------------------------------------------------------
	inline woptr<MyGUI::OgrePlatform> MyGuiManager::getOgrePlatform()
	{
		return this->ogrePlatform;
	}
	//---------------------------------------------------------
}

#endif //__MyGuiManager_h__3_11_2011__1_15_01__