/**************************************************************
	created:	2010/08/20 at 0:09
	filename: 	SimpleStateMachine.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __SimpleStateMachine_h__20_8_2010__0_09_18__
#define __SimpleStateMachine_h__20_8_2010__0_09_18__

#include "core_base/Object.h"
#include "core_util/SimpleState.h"

namespace Orkige
{
	//! a simple state machine
	class ORKIGE_DLL SimpleStateMachine : public Object 
	{
		OOBJECT(SimpleStateMachine, Object)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		typedef std::map<String,optr<SimpleState> > SimpleStateRegistry;
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		SimpleStateRegistry registry;
		optr<SimpleState> currentState;
		//--- Methods -----------------------------------------
	public:
		//! constructor
		SimpleStateMachine();
		//! destructor
		virtual ~SimpleStateMachine();
		//! add a state to the registry
		bool addState(optr<SimpleState> state);
		//! set new state and leave current if there is any
		bool setState(String const & stateId);
		//! send a message to current state
		void sendMessage(optr<Object> messageObject);
		//! get id of current state
		inline String const & getCurrentStateID();
		//! update states and transitions
		void update();
	protected:
	private:
	};
	//---------------------------------------------------------
	inline String const &  SimpleStateMachine::getCurrentStateID() 
	{	
		oAssert(this->currentState.get()); 
		return this->currentState->getObjectID();	
	}
	//---------------------------------------------------------
}

#endif //__SimpleStateMachine_h__20_8_2010__0_09_18__
