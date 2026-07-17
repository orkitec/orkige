/********************************************************************
	created:	Thursday 2026/07/17 at 12:00
	filename: 	SkinnedRig.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file SkinnedRig.cpp
//! @brief pure helpers of the backend-neutral skinned-rig description

#include "engine_render/SkinnedRig.h"

#include <cmath>

namespace Orkige
{
	namespace
	{
		//! quaternion-rotate a vector (q * v * q^-1, unit q)
		SkinnedRig::Vec3 rotate(SkinnedRig::Quat const & q,
			SkinnedRig::Vec3 const & v)
		{
			// t = 2 * cross(q.xyz, v); v' = v + q.w * t + cross(q.xyz, t)
			const float tx = 2.0f * (q.y * v.z - q.z * v.y);
			const float ty = 2.0f * (q.z * v.x - q.x * v.z);
			const float tz = 2.0f * (q.x * v.y - q.y * v.x);
			return SkinnedRig::Vec3{
				v.x + q.w * tx + (q.y * tz - q.z * ty),
				v.y + q.w * ty + (q.z * tx - q.x * tz),
				v.z + q.w * tz + (q.x * ty - q.y * tx)};
		}
	}
	//---------------------------------------------------------
	SkinnedRig::Vec3 SkinnedRig::jointModelPosition(size_t jointIndex) const
	{
		if(jointIndex >= this->joints.size())
		{
			return Vec3();
		}
		// compose the local transforms up the parent chain (position first,
		// then each ancestor applies scale, rotation and its own offset)
		Vec3 position = this->joints[jointIndex].position;
		int parent = this->joints[jointIndex].parent;
		while(parent >= 0 && static_cast<size_t>(parent) < this->joints.size())
		{
			Joint const & ancestor = this->joints[static_cast<size_t>(parent)];
			const Vec3 scaled{position.x * ancestor.scale.x,
				position.y * ancestor.scale.y, position.z * ancestor.scale.z};
			const Vec3 rotated = rotate(ancestor.orientation, scaled);
			position = Vec3{ancestor.position.x + rotated.x,
				ancestor.position.y + rotated.y, ancestor.position.z + rotated.z};
			parent = ancestor.parent;
		}
		return position;
	}
	//---------------------------------------------------------
	SkinnedRig::Vec3 SkinnedRig::sampleVecKeys(
		std::vector<VecKey> const & keys, float seconds, Vec3 const & fallback)
	{
		if(keys.empty())
		{
			return fallback;
		}
		if(seconds <= keys.front().time)
		{
			return keys.front().value;
		}
		for(size_t each = 1; each < keys.size(); ++each)
		{
			if(seconds <= keys[each].time)
			{
				const float span = keys[each].time - keys[each - 1].time;
				const float blend =
					span > 0.0f ? (seconds - keys[each - 1].time) / span : 0.0f;
				Vec3 const & a = keys[each - 1].value;
				Vec3 const & b = keys[each].value;
				return Vec3{a.x + (b.x - a.x) * blend,
					a.y + (b.y - a.y) * blend, a.z + (b.z - a.z) * blend};
			}
		}
		return keys.back().value;
	}
	//---------------------------------------------------------
	SkinnedRig::Quat SkinnedRig::sampleQuatKeys(
		std::vector<QuatKey> const & keys, float seconds)
	{
		if(keys.empty())
		{
			return Quat();
		}
		if(seconds <= keys.front().time)
		{
			return keys.front().value;
		}
		for(size_t each = 1; each < keys.size(); ++each)
		{
			if(seconds <= keys[each].time)
			{
				const float span = keys[each].time - keys[each - 1].time;
				const float blend =
					span > 0.0f ? (seconds - keys[each - 1].time) / span : 0.0f;
				Quat const & a = keys[each - 1].value;
				Quat b = keys[each].value;
				// spherical linear interpolation, shortest arc
				float cosom = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
				if(cosom < 0.0f)
				{
					cosom = -cosom;
					b = Quat{-b.w, -b.x, -b.y, -b.z};
				}
				float scaleA, scaleB;
				if(cosom < 0.9999f)
				{
					const float omega = std::acos(cosom);
					const float sinom = std::sin(omega);
					scaleA = std::sin((1.0f - blend) * omega) / sinom;
					scaleB = std::sin(blend * omega) / sinom;
				}
				else
				{
					// nearly identical: linear blend avoids the 0/0
					scaleA = 1.0f - blend;
					scaleB = blend;
				}
				return Quat{scaleA * a.w + scaleB * b.w,
					scaleA * a.x + scaleB * b.x,
					scaleA * a.y + scaleB * b.y,
					scaleA * a.z + scaleB * b.z};
			}
		}
		return keys.back().value;
	}
}
