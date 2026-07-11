/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderTexture.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderTexture_h__8_7_2026__12_00_00__
#define __RenderTexture_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>
#include <memory>

namespace Orkige
{
	//! @brief an offscreen render target - the editor's RTT scene panel
	//! @remarks API sized by tools/editor's SceneRenderTarget: create at a
	//! size, show a camera, resize on panel changes, hand the texture to
	//! ImGui, save to file. Resize recreates the backend texture (that is
	//! what the editor does today); getNativeTextureId therefore changes
	//! after resize - re-fetch it every frame.
	//!
	//! Backend mapping (whole class): classic = TextureManager::createManual
	//! (TU_RENDERTARGET) + getBuffer()->getRenderTarget()->addViewport;
	//! next = TextureGpuManager::createTexture(RenderToTexture) + compositor
	//! workspace targeting it; filament = Texture(SAMPLEABLE|COLOR_ATTACHMENT)
	//! + RenderTarget::Builder + a dedicated View.
	//!
	//! Inherits enable_shared_from_this so a target can hand its own handle
	//! to the layers it owns (createLayer): a layer keeps its target alive,
	//! so the target's per-surface state (its 2D visibility band) outlives
	//! every layer that composites into it.
	class ORKIGE_ENGINE_DLL RenderTexture
		: public std::enable_shared_from_this<RenderTexture>
	{
		//--- Types -------------------------------------------------
	public:
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend target guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - removes viewports and destroys the backend texture
		//! map: classic=RenderTarget::removeAllViewports+TextureManager::remove | next=destroy workspace+TextureGpu | filament=destroy View/RenderTarget/Texture
		~RenderTexture();

		//--- camera / viewport ---
		//! @brief render the given camera into this texture (replaces the
		//! previous one); also sets the camera's aspect ratio
		//! map: classic=RenderTarget::addViewport(camera) | next=compositor workspace with the camera | filament=View::setCamera
		void setCamera(optr<RenderCamera> const & camera);
		//! clear colour of the target
		//! map: classic=Viewport::setBackgroundColour | next=compositor clear pass colour | filament=Renderer::setClearOptions per View
		void setBackgroundColour(Color const & colour);
		//! @brief keep 2D overlays (ImGui, Gui) out of / in the target
		//! (the editor turns them OFF so the scene panel shows only scene)
		//! map: classic=Viewport::setOverlaysEnabled | next=overlay compositor pass toggle | filament=separate UI View (nothing to toggle)
		void setOverlaysEnabled(bool enabled);
		//! map: classic=Viewport::setShadowsEnabled | next=shadow node toggle in the workspace | filament=View::setShadowingEnabled
		void setShadowsEnabled(bool enabled);

		//--- size ---
		//! @brief recreate the target at a new size (editor panel resize);
		//! keeps camera and viewport settings
		void resize(unsigned int width, unsigned int height);
		unsigned int getWidth() const;
		unsigned int getHeight() const;

		//--- offscreen 2D composition (the GUI Preview stage) ---
		//! @brief does this backend support 2D layers compositing INTO the
		//! target (createLayer)? The generalization of the window-only
		//! DrawLayer2D contract to per-target surfaces. Ogre-Next: yes;
		//! classic OGRE: no (the compositor hook is main-window-only there,
		//! so the editor shows its GUI Preview tab disabled - see the flavor
		//! capability matrix in Docs/render-abstraction.md).
		//! map: classic=false | next=true | filament=true (dedicated UI View)
		static bool canOwnLayers();
		//! @brief create a 2D overlay layer that composites INTO this target
		//! (not the main window) at the target's OWN pixel size - the same
		//! DrawLayer2D contract (@see DrawLayer2D) but with this RenderTexture
		//! as the surface. The GUI Preview stage points a whole gui at a
		//! preview RTT this way. Empty (NULL) on a backend where
		//! canOwnLayers() is false. Ordering among a target's layers follows
		//! zOrder then creation order, exactly like the window layers.
		//! @remarks a target that owns layers composites them AFTER its 3D
		//! scene pass (when a camera is set) or over its clear colour (when
		//! none is - a pure UI surface, the preview case). The layers are
		//! their own painter band per target; the window's 2D layers never
		//! leak in and vice versa.
		//! map: classic=unsupported (NULL) | next=per-target UI pass in the target workspace, own visibility band | filament=dedicated UI View on the target
		optr<DrawLayer2D> createLayer(int zOrder = 0);

		//--- consumption ---
		//! @brief opaque renderer-API texture id (invalidated by resize -
		//! re-fetch per frame). NOTE: showing an RTT inside ImGui no longer
		//! needs this - DrawLayer2D::addTriangles binds the facade HANDLE
		//! (resize-safe); this id stays for external integrations.
		//! map: classic=Ogre resource handle (TextureManager::getByHandle) | next=the TextureGpu* (API texture via getCustomAttribute) | filament=filament::Texture*
		unsigned long long getNativeTextureId() const;
		//! @brief write the current contents to an image file
		//! map: classic=RenderTarget::writeContentsToFile | next=TextureGpu readback (TextureBox download) + save | filament=Renderer::readPixels + encode
		void writeContentsToFile(String const & fileName) const;
	protected:
		//! render textures are created by RenderSystem::createRenderTexture only
		RenderTexture();
	private:
		RenderTexture(RenderTexture const &);				// non-copyable
		RenderTexture & operator=(RenderTexture const &);	// non-copyable
	};
}

#endif //__RenderTexture_h__8_7_2026__12_00_00__
