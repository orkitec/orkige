/********************************************************************
	created:	Monday 2010/08/09 at 18:50
	filename: 	Value.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __Value_h__9_8_2010__18_50_23__
#define __Value_h__9_8_2010__18_50_23__

#include "core_base/Object.h"

namespace Orkige
{
	//! wrapper for non Object Types
	template<typename Type>
	class Value : public Object
	{
		OOBJECT_EXPORT(Value<Type>,Object, ORKIGE_CORE_DLL);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		Type value;	//!< wrapped value
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor takes wrapped object
		inline Value(Type const & val)
			: Object(),value(val)						{					};

		//! constructor takes id and wrapped object
		inline Value(String const & id,const Type& val)
			: Object(id),value(val)						{					};

		//! destructor
		virtual ~Value()								{					};

		//! get wrapped Value
		inline void setValue(Type const & v)			{	this->value = v;};

		//! get wrapped Value
		inline Type const & getValue()					{	return value;	};

		//! save wrapped value to Archive
		virtual void save(optr<IArchive> const & ar)	
		{	
			Object::save(ar);
			ar << value;	
		};

		//! load wrapped value from Archive
		virtual void load(optr<IArchive> const & ar)	
		{
			Object::load(ar);
			ar >> value;	
		};
	protected:
	private:
		//! Default constructor for TypeManager
		Value(){}
	};
	//---------------------------------------------------------------
	//! wrap any type into Value Object
	template<class Type> 
	inline optr<Value<Type> > wrapValue(Type const & val)
	{
		return onew(new Value<Type>(val));
	}
	//---------------------------------------------------------------
}

#endif //__Value_h__9_8_2010__18_50_23__
