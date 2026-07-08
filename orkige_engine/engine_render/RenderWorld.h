/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderWorld.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderWorld_h__8_7_2026__12_00_00__
#define __RenderWorld_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief one renderable scene: node hierarchy, scene content and queries
	//! @remarks Facade over the backend scene manager. Owned by RenderSystem
	//! (one world for now - the door to several stays open). All create*
	//! calls return optr handles; the handle owns the backend object and
	//! destroying it removes the object from the scene (RAII, no destroy*
	//! methods needed).
	//!
	//! Backend mapping (whole class): classic = Ogre::SceneManager;
	//! next = Ogre::SceneManager (v2, created with worker-thread count);
	//! filament = filament::Scene + the manager singletons (EntityManager,
	//! TransformManager, LightManager, RenderableManager).
	class ORKIGE_ENGINE_DLL RenderWorld
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief one hit of a scene ray query, sorted by distance
		//! @remarks plain data (scripting-friendly, mirrors
		//! PhysicsWorld::RayHit). Bounding-box accurate - triangle-accurate
		//! picking goes through PhysicsWorld::castRay against collision
		//! shapes instead (the CollisionTools successor).
		struct ORKIGE_ENGINE_DLL RayQueryHit
		{
			Real	distance;		//!< distance along the ray to the AABB entry
			optr<RenderNode> node;	//!< the node the hit content is attached to (may be NULL for non-facade content)
			void*	userPointer;	//!< first user pointer found walking node->parents (@see RenderNode::setUserPointer) or NULL
			RayQueryHit();			// defined by the backend TU
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend scene guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - drops the backend scene (all handles must be gone)
		~RenderWorld();

		//--- node hierarchy ---
		//! @brief the world root node (owned by the world, do not re-parent)
		//! map: classic/next=SceneManager::getRootSceneNode | filament=implicit (parentless transforms)
		optr<RenderNode> getRootNode() const;
		//! @brief create a node under the root (empty name = generated)
		//! map: classic/next=getRootSceneNode()->createChildSceneNode | filament=EntityManager::create+TransformManager::create
		optr<RenderNode> createNode(String const & name = "");

		//--- scene content factories ---
		//! @brief create a mesh instance from a mesh resource name
		//! (resolves through the resource system incl. project assets)
		//! map: classic=SceneManager::createEntity | next=createItem (v2 mesh; v1 meshes need Mesh::importV1) | filament=gltfio/filamesh loader + RenderableManager
		optr<MeshInstance> createMeshInstance(String const & meshName);
		//! @brief create a textured alpha-blended sprite quad (2D building block)
		//! map: classic=ManualObject + generated "Sprite/<tex>" material | next=ManualObject v2 + HlmsUnlit datablock | filament=quad VB/IB + unlit filamat instance
		optr<SpriteQuad> createSpriteQuad(String const & textureName);
		//! @brief create a camera (attach it to a node to place it)
		//! map: classic/next=SceneManager::createCamera | filament=Engine::createCamera(entity)
		optr<RenderCamera> createCamera(String const & name = "");
		//! @brief create a light (attach it to a node to place it)
		//! map: classic/next=SceneManager::createLight | filament=LightManager::Builder
		optr<RenderLight> createLight();

		//--- procedural built-in meshes ---
		//! @brief ensure the shared vertex-coloured cube MESH RESOURCE exists
		//! (idempotent) - the editor's "Create Cube" content. Scenes reference
		//! it by name through ModelComponent, so every app that loads such
		//! scenes calls this before the scene load; afterwards the cube loads
		//! like any mesh via createMeshInstance. Defaults mirror the editor's
		//! PrimitiveUtil constants (CUBE_MESH_NAME / CUBE_MESH_HALF_EXTENT).
		//! Creates the shared unlit "VertexColour" vertex-colour-tracking
		//! material along the way (@see MeshInstance::setVertexColourUnlit).
		//! map: classic=ManualObject::convertToMesh (engine_util/PrimitiveUtil recipe, backend-private) | next=v2 mesh built from the same vertex data + HlmsUnlit | filament=prebuilt vertex/index buffers + unlit filamat
		void createVertexColourCubeMesh(String const & meshName = "EditorCube.mesh",
			Real halfExtent = Real(0.8));

		//--- global lighting ---
		//! @brief the ambient light minimum every app sets today
		//! map: classic/next=SceneManager::setAmbientLight (next takes hemisphere upper/lower - impl passes colour twice) | filament=IndirectLight intensity or ambient term in material
		void setAmbientLight(Color const & colour);
		//! map: classic/next=SceneManager::getAmbientLight | filament=cached facade value
		Color const & getAmbientLight() const;

		//--- queries (editor picking) ---
		//! @brief all scene content whose bounds the ray hits, nearest first
		//! @param queryMask only content with overlapping query flags is
		//! returned (facade content defaults to QUERYFLAG_DEFAULT; the editor
		//! grid opts out with 0)
		//! map: classic=SceneManager::createRayQuery/execute/destroyQuery | next=same (v2 RaySceneQuery) | filament=no scene query - impl-side AABB walk (or View::pick GPU picking)
		std::vector<RayQueryHit> queryRay(Ray3 const & ray,
			unsigned int queryMask = 0xFFFFFFFF) const;

		//--- default query flags for facade-created content ---
		static const unsigned int QUERYFLAG_DEFAULT;	//!< query flags new content starts with (1)
	protected:
		//! worlds are created by RenderSystem only
		RenderWorld();
	private:
		RenderWorld(RenderWorld const &);				// non-copyable
		RenderWorld & operator=(RenderWorld const &);	// non-copyable
	};
}

#endif //__RenderWorld_h__8_7_2026__12_00_00__
