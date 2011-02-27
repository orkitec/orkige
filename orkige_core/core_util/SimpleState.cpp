/**************************************************************
	created:	2010/08/19 at 23:27
	filename: 	SimpleState.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_util/SimpleState.h"
#include "core_util/String.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SimpleState::SimpleState(String const & id) : Object(id)
	{
		this->resetNextState();	
	}
	//---------------------------------------------------------
	SimpleState::~SimpleState()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SimpleState::onEnter()
	{

	}
	//---------------------------------------------------------
	void SimpleState::onExit()
	{

	}
	//---------------------------------------------------------
	void SimpleState::onUpdate()
	{

	}
	//---------------------------------------------------------
	void SimpleState::onMessage(optr<Object> messageObject)
	{

	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OWRAPPER_START(SimpleState)
		OWRAPPER_CONSTRUCTOR1(String const &)
		OWRAPPER_FUNC(void,onEnter)
		OWRAPPER_FUNC(void,onExit)
		OWRAPPER_FUNC(void,onUpdate)
		OWRAPPER_FUNC1(void,onMessage,optr<Object>)
	OWRAPPER_END

	OVIRTUAL_OBJECT_IMPL(SimpleState)
		OVIRTUAL_CONSTRUCTOR1(String)
		OVIRTUAL_FUNC(onEnter)
		OVIRTUAL_FUNC(onExit)
		OVIRTUAL_FUNC(onUpdate)
		OVIRTUAL_FUNC(onMessage)
		OVIRTUAL_NOTWRAPPED_FUNC(setNextState)
		OVIRTUAL_NOTWRAPPED_FUNCCR(getStateID)
		OVIRTUAL_NOTWRAPPED_FUNCCR(getNextStateID)
		OVIRTUAL_NOTWRAPPED_FUNCCR(getPreviousStateID)
	OOBJECT_END
}


namespace Orkige
{
	//--public:----------------------------------
 
	//--protected:----------------------------------

	//--private:----------------------------------

}