/**************************************************************
	created:	2010/08/15 at 15:51
	filename: 	GameState.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __GameState_h__15_8_2010__15_51_24__
#define __GameState_h__15_8_2010__15_51_24__

#include "core_event/GlobalEventManager.h"
#include "core_util/foreach.h"

namespace Orkige
{
	class GameStateManager;

	//! base state for GameStateManager
	class ORKIGE_DLL GameState : public Object
	{
		friend class GameStateManager;
		OOBJECT_WD(GameState,Object)
		//--- Types -------------------------------------------
	public:
	protected:
		typedef std::vector<optr<EventListener> >			EventListenerVector; //!< vector with pointers to EventListeners
		typedef std::map<EventType, EventListenerVector >	EventTypeListenerMap;//!< maps EventTypes to EventListenerVectors
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		EventTypeListenerMap listeners;					//!< added EventListeners
		String transitionState;							//!< if set GameStateManager goes to this GameState on next update cycle
		String pushState;								//!< if set GameStateManager goes to this sub GameState on next update cycle
		bool popState;									//!< if set GameStateManager leaves current sub GameState on next update cycle
	private:
		//--- Methods -----------------------------------------
	public:
		//! state with given id
		explicit GameState(String const & id);
		//! destructor
		virtual ~GameState();
		//! @brief sets a state that should be entered on the next update cycle
		void setTransition(String const & id);
		void setPush(String const & id);
		void setPop();
	protected:
		//! @brief overridable when GameState is entered and is not and StateStack
		virtual void onEnter();
		//! @brief overridable when GameState is entered and was already on StateStack
		virtual void onReturn();
		//! @brief overridable when GameState is left but stays in StateStack
		virtual void onLeave();
		//! @brief overridable when GameState is removed from StateStack
		virtual void onExit();

		//! @brief register a Event
		template<class F,class T>
		void registerEvent(EventType const & eventType, F const & handlerFunction, T * const handlerClass);
		//! @brief add a EventListener for given EventType
		void addListener(optr<EventListener> inListener,EventType const & eventType);
		//! @brief enable all added EventListeners
		void enableListeners();
		//! @brief disable all added EventListeners
		void disableListeners();
		//! @brief delete all added EventListeners
		void deleteListeners();
		//! @brief delete given EventListener
		void delListener(optr<EventListener>);
		//! @brief FrameListener for for updating the TaskQueue
		bool onApplicationUpdate(Event const & event);
	private:
	};
	//---------------------------------------------------------
	template<class F,class T>
	void GameState::registerEvent(EventType const & eventType, F const & handlerFunction, T * const handlerClass)
	{
		this->addListener(createEventListenerPtr(handlerFunction, handlerClass), eventType);
	}
	//---------------------------------------------------------
}

#endif //__GameState_h__15_8_2010__15_51_24__
