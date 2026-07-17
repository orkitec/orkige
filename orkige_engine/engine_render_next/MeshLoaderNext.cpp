/********************************************************************
	created:	Wednesday 2026/07/08 at 22:00
	filename: 	MeshLoaderNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MeshLoaderNext.cpp
//! @brief the Next backend's mesh import path
//! @remarks Ogre-Next has no assimp codec (classic loads glb through
//! OGRE 14's Codec_Assimp), so the backend links assimp directly and
//! owns the import end to end - THE decided glb/mesh path:
//!
//!   *.mesh          -> v1::MeshManager (serializer) -> importV1
//!   glb/gltf/obj/.. -> assimp (from the resource stream, in memory)
//!                      -> v1::ManualObject -> convertToMesh -> importV1
//!   anything else already registered as a v2 mesh (the cube-mesh
//!   service) is used as-is.
//!
//! Both roads end in MeshManager::createByImportingV1 and hand back a
//! v2 mesh named exactly like the resource, so MeshInstance creation is
//! one createItem call. Materials: one HLMS PBS datablock per sub-mesh
//! ("<mesh>/mat<i>", idempotent) carrying the assimp diffuse/base
//! colour and diffuse texture - glb-embedded textures are decoded from
//! memory, external references resolve through the resource groups.
//! Assimp generates smooth normals (PBS needs them; the test glbs ship
//! none), and UV-mapped imports additionally get TANGENTS built on the
//! v1 intermediate (normal-mapping materials - RenderSystem::
//! createMaterial - need them).
//!
//! The assimp road forks on SKINNING: a STATIC source (no bones, no
//! clips) gets its node transforms BAKED (aiProcess_PreTransformVertices,
//! exactly the historical static-only path); a SKINNED/ANIMATED source
//! keeps the node hierarchy instead and imports the whole rig - the
//! bone nodes become a v1::Skeleton (OldSkeletonManager), the assimp
//! channels become v1 animation tracks, the per-vertex weights become
//! v1 bone assignments, and Mesh::importV1 carries all of it into the
//! v2 mesh (SkeletonDef + compiled blend buffers), where the facade
//! animation surface (v2 SkeletonInstance, MeshInstanceNext.cpp) plays
//! it. Node transforms are applied explicitly on that road (the walk
//! PreTransformVertices would otherwise do), so a mesh lands in model
//! space with the skeleton owning the bind pose. The skeleton/clip/skin
//! semantics themselves come from the BACKEND-NEUTRAL extraction
//! (engine_render/SkinnedRigExtract.h -> SkinnedRig) - written once so
//! the two importer roads never drift (see the SkinnedRig.h remarks);
//! this file only realises the rig as a v1 skeleton.

#include "engine_render_next/NextBackend.h"
#include "engine_render/SkinnedRigExtract.h"

#include <OgreSceneManager.h>
#include <OgreMesh.h>
#include <OgreMesh2.h>
#include <OgreSubMesh.h>
#include <OgreSubMesh2.h>
#include <OgreMeshManager2.h>
#include <OgreMeshManager.h>
#include <OgreManualObject.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsPbs.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreRoot.h>
#include <OgreLogManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreException.h>
#include <OgreId.h>
#include <OgreSkeleton.h>
#include <OgreOldBone.h>
#include <OgreOldSkeletonManager.h>
#include <OgreAnimation.h>
#include <OgreAnimationTrack.h>
#include <OgreKeyFrame.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <map>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! log an import failure (mirrors the classic backend's error path)
		void logImportError(String const & meshName, String const & why)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: mesh '" + meshName +
				"' failed to import: " + why);
		}

		//! the datablock (name) for one assimp material - PBS with the
		//! diffuse/base colour and, when present, the diffuse texture
		//! (embedded textures decode from the glb blob); idempotent
		String getOrCreateMeshDatablock(aiScene const * scene,
			unsigned int materialIndex, String const & meshName)
		{
			const String name = meshName + "/mat" +
				std::to_string(materialIndex);
			Ogre::HlmsManager* hlmsManager =
				Ogre::Root::getSingleton().getHlmsManager();
			if(hlmsManager->getDatablockNoDefault(name))
			{
				return name;
			}

			aiMaterial const * material = scene->mMaterials[materialIndex];
			// texture: assimp maps glTF baseColorTexture to BASE_COLOR
			// (and mirrors it to DIFFUSE for most importers) - probe both
			Ogre::TextureGpu* texture = NULL;
			aiString texturePath;
			if(material->GetTexture(aiTextureType_BASE_COLOR, 0,
					&texturePath) != AI_SUCCESS &&
				material->GetTexture(aiTextureType_DIFFUSE, 0,
					&texturePath) != AI_SUCCESS)
			{
				texturePath.Clear();
			}
			if(texturePath.length > 0)
			{
				if(aiTexture const * embedded =
					scene->GetEmbeddedTexture(texturePath.C_Str()))
				{
					// compressed blob (png/jpg); mHeight==0 marks compressed
					if(embedded->mHeight == 0)
					{
						texture = RenderBackend::createTexture2DFromMemory(
							meshName + "/tex" + std::to_string(materialIndex),
							embedded->pcData, embedded->mWidth,
							embedded->achFormatHint);
					}
				}
				else
				{
					// external reference - resolve by plain file name
					// through the resource groups (strip any path prefix)
					String fileName = texturePath.C_Str();
					const size_t slash = fileName.find_last_of("/\\");
					if(slash != String::npos)
					{
						fileName = fileName.substr(slash + 1);
					}
					texture = RenderBackend::loadTexture2D(fileName);
				}
			}

			aiColor4D colour(1.0f, 1.0f, 1.0f, 1.0f);
			if(material->Get(AI_MATKEY_BASE_COLOR, colour) != AI_SUCCESS)
			{
				material->Get(AI_MATKEY_COLOR_DIFFUSE, colour);
			}

			Ogre::HlmsPbs* pbs = static_cast<Ogre::HlmsPbs*>(
				hlmsManager->getHlms(Ogre::HLMS_PBS));
			Ogre::HlmsPbsDatablock* datablock =
				static_cast<Ogre::HlmsPbsDatablock*>(pbs->createDatablock(
					name, name, Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(),
					Ogre::HlmsParamVec()));
			datablock->setDiffuse(Ogre::Vector3(colour.r, colour.g, colour.b));
			if(texture)
			{
				datablock->setTexture(Ogre::PBSM_DIFFUSE, texture);
			}
			RenderBackend::registerContentDatablock(datablock);
			return name;
		}

		//! feed one assimp mesh into an open v1::ManualObject section
		//! @remarks sections carry the placeholder "BaseWhite" low-level
		//! material (v1 MO refuses unknown MATERIAL names and knows
		//! nothing about HLMS datablocks); the real datablock names are
		//! written onto the v2 sub-meshes after importV1. @p derived, when
		//! given, is the owning node's derived transform - the skinned road
		//! applies it here (its scene keeps the node hierarchy, so vertices
		//! arrive in mesh-node space); the static road passes NULL because
		//! aiProcess_PreTransformVertices already baked it
		void addMeshSection(Ogre::v1::ManualObject & builder,
			aiMesh const * mesh, aiMatrix4x4 const * derived = NULL)
		{
			// normals transform by the inverse-transpose of the linear part
			aiMatrix3x3 normalMatrix;
			if(derived)
			{
				normalMatrix = aiMatrix3x3(*derived);
				normalMatrix.Inverse().Transpose();
			}
			builder.begin("BaseWhite", Ogre::OT_TRIANGLE_LIST,
				Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME);
			builder.estimateVertexCount(mesh->mNumVertices);
			builder.estimateIndexCount(mesh->mNumFaces * 3);
			const bool hasUVs = mesh->HasTextureCoords(0);
			const bool hasColours = mesh->HasVertexColors(0);
			for(unsigned int each = 0; each < mesh->mNumVertices; ++each)
			{
				aiVector3D position = mesh->mVertices[each];
				if(derived)
				{
					position = (*derived) * position;
				}
				builder.position(position.x, position.y, position.z);
				// GenSmoothNormals guarantees normals (PBS lighting input)
				aiVector3D normal = mesh->mNormals[each];
				if(derived)
				{
					normal = normalMatrix * normal;
					normal.NormalizeSafe();
				}
				builder.normal(normal.x, normal.y, normal.z);
				if(hasUVs)
				{
					builder.textureCoord(mesh->mTextureCoords[0][each].x,
						mesh->mTextureCoords[0][each].y);
				}
				if(hasColours)
				{
					aiColor4D const & colour = mesh->mColors[0][each];
					builder.colour(colour.r, colour.g, colour.b, colour.a);
				}
			}
			for(unsigned int each = 0; each < mesh->mNumFaces; ++each)
			{
				aiFace const & face = mesh->mFaces[each];
				if(face.mNumIndices == 3)	// SortByPType filtered the rest
				{
					builder.triangle(face.mIndices[0], face.mIndices[1],
						face.mIndices[2]);
				}
			}
			builder.end();
		}

		//! v1 mesh -> v2 mesh named meshName (the importV1 finish of both
		//! roads). createByImportingV1 is DEFERRED: it only records the v1
		//! mesh's name/group and re-imports on every (re)load - so load()
		//! explicitly here AND keep the v1 intermediate alive (it is the
		//! reload source after a device-lost event; the memory cost is the
		//! deliberate price of Next's own recommended import recipe)
		Ogre::MeshPtr importV1Mesh(Ogre::v1::MeshPtr const & v1Mesh,
			String const & meshName)
		{
			Ogre::MeshPtr v2Mesh = Ogre::MeshManager::getSingleton()
				.createByImportingV1(meshName,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
					v1Mesh.get(), false /*halfPos*/, false /*halfTexCoords*/,
					false /*qTangents*/);
			v2Mesh->load();
			return v2Mesh;
		}

		//--- the skinned road (skeleton + clips kept) ----------------

		//! per-mesh skinned-import facts the instance surface reads back
		//! (@see RenderBackend::registerSkinnedMesh); populated once per
		//! import, tiny, kept for the process lifetime like datablocks
		struct SkinnedMeshInfo
		{
			Real			boneRadius = 0;	//!< max skinned-vertex distance from its bone (bind pose)
			StringVector	clipNames;		//!< clip names in import order
		};
		std::map<String, SkinnedMeshInfo> & skinnedMeshRegistry()
		{
			static std::map<String, SkinnedMeshInfo> registry;
			return registry;
		}

		//! one v1 section of the skinned road: an aiMesh instanced by a
		//! scene node, with that node's derived (model-space) transform
		struct SkinnedSection
		{
			aiMesh const *	mesh;
			unsigned int	meshIndex;	//!< index into the scene's mesh list (= the rig's skin index)
			aiMatrix4x4		derived;
		};

		//! depth-first (node, mesh) walk collecting the sections to build -
		//! the explicit form of what aiProcess_PreTransformVertices does
		//! for the static road, kept by hand here so bones/weights/clips
		//! survive. Bone-less meshes inside an animated scene are DROPPED
		//! (their sub-mesh would carry no blend data for the skinning
		//! shader) with a log line.
		void collectSkinnedSections(aiScene const * scene, aiNode const * node,
			aiMatrix4x4 const & parentDerived,
			std::vector<SkinnedSection> & sections)
		{
			const aiMatrix4x4 derived = parentDerived * node->mTransformation;
			for(unsigned int each = 0; each < node->mNumMeshes; ++each)
			{
				const unsigned int meshIndex = node->mMeshes[each];
				aiMesh const * mesh = scene->mMeshes[meshIndex];
				if(mesh->HasBones())
				{
					sections.push_back(SkinnedSection{mesh, meshIndex,
						derived});
				}
				else
				{
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: skipping bone-less mesh '" +
						String(mesh->mName.C_Str()) +
						"' inside an animated scene");
				}
			}
			for(unsigned int each = 0; each < node->mNumChildren; ++each)
			{
				collectSkinnedSections(scene, node->mChildren[each], derived,
					sections);
			}
		}

		//! build the v1 bones + hierarchy from the neutral rig (joints are
		//! parents-before-children, so a bone HANDLE equals its rig joint
		//! index). Rest pose = position + orientation, like the classic
		//! codec (scale stays unit).
		void buildV1Skeleton(Ogre::v1::Skeleton* skeleton,
			SkinnedRig const & rig)
		{
			for(SkinnedRig::Joint const & joint : rig.joints)
			{
				Ogre::v1::OldBone* bone = skeleton->createBone(joint.name);
				bone->setPosition(joint.position.x, joint.position.y,
					joint.position.z);
				bone->setOrientation(joint.orientation.w, joint.orientation.x,
					joint.orientation.y, joint.orientation.z);
			}
			for(size_t each = 0; each < rig.joints.size(); ++each)
			{
				if(rig.joints[each].parent >= 0)
				{
					skeleton->getBone(static_cast<unsigned short>(
						rig.joints[each].parent))->addChild(
						skeleton->getBone(static_cast<unsigned short>(each)));
				}
			}
			skeleton->setBindingPose();
		}

		//! build one v1 animation per rig clip. A track keyframe stores the
		//! pose RELATIVE to the bone's rest transform (poseToKey =
		//! inverse(rest) * channelPose - v1 keyframes apply on top of the
		//! binding pose), with the classic codec's root-bone translation
		//! handling kept so both flavors move identically.
		void buildV1Animations(Ogre::v1::Skeleton* skeleton,
			SkinnedRig const & rig)
		{
			for(SkinnedRig::Clip const & clip : rig.clips)
			{
				Ogre::v1::Animation* animation =
					skeleton->createAnimation(clip.name, clip.duration);
				animation->setInterpolationMode(
					Ogre::v1::Animation::IM_LINEAR);
				for(SkinnedRig::Channel const & channel : clip.channels)
				{
					if(channel.joint < 0)
					{
						continue;
					}
					Ogre::v1::OldBone* bone = skeleton->getBone(
						static_cast<unsigned short>(channel.joint));
					Ogre::Matrix4 restPoseInv;
					restPoseInv.makeInverseTransform(bone->getPosition(),
						bone->getScale(), bone->getOrientation());
					Ogre::v1::OldNodeAnimationTrack* track =
						animation->createOldNodeTrack(bone->getHandle(), bone);

					// the union of the channel's key times (seconds)
					std::vector<float> times;
					times.reserve(channel.positionKeys.size() +
						channel.rotationKeys.size() +
						channel.scaleKeys.size());
					for(SkinnedRig::VecKey const & key : channel.positionKeys)
					{
						times.push_back(key.time);
					}
					for(SkinnedRig::QuatKey const & key : channel.rotationKeys)
					{
						times.push_back(key.time);
					}
					for(SkinnedRig::VecKey const & key : channel.scaleKeys)
					{
						times.push_back(key.time);
					}
					std::sort(times.begin(), times.end());
					times.erase(std::unique(times.begin(), times.end()),
						times.end());

					for(float seconds : times)
					{
						const SkinnedRig::Vec3 rigTrans =
							SkinnedRig::sampleVecKeys(channel.positionKeys,
								seconds, SkinnedRig::Vec3());
						const SkinnedRig::Quat rigRot =
							SkinnedRig::sampleQuatKeys(channel.rotationKeys,
								seconds);
						const SkinnedRig::Vec3 rigScale =
							SkinnedRig::sampleVecKeys(channel.scaleKeys,
								seconds,
								SkinnedRig::Vec3{1.0f, 1.0f, 1.0f});

						const Ogre::Vector3 trans(rigTrans.x, rigTrans.y,
							rigTrans.z);
						const Ogre::Quaternion rot(rigRot.w, rigRot.x,
							rigRot.y, rigRot.z);
						const Ogre::Vector3 scale(rigScale.x, rigScale.y,
							rigScale.z);

						Ogre::Matrix4 fullTransform;
						fullTransform.makeTransform(trans, scale, rot);
						const Ogre::Matrix4 poseToKey =
							restPoseInv * fullTransform;
						Ogre::Vector3 keyTrans, keyScale;
						Ogre::Quaternion keyRot;
						poseToKey.decomposition(keyTrans, keyScale, keyRot);
						if(skeleton->getRootBone() == bone)
						{
							// a root bone's channel is already in skeleton
							// space - keep the translation absolute-relative
							// to the rest position (the classic codec rule)
							keyTrans = trans - bone->getPosition();
						}

						Ogre::v1::TransformKeyFrame* keyFrame =
							track->createNodeKeyFrame(seconds);
						keyFrame->setTranslate(keyTrans);
						keyFrame->setRotation(keyRot);
						keyFrame->setScale(keyScale);
					}
				}
			}
			skeleton->optimiseAllAnimations();
		}

		//! the max distance of any skinned vertex from its joint's bind
		//! position (model space) - the animated-bounds padding the
		//! instance surface expands live bone positions by (the classic
		//! bone-bounding-radius semantics)
		Real computeBoneRadius(std::vector<SkinnedSection> const & sections,
			SkinnedRig const & rig)
		{
			Real radius = 0;
			for(SkinnedSection const & section : sections)
			{
				if(section.meshIndex >= rig.skins.size())
				{
					continue;
				}
				for(SkinnedRig::Weight const & weight :
					rig.skins[section.meshIndex].weights)
				{
					if(weight.joint < 0 ||
						weight.vertexIndex >= section.mesh->mNumVertices)
					{
						continue;
					}
					const SkinnedRig::Vec3 jointPos = rig.jointModelPosition(
						static_cast<size_t>(weight.joint));
					const aiVector3D vertex = section.derived *
						section.mesh->mVertices[weight.vertexIndex];
					const aiVector3D delta(vertex.x - jointPos.x,
						vertex.y - jointPos.y, vertex.z - jointPos.z);
					radius = std::max(radius,
						static_cast<Real>(delta.Length()));
				}
			}
			return radius;
		}

		//! the whole skinned road: neutral rig extraction + sections ->
		//! v1 skeleton/clips/weights -> v1 mesh -> importV1 (which compiles
		//! the bone assignments and converts the skeleton to a v2
		//! SkeletonDef) -> datablock names
		bool importSkinnedScene(Ogre::SceneManager* sceneManager,
			aiScene const * scene, String const & meshName)
		{
			// the ONE shared skeleton/clip/skin extraction (SkinnedRigExtract)
			SkinnedRig rig;
			if(!extractSkinnedRig(scene, rig) || !rig.hasSkeleton())
			{
				logImportError(meshName,
					"animated scene carries no usable skeleton");
				return false;
			}
			std::vector<SkinnedSection> sections;
			collectSkinnedSections(scene, scene->mRootNode, aiMatrix4x4(),
				sections);
			if(sections.empty())
			{
				logImportError(meshName,
					"animated scene carries no skinned mesh");
				return false;
			}

			// the skeleton: rig joints -> v1 bones + hierarchy + clips.
			// Idempotent by name - a re-import (same deterministic source)
			// reuses the already-built skeleton, matching the SkeletonDef
			// cache v2 keeps by name.
			const String skeletonName = meshName + "/skeleton";
			Ogre::v1::SkeletonPtr skeleton =
				std::static_pointer_cast<Ogre::v1::Skeleton>(
					Ogre::v1::OldSkeletonManager::getSingleton().getByName(
						skeletonName,
						Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME));
			if(!skeleton)
			{
				skeleton = std::static_pointer_cast<Ogre::v1::Skeleton>(
					Ogre::v1::OldSkeletonManager::getSingleton().create(
						skeletonName,
						Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
						true /*isManual*/));
				buildV1Skeleton(skeleton.get(), rig);
				buildV1Animations(skeleton.get(), rig);
			}
			StringVector clipNames;
			clipNames.reserve(rig.clips.size());
			for(SkinnedRig::Clip const & clip : rig.clips)
			{
				clipNames.push_back(clip.name);
			}

			// materials first (v1 sections resolve datablocks by name)
			std::vector<String> datablockNames(sections.size());
			for(size_t each = 0; each < sections.size(); ++each)
			{
				datablockNames[each] = getOrCreateMeshDatablock(scene,
					sections[each].mesh->mMaterialIndex, meshName);
			}

			// geometry: the throwaway v1 builder, node transforms applied
			// per section (the skeleton owns the bind pose)
			Ogre::v1::ManualObject builder(
				Ogre::Id::generateNewId<Ogre::MovableObject>(),
				&sceneManager->_getEntityMemoryManager(Ogre::SCENE_DYNAMIC),
				sceneManager);
			builder.setReadable(true);	// importV1 reads the buffers back
			for(SkinnedSection const & section : sections)
			{
				addMeshSection(builder, section.mesh, &section.derived);
			}
			Ogre::v1::MeshPtr v1Mesh = builder.convertToMesh(
				meshName + "/v1import",
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				false /*buildShadowMapBuffers*/);
			// tangents for UV-mapped skinned imports, same rule as static
			bool anyUVs = false;
			for(SkinnedSection const & section : sections)
			{
				anyUVs = anyUVs || section.mesh->HasTextureCoords(0);
			}
			if(anyUVs)
			{
				try
				{
					v1Mesh->buildTangentVectors();
				}
				catch(Ogre::Exception const & e)
				{
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: mesh '" + meshName +
						"': tangent generation failed (" + e.getDescription() +
						") - normal-mapping materials will not apply");
				}
			}

			// skin: link the skeleton, then the per-vertex weights (a
			// section maps 1:1 onto its v1 sub-mesh, so the rig's vertex
			// ids are the sub-mesh's own; a bone handle equals its rig
			// joint index by construction). importV1 compiles them into
			// blend buffers and carries the skeleton to the v2 mesh.
			v1Mesh->setSkeletonName(skeletonName);
			for(size_t each = 0; each < sections.size(); ++each)
			{
				Ogre::v1::SubMesh* subMesh = v1Mesh->getSubMesh(
					static_cast<unsigned int>(each));
				if(sections[each].meshIndex >= rig.skins.size())
				{
					continue;
				}
				for(SkinnedRig::Weight const & weight :
					rig.skins[sections[each].meshIndex].weights)
				{
					if(weight.joint < 0)
					{
						continue;
					}
					Ogre::v1::VertexBoneAssignment assignment;
					assignment.vertexIndex = weight.vertexIndex;
					assignment.boneIndex =
						static_cast<unsigned short>(weight.joint);
					assignment.weight = weight.weight;
					subMesh->addBoneAssignment(assignment);
				}
			}

			Ogre::MeshPtr v2Mesh = importV1Mesh(v1Mesh, meshName);
			if(!v2Mesh)
			{
				return false;
			}
			for(size_t each = 0; each < sections.size(); ++each)
			{
				v2Mesh->getSubMesh(static_cast<Ogre::uint32>(each))
					->setMaterialName(datablockNames[each]);
			}
			RenderBackend::registerSkinnedMesh(meshName,
				computeBoneRadius(sections, rig), clipNames);
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: mesh '" + meshName +
				"' imported skinned: " +
				std::to_string(skeleton->getNumBones()) + " bones, " +
				std::to_string(clipNames.size()) + " clips");
			return true;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::registerSkinnedMesh(String const & meshName,
		Real boneRadius, StringVector const & clipNames)
	{
		SkinnedMeshInfo & info = skinnedMeshRegistry()[meshName];
		info.boneRadius = boneRadius;
		info.clipNames = clipNames;
	}
	//---------------------------------------------------------
	Real RenderBackend::skinnedMeshBoneRadius(String const & meshName)
	{
		std::map<String, SkinnedMeshInfo> const & registry =
			skinnedMeshRegistry();
		std::map<String, SkinnedMeshInfo>::const_iterator found =
			registry.find(meshName);
		return found != registry.end() ? found->second.boneRadius : Real(0);
	}
	//---------------------------------------------------------
	StringVector const * RenderBackend::skinnedMeshClipNames(
		String const & meshName)
	{
		std::map<String, SkinnedMeshInfo> const & registry =
			skinnedMeshRegistry();
		std::map<String, SkinnedMeshInfo>::const_iterator found =
			registry.find(meshName);
		return found != registry.end() ? &found->second.clipNames : NULL;
	}
	//---------------------------------------------------------
	bool RenderBackend::ensureV2Mesh(Ogre::SceneManager* sceneManager,
		String const & meshName)
	{
		oAssert(sceneManager);
		Ogre::MeshManager & meshManager2 = Ogre::MeshManager::getSingleton();
		if(meshManager2.getByName(meshName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return true;	// already imported / backend-generated (cube)
		}
		try
		{
			//--- road 1: native v1 .mesh through the serializer ----------
			if(meshName.size() > 5 &&
				meshName.compare(meshName.size() - 5, 5, ".mesh") == 0)
			{
				Ogre::v1::MeshPtr v1Mesh =
					Ogre::v1::MeshManager::getSingleton().load(meshName,
						Ogre::ResourceGroupManager::
							AUTODETECT_RESOURCE_GROUP_NAME,
						Ogre::v1::HardwareBuffer::HBU_STATIC,
						Ogre::v1::HardwareBuffer::HBU_STATIC);
				// material names from the serializer carry over as-is
				return importV1Mesh(v1Mesh, meshName) != nullptr;
			}

			//--- road 2: assimp from the resource stream -----------------
			Ogre::DataStreamPtr stream =
				Ogre::ResourceGroupManager::getSingleton().openResource(
					meshName,
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
			const String bytes = stream->getAsString();
			const size_t dot = meshName.find_last_of('.');
			const String extensionHint =
				dot == String::npos ? String() : meshName.substr(dot + 1);

			Assimp::Importer importer;
			// parse WITHOUT post-processing first: the post steps depend on
			// whether the source is SKINNED/ANIMATED. assimp's ReadFile is
			// exactly parse + ApplyPostProcessing(flags), so the deferred
			// ApplyPostProcessing below yields byte-identical output to the
			// historical one-call static import.
			aiScene const * scene = importer.ReadFileFromMemory(bytes.data(),
				bytes.size(), 0u, extensionHint.c_str());
			if(!scene)
			{
				logImportError(meshName, importer.GetErrorString());
				return false;
			}
			bool skinned = scene->mNumAnimations > 0;
			for(unsigned int each = 0;
				!skinned && each < scene->mNumMeshes; ++each)
			{
				skinned = scene->mMeshes[each]->HasBones();
			}
			if(!skinned)
			{
				// the static road: PreTransformVertices bakes the node
				// hierarchy; SortByPType drops points/lines; FlipUVs flips V
				// to a top-left texel origin - the texture codecs upload row
				// 0 at V=0, so glTF's top-left UV origin needs the flip to
				// render upright (classic gets this from its assimp codec,
				// which enables aiProcess_FlipUVs; matching it keeps the two
				// flavours pixel-identical)
				scene = importer.ApplyPostProcessing(
					aiProcess_Triangulate | aiProcess_GenSmoothNormals |
					aiProcess_PreTransformVertices | aiProcess_SortByPType |
					aiProcess_FlipUVs);
			}
			else
			{
				// the skinned road keeps the node hierarchy (the skeleton
				// lives in it); LimitBoneWeights caps assimp at the four
				// weights per vertex the v1 blend buffers carry
				scene = importer.ApplyPostProcessing(
					aiProcess_Triangulate | aiProcess_GenSmoothNormals |
					aiProcess_SortByPType | aiProcess_FlipUVs |
					aiProcess_LimitBoneWeights);
			}
			if(!scene || scene->mNumMeshes == 0)
			{
				logImportError(meshName, importer.GetErrorString());
				return false;
			}
			if(skinned)
			{
				return importSkinnedScene(sceneManager, scene, meshName);
			}

			// materials first (v1 sections resolve datablocks by name)
			std::vector<String> datablockNames(scene->mNumMeshes);
			for(unsigned int each = 0; each < scene->mNumMeshes; ++each)
			{
				datablockNames[each] = getOrCreateMeshDatablock(scene,
					scene->mMeshes[each]->mMaterialIndex, meshName);
			}

			// geometry: a throwaway v1 builder (never enters the scene)
			Ogre::v1::ManualObject builder(
				Ogre::Id::generateNewId<Ogre::MovableObject>(),
				&sceneManager->_getEntityMemoryManager(Ogre::SCENE_DYNAMIC),
				sceneManager);
			builder.setReadable(true);	// importV1 reads the buffers back
			for(unsigned int each = 0; each < scene->mNumMeshes; ++each)
			{
				addMeshSection(builder, scene->mMeshes[each]);
			}
			Ogre::v1::MeshPtr v1Mesh = builder.convertToMesh(
				meshName + "/v1import",
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				false /*buildShadowMapBuffers*/);
			// UV-mapped imports get TANGENTS (from the v1 normals + UVs, they
			// survive importV1): the Hlms REFUSES to render a normal-mapping
			// material (RenderSystem::createMaterial) on a tangent-less mesh,
			// so every mesh that CAN carry one (has UVs) does. Meshes without
			// UVs can't take textured materials anyway - nothing to build.
			bool anyUVs = false;
			for(unsigned int each = 0; each < scene->mNumMeshes; ++each)
			{
				anyUVs = anyUVs || scene->mMeshes[each]->HasTextureCoords(0);
			}
			if(anyUVs)
			{
				try
				{
					v1Mesh->buildTangentVectors();
				}
				catch(Ogre::Exception const & e)
				{
					// degenerate UVs etc.: import without tangents (normal-
					// mapping materials will be refused for this mesh, logged)
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: mesh '" + meshName +
						"': tangent generation failed (" + e.getDescription() +
						") - normal-mapping materials will not apply");
				}
			}
			Ogre::MeshPtr v2Mesh = importV1Mesh(v1Mesh, meshName);
			if(!v2Mesh)
			{
				return false;
			}
			// now swap the placeholder for the generated datablock names
			// (Item creation resolves them through the HlmsManager)
			for(unsigned int each = 0; each < scene->mNumMeshes; ++each)
			{
				v2Mesh->getSubMesh(each)->setMaterialName(
					datablockNames[each]);
			}
			return true;
		}
		catch(Ogre::Exception const & e)
		{
			logImportError(meshName, e.getDescription());
			return false;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::createVertexColourCubeMesh(
		Ogre::SceneManager* sceneManager, String const & meshName,
		Real halfExtent)
	{
		oAssert(sceneManager);
		Ogre::MeshManager & meshManager2 = Ogre::MeshManager::getSingleton();
		if(meshManager2.getByName(meshName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return;	// idempotent, same contract as classic PrimitiveUtil
		}
		// the shared unlit vertex-colour look (classic: the "VertexColour"
		// material; here: the Unlit datablock of the same name)
		RenderBackend::getOrCreateVertexColourUnlitDatablock("VertexColour",
			NULL);

		// same palette/winding as classic PrimitiveUtil (facade parity)
		const Real s = halfExtent;
		const Ogre::Vector3 corners[8] = {
			{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
			{-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s},
		};
		const Ogre::ColourValue colors[8] = {
			Ogre::ColourValue(1, 0, 0), Ogre::ColourValue(0, 1, 0),
			Ogre::ColourValue(0, 0, 1), Ogre::ColourValue(1, 1, 0),
			Ogre::ColourValue(1, 0, 1), Ogre::ColourValue(0, 1, 1),
			Ogre::ColourValue(1, 1, 1), Ogre::ColourValue(0.5f, 0.5f, 0.5f),
		};
		const int quads[6][4] = {
			{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0},
		};
		Ogre::v1::ManualObject builder(
			Ogre::Id::generateNewId<Ogre::MovableObject>(),
			&sceneManager->_getEntityMemoryManager(Ogre::SCENE_DYNAMIC),
			sceneManager);
		builder.setReadable(true);
		// placeholder material, swapped for the datablock after importV1
		// (same dance as the assimp road - see addMeshSection remarks)
		builder.begin("BaseWhite", Ogre::OT_TRIANGLE_LIST,
			Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME);
		for(int each = 0; each < 8; ++each)
		{
			builder.position(corners[each]);
			builder.colour(colors[each]);
		}
		for(const int* quad : quads)
		{
			builder.quad(static_cast<Ogre::uint32>(quad[0]),
				static_cast<Ogre::uint32>(quad[1]),
				static_cast<Ogre::uint32>(quad[2]),
				static_cast<Ogre::uint32>(quad[3]));
		}
		builder.end();
		Ogre::v1::MeshPtr v1Mesh = builder.convertToMesh(
			meshName + "/v1import",
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
			false /*buildShadowMapBuffers*/);
		if(Ogre::MeshPtr v2Mesh = importV1Mesh(v1Mesh, meshName))
		{
			v2Mesh->getSubMesh(0)->setMaterialName("VertexColour");
		}
	}
	//---------------------------------------------------------
	void RenderBackend::createVertexColourLineListMesh(
		Ogre::SceneManager* sceneManager, String const & meshName,
		Vec3 const * points, Color const * colours, size_t pointCount)
	{
		oAssert(sceneManager);
		oAssert(!meshName.empty());
		oAssert(points && colours && pointCount >= 2 && pointCount % 2 == 0);
		Ogre::MeshManager & meshManager2 = Ogre::MeshManager::getSingleton();
		if(meshManager2.getByName(meshName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return;	// idempotent, same contract as the cube service
		}
		// the shared unlit vertex-colour look, same as the cube service
		RenderBackend::getOrCreateVertexColourUnlitDatablock("VertexColour",
			NULL);
		// the cube-service recipe on line primitives: v1 ManualObject
		// (OT_LINE_LIST survives convertToMesh AND importV1 - the sub-mesh
		// keeps its operation type) -> v2 mesh + the shared datablock
		Ogre::v1::ManualObject builder(
			Ogre::Id::generateNewId<Ogre::MovableObject>(),
			&sceneManager->_getEntityMemoryManager(Ogre::SCENE_DYNAMIC),
			sceneManager);
		builder.setReadable(true);
		builder.begin("BaseWhite", Ogre::OT_LINE_LIST,
			Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME);
		for(size_t each = 0; each < pointCount; ++each)
		{
			builder.position(points[each]);
			builder.colour(colours[each]);
			builder.index(static_cast<Ogre::uint32>(each));
		}
		builder.end();
		Ogre::v1::MeshPtr v1Mesh = builder.convertToMesh(
			meshName + "/v1import",
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
			false /*buildShadowMapBuffers*/);
		if(Ogre::MeshPtr v2Mesh = importV1Mesh(v1Mesh, meshName))
		{
			v2Mesh->getSubMesh(0)->setMaterialName("VertexColour");
		}
	}
}
