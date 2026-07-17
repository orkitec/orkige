/********************************************************************
	created:	Thursday 2026/07/17 at 12:00
	filename: 	SkinnedRigExtract.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SkinnedRigExtract_h__17_7_2026__12_00_00__
#define __SkinnedRigExtract_h__17_7_2026__12_00_00__

#include "engine_render/SkinnedRig.h"

//! the importer library's scene stays an implementation detail of the
//! extraction translation unit - consumers only see the neutral rig
struct aiScene;

namespace Orkige
{
	//! @brief fill a backend-neutral SkinnedRig from an assimp scene
	//! @remarks THE one skeleton/clip extraction both importer roads share
	//! (see the SkinnedRig.h remarks on the two-importer reality). Joint
	//! selection follows the classic assimp codec's marking rules - every
	//! node a mesh's bones name, its ancestors up to the mesh-holding
	//! node's parent, and each bone node's whole subtree - so the same
	//! source grows the same skeleton on every flavor. Joints land in
	//! depth-first order (parents before children), rest transforms are
	//! the nodes' LOCAL decomposed transforms, clip key times are
	//! converted to seconds (assimp ticks-per-second, 24 when unstated),
	//! nameless clips become "Animation<i>", channels on non-joint nodes
	//! are dropped, and zero weights are filtered from the skins.
	//! @return false when the scene carries neither bones nor clips
	bool extractSkinnedRig(aiScene const * scene, SkinnedRig & outRig);

	//! @brief parse an in-memory model file (glb/gltf/...) and extract its
	//! rig; @p extensionHint names the format like the import road does.
	//! Bone weights are capped at four per vertex (the importer's
	//! LimitBoneWeights step), matching what the mesh import feeds the
	//! renderer. Headless - made for tests and tools.
	bool extractSkinnedRigFromMemory(void const * bytes, size_t sizeBytes,
		String const & extensionHint, SkinnedRig & outRig);
}

#endif //__SkinnedRigExtract_h__17_7_2026__12_00_00__
