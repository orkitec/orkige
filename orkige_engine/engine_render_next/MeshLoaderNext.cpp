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
//! none), UV-mapped imports additionally get TANGENTS built on the v1
//! intermediate (normal-mapping materials - RenderSystem::
//! createMaterial - need them), and node transforms are BAKED
//! (aiProcess_PreTransformVertices): the importer is static-meshes-only
//! - skeletal glb import is an honest gap logged once (the facade
//! animation surface itself is implemented over v2 SkeletonInstance in
//! MeshInstanceNext.cpp).

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreMesh.h>
#include <OgreMesh2.h>
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

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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
		//! written onto the v2 sub-meshes after importV1
		void addMeshSection(Ogre::v1::ManualObject & builder,
			aiMesh const * mesh)
		{
			builder.begin("BaseWhite", Ogre::OT_TRIANGLE_LIST,
				Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME);
			builder.estimateVertexCount(mesh->mNumVertices);
			builder.estimateIndexCount(mesh->mNumFaces * 3);
			const bool hasUVs = mesh->HasTextureCoords(0);
			const bool hasColours = mesh->HasVertexColors(0);
			for(unsigned int each = 0; each < mesh->mNumVertices; ++each)
			{
				aiVector3D const & position = mesh->mVertices[each];
				builder.position(position.x, position.y, position.z);
				// GenSmoothNormals guarantees normals (PBS lighting input)
				aiVector3D const & normal = mesh->mNormals[each];
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
			// PreTransformVertices bakes the node hierarchy (static-only
			// import, see the file remarks); SortByPType drops points/lines;
			// FlipUVs flips V to a top-left texel origin - the texture codecs
			// upload row 0 at V=0, so glTF's top-left UV origin needs the flip
			// to render upright (classic gets this from Codec_Assimp, which
			// enables aiProcess_FlipUVs; matching it keeps the two flavours
			// pixel-identical)
			aiScene const * scene = importer.ReadFileFromMemory(bytes.data(),
				bytes.size(),
				aiProcess_Triangulate | aiProcess_GenSmoothNormals |
				aiProcess_PreTransformVertices | aiProcess_SortByPType |
				aiProcess_FlipUVs,
				extensionHint.c_str());
			if(!scene || scene->mNumMeshes == 0)
			{
				logImportError(meshName, importer.GetErrorString());
				return false;
			}
			if(scene->mNumAnimations > 0)
			{
				RenderBackend::notImplementedOnce(
					"skeletal animation import (assimp road bakes transforms)");
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
