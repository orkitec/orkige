/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertyReflect.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PropertyReflect_h__9_7_2026__16_00_00__
#define __PropertyReflect_h__9_7_2026__16_00_00__

#include "core_base/PropertyValue.h"

namespace Orkige
{
	//! @brief the OPROPERTY adapter layer: overloaded pack()/unpack() free
	//! functions that convert a concrete C++ field value to/from a
	//! PropertyValue. The OPROPERTY_REGISTER macro (core_base/PropertyMacros.h)
	//! emits lambdas that call these by the field's DEDUCED type, so the macro
	//! stays type-agnostic and new value types are added by declaring a new
	//! overload pair - NOT by touching the macro.
	//! @remarks core covers the scalar + POD-math + reference set; the engine
	//! adds Orkige::Vec3/Quat/Color overloads in
	//! engine_gocomponent/ComponentPropertyReflect.h (Ogre-containment: the
	//! Ogre<->POD copy stays engine-side). Qualified lookup at the macro's
	//! expansion point picks up whichever overloads the TU has included.
	namespace PropertyReflect
	{
		//--- pack: field value -> PropertyValue ------------------
		inline PropertyValue pack(int value)			{ return PropertyValue::makeInt(value); }
		inline PropertyValue pack(long value)			{ return PropertyValue::makeInt(static_cast<long long>(value)); }
		inline PropertyValue pack(long long value)		{ return PropertyValue::makeInt(value); }
		inline PropertyValue pack(unsigned value)		{ return PropertyValue::makeInt(static_cast<long long>(value)); }
		inline PropertyValue pack(unsigned long value)	{ return PropertyValue::makeInt(static_cast<long long>(value)); }
		inline PropertyValue pack(float value)			{ return PropertyValue::makeFloat(static_cast<double>(value)); }
		inline PropertyValue pack(double value)			{ return PropertyValue::makeFloat(value); }
		inline PropertyValue pack(bool value)			{ return PropertyValue::makeBool(value); }
		inline PropertyValue pack(String const & value)	{ return PropertyValue::makeString(value); }
		inline PropertyValue pack(char const * value)	{ return PropertyValue::makeString(value ? String(value) : String()); }
		inline PropertyValue pack(PropVec3 const & value)	{ return PropertyValue::makeVec3(value); }
		inline PropertyValue pack(PropQuat const & value)	{ return PropertyValue::makeQuat(value); }
		inline PropertyValue pack(PropColor const & value)	{ return PropertyValue::makeColor(value); }

		//--- unpack: PropertyValue -> field value ----------------
		inline void unpack(PropertyValue const & value, int & out)			{ out = static_cast<int>(value.asInt()); }
		inline void unpack(PropertyValue const & value, long & out)			{ out = static_cast<long>(value.asInt()); }
		inline void unpack(PropertyValue const & value, long long & out)		{ out = value.asInt(); }
		inline void unpack(PropertyValue const & value, unsigned & out)		{ out = static_cast<unsigned>(value.asInt()); }
		inline void unpack(PropertyValue const & value, unsigned long & out)	{ out = static_cast<unsigned long>(value.asInt()); }
		inline void unpack(PropertyValue const & value, float & out)		{ out = static_cast<float>(value.asFloat()); }
		inline void unpack(PropertyValue const & value, double & out)		{ out = value.asFloat(); }
		inline void unpack(PropertyValue const & value, bool & out)			{ out = value.asBool(); }
		inline void unpack(PropertyValue const & value, String & out)		{ out = value.asString(); }
		inline void unpack(PropertyValue const & value, PropVec3 & out)		{ out = value.asVec3(); }
		inline void unpack(PropertyValue const & value, PropQuat & out)		{ out = value.asQuat(); }
		inline void unpack(PropertyValue const & value, PropColor & out)		{ out = value.asColor(); }

		//! @brief pack an enum value tagged with its enum-type name. Kept SEPARATE
		//! from pack() so an unscoped enum never silently binds to pack(int) (its
		//! implicit int promotion) - OPROPERTY_ENUM routes here explicitly.
		template<typename EnumType>
		inline PropertyValue packEnum(String const & enumTypeName, EnumType value)
		{
			return PropertyValue::makeEnum(enumTypeName,
				static_cast<long long>(value));
		}
		//! @brief unpack an enum value (the reverse of packEnum)
		template<typename EnumType>
		inline void unpackEnum(PropertyValue const & value, EnumType & out)
		{
			out = static_cast<EnumType>(value.asInt());
		}
		//! @brief pack a reference id string with its kind (AssetRef/ObjectRef)
		//! and target-type hint
		inline PropertyValue packReference(PropertyKind kind, String const & hint,
			String const & id)
		{
			return kind == PropertyKind::ObjectRef
				? PropertyValue::makeObjectRef(hint, id)
				: PropertyValue::makeAssetRef(hint, id);
		}
	}
}

#endif //__PropertyReflect_h__9_7_2026__16_00_00__
