/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertyValue.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PropertyValue_h__9_7_2026__16_00_00__
#define __PropertyValue_h__9_7_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <cstdint>

namespace Orkige
{
	/** \addtogroup Base
	*  @{ */

	//! @brief the discriminant of a reflected PropertyValue - the closed set of
	//! value shapes the neutral property registry can carry. Deliberately
	//! mirrors CVarType (core_debug/CVarManager.h) for the scalar cases so the
	//! console, cvars and reflected properties speak ONE canonical-string
	//! dialect, extended with the math + reference shapes a component field
	//! needs. The math kinds store PLAIN PODs (PropVec3/PropQuat/PropColor):
	//! core never sees Ogre::* - the Orkige::Vec3 <-> PropVec3 adapter lives
	//! engine-side (engine_gocomponent/ComponentPropertyReflect.h), keeping the
	//! renderer-containment lint satisfied.
	enum class PropertyKind
	{
		Int,		//!< a signed integer
		Float,		//!< a single-precision float
		Bool,		//!< a boolean
		String,		//!< free-form text
		Enum,		//!< an integer value tagged with its enum-type name (@see enumTypeName)
		Vec3,		//!< a 3-float vector (position/scale/direction)
		Quat,		//!< a 4-float quaternion (w,x,y,z rotation)
		Color,		//!< a 4-float RGBA colour (components 0..1)
		AssetRef,	//!< an asset reference: an id string + an asset-kind hint
		ObjectRef	//!< an object reference: an object-id string + a type hint
	};

	//! @brief POD 3-float vector - the Ogre-free carrier for a Vec3 property.
	//! Layout matches Orkige::Vec3 (3x float) so the engine-side adapter is a
	//! trivial component copy.
	struct PropVec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	//! @brief POD quaternion (w,x,y,z) - the Ogre-free carrier for a Quat property
	struct PropQuat
	{
		float w = 1.0f;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	//! @brief POD RGBA colour - the Ogre-free carrier for a Color property
	struct PropColor
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	//! @brief the reflected-property value currency: a small tagged variant over
	//! the PropertyKind set with a CANONICAL STRING form (its get/set borrow the
	//! CVarManager coerce/format dialect) so the same value crosses the debug
	//! protocol wire, feeds the inspector and round-trips through the registry.
	//! @remarks Ogre-free by design (core must not see engine math types). Enum
	//! carries its value AND the enum-type name (so value<->label lookup through
	//! the EnumInfo registry stays possible later); the reference kinds carry an
	//! id string plus a target-type hint (asset-kind / object-type). Display
	//! metadata (min/max/step/tooltip/category) and the per-property flags word
	//! live on the DESCRIPTOR (PropertyDesc), not here - a value is just data.
	class ORKIGE_CORE_DLL PropertyValue
	{
		//--- Variables ---------------------------------------
	private:
		PropertyKind	mKind;		//!< the active variant tag
		long long		mInt;		//!< Int / Enum value / Bool (0|1)
		double			mFloat;		//!< Float
		float			mVec[4];	//!< Vec3(x,y,z) | Quat(w,x,y,z) | Color(r,g,b,a)
		String			mText;		//!< String value | reference id
		String			mTypeName;	//!< Enum type name | reference target-type hint
		//--- Methods -----------------------------------------
	public:
		//! construct an Int(0)
		PropertyValue();

		//--- factories (one per kind) ------------------------
		static PropertyValue makeInt(long long value);
		static PropertyValue makeFloat(double value);
		static PropertyValue makeBool(bool value);
		static PropertyValue makeString(String const & value);
		//! an enum value tagged with the enum-type name (for later value<->label)
		static PropertyValue makeEnum(String const & enumTypeName, long long value);
		static PropertyValue makeVec3(PropVec3 const & value);
		static PropertyValue makeQuat(PropQuat const & value);
		static PropertyValue makeColor(PropColor const & value);
		//! an asset reference: the stable asset id + an asset-kind hint ("texture"/"mesh"/...)
		static PropertyValue makeAssetRef(String const & assetKind, String const & id);
		//! an object reference: the object id + a target-type hint
		static PropertyValue makeObjectRef(String const & targetType, String const & id);

		//! the active variant tag
		PropertyKind kind() const { return this->mKind; }

		//--- typed reads (best-effort coercion, never throw) -
		long long asInt() const;
		double asFloat() const;
		bool asBool() const;
		//! the canonical string form (@see toString)
		String asString() const;
		PropVec3 asVec3() const;
		PropQuat asQuat() const;
		PropColor asColor() const;

		//! for Enum: the enum-type name this value belongs to ("" otherwise)
		String const & enumTypeName() const { return this->mTypeName; }
		//! for AssetRef/ObjectRef: the referenced id ("" otherwise)
		String const & referenceId() const { return this->mText; }
		//! for AssetRef/ObjectRef: the target-type hint (asset-kind / object-type)
		String const & referenceHint() const { return this->mTypeName; }

		//! @brief the canonical string form of the value: Int/Enum -> the integer,
		//! Float -> %.9g, Bool -> "1"/"0", Vec3 -> "x y z", Quat -> "w x y z",
		//! Color -> "r g b a", String/AssetRef/ObjectRef -> the text verbatim.
		String toString() const;

		//! @brief parse a string into THIS value keeping its current kind (and the
		//! kind's enum-type/reference hint). Returns false with *outError set (and
		//! the value unchanged) when the string does not parse for the kind.
		bool fromString(String const & text, String * outError = 0);

	private:
		//! private full constructor used by the factories
		PropertyValue(PropertyKind kind);
	};

	/** @} End of "addtogroup Base"*/
}

#endif //__PropertyValue_h__9_7_2026__16_00_00__
