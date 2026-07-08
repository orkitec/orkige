/**************************************************************
	created:	2010/08/20 at 0:04
	filename: 	SimpleStateMachine.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_util/SimpleStateMachine.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SimpleStateMachine::SimpleStateMachine()
	{
	}
	//---------------------------------------------------------
	SimpleStateMachine::~SimpleStateMachine()
	{
	}
	//---------------------------------------------------------
	bool SimpleStateMachine::addState(optr<SimpleState> state)
	{
		String stateId = state->getObjectID();
		SimpleStateRegistry::iterator it = this->registry.find(stateId);

		if(it != this->registry.end())
			return false;

		this->registry[stateId] = state;
		return true;
	}
	//---------------------------------------------------------
	bool SimpleStateMachine::setState(String const & stateId)
	{
		SimpleStateRegistry::iterator it = this->registry.find(stateId);

		if(it == this->registry.end())
			return false;

		String previousStateID;
		if(this->currentState.get())
		{
			this->currentState->onExit();
			this->currentState->resetNextState();
			previousStateID = this->currentState->getStateID();
		}
		this->currentState = it->second;
		this->currentState->setPreviousState(previousStateID);
		this->currentState->onEnter();
		return true;
	}
	//---------------------------------------------------------
	void SimpleStateMachine::sendMessage(optr<Object> messageObject)
	{
		oAssert(this->currentState.get());
		this->currentState->onMessage(messageObject);
	}
	//---------------------------------------------------------
	void SimpleStateMachine::update()
	{
		if(this->currentState)
		{
			oAssert(this->currentState.get());
			if(this->currentState->hasTransition())
				this->setState(this->currentState->getNextStateID());
			this->currentState->onUpdate();	
		}

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SimpleStateMachine)
		OCONSTRUCTOR0()
		OFUNC(setState)
		OFUNC(addState)
		OFUNC(sendMessage)
		OFUNC(update)
		OFUNCCR(getCurrentStateID)
	OOBJECT_END
}
