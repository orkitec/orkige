/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderMath.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderMath_h__8_7_2026__12_00_00__
#define __RenderMath_h__8_7_2026__12_00_00__

//! @file RenderMath.h
//! @brief the engine's math vocabulary - THE single renderer-math swap point
//! @remarks Decision (Docs/render-abstraction.md, "Math types"): Orkige math
//! IS Ogre math for now, exposed under engine-owned names. Classic OGRE and
//! Ogre-Next both provide these types with identical names, layout and
//! semantics, so the alias covers both planned backends at zero churn cost.
//! The documented later swap (required before a non-Ogre backend like
//! Filament can land): replace the aliases below with engine-owned structs of
//! identical layout (3x float etc.) - code that spells Orkige::Vec3 keeps
//! compiling, only this header changes. New code above engine_graphic should
//! therefore use THESE names, never Ogre::Vector3 directly.
//!
//! These includes are the math-only corner of OGRE (no scene graph, no
//! resources, no render system) and exist identically in classic OGRE 14
//! and Ogre-Next. Filament note: Filament uses filament::math::float3 /
//! quatf / mat4f (column-major, radians-only) - the future engine-owned
//! types convert trivially.

#include "engine_render/RenderPrerequisites.h"

#include <OgreVector2.h>
#include <OgreVector3.h>
#include <OgreVector4.h>
#include <OgreQuaternion.h>
#include <OgreMatrix3.h>
#include <OgreMatrix4.h>
#include <OgreColourValue.h>
#include <OgreMath.h>
#include <OgreRay.h>
#include <OgreAxisAlignedBox.h>

namespace Orkige
{
	//! scalar - Ogre::Real is float in both backends' default configuration
	typedef Ogre::Real			Real;
	typedef Ogre::Vector2		Vec2;		//!< 2D vector (UI, UV, screen coords)
	typedef Ogre::Vector3		Vec3;		//!< 3D vector (positions, scale, directions)
	typedef Ogre::Vector4		Vec4;		//!< 4D vector (rare; shader params)
	typedef Ogre::Quaternion	Quat;		//!< rotation
	typedef Ogre::Matrix3		Mat3;		//!< 3x3 rotation/axes matrix
	typedef Ogre::Matrix4		Mat4;		//!< 4x4 transform matrix
	typedef Ogre::Affine3		Affine3;	//!< affine 3x4 transform (TRS decomposition - editor gizmo)
	typedef Ogre::ColourValue	Color;		//!< RGBA colour, components 0..1
	typedef Ogre::Radian		Radian;		//!< angle in radians (explicit unit type)
	typedef Ogre::Degree		Degree;		//!< angle in degrees (explicit unit type)
	typedef Ogre::Ray			Ray3;		//!< origin + direction (picking)
	typedef Ogre::AxisAlignedBox AABB;		//!< axis-aligned bounding box
}

#endif //__RenderMath_h__8_7_2026__12_00_00__
