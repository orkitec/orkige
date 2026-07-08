/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	NextBackend.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __NextBackend_h__8_7_2026__20_00_00__
#define __NextBackend_h__8_7_2026__20_00_00__

//! @file NextBackend.h
//! @brief PRIVATE plumbing of the Ogre-Next engine_render backend
//! @remarks The Ogre-Next counterpart of engine_render_classic/
//! ClassicBackend.h: the one place that pairs the backend-free facade
//! headers with Ogre-Next types. Only the engine_render_next/*.cpp TUs
//! and the per-backend test bootstraps (tests/render_facade/
//! bootstrap_next.cpp, the render_next_smoke main) may include it.
//!
//! B1 SKELETON STATE (Docs/render-abstraction.md phase A2/WP-A2.1): the
//! boot path is REAL - Ogre::Root + the Metal render system + an
//! SDL-hosted window + a CompositorManager2 clear workspace + window
//! screenshots + resource locations + RenderNode/RenderCamera. The
//! content classes (MeshInstance, SpriteQuad, RenderLight,
//! RenderTexture, ray queries, cube-mesh service) are honest stubs:
//! they compile, return safe defaults and log ONCE per feature
//! ("not implemented on the next backend yet"). render_facade_selfcheck
//! is EXPECTED to fail on this backend until B2 (WP-A2.2/A2.3) fills
//! the stubs - it is registered DISABLED in ctest for the next flavor.
//!
//! Unlike classic (where Engine bootstraps OGRE and the facade wraps
//! it), the RenderSystem facade IS the boot on Next: RenderBackend::
//! createRenderSystem(NextBootOptions) brings the whole renderer up.

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

#include <core_debug/DebugMacros.h>	// oAssert (classic gets it via the EnginePrerequisites umbrella, which this flavor does not compile)

#include <chrono>
#include <vector>

#ifndef ORKIGE_RENDER_NEXT
#	error "engine_render_next compiled without ORKIGE_RENDER_NEXT - the build flavor wiring regressed (see ORKIGE_RENDER_BACKEND in CMake)"
#endif

namespace Ogre
{
	class Root;
	class Window;
	class SceneManager;
	class SceneNode;
	class Camera;
	class CompositorWorkspace;
}

namespace Orkige
{
	//--- backend state of the facade classes (pimpl bodies) -----------

	struct RenderSystem::Impl
	{
		Ogre::Root*			root = NULL;			//!< owned by the backend (destroyRenderSystem)
		Ogre::Window*		window = NULL;			//!< owned by root
		RenderWorld*		world = NULL;			//!< owned, deleted in ~RenderSystem
		optr<RenderCamera>	windowCamera;			//!< camera shown full-window (keeps it alive)
		Ogre::ColourValue	windowBackground = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
		Ogre::CompositorWorkspace*	workspace = NULL;	//!< the window clear/render workspace (owned by CompositorManager2)
		//--- frame timing (FrameStats on a backend without per-target stats)
		std::chrono::steady_clock::time_point	lastFrameTime;
		bool				haveLastFrameTime = false;
		float				lastFPS = 0.0f;
		float				avgFPS = 0.0f;
	};

