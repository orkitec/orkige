/********************************************************************
	created:	2009/07/18 at 0:12
	filename: 	Application.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "core_game/Application.h"
#include "core_game/GameObjectManager.h"
#include "core_game/GameStateManager.h"
#include "core_event/GlobalEventManager.h"
#include "core_util/Timer.h"

namespace Orkige
{
	IMPL_OSINGLETON(Application)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Application::Application(String const & _logConfigFileName) : _run(true), logConfigFileName(_logConfigFileName)
	{
	}
	//---------------------------------------------------------
	Application::~Application()
	{
		this->deinit();
	}
	//---------------------------------------------------------
	bool Application::run()
	{
		this->gom->processDeleteQueue();
		this->gem->tick();
		this->gem->trigger(Event(Application::UpdateEvent));
		return this->_run;
	}
	//---------------------------------------------------------
	void Application::quit()
	{
		this->_run = false;
	}
	//---------------------------------------------------------
	bool Application::init()
	{
#ifdef ORKIGE_NDS
		FS_Init(MI_DMA_MAX_NUM);
#elif ORKIGE_IPHONE
		//log needs to be manually created on iphone for destroying it in the right order pc and ds do this automagicly
		
#endif
		
		LogManager::getSingleton().loadConfig(this->logConfigFileName.c_str());//LogConfig has to Be in Path
		Timer::initialise();
		this->gom = onew(new GameObjectManager());
		this->gsm = onew(new GameStateManager());
		this->gem = onew(new GlobalEventManager());
		return true;
	}
	//---------------------------------------------------------
	bool Application::deinit()
	{
		this->gsm.reset();
		this->gom.reset();
		this->gem.reset();
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	IMPL_OWNED_EVENTTYPE(Application, UpdateEvent);
}