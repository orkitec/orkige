/********************************************************************
	created:	Monday 2010/08/09 at 18:49
	filename: 	TypeManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_base/TypeManager.h"
#include "core_util/optr.h"

namespace Orkige
{
	IMPL_OSINGLETON_GETCREATE(TypeManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TypeManager::TypeManager()
	{
		oInfo("...TypeManager created!...");
	}
	//---------------------------------------------------------
	TypeManager::~TypeManager()
	{
		oInfo("\t...TypeManager destroyed!...");
	}
	//---------------------------------------------------------
	void TypeManager::registerProperty(TypeInfo::TypeId typeId,
		PropertyDesc const & desc)
	{
		this->mSchemas[typeId].add(desc);
	}
	//---------------------------------------------------------
	PropertySchema const * TypeManager::getPropertySchema(
		TypeInfo::TypeId typeId) const
	{
		std::map<TypeInfo::TypeId, PropertySchema>::const_iterator it =
			this->mSchemas.find(typeId);
		return (it == this->mSchemas.end()) ? NULL : &it->second;
	}
	//---------------------------------------------------------
	EnumInfo & TypeManager::registerEnum(String const & enumTypeName)
	{
		std::map<String, EnumInfo>::iterator it =
			this->mEnums.find(enumTypeName);
		if (it == this->mEnums.end())
		{
			it = this->mEnums.insert(std::make_pair(enumTypeName,
				EnumInfo(enumTypeName))).first;
		}
		return it->second;
	}
	//---------------------------------------------------------
	EnumInfo const * TypeManager::findEnum(String const & enumTypeName) const
	{
		std::map<String, EnumInfo>::const_iterator it =
			this->mEnums.find(enumTypeName);
		return (it == this->mEnums.end()) ? NULL : &it->second;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}