/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertySchema.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PropertySchema_h__9_7_2026__16_00_00__
#define __PropertySchema_h__9_7_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_base/PropertyValue.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Orkige
{
	/** \addtogroup Base
	*  @{ */

	//! @brief per-property behaviour flags (a bitmask). P0/P1 only STORE them;
	//! the consumers that act on them (transient-skip in serialization, the
	//! inspector's read-only lock) arrive in later phases. Reserved here so a
	//! declaration site can already annotate a property.
	enum PropertyFlags
	{
		PROP_NONE		= 0,
		PROP_READONLY	= 1 << 0,	//!< the value is displayed but not editable
		PROP_TRANSIENT	= 1 << 1,	//!< not serialized (runtime-only state)
		PROP_HIDDEN		= 1 << 2	//!< not shown in the inspector
	};

	//! @brief display metadata for a property - RESERVED for the inspector (a
	//! later phase). Inert in P0/P1; a declaration may already fill it in via
	//! the OPROPERTY_META macro so the schema carries it forward.
	struct PropertyMeta
	{
		bool	hasRange = false;	//!< true when min/max/step are meaningful
		float	minValue = 0.0f;	//!< slider lower bound
		float	maxValue = 0.0f;	//!< slider upper bound
		float	step = 0.0f;		//!< slider / drag increment (0 = default)
		String	tooltip;			//!< hover help ("" = none)
		String	category;			//!< inspector grouping ("" = default group)
	};

	//! type-erased getter: reads a property off a concrete instance (passed as a
	//! void* the descriptor casts back to the owning type) into a PropertyValue
	typedef std::function<PropertyValue(void const *)> PropertyGetter;
	//! type-erased setter: writes a PropertyValue back onto a concrete instance
	//! (an EMPTY setter marks a read-only property)
	typedef std::function<void(void *, PropertyValue const &)> PropertySetter;
	//! @brief RESERVED live-apply hook slot: fired after an accepted set so a
	//! component can react (the reflection cousin of CVar::onChange). Unused in
	//! P0/P1 - declarations leave it empty.
	typedef std::function<void(void *)> PropertyChangeHook;

	//! @brief one reflected property: a name + kind + type-erased get/set that
	//! bridge the neutral registry to the concrete component field. The get/set
	//! std::functions carry the concrete-type knowledge (a static_cast inside a
	//! lambda), so the registry itself stays type-agnostic.
	class ORKIGE_CORE_DLL PropertyDesc
	{
		//--- Variables ---------------------------------------
	public:
		String				name;			//!< the property name (its schema key)
		PropertyKind		kind;			//!< the value shape
		uint32_t			flags;			//!< PropertyFlags bitmask (reserved)
		String				enumTypeName;	//!< Enum: the EnumInfo key (value<->label)
		String				referenceHint;	//!< AssetRef/ObjectRef: asset-kind / object-type hint
		PropertyMeta		meta;			//!< reserved display metadata
		PropertyGetter		get;			//!< type-erased reader
		PropertySetter		set;			//!< type-erased writer (empty => read-only)
		PropertyChangeHook	onChange;		//!< reserved live-apply hook
		//--- Methods -----------------------------------------
	public:
		PropertyDesc();
		//! a scalar / math property (Int/Float/Bool/String/Vec3/Quat/Color)
		PropertyDesc(String const & name, PropertyKind kind, uint32_t flags,
			PropertyGetter get, PropertySetter set);
		//! @brief an Enum property tagged with its EnumInfo key so the inspector
		//! can resolve value<->label through the enum registry
		static PropertyDesc makeEnum(String const & name,
			String const & enumTypeName, uint32_t flags,
			PropertyGetter get, PropertySetter set);
		//! @brief a reference property (AssetRef/ObjectRef) with a target-type
		//! hint (asset-kind for assets, object-type for object refs)
		static PropertyDesc makeReference(String const & name, PropertyKind kind,
			String const & referenceHint, uint32_t flags,
			PropertyGetter get, PropertySetter set);

		//! is this property read-only (flagged so OR no setter provided)
		bool isReadOnly() const
		{
			return (this->flags & PROP_READONLY) != 0 || !this->set;
		}
		//! is a flag set
		bool hasFlag(PropertyFlags flag) const
		{
			return (this->flags & flag) != 0;
		}
	};

	//! @brief the ordered property list of ONE type - the static per-type schema
	//! TypeManager owns keyed by TypeId. Ordered (declaration order = inspector
	//! order); add() is idempotent by name so a re-run of a type's
	//! OrkigeMetaExport (module re-init) does not duplicate entries.
	class ORKIGE_CORE_DLL PropertySchema
	{
		//--- Variables ---------------------------------------
	private:
		std::vector<PropertyDesc> mProperties;	//!< declaration-ordered descriptors
		//--- Methods -----------------------------------------
	public:
		//! append desc, or replace the existing descriptor of the same name
		void add(PropertyDesc const & desc);
		//! the descriptor for name, or NULL when absent
		PropertyDesc const * find(String const & name) const;
		//! the ordered descriptors
		std::vector<PropertyDesc> const & properties() const
		{
			return this->mProperties;
		}
		//! number of declared properties
		std::size_t size() const { return this->mProperties.size(); }
		//! is the schema empty
		bool empty() const { return this->mProperties.empty(); }
	};

	//! @brief value<->label table of ONE enum type, keyed by enum-type name in
	//! the TypeManager enum registry. Populated by the OENUM_REGISTER macros in
	//! EVERY scripting config (needed later for combo boxes and by-name enum
	//! serialization). Ordered by declaration; addValue is idempotent by label.
	class ORKIGE_CORE_DLL EnumInfo
	{
		//--- Variables ---------------------------------------
	public:
		String name;	//!< the enum-type name (the registry key)
	private:
		std::vector<std::pair<String, long long> > mValues;	//!< label -> value, declaration-ordered
		//--- Methods -----------------------------------------
	public:
		EnumInfo() {}
		explicit EnumInfo(String const & enumName) : name(enumName) {}
		//! append label->value, or replace the value of an existing label
		void addValue(String const & label, long long value);
		//! the value for a label; false when the label is unknown
		bool valueOf(String const & label, long long & outValue) const;
		//! the FIRST label mapping to value; false when the value is unmapped
		bool labelOf(long long value, String & outLabel) const;
		//! the ordered label->value pairs
		std::vector<std::pair<String, long long> > const & values() const
		{
			return this->mValues;
		}
		//! number of enum values
		std::size_t size() const { return this->mValues.size(); }
	};

	/** @} End of "addtogroup Base"*/
}

#endif //__PropertySchema_h__9_7_2026__16_00_00__
