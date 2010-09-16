/**************************************************************
	created:	2010/08/19 at 23:29
	filename: 	SimpleState.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SimpleState_h__19_8_2010__23_29_46__
#define __SimpleState_h__19_8_2010__23_29_46__

#include "core_base/Object.h"

namespace Orkige
{
	//! base State implementation for SimpleStateMachine
	class ORKIGE_DLL SimpleState : public Object
	{
		friend class SimpleStateMachine;
		OOBJECT_WD(SimpleState, Object)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		String nextStateID;
		String previousStateID;
		//--- Methods -----------------------------------------
	public:
		//! constructor takes id of the state
		explicit SimpleState(String const & stateId);
		//! destructor
		virtual ~SimpleState();
	protected:
		//! called when state is entered
		virtual void onEnter();
		//! called when state is left
		virtual void onExit();
		//! called when state receives a message
		virtual void onMessage(optr<Object> messageObject);
		//! called when state is updated
		virtual void onUpdate();
		//! get id of this state
		inline String const & getStateID();
		//! get id of previous state
		inline String const & getPreviousStateID();
		//! set id of next state
		inline String const & getNextStateID();
		//! set state that should be entered on next update cycle
		inline void setNextState(String const & stateId);
	private:
		inline void setPreviousState(String const & stateId);
		inline void resetNextState();
		inline bool hasTransition();
	};
	//---------------------------------------------------------
	inline String const & SimpleState::getStateID()					
	{	
		return this->getObjectID();			
	}
	//---------------------------------------------------------
	inline String const & SimpleState::getPreviousStateID()			
	{	
		return this->previousStateID;				
	}
	//---------------------------------------------------------
	inline String const & SimpleState::getNextStateID()				
	{	
		return this->nextStateID;			
	}
	//---------------------------------------------------------
	inline void SimpleState::setNextState(String const & stateId)	
	{	
		this->nextStateID = stateId;		
	}
	//---------------------------------------------------------
	inline void SimpleState::setPreviousState(String const & stateId)
	{	
		this->previousStateID = stateId;	
	}
	//---------------------------------------------------------
	inline void SimpleState::resetNextState()						
	{	
		this->nextStateID = String();		
	}
	//---------------------------------------------------------
	inline bool SimpleState::hasTransition()							
	{	
		return !this->nextStateID.empty();	
	}
	//---------------------------------------------------------
}

#endif //__SimpleState_h__19_8_2010__23_29_46__
