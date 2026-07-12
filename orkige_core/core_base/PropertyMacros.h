/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertyMacros.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PropertyMacros_h__9_7_2026__16_00_00__
#define __PropertyMacros_h__9_7_2026__16_00_00__

//! @file PropertyMacros.h
//! @brief the BACKEND-NEUTRAL half of the property-reflection declaration
//! vocabulary. Each Meta backend header (Meta_None.h, Meta_Lua.h) includes
//! this and defines the public OPROPERTY* family in
//! terms of the *_REGISTER macros below. The None backend uses them verbatim
//! (registry-only); the Lua backend adds its sol2 property registration ON
//! TOP. This is what makes the neutral property registry populate in EVERY
//! scripting config, including ORKIGE_SCRIPTING=OFF - the point of the whole
//! reflection substrate.
//!
//! All *_REGISTER macros are used INSIDE an OrkigeMetaExport body, where OSelf
//! (the class being exported, via ORKIGETTI) is in scope. The get/set lambdas
//! downcast the type-erased Orkige::Object pointer to OSelf* and call the
//! field's real accessors; erasing through Object (not void) makes that
//! downcast compiler-adjusted, so it stays address-correct even when OSelf
//! lists a non-Object base first (multiple inheritance). The value conversion
//! goes through Orkige::PropertyReflect
//! (core_base/PropertyReflect.h) whose overloads the component TU has pulled in
//! (scalars from core, Orkige::Vec3/Quat/Color from the engine adapter header).

#include "core_base/TypeManager.h"
#include "core_base/PropertySchema.h"
#include "core_base/PropertyReflect.h"

#include <type_traits>

//! @brief compile-time guard shared by every *_REGISTER macro: a reflected
//! property may only be declared on an Object-derived type, because the
//! type-erased get/set downcast the Orkige::Object pointer back to the concrete
//! type - a non-Object OSelf would make that downcast ill-formed. Expands
//! inside an OrkigeMetaExport body where OSelf and Orkige::Object are complete.
#define ORKIGE_PROP_REQUIRE_OBJECT()												\
	static_assert(std::is_base_of_v<Orkige::Object, OSelf>,							\
		"reflected properties require the declaring type to derive from "			\
		"Orkige::Object (the type-erased accessors downcast an Object pointer)")

//! the reflected getter lambda for a scalar/math property
#define ORKIGE_PROP_GET(Getter)														\
	[](Orkige::Object const * orkigeObj) -> Orkige::PropertyValue					\
	{																				\
		return Orkige::PropertyReflect::pack(										\
			static_cast<OSelf const *>(orkigeObj)->Getter());						\
	}

//! the reflected setter lambda for a scalar/math property (deduces the field
//! type from the getter's return type, so it works for Vec3/Quat/... too)
#define ORKIGE_PROP_SET(Getter, Setter)												\
	[](Orkige::Object * orkigeObj, Orkige::PropertyValue const & orkigeValue)		\
	{																				\
		std::remove_cvref_t<decltype(												\
			static_cast<OSelf *>(orkigeObj)->Getter())> orkigeTemp{};				\
		Orkige::PropertyReflect::unpack(orkigeValue, orkigeTemp);					\
		static_cast<OSelf *>(orkigeObj)->Setter(orkigeTemp);						\
	}

//! neutral registration of a scalar/math property into the per-type schema
#define OPROPERTY_REGISTER(PropName, Kind, Getter, Setter, Flags)					\
	ORKIGE_PROP_REQUIRE_OBJECT();													\
	Orkige::TypeManager::getSingleton().registerProperty(							\
		OSelf::getClassTypeInfo().getId(),											\
		Orkige::PropertyDesc((PropName), (Kind), (Flags),							\
			ORKIGE_PROP_GET(Getter), ORKIGE_PROP_SET(Getter, Setter)));

//! @brief neutral registration of a READ-ONLY scalar/math property: a getter,
//! no setter (an empty PropertySetter marks the descriptor read-only). Used for
//! runtime telemetry that reflects but must not be assigned - velocity readback,
//! has-body, a script's started/error state. Combine with PROP_TRANSIENT to keep
//! such a property out of serialization while still exposing it to the debug
//! protocol, inspector and MCP off the ONE registry.
#define OPROPERTY_READONLY_REGISTER(PropName, Kind, Getter, Flags)					\
	ORKIGE_PROP_REQUIRE_OBJECT();													\
	Orkige::TypeManager::getSingleton().registerProperty(							\
		OSelf::getClassTypeInfo().getId(),											\
		Orkige::PropertyDesc((PropName), (Kind), (Flags),							\
			ORKIGE_PROP_GET(Getter), Orkige::PropertySetter()));

