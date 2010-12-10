/********************************************************************
	created:	Monday 2010/08/16 at 16:52
	filename: 	GameStateManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __GameStateManager_h__16_8_2010__16_52_47__
#define __GameStateManager_h__16_8_2010__16_52_47__

#include "core_game/GameState.h" 
#include "core_util/Singleton.h"

namespace Orkige
{
	//! map with String key and GameState pointers
	typedef std::map<String,optr<GameState> > StateRegistryMap;
	//! pair with String key and GameState pointer
	typedef std::pair<String,optr<GameState> > StateRegistryPair;
	//! manages GameState's in Hierarchical StateMachine
	class ORKIGE_DLL GameStateManager : public Singleton<GameStateManager> , public Object
	{
		OOBJECT(GameStateManager,Object);
		DECL_OSINGLETON(GameStateManager);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when GameState changes
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(GameStateChangedEvent);
		//! error that gets thrown on errors regarding GameState requests
		class GameStateError : public std::runtime_error 
		{
		public:
			//! constructor
			GameStateError(const String& msg = "") : std::runtime_error(msg) {}
		};
	protected:
	private:

		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		StateRegistryMap stateRegistry;
		std::list<optr<GameState> > statePath;
		String previousStateId;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		GameStateManager();
		//! destructor
		~GameStateManager();
		//! register a GameState
		void registerState(optr<GameState> state);
		//! pushes a state to the state stack and sets it active
		void pushState(String  const & id);
		//! pop current state from the state path
		void popState();
		//! set a state immediate pop current state and push the new on
		void setState(String const & id);
		//! get current active GameState
		woptr<GameState> getCurrent();
		//! get state with given id
		woptr<GameState> getState(String const & id);
		//! get id of current GameState
		inline String getCurrentStateID();
		//! get id of previous GameState
		inline String const & getPreviousStateID();
		//! get / seperated string of active GameState Stack
		String getStatePathString();
		//! delete given EventListener from all GameState's
		void delListener(optr<EventListener> listener);
		//! remove all listeners from state with given id
		void resetState(String  const & id);
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline String GameStateManager::getCurrentStateID() 
	{
		try
		{
			return this->getCurrent().lock()->getObjectID();
		}
		catch (GameStateManager::GameStateError const &)
		{
			return "";	
		}			
	}
	//---------------------------------------------------------------
	inline String const & GameStateManager::getPreviousStateID() 
	{
		return this->previousStateId;
	}
}

#endif //__GameStateManager_h__16_8_2010__16_52_47__
