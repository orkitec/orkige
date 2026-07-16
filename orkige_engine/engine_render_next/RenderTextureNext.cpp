/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderTextureNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderTextureNext.cpp
//! @brief Ogre-Next implementation of the RenderTexture facade
//! @remarks the doc's mapping made real: a TextureGpu created with
//! RenderToTexture + one basic compositor workspace per target that
//! renders the fed camera into it every frame (Next renders nothing
//! without a workspace). setCamera/resize recreate texture+workspace -
//! same resize-by-recreate contract as classic, so getNativeTextureId
//! changes across resizes (re-fetch per frame). The overlays toggle is
//! a facade cache (no overlay component compiles on this flavor); the
//! shadows toggle is real: while the WORLD's shadows are active (@see
//! RenderBackend::activeShadowNodeName) an enabled target's scene pass
//! carries the PSSM shadow node, a disabled one stays shadow-free.
//! writeContentsToFile is a plain
//! TextureGpu readback (Image2::convertFromTexture), no manual-swap
//! dance needed - only the WINDOW backbuffer needs that on Metal.

#include "engine_render_next/NextBackend.h"

#include <OgreRoot.h>
#include <OgreCamera.h>
#include <OgreSceneManager.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreImage2.h>
#include <OgrePixelFormatGpuUtils.h>
#include <OgreRenderSystem.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlitDatablock.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorWorkspaceDef.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h>