	struct RenderWorld::Impl
	{
		Ogre::SceneManager*	sceneManager = NULL;	//!< owned by the backend (root teardown)
		optr<RenderNode>	rootNode;				//!< stable facade handle of the root node (owned=false)
		Ogre::ColourValue	ambient = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);	//!< facade cache (Next splits ambient into hemispheres)
	};

	struct RenderNode::Impl
	{
		Ogre::SceneNode*	node = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;			//!< false for the world root (never destroyed by the handle)
		void*				userPointer = NULL;		//!< @see RenderNode::setUserPointer
		woptr<RenderNode>	parent;					//!< facade graph mirror (backend child lists are never walked)
		std::vector<woptr<RenderNode>>	children;	//!< facade graph mirror, pruned lazily
		//! v2 nodes store transforms SoA and return them BY VALUE - the
		//! facade getters return const refs, so the last read is cached
		mutable Ogre::Vector3		positionCache = Ogre::Vector3::ZERO;
		mutable Ogre::Quaternion	orientationCache = Ogre::Quaternion::IDENTITY;
		mutable Ogre::Vector3		scaleCache = Ogre::Vector3::UNIT_SCALE;
	};

	//--- B1 stubs: no backend objects yet, facade-side state only -----

	struct MeshInstance::Impl
	{
		String				meshName;
		optr<RenderNode>	attachedTo;
	};

	struct SpriteQuad::Impl
	{
		String				textureName;
		optr<RenderNode>	attachedTo;
	};

	struct RenderCamera::Impl
	{
		Ogre::Camera*		camera = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;
		optr<RenderNode>	attachedTo;
	};

	struct RenderLight::Impl
	{
		optr<RenderNode>	attachedTo;
		LightType			type = LT_POINT;
	};

	struct RenderTexture::Impl
	{
		String				name;
		unsigned int		width = 0;
		unsigned int		height = 0;
		optr<RenderCamera>	camera;
	};

	//--- the backend hub -----------------------------------------------
	//! @brief the Ogre-Next backend's cross-class door (befriended by
	//! every facade class, @see RenderPrerequisites.h)
	struct RenderBackend
	{
		//! @brief everything the Next boot needs from the host app
		//! @remarks the sanctioned boot block of a Next-flavor app is
		//! exactly: SDL window creation + filling this struct + one
		//! createRenderSystem call (the classic counterpart is the
		//! Engine ctor/setup block, see Docs/render-abstraction.md)
		struct NextBootOptions
		{
			String			windowTitle = "Orkige";
			unsigned int	width = 1280;
			unsigned int	height = 720;
			//! stringified native handle (NSWindow* on macOS - the
			//! engine_util/SDLNativeWindow.mm bridge); empty lets
			//! Ogre-Next create its own window (tests/headless)
			String			nativeWindowHandle;
			String			logFileName = "orkige_next.log";
			//! directory containing Hlms/{Common,Pbs,Unlit} shader
			//! templates (the ogre-next port ships them under
			//! share/ogre-next/Media); empty skips Hlms registration
			//! (enough for clear-only rendering, nothing with materials)
			String			hlmsMediaDir;
		};

		//--- lifecycle ---------------------------------------------
		//! boot Ogre-Next (Root + Metal RS + window + world) and create
		//! the facade RenderSystem; idempotent (returns the live system)
		static RenderSystem* createRenderSystem(NextBootOptions const & options);
		//! tear the facade AND the Ogre-Next root down again (idempotent)
		static void destroyRenderSystem();
		//! the live RenderSystem or NULL (backs RenderSystem::get)
		static RenderSystem* system();

		//--- handle factories (protected facade ctors) --------------
		//! wrap an existing backend node into a facade handle and
		//! register it; parent is the facade-graph parent (NULL for root)
		static optr<RenderNode> wrapNode(Ogre::SceneNode* node, bool owned,
			optr<RenderNode> const & parent);
		static optr<RenderCamera> createCamera(
			Ogre::SceneManager* sceneManager, String const & name);

		//--- guts accessors (NULL-safe) ------------------------------
		static Ogre::SceneNode* sceneNode(optr<RenderNode> const & node);
		static Ogre::Camera* ogreCamera(optr<RenderCamera> const & camera);

		//--- node registry (Ogre::SceneNode* -> facade handle) -------
		static void registerNode(Ogre::SceneNode* node,
			optr<RenderNode> const & handle);
		static void unregisterNode(Ogre::SceneNode* node);
		static optr<RenderNode> findNode(Ogre::SceneNode* node);
		static void* findUserPointerUpwards(Ogre::SceneNode* node);

		//--- shared services -----------------------------------------
		//! unique name for backend-created content ("prefix.<n>")
		static String generateName(String const & prefix);
		//! (re)build the window workspace from the stored camera +
		//! background colour (Next renders NOTHING without a workspace)
		static void recreateWindowWorkspace();
		//! B1 stub discipline: log the missing feature ONCE, stay silent
		//! afterwards - callers then return their safe default
		static void notImplementedOnce(char const * feature);
	};
}

#endif //__NextBackend_h__8_7_2026__20_00_00__
