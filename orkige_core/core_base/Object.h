/********************************************************************
	created:	Monday 2010/08/09 at 18:40
	filename: 	Object.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __Object_h__9_8_2010__18_40_46__
#define __Object_h__9_8_2010__18_40_46__

#include "core_base/Interface.h"
#include "core_serialization/ISerializeable.h"
#include "core_util/AttributeHolder.h"

namespace Orkige
{
	class Object;

	//! AttributeHolder holds Objects mapped to Strings
	typedef AttributeHolder<String, Object> ObjectAttributeHolder;
	//! maps a string to a Object optr
	typedef std::pair<String,optr<Object> > ObjectPair;
	//! map of Objects with String ids
	typedef std::map<String,optr<Object> > ObjectMap;
	
	//! @brief	base Object
	//!
	//!	Object Types provide a simple base for serialization and script export
	//!	also Objects can have attributes which allow programming like in script languages
	//!	you don't need to declare Attributes in headers but can use theme everywhere
	//!	Attributes are automatic exposed to Scripting and are automatic serialized
	//!	so this allows fast prototypic programming
	//!	Objects have also an optional String which can serve as some form of very simple Object recognition mechanism
	class ORKIGE_DLL Object : public ObjectAttributeHolder
	{
		OOBJECT(Object, ObjectAttributeHolder);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		String objectId; //!< optional id of this object
	private:
		//--- Methods -----------------------------------------------
	public:
		//! DefaultConstructor initializes an Object with no assigned ID	 
		inline Object();

		//! Initialize an Object with given ID	 
		inline explicit Object(String const & id);

		//! Copy Constructor
		inline Object(Object const & other);

		//! Destructor
		virtual ~Object();

		//! get the ID of an Object (empty String if no id is assigned)
		inline String const & getObjectID() const;

		//--- SERIALIZATION ---
		//! save this object to Archive
		virtual void save(optr<IArchive> const & ar);
		//! load this Object from Archive
		virtual void load(optr<IArchive> const & ar);
	protected:
	private:
	};
	//--------------------------------------------------------------- 
	inline Object::Object() : ObjectAttributeHolder() 
	{
	};
	//--------------------------------------------------------------- 
	inline Object::Object(String const & id) : objectId(id)
	{
	};
	//---------------------------------------------------------------
	inline Object::Object(Object const & other) : ObjectAttributeHolder(other), objectId(other.objectId) 
	{
	};
	//---------------------------------------------------------------
	inline String const & Object::getObjectID() const 
	{	
		return this->objectId;	
	}
	//---------------------------------------------------------------
}

#endif //__Object_h__9_8_2010__18_40_46__

