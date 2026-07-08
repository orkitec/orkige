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
#include <core_util/String.h>

namespace Orkige
{
	//! @brief the renderer entry point: startup, main window, frame loop,
	//! resources and the small services every app calls today
	//! @remarks The facade RenderSystem is what engine_graphic/Engine
	//! becomes: in phase A1 Engine implements this surface and apps migrate
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
			size_t	triangleCount;	//!< triangles submitted last frame
			size_t	batchCount;		//!< draw batches last frame
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

		//--- frame loop ---
		//! @brief render one frame to all active targets
		//! map: classic/next=Root::renderOneFrame | filament=Renderer::beginFrame/render(views)/endFrame
		bool renderOneFrame();

		//--- main window ---
		//! @brief show a camera full-window (createDefaultCameraAndViewport
		//! successor); replaces the previous window camera
		//! map: classic=RenderWindow::addViewport | next=compositor workspace on the window | filament=main View::setCamera
		void showCameraOnWindow(optr<RenderCamera> const & camera);
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

		//--- stats ---
		//! @see RenderSystem::FrameStats
		FrameStats getFrameStats() const;
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
