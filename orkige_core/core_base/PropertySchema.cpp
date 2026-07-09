/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertySchema.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_base/PropertySchema.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- PropertyDesc ----------------------------------------
	//---------------------------------------------------------
	PropertyDesc::PropertyDesc()
		: kind(PropertyKind::Int)
		, flags(PROP_NONE)
	{
	}
	//---------------------------------------------------------
	PropertyDesc::PropertyDesc(String const & name, PropertyKind kind,
		uint32_t flags, PropertyGetter get, PropertySetter set)
		: name(name)
		, kind(kind)
		, flags(flags)
		, get(std::move(get))
		, set(std::move(set))
	{
	}
	//---------------------------------------------------------
	PropertyDesc PropertyDesc::makeEnum(String const & name,
		String const & enumTypeName, uint32_t flags,
		PropertyGetter get, PropertySetter set)
	{
		PropertyDesc desc(name, PropertyKind::Enum, flags,
			std::move(get), std::move(set));
		desc.enumTypeName = enumTypeName;
		return desc;
	}
	//---------------------------------------------------------
	PropertyDesc PropertyDesc::makeReference(String const & name,
		PropertyKind kind, String const & referenceHint, uint32_t flags,
		PropertyGetter get, PropertySetter set)
	{
		PropertyDesc desc(name, kind, flags, std::move(get), std::move(set));
		desc.referenceHint = referenceHint;
		return desc;
	}
	//---------------------------------------------------------
	//--- PropertySchema --------------------------------------
	//---------------------------------------------------------
	void PropertySchema::add(PropertyDesc const & desc)
	{
		for (PropertyDesc & existing : this->mProperties)
		{
			if (existing.name == desc.name)
			{
				// idempotent: a re-run of the owning type's OrkigeMetaExport
				// (module re-init) refreshes, never duplicates
				existing = desc;
				return;
			}
		}
		this->mProperties.push_back(desc);
	}
	//---------------------------------------------------------
	PropertyDesc const * PropertySchema::find(String const & name) const
	{
		for (PropertyDesc const & desc : this->mProperties)
		{
			if (desc.name == name)
			{
				return &desc;
			}
		}
		return NULL;
	}
	//---------------------------------------------------------
	//--- EnumInfo --------------------------------------------
	//---------------------------------------------------------
	void EnumInfo::addValue(String const & label, long long value)
	{
		for (std::pair<String, long long> & existing : this->mValues)
		{
			if (existing.first == label)
			{
				existing.second = value;
				return;
			}
		}
		this->mValues.push_back(std::make_pair(label, value));
	}
	//---------------------------------------------------------
	bool EnumInfo::valueOf(String const & label, long long & outValue) const
	{
		for (std::pair<String, long long> const & entry : this->mValues)
		{
			if (entry.first == label)
			{
				outValue = entry.second;
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	bool EnumInfo::labelOf(long long value, String & outLabel) const
	{
		for (std::pair<String, long long> const & entry : this->mValues)
		{
			if (entry.second == value)
			{
				outLabel = entry.first;
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
}
