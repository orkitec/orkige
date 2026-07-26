/********************************************************************
	created:	Monday 2010/08/09 at 18:49
	filename: 	TypeManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
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
	void TypeManager::registerParentType(TypeInfo::TypeId childId,
		TypeInfo::TypeId parentId)
	{
		// a type is never its own base (the Object root's OParent chain bottoms
		// out at itself in some macro expansions) - guard the self-link so the
		// walk in getInheritedPropertySchema terminates
		if (childId != parentId)
		{
			this->mParentTypes[childId] = parentId;
		}
	}
	//---------------------------------------------------------
	PropertySchema TypeManager::getInheritedPropertySchema(
		TypeInfo::TypeId typeId) const
	{
		// gather the base chain leaf->root, then compose BASE-FIRST so a
		// subclass property replaces an inherited one of the same name (the
		// frozen ScriptComponent/AtmosphereComponent `enabled` shadows the
		// generic base one this way)
		std::vector<TypeInfo::TypeId> chain;
		TypeInfo::TypeId current = typeId;
		// bounded by the map size: the parent links form a DAG rooted at a type
		// with no recorded parent, so at most one visit per known type
		std::size_t guard = this->mParentTypes.size() + 1;
		while (guard-- > 0)
		{
			chain.push_back(current);
			std::map<TypeInfo::TypeId, TypeInfo::TypeId>::const_iterator it =
				this->mParentTypes.find(current);
			if (it == this->mParentTypes.end())
			{
				break;
			}
			current = it->second;
		}
		PropertySchema composed;
		for (std::vector<TypeInfo::TypeId>::const_reverse_iterator it =
			chain.rbegin(); it != chain.rend(); ++it)
		{
			if (PropertySchema const * own = this->getPropertySchema(*it))
			{
				for (PropertyDesc const & desc : own->properties())
				{
					composed.add(desc);
				}
			}
		}
		return composed;
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