/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	ClassicBackend.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ClassicBackend_h__8_7_2026__18_00_00__
#define __ClassicBackend_h__8_7_2026__18_00_00__

//! @file ClassicBackend.h
//! @brief PRIVATE plumbing of the classic-OGRE engine_render backend
//! @remarks This header is the one place that pairs the backend-free
//! facade headers (engine_render/) with classic OGRE 14 types. Only the
//! engine_render_classic/*.cpp TUs and engine_graphic/Engine.cpp (the
//! classic bootstrapper, which creates/destroys the RenderSystem) may
//! include it - application code and everything above engine_graphic
//! talk to the facade headers exclusively. In phase A2 an
//! engine_render_next/ClassicBackend counterpart mirrors this file
//! against Ogre-Next (Docs/render-abstraction.md, "Directory layout").

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "engine_render/MeshInstance.h"
#include "engine_render/SpriteQuad.h"
#include "engine_render/RenderCamera.h"
#include "engine_render/RenderLight.h"
#include "engine_render/RenderTexture.h"
#include "engine_module/EnginePrerequisites.h"

#include <vector>

#ifndef ORKIGE_RENDER_CLASSIC
#	error "engine_render_classic compiled without ORKIGE_RENDER_CLASSIC - the build flavor wiring regressed (see ORKIGE_RENDER_BACKEND in CMake)"
#endif

namespace Orkige
{
	class Engine;

	//--- backend state of the facade classes (pimpl bodies) -----------
	// Out-of-line definitions of the protected nested Impl structs; each
	// facade class's methods (its own TU) see its own Impl directly,
	// cross-class access goes through the RenderBackend statics below.

	struct RenderSystem::Impl
	{
		Engine*				engine = NULL;			//!< the classic bootstrapper (Engine singleton, not owned)
		RenderWorld*		world = NULL;			//!< the one scene world (owned, deleted in ~RenderSystem)
		optr<RenderCamera>	windowCamera;			//!< camera currently shown full-window (keeps it alive)
		Ogre::ColourValue	windowBackground = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
	};

	struct RenderWorld::Impl
	{
		Ogre::SceneManager*	sceneManager = NULL;	//!< owned by Engine (classic bootstrap), not by the facade
		optr<RenderNode>	rootNode;				//!< stable facade handle of the root node (owned=false)
	};

	struct RenderNode::Impl
	{
		Ogre::SceneNode*	node = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;			//!< false for the world root (never destroyed by the handle)
		void*				userPointer = NULL;		//!< @see RenderNode::setUserPointer
		woptr<RenderNode>	parent;					//!< facade graph mirror (backend child lists are never walked)
		std::vector<woptr<RenderNode>>	children;	//!< facade graph mirror, pruned lazily
	};

	struct MeshInstance::Impl
	{
		Ogre::Entity*		entity = NULL;
		Ogre::SceneManager*	creator = NULL;
		String				meshName;
		optr<RenderNode>	attachedTo;				//!< keeps the node alive while content hangs off it
	};

	struct SpriteQuad::Impl
	{
		Ogre::ManualObject*	quad = NULL;
		Ogre::SceneManager*	creator = NULL;
		String				textureName;
		float				texelWidth = 0.0f;		//!< texture size in texels (aspect derivation)
		float				texelHeight = 0.0f;
		float				width = 0.0f;			//!< configured size; <= 0 derives from the texture aspect
		float				height = 0.0f;
		float				u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		Ogre::ColourValue	tint = Ogre::ColourValue::White;
		bool				flipX = false;
		bool				flipY = false;
		int					zOrder = 0;
		optr<RenderNode>	attachedTo;

		//! rebuild the quad vertex data from the state above (same honest
		//! v1 sprite rules SpriteComponent renders with today)
		void rebuild();
	};

	struct RenderCamera::Impl
	{
		Ogre::Camera*		camera = NULL;
		Ogre::SceneManager*	creator = NULL;
		optr<RenderNode>	attachedTo;
	};

	struct RenderLight::Impl
	{
		Ogre::Light*		light = NULL;
		Ogre::SceneManager*	creator = NULL;
		optr<RenderNode>	attachedTo;
	};

