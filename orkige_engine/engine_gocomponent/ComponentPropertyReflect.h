/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	ComponentPropertyReflect.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ComponentPropertyReflect_h__9_7_2026__16_00_00__
#define __ComponentPropertyReflect_h__9_7_2026__16_00_00__

#include "core_base/PropertyReflect.h"
#include "engine_render/RenderMath.h"

namespace Orkige
{
	//! @brief the ENGINE-SIDE half of the OPROPERTY adapter layer: pack()/unpack()
	//! overloads for the Orkige math vocabulary (Vec3/Quat/Color). They live here
	//! - not in core - because the math types ARE Ogre types (RenderMath.h
	//! alias): the Ogre <-> Ogre-free-POD copy must stay inside the render zone
	//! so core never sees Ogre and the containment lint stays green. A component
	//! .cpp that declares OPROPERTY on a Vec3/Quat/Color field includes THIS
	//! header before its OrkigeMetaExport block, so qualified lookup of
	//! Orkige::PropertyReflect::pack/unpack picks these overloads up.
	//! @remarks the layouts are identical (3x/4x float), so every conversion is a
	//! trivial field copy.
	namespace PropertyReflect
	{
		inline PropertyValue pack(Vec3 const & value)
		{
			PropVec3 pod;
			pod.x = value.x;
			pod.y = value.y;
			pod.z = value.z;
			return PropertyValue::makeVec3(pod);
		}
		inline PropertyValue pack(Quat const & value)
		{
			PropQuat pod;
			pod.w = value.w;
			pod.x = value.x;
			pod.y = value.y;
			pod.z = value.z;
			return PropertyValue::makeQuat(pod);
		}
		inline PropertyValue pack(Color const & value)
		{
			PropColor pod;
			pod.r = value.r;
			pod.g = value.g;
			pod.b = value.b;
			pod.a = value.a;
			return PropertyValue::makeColor(pod);
		}
		inline void unpack(PropertyValue const & value, Vec3 & out)
		{
			const PropVec3 pod = value.asVec3();
			out = Vec3(pod.x, pod.y, pod.z);
		}
		inline void unpack(PropertyValue const & value, Quat & out)
		{
			const PropQuat pod = value.asQuat();
			out = Quat(pod.w, pod.x, pod.y, pod.z);
		}
		inline void unpack(PropertyValue const & value, Color & out)
		{
			const PropColor pod = value.asColor();
			out = Color(pod.r, pod.g, pod.b, pod.a);
		}
	}
}

#endif //__ComponentPropertyReflect_h__9_7_2026__16_00_00__