namespace Orkige
{
	//---------------------------------------------------------
	optr<RenderTexture> RenderBackend::createRenderTexture(String const & name,
		unsigned int width, unsigned int height)
	{
		oAssert(!name.empty());
		oAssert(width > 0 && height > 0);
		optr<RenderTexture> handle(new RenderTexture());
		handle->mImpl->name = name;
		handle->mImpl->width = width;
		handle->mImpl->height = height;
		// registered so a shadow-state change rebuilds this workspace too
		RenderBackend::registerRenderTarget(handle.get());
		handle->mImpl->recreate();
		return handle;
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::renderTextureGpu(
		optr<RenderTexture> const & texture)
	{
		return texture ? texture->mImpl->texture : NULL;
	}
	//---------------------------------------------------------
	String RenderBackend::renderTextureName(optr<RenderTexture> const & texture)
	{
		return texture ? texture->mImpl->name : String();
	}
	//---------------------------------------------------------
	void RenderTexture::Impl::destroyTarget()
	{
		Ogre::Root* root = RenderBackend::ogreRoot();
		if(!root)
		{
			return;	// late teardown: everything died with the Ogre root
		}
		if(this->workspace)
		{
			root->getCompositorManager2()->removeWorkspace(this->workspace);
			this->workspace = NULL;
		}
		if(this->texture)
		{
			// this incarnation owns its own "DrawLayer2D/RTT/<backendTexName>"
			// datablock (per-incarnation identity, DrawLayer2DNext.cpp). Retire
			// it: the texture-gpu manager reuses this pointer for the next
			// incarnation, so a datablock kept across the recreate would bind a
			// descriptor set the driver caches against the pointer - this
			// incarnation's freed image view. The retired datablock is dropped
			// once its batch stops linking it (flushRetiredRTTDatablocks).
			RenderBackend::retireRTTDatablock(
				RenderBackend::rttDatablockName(this->texture));
			root->getRenderSystem()->getTextureGpuManager()
				->destroyTexture(this->texture);
			this->texture = NULL;
		}
	}
	//---------------------------------------------------------
	String RenderTexture::Impl::uiCameraName() const
	{
		return this->uiCamName;
	}
	//---------------------------------------------------------
	void RenderTexture::Impl::recreate()
	{
		this->destroyTarget();
		Ogre::Root* root = RenderBackend::ogreRoot();
		oAssert(root);
		Ogre::TextureGpuManager* textureManager =
			root->getRenderSystem()->getTextureGpuManager();
		// unique backend name per incarnation (resize-by-recreate may
		// overlap with the old texture's async destruction)
		this->texture = textureManager->createTexture(
			RenderBackend::generateName(this->name),
			Ogre::GpuPageOutStrategy::Discard,
			Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
		this->texture->setResolution(this->width, this->height);
		// non-sRGB, like every surface of this backend (classic colour
		// parity - see the boot's "gamma" note in NextBackend.cpp)
		this->texture->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
		this->texture->setNumMipmaps(1u);
		this->texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);

		Ogre::Camera* backendCamera = RenderBackend::ogreCamera(this->camera);
		// the target renders its 2D layers when it hosts any (createLayer):
		// re-shape the per-target pixel-space UI camera to the current size
		if(this->hostsLayers && this->uiCamera)
		{
			RenderBackend::shapeUICamera(this->uiCamera, this->width,
				this->height);
		}
		const bool hasScenePass = (backendCamera != NULL);
		const bool hasUIPass = (this->hostsLayers && this->uiCamera != NULL);
		if(!hasScenePass && !hasUIPass)
		{
			return;	// workspace arrives with setCamera / createLayer
		}
		// one workspace per target incarnation; the background colour bakes
		// into the FIRST pass' clear. Hand-built instead of
		// createBasicWorkspaceDef: the scene pass stops BELOW the UI render
		// queue (so an ordinary RTT never shows 2D layers - the facade
		// contract), and a target that OWNS layers (the GUI Preview surface)
		// adds a UI pass that draws ONLY that queue, masked to the target's
		// own visibility bit, through the per-target pixel-space UI camera.
		// A pure UI surface (no scene camera) is one clear + UI pass.
		Ogre::CompositorManager2* compositorManager =
			root->getCompositorManager2();
		const String definitionName =
			RenderBackend::generateName("Orkige/RTTWorkspace");
		Ogre::CompositorNodeDef* nodeDefinition =
			compositorManager->addNodeDefinition(definitionName + "/Node");
		nodeDefinition->addTextureSourceName("TargetRT", 0,
			Ogre::TextureDefinitionBase::TEXTURE_INPUT);
		nodeDefinition->setNumTargetPass(1);
		Ogre::CompositorTargetDef* targetDefinition =
			nodeDefinition->addTargetPass("TargetRT");
		targetDefinition->setNumPasses((hasScenePass ? 1 : 0) +
			(hasUIPass ? 1 : 0));
		if(hasScenePass)
		{
			Ogre::CompositorPassSceneDef* scenePass =
				static_cast<Ogre::CompositorPassSceneDef*>(
					targetDefinition->addPass(Ogre::PASS_SCENE));
			scenePass->setAllLoadActions(Ogre::LoadAction::Clear);
			scenePass->setAllClearColours(this->background);
			scenePass->mFirstRQ = 0;
			scenePass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
			// dynamic shadows: an enabled target renders with the world's
			// PSSM shadow node while shadows are active (the facade
			// setShadowsEnabled(false) opt-out keeps e.g. the parity RTT
			// byte-stable regardless of the scene's lights)
			if(this->shadowsEnabled)
			{
				const String shadowNode = RenderBackend::activeShadowNodeName();
				if(!shadowNode.empty())
				{
					scenePass->mShadowNode = Ogre::IdString(shadowNode);
				}
			}
		}
		if(hasUIPass)
		{
			Ogre::CompositorPassSceneDef* uiPass =
				static_cast<Ogre::CompositorPassSceneDef*>(
					targetDefinition->addPass(Ogre::PASS_SCENE));
			if(hasScenePass)
			{
				uiPass->setAllLoadActions(Ogre::LoadAction::Load);
			}
			else
			{
				uiPass->setAllLoadActions(Ogre::LoadAction::Clear);
				uiPass->setAllClearColours(this->background);
			}
			uiPass->mFirstRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
			uiPass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE + 1;
			uiPass->mCameraName = this->uiCameraName();
			// draw ONLY this target's 2D batches (its own visibility bit),
			// never the window's or another target's
			uiPass->setVisibilityMask(this->uiVisibilityFlag);
		}
		Ogre::CompositorWorkspaceDef* workspaceDefinition =
			compositorManager->addWorkspaceDefinition(definitionName);
		workspaceDefinition->connectExternal(0, definitionName + "/Node", 0);
		this->workspace = compositorManager->addWorkspace(
			RenderBackend::worldSceneManager(), this->texture,
			backendCamera ? backendCamera : this->uiCamera, definitionName,
			true /*enabled*/);
		if(backendCamera)
		{
			backendCamera->setAspectRatio(
				Ogre::Real(this->width) / Ogre::Real(this->height));
		}
	}
	//---------------------------------------------------------
	RenderTexture::RenderTexture()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderTexture::~RenderTexture()
	{
		RenderBackend::unregisterRenderTarget(this);
		this->mImpl->destroyTarget();
		// the per-target UI camera + its visibility bit outlive the workspace
		// across resizes, so they are freed only here (not in destroyTarget).
		// Any layers that composited into this target are already gone: they
		// held the target alive (optr), so the target dtor runs after them.
		if(this->mImpl->uiCamera && RenderBackend::ogreRoot())
		{
			RenderBackend::worldSceneManager()->destroyCamera(
				this->mImpl->uiCamera);
		}
		this->mImpl->uiCamera = NULL;
		RenderBackend::freeUiVisibilityFlag(this->mImpl->uiVisibilityFlag);
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderTexture::setCamera(optr<RenderCamera> const & camera)
	{
		oAssert(camera);
		this->mImpl->camera = camera;
		// simplest correct path: rebuild target + workspace around the new
		// camera (identical cost profile to the classic backend)
		this->mImpl->recreate();
	}
	//---------------------------------------------------------
	void RenderTexture::setBackgroundColour(Color const & colour)
	{
		this->mImpl->background = colour;
		if(this->mImpl->workspace)
		{
			// the clear colour lives in the workspace definition - rebuild
			// (cheap, editor-frequency operation, same as the window path)
			this->mImpl->recreate();
		}
	}
	//---------------------------------------------------------
	void RenderTexture::setOverlaysEnabled(bool enabled)
	{
		// facade cache only: no overlay component compiles on this flavor,
		// so the target never contains overlays either way (see remarks)
		this->mImpl->overlaysEnabled = enabled;
	}
	//---------------------------------------------------------
	void RenderTexture::setShadowsEnabled(bool enabled)
	{
		if(this->mImpl->shadowsEnabled == enabled)
		{
			return;
		}
		this->mImpl->shadowsEnabled = enabled;
		if(this->mImpl->workspace)
		{
			// the shadow node reference lives in the workspace definition -
			// rebuild (same cheap path as setBackgroundColour)
			this->mImpl->recreate();
		}
	}
	//---------------------------------------------------------
	void RenderTexture::resize(unsigned int width, unsigned int height)
	{
		oAssert(width > 0 && height > 0);
		this->mImpl->width = width;
		this->mImpl->height = height;
		this->mImpl->recreate();
	}
	//---------------------------------------------------------
	unsigned int RenderTexture::getWidth() const
	{
		return this->mImpl->width;
	}
	//---------------------------------------------------------
	unsigned int RenderTexture::getHeight() const
	{
		return this->mImpl->height;
	}
	//---------------------------------------------------------
	unsigned long long RenderTexture::getNativeTextureId() const
	{
		// the opaque per-backend id (facade contract: non-zero while a
		// target exists, changes across resizes): the TextureGpu pointer.
		// A future ImGui-on-Next integration resolves the actual API
		// texture from it (TextureGpu::getCustomAttribute) - the editor
		// is classic-only today (by design)
		return reinterpret_cast<unsigned long long>(this->mImpl->texture);
	}
	//---------------------------------------------------------
	void RenderTexture::writeContentsToFile(String const & fileName) const
	{
		oAssert(this->mImpl->texture);
		Ogre::Image2 image;
		image.convertFromTexture(this->mImpl->texture, 0u, 0u);
		RenderBackend::makeImageAlphaOpaque(image);
		image.save(fileName, 0u, 1u);
	}
	//---------------------------------------------------------
	optr<DrawLayer2D> RenderTexture::createLayer(int zOrder)
	{
		// first layer: allocate this target's visibility bit + its pixel-space
		// UI camera, then rebuild the workspace to grow a UI pass (@see
		// recreate). Later layers reuse them (one bit, one camera per target)
		if(!this->mImpl->hostsLayers)
		{
			this->mImpl->uiVisibilityFlag =
				RenderBackend::allocateUiVisibilityFlag();
			Ogre::SceneManager* sceneManager =
				RenderBackend::worldSceneManager();
			oAssert(sceneManager);
			// unique per incarnation so a preview device switch never collides
			// with the dying incarnation's still-live camera (@see uiCamName)
			this->mImpl->uiCamName = RenderBackend::generateName(
				"Orkige/DrawLayer2D/TargetCamera/" + this->mImpl->name);
			this->mImpl->uiCamera =
				sceneManager->createCamera(this->mImpl->uiCameraName());
			this->mImpl->uiCamera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
			this->mImpl->uiCamera->setNearClipDistance(Ogre::Real(1.0));
			this->mImpl->uiCamera->setFarClipDistance(Ogre::Real(20000.0));
			// pure view-space depth: the batch nodes' z alone decides the
			// painter order (same rule as the window UI camera)
			this->mImpl->uiCamera->mSortMode =
				Ogre::Camera::SortModeDepthRadiusIgnoring;
			this->mImpl->hostsLayers = true;
			this->mImpl->recreate();	// grows the UI pass, shapes the camera
		}
		return RenderBackend::createTargetDrawLayer2D(
			this->shared_from_this(), this->mImpl->uiVisibilityFlag, zOrder);
	}
}