	struct RenderTexture::Impl
	{
		Ogre::TexturePtr	texture;
		String				name;
		unsigned int		width = 0;
		unsigned int		height = 0;
		optr<RenderCamera>	camera;					//!< keeps the fed camera alive
		Ogre::ColourValue	background = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
		bool				overlaysEnabled = true;
		bool				shadowsEnabled = true;

		//! (re)create the backend texture + viewport from the state above
		//! (resize-by-recreate, the editor's proven RTT pattern)
		void recreate();
		//! push background/overlays/shadows onto the current viewport
		void applyViewportState();
		//! drop viewport + texture (dtor and the recreate path)
		void destroyTexture();
	};

	//--- the backend hub -----------------------------------------------
	//! @brief the classic backend's cross-class door (befriended by every
	//! facade class, @see engine_render/RenderPrerequisites.h)
	//! @remarks Static-only. Holds the process-wide backend state: the
	//! RenderSystem singleton pointer behind RenderSystem::get() and the
	//! SceneNode -> facade-handle registry that back-maps ray query hits,
	//! getParent navigation and the user-pointer walks. One window, one
	//! world, one registry - multi-anything stays frozen per the decided
	//! design questions.
	struct RenderBackend
	{
		//--- lifecycle (called from the classic bootstrapper Engine) ---
		//! create the facade RenderSystem over a set-up Engine (scene
		//! manager + render window must exist); wires RenderSystem::get
		static RenderSystem* createRenderSystem(Engine* engine);
		//! tear the facade down again (idempotent; called from ~Engine
		//! while the Ogre root is still alive)
		static void destroyRenderSystem();
		//! the live RenderSystem or NULL (backs RenderSystem::get)
		static RenderSystem* system();

		//--- handle factories (protected facade ctors) -----------------
		//! wrap an existing backend node into an owning facade handle and
		//! register it; parent is the facade-graph parent (NULL for root)
		static optr<RenderNode> wrapNode(Ogre::SceneNode* node, bool owned,
			optr<RenderNode> const & parent);
		static optr<MeshInstance> createMeshInstance(
			Ogre::SceneManager* sceneManager, String const & meshName);
		static optr<SpriteQuad> createSpriteQuad(
			Ogre::SceneManager* sceneManager, String const & textureName);
		static optr<RenderCamera> createCamera(
			Ogre::SceneManager* sceneManager, String const & name);
		static optr<RenderLight> createLight(Ogre::SceneManager* sceneManager);
		static optr<RenderTexture> createRenderTexture(String const & name,
			unsigned int width, unsigned int height);

		//--- guts accessors (NULL-safe) ---------------------------------
		static Ogre::SceneNode* sceneNode(optr<RenderNode> const & node);
		static Ogre::Camera* ogreCamera(optr<RenderCamera> const & camera);

		//--- node registry (Ogre::SceneNode* -> facade handle) ----------
		static void registerNode(Ogre::SceneNode* node,
			optr<RenderNode> const & handle);
		static void unregisterNode(Ogre::SceneNode* node);
		//! the facade handle wrapping the node, or NULL for non-facade nodes
		static optr<RenderNode> findNode(Ogre::SceneNode* node);
		//! first non-NULL user pointer from node towards the root
		static void* findUserPointerUpwards(Ogre::SceneNode* node);

		//--- shared services --------------------------------------------
		//! unique name for backend-created content ("prefix.<n>")
		static String generateName(String const & prefix);
		//! the RTSS material scheme wiring every viewport needs on the
		//! shader-only render systems (same as Engine/editor apply today)
		static void applyRTSSScheme(Ogre::Viewport* viewport);
		//! the generated per-texture "Sprite/<tex>" material (idempotent;
		//! same recipe as SpriteComponent until WP-A1.2 migrates it here)
		static Ogre::MaterialPtr getOrCreateSpriteMaterial(
			Ogre::TexturePtr const & texture);
		//! zOrder -> render queue id (painter's sorting around MAIN)
		static Ogre::uint8 renderQueueForZOrder(int zOrder);
	};
}

#endif //__ClassicBackend_h__8_7_2026__18_00_00__
