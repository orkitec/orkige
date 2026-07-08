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
//! changes across resizes (re-fetch per frame). Overlays/shadows
//! toggles are facade caches: the Next flavor compiles no overlay
//! component and the basic workspace has no shadow node, so "off"
//! already holds structurally. writeContentsToFile is a plain
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
			// a 2D-layer batch may have bound this incarnation into the
			// per-target "DrawLayer2D/RTT/<name>" datablock - detach before
			// the texture dies (the next batch build re-points it)
			if(Ogre::HlmsDatablock* datablock = root->getHlmsManager()
				->getDatablockNoDefault("DrawLayer2D/RTT/" + this->name))
			{
				Ogre::HlmsUnlitDatablock* unlitBlock =
					static_cast<Ogre::HlmsUnlitDatablock*>(datablock);
				if(unlitBlock->getTexture(0u) == this->texture)
				{
					unlitBlock->setTexture(0u, (Ogre::TextureGpu*)NULL);
				}
			}
			root->getRenderSystem()->getTextureGpuManager()
				->destroyTexture(this->texture);
			this->texture = NULL;
		}
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
		if(!backendCamera)
		{
			return;	// workspace arrives with setCamera
		}
		// one (clear + scene) workspace per target incarnation; the
		// background colour bakes into the definition's clear pass.
		// Hand-built instead of createBasicWorkspaceDef since the
		// DrawLayer2D port: the scene pass stops BELOW the UI render
		// queue, so 2D layers never leak into offscreen targets (the
		// facade contract - they composite over the main window only)
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
		targetDefinition->setNumPasses(1);
		{
			Ogre::CompositorPassSceneDef* scenePass =
				static_cast<Ogre::CompositorPassSceneDef*>(
					targetDefinition->addPass(Ogre::PASS_SCENE));
			scenePass->setAllLoadActions(Ogre::LoadAction::Clear);
			scenePass->setAllClearColours(this->background);
			scenePass->mFirstRQ = 0;
			scenePass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
		}
		Ogre::CompositorWorkspaceDef* workspaceDefinition =
			compositorManager->addWorkspaceDefinition(definitionName);
		workspaceDefinition->connectExternal(0, definitionName + "/Node", 0);
		this->workspace = compositorManager->addWorkspace(
			RenderBackend::worldSceneManager(), this->texture,
			backendCamera, definitionName, true /*enabled*/);
		backendCamera->setAspectRatio(
			Ogre::Real(this->width) / Ogre::Real(this->height));
	}
	//---------------------------------------------------------
	RenderTexture::RenderTexture()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderTexture::~RenderTexture()
	{
		this->mImpl->destroyTarget();
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
		// facade cache only: the basic workspace carries no shadow node
		this->mImpl->shadowsEnabled = enabled;
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
		// is classic-only today (decided question #3)
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
}
