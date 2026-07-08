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
	class ORKIGE_ENGINE_DLL RenderTexture
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
		//! @brief keep 2D overlays (ImGui, FastGui) out of / in the target
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
