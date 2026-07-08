/**************************************************************
	created:	2026/07/07 at 22:20
	filename: 	PrimitiveUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PrimitiveUtil_h__7_7_2026__22_20_00__
#define __PrimitiveUtil_h__7_7_2026__22_20_00__

#include "engine_module/EnginePrerequisitesClassic.h"

namespace Orkige
{
	//! @brief helpers for the built-in vertex-coloured primitives (the
	//! classic-backend recipe behind the facade cube-mesh service).
	//! @remarks the editor's "Create Cube" produces a real in-memory mesh
	//! resource (ManualObject::convertToMesh) instead of a per-object
	//! ManualObject, so cubes go through ModelComponent and round-trip through
	//! scene files as a plain mesh name. Every app that loads such a scene
	//! must create the mesh under the same name before
	//! SceneSerializer::loadScene runs - APPS do that through the facade
	//! (RenderWorld::createVertexColourCubeMesh, whose classic impl calls
	//! down here; A1, Docs/render-abstraction.md); the ManualObject guts stay
	//! backend-private. Direct callers left: the classic backend and the
	//! (classic-only) editor.
	namespace PrimitiveUtil
	{
		//! mesh resource name used by the editor's "Create Cube"
		static const char* const CUBE_MESH_NAME = "EditorCube.mesh";
		//! half extent of the editor cube mesh
		static const float CUBE_MESH_HALF_EXTENT = 0.8f;

		//! @brief create the shared unlit "VertexColour" material (idempotent)
		//! @remarks the RTSS only reads vertex colours when the pass tracks them
		static inline void createVertexColourMaterial()
		{
			Ogre::MaterialManager& materialManager = Ogre::MaterialManager::getSingleton();
			if(materialManager.resourceExists("VertexColour", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
				return;
			Ogre::MaterialPtr material = materialManager.create("VertexColour",
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
			pass->setLightingEnabled(false);
			pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		}
		//---------------------------------------------------------
		//! @brief build a vertex-coloured cube as a real in-memory mesh resource (idempotent)
		//! @remarks creates the "VertexColour" material first if needed
		static inline Ogre::MeshPtr createVertexColourCubeMesh(Ogre::SceneManager* sceneManager,
			String const & meshName = CUBE_MESH_NAME, float halfExtent = CUBE_MESH_HALF_EXTENT)
		{
			oAssert(sceneManager);
			Ogre::MeshManager& meshManager = Ogre::MeshManager::getSingleton();
			if(meshManager.resourceExists(meshName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
				return meshManager.getByName(meshName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

			createVertexColourMaterial();

			Ogre::ManualObject* cube = sceneManager->createManualObject(meshName + ".manual");
			cube->begin("VertexColour", Ogre::RenderOperation::OT_TRIANGLE_LIST);
			const float s = halfExtent;
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
			for(int i = 0; i < 8; ++i)
			{
				cube->position(corners[i]);
				cube->colour(colors[i]);
			}
			const int quads[6][4] = {
				{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0},
			};
			for(const int* q : quads)
			{
				cube->quad(q[0], q[1], q[2], q[3]);
			}
			cube->end();
			Ogre::MeshPtr mesh = cube->convertToMesh(meshName);
			sceneManager->destroyManualObject(cube);
			return mesh;
		}
		//---------------------------------------------------------
		//! @brief render an entity's materials unlit with vertex colours
		//! @remarks Codec_Assimp keeps lighting enabled on imported materials
		//! (it generates normals); under an ambient-only scene the vertex
		//! colours would drown. ModelComponent does not serialize material
		//! tweaks (yet), so apps re-apply this after loading a scene.
		static inline void makeEntityVertexColourUnlit(Ogre::Entity* entity)
		{
			oAssert(entity);
			for(unsigned int i = 0; i < entity->getNumSubEntities(); ++i)
			{
				Ogre::Pass* pass = entity->getSubEntity(i)->getMaterial()
					->getTechnique(0)->getPass(0);
				pass->setLightingEnabled(false);
				pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
			}
		}
	}
}

#endif //__PrimitiveUtil_h__7_7_2026__22_20_00__
