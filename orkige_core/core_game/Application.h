/********************************************************************
	created:	2009/07/18 at 0:14
	filename: 	Application.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __Application_h__18_7_2009__0_14_46__
#define __Application_h__18_7_2009__0_14_46__

#include <core_debug/MemoryManager.h>
#include <core_util/Singleton.h>
#include <core_util/optr.h>
#include <core_event/Event.h>

namespace Orkige
{
	class GameObjectManager;
	class GameStateManager;
	class GlobalEventManager;

	/**
	* @defgroup EngineEvents EngineEvents
	* @{
	* Events triggered from Orkige
	* @} End of "defgroup EngineEvents".
	*/

	//! Base Game Application
	class Application : public Singleton<Application>
	{
		DECL_OSINGLETON(Application)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered once per Application update cycle
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(UpdateEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<GameObjectManager> gom;	//!< GameObjectManager Singleton
		optr<GameStateManager> gsm;		//!< GameStateManager Singleton
		optr<GlobalEventManager> gem;	//!< GlobalEventManager Singleton
		bool _run;						//!< true as long application should run
		String logConfigFileName;		//!< name of LogConfig
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor takes LogConfig filename
		Application(String const & logConfigFileName = "data/LogConfig.xml");
		//! destructor
		virtual ~Application();
		//! create and init Timer, GameObjectManager, GameStateManager, GlobalEventManager and load LogConfig
		virtual bool init();
		//! destroy GameObjectManager, GameStateManager, GlobalEventManager
		virtual bool deinit();
		//! run Application and trigger Application::UpdateEvent
		virtual bool run();
		//! stop Application
		virtual void quit();
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__Application_h__18_7_2009__0_14_46__