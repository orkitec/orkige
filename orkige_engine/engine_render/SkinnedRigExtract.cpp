/********************************************************************
	created:	Thursday 2026/07/17 at 12:00
	filename: 	SkinnedRigExtract.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file SkinnedRigExtract.cpp
//! @brief assimp scene -> backend-neutral SkinnedRig (skeleton + clips +
//! skins); the only translation unit of the neutral layer that sees
//! assimp types

#include "engine_render/SkinnedRigExtract.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <map>
#include <set>

namespace Orkige
{
	namespace
	{
		//! mark a whole node subtree as joint material
		void markNodeSubtree(aiNode const * node, std::set<String> & needed)
		{
			needed.insert(String(node->mName.C_Str()));
			for(unsigned int each = 0; each < node->mNumChildren; ++each)
			{
				markNodeSubtree(node->mChildren[each], needed);
			}
		}

		//! mark every node the skins need as a joint: each node an aiBone
		//! names, its ancestors up to the mesh-holding node (or that
		//! node's parent), and the bone node's whole subtree - the classic
		//! assimp codec's marking rules (@see SkinnedRigExtract.h)
		void markJointNodes(aiScene const * scene, aiNode const * node,
			std::set<String> & needed)
		{
			for(unsigned int meshIdx = 0; meshIdx < node->mNumMeshes; ++meshIdx)
			{
				aiMesh const * mesh = scene->mMeshes[node->mMeshes[meshIdx]];
				for(unsigned int each = 0; each < mesh->mNumBones; ++each)
				{
					aiNode const * boneNode = scene->mRootNode->FindNode(
						mesh->mBones[each]->mName);
					if(!boneNode)
					{
						continue;
					}
					aiNode const * walk = boneNode;
					while(walk)
					{
						needed.insert(String(walk->mName.C_Str()));
						if(walk == node || walk == node->mParent)
						{
							break;
						}
						walk = walk->mParent;
					}
					markNodeSubtree(boneNode, needed);
				}
			}
			for(unsigned int each = 0; each < node->mNumChildren; ++each)
			{
				markJointNodes(scene, node->mChildren[each], needed);
			}
		}

		//! depth-first joint creation from the marked nodes - parents
		//! always precede their children in the joint list
		void collectJoints(aiNode const * node, std::set<String> const & needed,
			int parentJoint, SkinnedRig & rig,
			std::map<String, int> & jointIndexByName)
		{
			int ownJoint = parentJoint;
			const String name(node->mName.C_Str());
			if(needed.count(name) && !jointIndexByName.count(name))
			{
				SkinnedRig::Joint joint;
				joint.name = name;
				joint.parent = parentJoint;
				aiMatrix4x4 const & transform = node->mTransformation;
				if(!transform.IsIdentity())
				{
					aiVector3D scale, position;
					aiQuaternion rotation;
					transform.Decompose(scale, rotation, position);
					joint.position =
						SkinnedRig::Vec3{position.x, position.y, position.z};
					joint.orientation = SkinnedRig::Quat{rotation.w,
						rotation.x, rotation.y, rotation.z};
					joint.scale = SkinnedRig::Vec3{scale.x, scale.y, scale.z};
				}
				ownJoint = static_cast<int>(rig.joints.size());
				jointIndexByName[name] = ownJoint;
				rig.joints.push_back(joint);
			}
			for(unsigned int each = 0; each < node->mNumChildren; ++each)
			{
				collectJoints(node->mChildren[each], needed, ownJoint, rig,
					jointIndexByName);
			}
		}

		//! one clip from one assimp animation (times to seconds, name
		//! fallback, channels on non-joint nodes dropped)
		SkinnedRig::Clip extractClip(aiAnimation const * anim,
			unsigned int animIndex,
			std::map<String, int> const & jointIndexByName)
		{
			SkinnedRig::Clip clip;
			clip.name = String(anim->mName.C_Str());
			if(clip.name.empty())
			{
				clip.name = "Animation" + std::to_string(animIndex);
			}
			const double ticksPerSecond =
				anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 24.0;
			clip.duration =
				static_cast<float>(anim->mDuration / ticksPerSecond);
			for(unsigned int chanIdx = 0; chanIdx < anim->mNumChannels;
				++chanIdx)
			{
				aiNodeAnim const * channel = anim->mChannels[chanIdx];
				std::map<String, int>::const_iterator joint =
					jointIndexByName.find(String(channel->mNodeName.C_Str()));
				if(joint == jointIndexByName.end())
				{
					continue;	// a channel on a non-joint node (camera etc.)
				}
				SkinnedRig::Channel outChannel;
				outChannel.joint = joint->second;
				outChannel.positionKeys.reserve(channel->mNumPositionKeys);
				for(unsigned int each = 0; each < channel->mNumPositionKeys;
					++each)
				{
					aiVectorKey const & key = channel->mPositionKeys[each];
					outChannel.positionKeys.push_back(SkinnedRig::VecKey{
						static_cast<float>(key.mTime / ticksPerSecond),
						SkinnedRig::Vec3{key.mValue.x, key.mValue.y,
							key.mValue.z}});
				}
				outChannel.rotationKeys.reserve(channel->mNumRotationKeys);
				for(unsigned int each = 0; each < channel->mNumRotationKeys;
					++each)
				{
					aiQuatKey const & key = channel->mRotationKeys[each];
					outChannel.rotationKeys.push_back(SkinnedRig::QuatKey{
						static_cast<float>(key.mTime / ticksPerSecond),
						SkinnedRig::Quat{key.mValue.w, key.mValue.x,
							key.mValue.y, key.mValue.z}});
				}
				outChannel.scaleKeys.reserve(channel->mNumScalingKeys);
				for(unsigned int each = 0; each < channel->mNumScalingKeys;
					++each)
				{
					aiVectorKey const & key = channel->mScalingKeys[each];
					outChannel.scaleKeys.push_back(SkinnedRig::VecKey{
						static_cast<float>(key.mTime / ticksPerSecond),
						SkinnedRig::Vec3{key.mValue.x, key.mValue.y,
							key.mValue.z}});
				}
				clip.channels.push_back(outChannel);
			}
			return clip;
		}
	}
	//---------------------------------------------------------
	bool extractSkinnedRig(aiScene const * scene, SkinnedRig & outRig)
	{
		outRig = SkinnedRig();
		if(!scene || !scene->mRootNode)
		{
			return false;
		}
		bool anyBones = false;
		for(unsigned int each = 0; !anyBones && each < scene->mNumMeshes;
			++each)
		{
			anyBones = scene->mMeshes[each]->HasBones();
		}
		if(!anyBones && scene->mNumAnimations == 0)
		{
			return false;	// a static scene has no rig to extract
		}

		// the skeleton
		std::set<String> needed;
		markJointNodes(scene, scene->mRootNode, needed);
		std::map<String, int> jointIndexByName;
		collectJoints(scene->mRootNode, needed, -1, outRig, jointIndexByName);

		// the clips
		outRig.clips.reserve(scene->mNumAnimations);
		for(unsigned int each = 0; each < scene->mNumAnimations; ++each)
		{
			outRig.clips.push_back(extractClip(scene->mAnimations[each], each,
				jointIndexByName));
		}

		// the skins (indexed like the scene's mesh list; zero weights and
		// weights naming unknown joints are filtered)
		outRig.skins.resize(scene->mNumMeshes);
		for(unsigned int meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
		{
			aiMesh const * mesh = scene->mMeshes[meshIdx];
			SkinnedRig::Skin & skin = outRig.skins[meshIdx];
			for(unsigned int boneIdx = 0; boneIdx < mesh->mNumBones; ++boneIdx)
			{
				aiBone const * bone = mesh->mBones[boneIdx];
				std::map<String, int>::const_iterator joint =
					jointIndexByName.find(String(bone->mName.C_Str()));
				if(joint == jointIndexByName.end())
				{
					continue;
				}
				for(unsigned int each = 0; each < bone->mNumWeights; ++each)
				{
					aiVertexWeight const & weight = bone->mWeights[each];
					if(weight.mWeight <= 0.0f ||
						weight.mVertexId >= mesh->mNumVertices)
					{
						continue;
					}
					skin.weights.push_back(SkinnedRig::Weight{
						weight.mVertexId, joint->second, weight.mWeight});
				}
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool extractSkinnedRigFromMemory(void const * bytes, size_t sizeBytes,
		String const & extensionHint, SkinnedRig & outRig)
	{
		Assimp::Importer importer;
		// the same weight cap the import road applies (four per vertex)
		aiScene const * scene = importer.ReadFileFromMemory(bytes, sizeBytes,
			aiProcess_LimitBoneWeights, extensionHint.c_str());
		return extractSkinnedRig(scene, outRig);
	}
}
