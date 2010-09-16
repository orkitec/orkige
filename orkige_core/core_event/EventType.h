/**************************************************************
	created:	2010/07/26 at 15:22
	filename: 	EventType.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __EventType_h__26_7_2010__15_22_26__
#define __EventType_h__26_7_2010__15_22_26__

#include "core_base/Meta.h"
#include "core_base/Interface.h"
#include "core_util/optr.h"

namespace Orkige
{
	//! Data necessary to register and event classification
	class ORKIGE_DLL EventType : public Interface, public TypeInfo
	{
		OOBJECT2(EventType,Interface,TypeInfo)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! create event type with given name @see TypeInfo::TypeInfo
		explicit inline EventType( String const & id );
		//! copy constructor
		inline EventType( EventType const & other );
		//! sorting operator
		inline bool operator< ( EventType const & o ) const;
		//! @see TypeInfo::isEqual
		inline bool operator == ( EventType const & o ) const;
		//! assignment operator
		inline EventType& operator = (const EventType &o);
	protected:
	private:
		//! private default constructor
		inline EventType( );
	};
	//---------------------------------------------------------
	inline EventType::EventType( String const & id ) 	: TypeInfo(id)
	{
	}
	//---------------------------------------------------------
	inline EventType::EventType( EventType const & other ) 	: TypeInfo(other)
	{
	}
	//---------------------------------------------------------
	inline bool EventType::operator< ( EventType const & o ) const		
	{	
		return (this->getId() < o.getId());			
	}
	//---------------------------------------------------------
	inline bool EventType::operator == ( EventType const & o ) const		
	{	
		return (this->isEqual(o));			
	}
	//---------------------------------------------------------
	inline EventType& EventType::operator = (const EventType &o)
	{
		this->id = o.id;
		this->name = o.name;
		return *this;
	}
	//---------------------------------------------------------
	inline EventType::EventType( ) 	: TypeInfo("")
	{
	}
	//---------------------------------------------------------
	String const WildcardEventType("*");													//!< register for this EventType to listen to all Events
	//---------------------------------------------------------
	EventType::TypeId const WildcardEventTypeHash(BoostHashFromString(WildcardEventType));	//!< register for this EventType to listen to all Events
	//---------------------------------------------------------
	//! declares a static EventType
#	define DECL_EVENTTYPE(id) static ::Orkige::EventType id
	//! implements a static EventType
#	define IMPL_EVENTTYPE(id) ::Orkige::EventType id(#id)
	//! implements a static EventType owned by a class
#	define IMPL_OWNED_EVENTTYPE(OwnerClassName, id) ::Orkige::EventType OwnerClassName::id(#id)
}

#endif //__EventType_h__26_7_2010__15_22_26__
