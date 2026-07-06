/**************************************************************
	created:	2010/07/26 at 13:04
	filename: 	Event.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __Event_h__26_7_2010__13_04_33__
#define __Event_h__26_7_2010__13_04_33__

#include "core_event/EventType.h"
#include "core_base/Object.h"
#include "core_util/Timer.h"

namespace Orkige
{
	//! Event with type and optional assigned data
	class ORKIGE_CORE_DLL Event : public Object
	{
		OOBJECT(Event,Object)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		EventType type;     //!< the identified type of the event
		optr<Object> data;	//!< optional event data
		//--- Methods -----------------------------------------
	public:
		//! constructor creates event with given name as EventType and no assigned data
		explicit inline Event( String const & inEventTypeName );
		//! constructor creates event with given name as EventType and given data
		explicit inline Event( String const & inEventTypeName, optr<Object> const & inData );
		//! constructor creates event with given EventType and no assigned data
		explicit inline Event( EventType const & inEventType );
		//! constructor creates event with given EventType and given data
		explicit inline Event( EventType const & inEventType, optr<Object> const & inData );
		//! copy constructor
		inline Event( Event const & o );
		//! destructor
		virtual ~Event();
		//! get Eventtype of this Event
		inline EventType const & getType() const;
		//! get assigned data if there is any
		inline optr<Object> getData() const;
		//! get assigned data if there is any and cast to giben type
		template<typename Type>
		inline optr<Type> getDataPtr() const;
		//! set data of this event
		inline void setData(optr<Object> const & inData);
	protected:
		//! protected default constructor
		Event() : type("") {}
	private:
	};
	//---------------------------------------------------------
	inline Event::Event( String const & inEventTypeName)
		: Object(inEventTypeName), type( inEventTypeName ), data(oNull<Object>())
	{
	}
	//---------------------------------------------------------
	inline Event::Event( String const & inEventTypeName, optr<Object> const & inData)
		: Object(inEventTypeName), type( inEventTypeName ), data(inData)
	{
	}
	//---------------------------------------------------------
	inline Event::Event( EventType const & inEventType )
		: Object(inEventType.getName()), type( inEventType ), data(oNull<Object>())
	{
	}
	//---------------------------------------------------------
	inline Event::Event( EventType const & inEventType, optr<Object> const & inData)
		: Object(inEventType.getName()), type( inEventType ), data(inData)
	{
	}
	//---------------------------------------------------------
	inline Event::Event( Event const & o )
		: Object(o) , type(o.type)	, data(o.data)	
	{
	}
	//---------------------------------------------------------
	EventType const & Event::getType() const
	{
		return this->type;
	}
	//---------------------------------------------------------
	inline optr<Object> Event::getData() const
	{
		return this->data;
	}
	//---------------------------------------------------------
	inline void Event::setData(optr<Object> const & inData) 
	{ 
		this->data = inData;
	}
	//---------------------------------------------------------
	template<typename Type>
	inline optr<Type> Event::getDataPtr() const
	{
		return std::static_pointer_cast<Type>(this->data);
	}
}

#endif //__Event_h__26_7_2010__13_04_33__