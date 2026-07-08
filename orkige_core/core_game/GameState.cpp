/**************************************************************
	created:	2010/08/15 at 16:03
	filename: 	GameState.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_game/GameState.h"
#include "core_util/Timer.h"
#include "core_game/Application.h"
#include "core_game/GameStateManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameState::GameState(String const & id) : Object(id), popState(false)
	{
		this->registerEvent(Application::UpdateEvent, &GameState::onApplicationUpdate, this);
	}
	//---------------------------------------------------------
	GameState::~GameState()
	{
	}
	//---------------------------------------------------------
	void GameState::setTransition(String const & id)
	{
		this->transitionState = id;
	}
	//---------------------------------------------------------
	void GameState::setPush(String const & id)
	{
		this->pushState = id;
	}
	//---------------------------------------------------------
	void GameState::setPop()
	{
		this->popState = true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GameState::onEnter()
	{

	}
	//---------------------------------------------------------
	void GameState::onReturn()
	{

	}
	//---------------------------------------------------------
	void GameState::onLeave()
	{

	}
	//---------------------------------------------------------
	void GameState::onExit()
	{

	}
	//---------------------------------------------------------
	void GameState::addListener(optr<EventListener> inListener, EventType const & eventType)
	{
		EventTypeListenerMap::iterator it = this->listeners.find(eventType);
		EventListenerVector elv;
		if(it == this->listeners.end())
		{
			elv.push_back(inListener);
			this->listeners[eventType] = elv;
		}
		else
		{
			it->second.push_back(inListener);
		}
	}
	//---------------------------------------------------------
	void GameState::enableListeners()
	{
		foreach(EventTypeListenerMap::value_type const & vt, listeners)
		{
			foreach(optr<EventListener> listener, vt.second)
			{
				GlobalEventManager::getSingleton().addListener(listener, vt.first);
			}
		}
	}
	//---------------------------------------------------------
	void GameState::disableListeners()
	{
		foreach(EventTypeListenerMap::value_type const & vt, listeners)
		{
			foreach(optr<EventListener> listener, vt.second)
			{
				GlobalEventManager::getSingleton().delListener(listener, vt.first);
			}
		}
	}
	//---------------------------------------------------------
	void GameState::delListener(optr<EventListener> remListener)
	{
		for(EventTypeListenerMap::iterator it = this->listeners.begin(), itend = this->listeners.end(); it != itend; ++it)
		{
			for(EventListenerVector::iterator jit = it->second.begin(), jitend = it->second.end(); jit != jitend; ++jit)
			{
				if(remListener == (*jit))
				{
					GlobalEventManager::getSingleton().delListener((*jit), it->first);//<- not sure abaout this one
					it->second.erase(jit);
					jit = it->second.begin();
					jitend = it->second.end();
				}
			}
			if(it->second.empty())
			{
				this->listeners.erase(it);
				it = this->listeners.begin();
				itend = this->listeners.end();
			}
		}
	}
	//---------------------------------------------------------
	void GameState::deleteListeners()
	{
		this->disableListeners();
		this->listeners.clear();
	}
	//---------------------------------------------------------
	bool GameState::onApplicationUpdate(Event const & event)
	{
		if(this->popState)
		{
			// we allow popping directly followed by transit to sibling state
			//oAssertDesc(this->transitionState.empty() && this->pushState.empty(), "GameState transition ambigous");

			GameStateManager::getSingleton().popState();
			this->popState = false;
			return true;
		}
		else if(!this->transitionState.empty())
		{
			oAssertDesc(this->pushState.empty() && !this->popState, "GameState transition ambigous");

			GameStateManager::getSingleton().setState(transitionState);
			this->transitionState.clear();
			return true;
		}
		else if(!this->pushState.empty())
		{
			oAssertDesc(this->transitionState.empty() && !this->popState, "GameState transition ambigous");

			GameStateManager::getSingleton().pushState(pushState);
			this->pushState.clear();
			return true;
		}

		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OWRAPPER_START(GameState)
		OWRAPPER_CONSTRUCTOR0()
		OWRAPPER_CONSTRUCTOR1(String const &)
		OWRAPPER_FUNC(void,onEnter)
		OWRAPPER_FUNC(void,onReturn)
		OWRAPPER_FUNC(void,onLeave)
		OWRAPPER_FUNC(void,onExit)
	OWRAPPER_END

	OVIRTUAL_OBJECT_IMPL(GameState)
		OVIRTUAL_CONSTRUCTOR1(String)
		OVIRTUAL_FUNC(onEnter)
		OVIRTUAL_FUNC(onReturn)
		OVIRTUAL_FUNC(onLeave)
		OVIRTUAL_FUNC(onExit)
	OOBJECT_END
}
