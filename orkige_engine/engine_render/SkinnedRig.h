/********************************************************************
	created:	Thursday 2026/07/17 at 12:00
	filename: 	SkinnedRig.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SkinnedRig_h__17_7_2026__12_00_00__
#define __SkinnedRig_h__17_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief a BACKEND-NEUTRAL skinned-rig description: joint hierarchy,
	//! animation clips (raw keyframe tracks) and per-vertex skin weights
	//! @remarks the one shared vocabulary between a mesh importer's source
	//! (SkinnedRigExtract.h fills it from an assimp scene) and a render
	//! backend's skeleton build - pure data plus pure sampling helpers, no
	//! renderer or importer types (the AtmosphereDesc precedent). The engine
	//! carries TWO importer roads (classic = the upstream render library's
	//! own assimp codec, next = the backend's MeshLoaderNext); this layer
	//! keeps the NEW skeleton/clip semantics written once, so a backend that
	//! needs to import a rig itself consumes the same extraction instead of
	//! rewriting it. Conventions: joints are ordered parents-before-children
	//! (a joint's parent index is always smaller), rest transforms are the
	//! source nodes' LOCAL transforms, key times are SECONDS.
	struct SkinnedRig
	{
		//--- Types -------------------------------------------------
	public:
		struct Vec3
		{
			float	x = 0, y = 0, z = 0;
		};
		struct Quat
		{
			float	w = 1, x = 0, y = 0, z = 0;
		};
		//! one joint: name + parent index (-1 = root) + LOCAL rest transform
		struct Joint
		{
			String	name;
			int		parent = -1;
			Vec3	position;
			Quat	orientation;
			Vec3	scale = Vec3{1.0f, 1.0f, 1.0f};
		};
		struct VecKey
		{
			float	time = 0;	//!< seconds
			Vec3	value;
		};
		struct QuatKey
		{
			float	time = 0;	//!< seconds
			Quat	value;
		};
		//! one clip channel: the raw source key tracks of one joint (a
		//! missing track means "hold the rest value" - fallbacks below)
		struct Channel
		{
			int						joint = -1;
			std::vector<VecKey>		positionKeys;
			std::vector<QuatKey>	rotationKeys;
			std::vector<VecKey>		scaleKeys;
		};
		struct Clip
		{
			String					name;
			float					duration = 0;	//!< seconds
			std::vector<Channel>	channels;
		};
		//! one vertex-to-joint binding of a skinned source mesh
		struct Weight
		{
			unsigned int	vertexIndex = 0;
			int				joint = -1;
			float			weight = 0;
		};
		//! the skin of ONE source mesh (indexed like the source's mesh list;
		//! empty weights = that source mesh is not skinned)
		struct Skin
		{
			std::vector<Weight>	weights;
		};

		//--- Variables ---------------------------------------------
	public:
		std::vector<Joint>	joints;
		std::vector<Clip>	clips;
		std::vector<Skin>	skins;

		//--- Methods -----------------------------------------------
	public:
		//! anything to build a skeleton from?
		bool hasSkeleton() const { return !this->joints.empty(); }

		//! a joint's rest position in MODEL space (the local transforms
		//! composed up the parent chain) - bind-pose geometry queries
		Vec3 jointModelPosition(size_t jointIndex) const;

		//--- pure key sampling (linear / spherical-linear, clamped) ---
		//! sample a vector track at @p seconds; an empty track answers
		//! @p fallback (the "hold the rest value" convention)
		static Vec3 sampleVecKeys(std::vector<VecKey> const & keys,
			float seconds, Vec3 const & fallback);
		//! sample a rotation track at @p seconds; empty answers identity
		static Quat sampleQuatKeys(std::vector<QuatKey> const & keys,
			float seconds);
	};
}

#endif //__SkinnedRig_h__17_7_2026__12_00_00__
