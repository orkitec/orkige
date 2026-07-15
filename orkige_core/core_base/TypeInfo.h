/********************************************************************
	created:	Monday 2010/08/09 at 18:45
	filename: 	TypeInfo.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __TypeInfo_h__9_8_2010__18_45_29__
#define __TypeInfo_h__9_8_2010__18_45_29__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Hash.h"
#include "core_util/String.h"
#include <list>

namespace Orkige
{
	//! Custom RTTI TypeInfo implementation
	class ORKIGE_CORE_DLL TypeInfo
	{
	public:
		//! TypeInfo Meta Data
		static void OrkigeMetaExport(const char * currentOrkigeModuleName);
		//--- Types -------------------------------------------------
	public:
		typedef std::size_t TypeId;	//!< used HashType
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		/*const*/ TypeId id;		//!< hash id of this Type
		/*const*/ String name;		//!< name of this Type
	private:
		//--- Methods -----------------------------------------------
	public:
		//! create a TypeInfo with given name as String
		inline explicit TypeInfo(String const & cname);

		//! copy constructor
		inline explicit TypeInfo(TypeInfo const & other);

		//! get name of this Type
		inline String const & getName() const;

		//! get Hash id of this type
		inline TypeId const & getId() const;

		//! check if Type is equal to given TypeInfo 
		inline bool isEqual(TypeInfo const & other) const;

		//! check if Type is equal to given TypeInfo 
		inline bool isNotEqual(TypeInfo const & other) const;

		//! @see TypeInfo::isEqual
		inline bool operator==(TypeInfo const & other) const;

		//! @see TypeInfo::isNotEqual
		inline bool operator!=(TypeInfo const & other) const;

		//! @see std::string <
		inline bool operator<(TypeInfo const & other) const;
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline TypeInfo::TypeInfo(String const & cname) 
		: id(BoostHashFromString(cname))
		, name(cname)
	{
	}
	//---------------------------------------------------------------
	inline TypeInfo::TypeInfo(TypeInfo const & other) 
		: id(other.id) 
		, name(other.name)

	{
	}
	//---------------------------------------------------------------
	inline String const & TypeInfo::getName() const
	{
		return this->name;
	}
	//---------------------------------------------------------------
	inline TypeInfo::TypeId const & TypeInfo::getId() const
	{
		return this->id;
	}
	//---------------------------------------------------------------
	inline bool TypeInfo::isEqual(TypeInfo const & other) const
	{
		return this->id == other.id;
	}
	//---------------------------------------------------------------
	inline bool TypeInfo::isNotEqual(TypeInfo const & other) const
	{
		return this->id != other.id;
	}
	//---------------------------------------------------------------
	inline bool TypeInfo::operator==(TypeInfo const & other) const
	{
		return this->isEqual(other);
	}
	//---------------------------------------------------------------
	inline bool TypeInfo::operator!=(TypeInfo const & other) const
	{
		return this->isNotEqual(other);
	}
	//---------------------------------------------------------------
	inline bool TypeInfo::operator<(TypeInfo const & other) const
	{
		return this->id < other.id;
	}
	//---------------------------------------------------------------
	typedef std::list<TypeInfo> TypeInfoList;		//!< list of TypeInfos
	//---------------------------------------------------------------

}

#endif //__TypeInfo_h__9_8_2010__18_45_29__