//! neutral registration of a scalar/math property carrying display metadata
//! (MetaExpr is an Orkige::PropertyMeta value - reserved for the inspector)
#define OPROPERTY_REGISTER_META(PropName, Kind, Getter, Setter, Flags, MetaExpr)	\
	{																				\
		ORKIGE_PROP_REQUIRE_OBJECT();												\
		Orkige::PropertyDesc orkigeDesc((PropName), (Kind), (Flags),				\
			ORKIGE_PROP_GET(Getter), ORKIGE_PROP_SET(Getter, Setter));				\
		orkigeDesc.meta = (MetaExpr);												\
		Orkige::TypeManager::getSingleton().registerProperty(						\
			OSelf::getClassTypeInfo().getId(), orkigeDesc);							\
	}

//! neutral registration of an Enum property (tagged with its EnumInfo key)
#define OPROPERTY_ENUM_REGISTER(PropName, EnumTypeName, Getter, Setter, Flags)		\
	ORKIGE_PROP_REQUIRE_OBJECT();													\
	Orkige::TypeManager::getSingleton().registerProperty(							\
		OSelf::getClassTypeInfo().getId(),											\
		Orkige::PropertyDesc::makeEnum((PropName), (EnumTypeName), (Flags),			\
			[](Orkige::Object const * orkigeObj) -> Orkige::PropertyValue			\
			{																		\
				return Orkige::PropertyReflect::packEnum((EnumTypeName),			\
					static_cast<OSelf const *>(orkigeObj)->Getter());				\
			},																		\
			[](Orkige::Object * orkigeObj, Orkige::PropertyValue const & orkigeValue)\
			{																		\
				std::remove_cvref_t<decltype(										\
					static_cast<OSelf *>(orkigeObj)->Getter())> orkigeTemp{};		\
				Orkige::PropertyReflect::unpackEnum(orkigeValue, orkigeTemp);		\
				static_cast<OSelf *>(orkigeObj)->Setter(orkigeTemp);				\
			}));

//! neutral registration of a reference property (AssetRef/ObjectRef). Getter
//! returns the id String, Setter takes the id String; Hint is the asset-kind
//! ("texture"/"mesh"/...) or object-type hint.
#define OPROPERTY_REF_REGISTER(PropName, RefKind, Hint, Getter, Setter, Flags)		\
	ORKIGE_PROP_REQUIRE_OBJECT();													\
	Orkige::TypeManager::getSingleton().registerProperty(							\
		OSelf::getClassTypeInfo().getId(),											\
		Orkige::PropertyDesc::makeReference((PropName), (RefKind), (Hint), (Flags),	\
			[](Orkige::Object const * orkigeObj) -> Orkige::PropertyValue			\
			{																		\
				return Orkige::PropertyReflect::packReference((RefKind), (Hint),	\
					static_cast<OSelf const *>(orkigeObj)->Getter());				\
			},																		\
			ORKIGE_PROP_SET(Getter, Setter)));

//! @brief neutral enum value<->label registration (identical in every backend).
//! Used inside an OrkigeMetaExport body where EnumType names an enum in scope;
//! populates the TypeManager enum registry so combo boxes and by-name enum
//! serialization can resolve labels later. Coexists with the sol OENUM_START
//! block (they are independent - one feeds Lua, this feeds the neutral registry).
#define OENUM_REGISTER_START(EnumTypeName, EnumType)								\
	{																				\
		Orkige::EnumInfo & orkigeEnumInfo =											\
			Orkige::TypeManager::getSingleton().registerEnum((EnumTypeName));		\
		typedef EnumType OrkigeReflectedEnum;

#define OENUM_REGISTER_VALUE(ValueName)												\
		orkigeEnumInfo.addValue(#ValueName,											\
			static_cast<long long>(OrkigeReflectedEnum::ValueName));

#define OENUM_REGISTER_END															\
	}

#endif //__PropertyMacros_h__9_7_2026__16_00_00__
