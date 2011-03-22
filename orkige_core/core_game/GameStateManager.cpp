/********************************************************************
	created:	Monday 2010/08/16 at 17:20
	filename: 	GameStateManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_game/GameStateManager.h"

namespace Orkige
{
	IMPL_OSINGLETON(GameStateManager);
	IMPL_OWNED_EVENTTYPE(GameStateManager, GameStateChangedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameStateManager::GameStateManager() : Object()
	{
	}
	//---------------------------------------------------------
	GameStateManager::~GameStateManager()
	{
		while(!this->statePath.empty())
		{
			this->popState();
		}
	}
	//---------------------------------------------------------
	void GameStateManager::registerState(optr<GameState> state)
	{
		String stateId = state->getObjectID();
		if(this->stateRegistry.find(stateId) != this->stateRegistry.end())
		{
			throw GameStateError(stateId + " State already registered!");
		}

		this->stateRegistry[stateId] = state;
	}
	//---------------------------------------------------------
	void GameStateManager::setInitialState(String const & id)
	{
		oAssertDesc(this->previousStateId.empty(), "GameStateManager: already in a state");
		oAssertDesc(this->statePath.empty(), "GameStateManager: already in a state");
		this->pushState(id);
	}
	//---------------------------------------------------------
	void GameStateManager::pushState(String const & id)
	{
		if(this->stateRegistry.find(id) == this->stateRegistry.end())
		{
			throw GameStateError(id + " State not registered!");
		}

		//if there is already a state active then pause it and leave the state
		if(this->statePath.size() > 0)
		{
			this->statePath.back()->disableListeners();
			this->statePath.back()->onLeave();
		}
		//put the new state on the stack
		this->statePath.push_back(stateRegistry[id]);
		//init the new state
		this->statePath.back()->enableListeners();
		this->statePath.back()->onEnter();

		GlobalEventManager::getSingleton().trigger(Event(GameStateManager::GameStateChangedEvent));
	}
	//---------------------------------------------------------
	void GameStateManager::popState()
	{
		if(this->statePath.empty())
		{
			throw GameStateError("State Path empty! No State to pop!");
		}

		//cleanup
		this->statePath.back()->disableListeners();
		this->statePath.back()->onExit();

		//remove current state
		this->statePath.pop_back();

		//if there are any previous states on the path go back to previous
		if(this->statePath.size()>0)
		{
			this->statePath.back()->enableListeners();
			this->statePath.back()->onReturn();
		}

		GlobalEventManager::getSingleton().trigger(Event(GameStateManager::GameStateChangedEvent));
	}
	//---------------------------------------------------------
	void GameStateManager::setState(String const & id)
	{
		this->previousStateId.clear();
		if(!this->statePath.empty())
		{
			this->previousStateId = this->getCurrentStateID();
			this->popState();
		}
		this->pushState(id);
	}
	//---------------------------------------------------------
	woptr<GameState> GameStateManager::getCurrent()
	{
		if(this->statePath.empty())
		{
			throw GameStateError("State Path empty! No State to Get!");
		}
		return this->statePath.back();
	}
	//---------------------------------------------------------
	woptr<GameState> GameStateManager::getState(String const & id)
	{
		StateRegistryMap::iterator it = this->stateRegistry.find(id);
		if(it != this->stateRegistry.end())
		{
			return it->second;
		}

		return oNULL(GameState);
	}
	//---------------------------------------------------------
	String GameStateManager::getStatePathString()
	{
		String path = "";
		foreach(optr<GameState> current, statePath)
		{
			path += "/";
			path += current->getObjectID();
		}
		return path;
	}
	//---------------------------------------------------------
	void GameStateManager::resetState(String const & id)
	{
		this->stateRegistry[id]->deleteListeners();
	}
	//---------------------------------------------------------
	void GameStateManager::delListener(optr<EventListener> listener)
	{
		foreach(StateRegistryPair srp, stateRegistry)
		{
			srp.second->delListener(listener);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(GameStateManager)
		OCONSTRUCTOR0()
		OSINGLETON()
		OFUNC(pushState)
		OFUNC(popState)
		OFUNC(registerState)
		OFUNC(getCurrent)
		OFUNC(getCurrentStateID)
		OFUNC(getStatePathString)
#ifndef ORKIGE_NDS
		OFUNC(bind)
#endif
	OOBJECT_END
}
