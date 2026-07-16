/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderSystem.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderSystem_h__8_7_2026__12_00_00__
#define __RenderSystem_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include "engine_render/RenderMaterial.h"
#include "engine_render/RenderWater.h"
#include "engine_render/RenderCaps.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief the renderer entry point: startup, main window, frame loop,
	//! resources and the small services every app calls today
	//! @remarks The facade RenderSystem is what engine_graphic/Engine
	//! becomes: Engine implements this surface and apps migrate
	//! to it; the config-file/window plumbing stays in Engine (backend-
	//! independent). Frame events (FrameStartedEvent etc.) stay on the core
	//! event system, fired around renderOneFrame - they are not part of the
	//! render facade.
	//!
	//! NAMING NOTE: "render system" is overloaded. THIS class is the facade
	//! entry per BACKEND (classic OGRE vs Ogre-Next vs Filament, chosen at
	//! BUILD time via ORKIGE_RENDER_BACKEND - ODR forbids runtime choice).
	//! setPreferredRenderSystem/ORKIGE_RENDERSYSTEM below keeps its existing
	//! meaning: the WITHIN-CLASSIC graphics API choice (GL3Plus/Metal/
	//! Vulkan/GLES2), a runtime pick among the plugins one backend linked.
	//!
	//! Backend mapping (whole class): classic = Ogre::Root + RenderWindow
	//! (what Engine drives today); next = Ogre::Root + Window + a
	//! CompositorManager2 workspace (Next renders nothing without a
	//! workspace); filament = filament::Engine + SwapChain + Renderer.
	class ORKIGE_ENGINE_DLL RenderSystem
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief frame statistics (editor stats panel, selfcheck probes)
		//! map: classic=RenderTarget::getStatistics()/FrameStats | next=CompositorWorkspace stats+RenderSystem metrics | filament=Renderer::getFrameInfo (triangle count approximated)
		struct ORKIGE_ENGINE_DLL FrameStats
		{
			float	lastFPS;		//!< fps of the last completed frame
			float	avgFPS;			//!< smoothed fps
			float	bestFPS;		//!< best fps since the last resetFrameStats
			float	worstFPS;		//!< worst fps since the last resetFrameStats
			size_t	triangleCount;	//!< triangles submitted last frame
			size_t	batchCount;		//!< draw batches last frame
			size_t	textureMemoryBytes;	//!< texture memory the backend reports (0 where it has no number)
			FrameStats();			// defined by the backend TU
		};
		//! how a resource location is stored on disk
		//! map: classic/next=Ogre archive type name ("FileSystem"/"Zip"/BigZip) | filament=impl-side VFS
		enum LocationType
		{
			LT_FILESYSTEM = 0,	//!< plain directory
			LT_ZIP,				//!< zip archive
			LT_BIGZIP			//!< engine_filesystem BigZip sub-tree archive
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
		Impl*	mImpl;	//!< backend root guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief the process-wide render system (created by Engine in A1)
		//! @remarks NULL before Engine::setup - mirrors the existing
		//! Engine singleton access pattern
		static RenderSystem* get();

		//--- capabilities ---
		//! @brief does the active render backend support a capability? The one
		//! probe surface for every flavor delta - a single call in place of a
		//! per-capability accessor. The set is filled ONCE at boot per backend;
		//! @see RenderCaps (the X-macro vocabulary), the Lua
		//! `engine:supports("skyDome")` binding, MCP get_state's capabilities
		//! object, and the generated matrix in Docs/render-abstraction.md.
		//! map: classic/next=a per-backend bitset set in createRenderSystem | filament=its own set
		bool supports(RenderCaps cap) const;

		//--- frame loop ---
		//! @brief render one frame to all active targets
		//! map: classic/next=Root::renderOneFrame | filament=Renderer::beginFrame/render(views)/endFrame
		bool renderOneFrame();

		//--- main window ---
		//! @brief show a camera full-window (createDefaultCameraAndViewport
		//! successor); replaces the previous window camera
		//! map: classic=RenderWindow::addViewport | next=compositor workspace on the window | filament=main View::setCamera
		void showCameraOnWindow(optr<RenderCamera> const & camera);
		//! @brief the camera currently shown on the main window, or NULL when
		//! none is - what CameraComponent takes over when it attaches
		//! @remarks classic migration note: when the window camera was set up
		//! through Engine::createDefaultCameraAndViewport (every app),
		//! this wraps that camera into a facade handle, so component
		//! code stays backend-free while the Engine path is still live
		//! map: classic=facade handle over the window viewport's camera | next=workspace camera | filament=main View camera
		optr<RenderCamera> getWindowCamera() const;
		//! @brief switch the main window into UI-ONLY mode: it stops
		//! rendering the 3D scene and composites nothing but 2D layers
		//! (DrawLayer2D) over the window background colour - the editor
		//! shell's mode (its scene renders offscreen into a RenderTexture
		//! shown inside the ImGui layer). Replaces any window camera;
		//! getWindowCamera answers NULL while this mode is active
		//! (a later showCameraOnWindow switches back).
		//! map: classic=window viewport with visibility mask 0 (an internal camera feeds it - viewports need one) | next=window workspace with a clear + 2D-queue-only pass | filament=main View without a scene
		void showUIOnlyWindow();
		//! window clear colour (Engine::setViewportBackgroundColour successor)
		//! map: classic=Viewport::setBackgroundColour | next=workspace clear colour | filament=Renderer::setClearOptions
		void setWindowBackgroundColour(Color const & colour);
		//! pixel size of the main window's drawable (HUD layout, aspect)
		//! map: classic=Viewport::getActualWidth/Height | next=Window::getWidth/Height | filament=SwapChain size (tracked)
		void getWindowSize(unsigned int & outWidth, unsigned int & outHeight) const;
		//! @brief the host resized the window (SDL resize event handler)
		//! map: classic=RenderWindow::windowMovedOrResized | next=Window::requestResolution/notify | filament=SwapChain recreation
		void notifyWindowResized();
		//! @brief write the window contents to an image file (screenshots -
		//! five call sites today go through the RenderWindow directly)
		//! map: classic=RenderWindow::writeContentsToFile | next=TextureGpu readback of the window | filament=Renderer::readPixels + encode
		void saveWindowContents(String const & fileName) const;

		//--- offscreen targets ---
		//! @brief create an offscreen target (editor scene panel)
		//! @see RenderTexture for the mapping
		optr<RenderTexture> createRenderTexture(String const & name,
			unsigned int width, unsigned int height);

		//--- screen-space 2D ---
		//! @brief create a 2D overlay layer compositing over the main
		//! window (gui HUDs, ImGui) - @see DrawLayer2D for the contract
		//! @param zOrder layers composite in ascending zOrder (ties:
		//! creation order)
		optr<DrawLayer2D> createDrawLayer2D(int zOrder = 0);
		//! @brief load a 2D texture through the resource system (any
		//! group, like SpriteQuad textures) and report its texel size -
		//! the 2D layer clients (atlas metrics) lay out against it
		//! @return false + a log line when the resource is missing/broken
		//! map: classic=TextureManager::load + getWidth/Height | next=TextureGpuManager createOrRetrieve + waitForMetadata | filament=impl decode + Texture dims
		bool getTextureSize(String const & textureName,
			unsigned int & outWidth, unsigned int & outHeight) const;
		//! @brief create (or replace) a 2D texture from raw RGBA8 pixels
		//! under a name the 2D layer batches can bind (the ImGui font
		//! atlas service anticipated in DrawLayer2D.h). Pixels are tightly
		//! packed rows of width*4 bytes (R,G,B,A order), straight alpha,
		//! copied on the call. Re-uploading under an existing name
		//! replaces the contents (atlas rebuilds).
		//! @return false + a log line when the backend refuses the upload
		//! map: classic=TextureManager::createManual + blitFromMemory | next=TextureGpu + Image2 raw upload | filament=Texture::setImage
		bool createTexture2D(String const & name,
			unsigned char const * rgbaPixels,
			unsigned int width, unsigned int height);
		//! @brief destroy a createTexture2D upload again (idempotent; the
		//! owner calls this before the render system goes down - manual
		//! textures are not resource-group content, so nothing else frees
		//! their GPU memory in time for strict backends like Vulkan)
		//! map: classic=TextureManager::remove (+ drop the cached 2D-layer material) | next=TextureGpuManager::destroyTexture (+ datablock detach) | filament=Engine::destroy(texture)
		void destroyTexture2D(String const & name);

		//--- scene-content materials ---
		//! @brief create OR UPDATE the named scene-content material from a
		//! PBS surface description (@see RenderMaterialDesc for the
		//! per-backend capability statement). Idempotent per name: calling
		//! again with new values updates the LIVE material, so everything
		//! rendering with it follows (the scalar-drive path - e.g. lowering
		//! `roughness` on a ground material to read wet). Texture names
		//! resolve through the resource groups.
		//! @return false + a log line when a referenced texture is missing
		//! (the material is still created/updated with everything that DID
		//! resolve) or when the name collides with a different material
		//! family (a generated sprite/unlit material)
		//! map: classic=MaterialManager Blinn-Phong material (approximation, @see RenderMaterialDesc) | next=HlmsPbs datablock (metallic workflow) | filament=lit material instance
		bool createMaterial(String const & name, RenderMaterialDesc const & desc);

		//--- animated water surfaces ---
		//! @brief create OR UPDATE the named animated water material from a
		//! water surface description (@see RenderWaterDesc for the per-backend
		//! capability statement). Idempotent per name: calling again with new
		//! values updates the LIVE material (the colour/opacity/wave-drive
		//! path). The normal-map name resolves through the resource groups.
		//! @return false + a log line when the referenced normal map is missing
		//! (the material is still created/updated with everything that DID
		//! resolve) or when the name collides with a different material family
		//! map: classic=MaterialManager transparent Blinn-Phong plane + scrolling shimmer (approximation, @see RenderWaterDesc) | next=HlmsPbs datablock (two scrolling detail normal maps + fresnel transparency)
		bool createWaterMaterial(String const & name, RenderWaterDesc const & desc);
		//! @brief advance a water material's ripple animation to @p seconds - a
		//! cheap per-frame material-parameter update (scrolls the surface's
		//! normal detail, no per-vertex CPU work). A name with no water material
		//! is a silent no-op (so a paused editor that never ticks leaves the
		//! surface static - the established dormancy rule).
		//! map: classic=TextureUnitState::setTextureScroll on the shimmer unit | next=HlmsPbsDatablock::setDetailMapOffsetScale on the two detail normals
		void setWaterTime(String const & name, float seconds);

		//--- the scene ---
		//! the one world (multiple worlds stay a facade-compatible extension)
		RenderWorld* getWorld() const;

		//--- resources ---
		//! @brief register a resource location (every app repeats the raw
		//! ResourceGroupManager calls today - this replaces them)
		//! @param groupName empty = the default/general group
		//! map: classic/next=ResourceGroupManager::addResourceLocation | filament=impl-side search path list
		void addResourceLocation(String const & path,
			LocationType type = LT_FILESYSTEM,
			String const & groupName = "",
			bool recursive = false);
		//! @brief parse scripts/indices of everything registered so far
		//! map: classic/next=ResourceGroupManager::initialiseAllResourceGroups | filament=no-op
		void initialiseResourceGroups();
		//! @brief unregister a resource location again (idempotent - a
		//! location that was never registered is a no-op). The editor's mesh
		//! import re-registers a directory to re-index a just-copied file.
		//! map: classic/next=ResourceGroupManager::removeResourceLocation | filament=drop from the impl search path list
		void removeResourceLocation(String const & path,
			String const & groupName = "");
		//! @brief does the resource group exist (was anything registered
		//! under it)? Project switching probes before tearing down.
		//! map: classic/next=ResourceGroupManager::resourceGroupExists | filament=impl-side group table
		bool resourceGroupExists(String const & groupName) const;
		//! @brief unload and unregister EVERYTHING in the group (the editor's
		//! clean project switch - name-cached resources must not leak into
		//! the next project); no-op when the group does not exist
		//! map: classic/next=ResourceGroupManager::destroyResourceGroup | filament=drop the impl group + its paths
		void destroyResourceGroup(String const & groupName);
		//! @brief is a resource of that name indexed (loadable) in the group?
		//! @param groupName empty = the default/general group
		//! map: classic/next=ResourceGroupManager::resourceExists | filament=impl VFS lookup
		bool resourceExists(String const & resourceName,
			String const & groupName = "") const;
		//! @brief read a named text resource (searched across ALL groups, like
		//! sprite textures) into a string; false (outText untouched) when it is
		//! not found. Lets backend-neutral code (e.g. SpriteComponent's .oatlas
		//! loader) read small config resources without touching the renderer.
		//! map: classic/next=ResourceGroupManager::openResource(...).getAsString
		bool readResourceText(String const & resourceName,
			String & outText) const;

		//--- stats ---
		//! @see RenderSystem::FrameStats
		FrameStats getFrameStats() const;
		//! restart the best/worst fps tracking (gui's stats HUD resets
		//! them per game state; average follows the backend's own window)
		//! map: classic=RenderTarget::resetStatistics | next=backend-tracked min/max reset | filament=impl counters reset
		void resetFrameStats();
	protected:
		//! constructed by the backend (Engine::setup) only
		RenderSystem();
		~RenderSystem();
	private:
		RenderSystem(RenderSystem const &);					// non-copyable
		RenderSystem & operator=(RenderSystem const &);		// non-copyable
	};
}

#endif //__RenderSystem_h__8_7_2026__12_00_00__
