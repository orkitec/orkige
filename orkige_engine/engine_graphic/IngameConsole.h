/**************************************************************
	created:	2010/08/07 at 0:24
	filename: 	IngameConsole.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __IngameConsole_h__7_8_2010__0_24_16__
#define __IngameConsole_h__7_8_2010__0_24_16__

#include <core_event/Event.h>
#include <core_event/EventListener.h>
#include <core_util/Singleton.h>
#include <core_util/FastDelegate.h>
#include <core_util/String.h>
#include <core_util/foreach.h>
#include "engine_module/EnginePrerequisites.h"
#include <list>
#include <vector>

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! console to show Ingame log and execute commands
	class ORKIGE_ENGINE_DLL IngameConsole : public Singleton<IngameConsole>, Ogre::LogListener, public Object
	{
		OOBJECT(IngameConsole,Object);
		DECL_OSINGLETON(IngameConsole);
		//--- Types -------------------------------------------
	public:
		typedef fastdelegate::FastDelegate1<StringVector const &, void>		ConsoleCommand;			//!< defines command function type
		typedef std::map<String, ConsoleCommand>							ConsoleCommandRegistry;	//!< registers command name with ConsoleCommand
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		bool												visible;
		bool												initialized;
		bool												pythonMode;
		std::vector<String>									history;
		std::size_t											history_pos;			
		Ogre::SceneManager									*scene;
		Ogre::Rectangle2D									*rect;
		Ogre::SceneNode										*node;
		Ogre::OverlayElement								*textbox;
		Ogre::Overlay										*overlay;

		float												height;
		float												blink;
		bool												update_overlay;
		std::size_t											start_line;
		std::size_t											cursor_pos;
		String												cursor;
		std::list<String>									lines;
		String												prompt;
		ConsoleCommandRegistry								commands;
		optr<EventListener>									keyListener;
		//--- Methods -----------------------------------------
	public:
		//! constructor
		IngameConsole();
		//! destructor
		~IngameConsole();
		//! int console
		void init();
		//! deinit console
		void shutdown();
		//! @brief show or hide console
		//! @param visible if true console is visible / false invisible
		void setVisible(bool visible);
		//! is console currently visible
		inline bool isVisible();
		//! toggle console visibility
		void switchVisible();
		//! print given text to console
		void print(const String &text);
		//! add command function to the registry
		void addCommand(String const & command, ConsoleCommand func);
		//! remove command
		void removeCommand(String const & command);
		//! get all command names
		Orkige::StringVector getCommandNames();
	protected:
		//! update console on frame updates
		bool onFrameStarted(Event const & event);
		//! react on key press to capture text
		bool onKeyPressed(Event const & event);
		//! capture Ogre::LogListener output
		virtual void messageLogged(const String& message, Ogre::LogMessageLevel lml, bool maskDebug, const String &logName, bool& skipThisMessage);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------
	inline bool IngameConsole::isVisible()
	{
		return this->visible;
	}
	//---------------------------------------------------------
	//! default method that gets executed when you print "help" in the console (can be replace by adding a custom command for "help"
	static inline void printDefaultConsoleHelp(Orkige::StringVector const & params)
	{
		IngameConsole::getSingleton().print("Available console commands:");
		foreach(String const & command, IngameConsole::getSingleton().getCommandNames())
		{
			IngameConsole::getSingleton().print(command);
		}
	}
}

#endif //__IngameConsole_h__7_8_2010__0_24_16__

