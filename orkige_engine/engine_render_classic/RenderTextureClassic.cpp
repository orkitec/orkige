/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderTextureClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderTextureClassic.cpp
//! @brief classic-OGRE implementation of the RenderTexture facade
//! @remarks the editor's SceneRenderTarget pattern moved behind the
//! facade: a manual TU_RENDERTARGET texture whose render target carries
//! one viewport; resize recreates the texture (so getNativeTextureId
//! changes - re-fetch it per frame, the ImGui overlay resolves ids per
//! draw call and degrades gracefully for the one visible frame)

#include "engine_render_classic/ClassicBackend.h"

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
	Ogre::TexturePtr RenderBackend::ogreTexture(
		optr<RenderTexture> const & texture)
	{
		return texture ? texture->mImpl->texture : Ogre::TexturePtr();
	}
	//---------------------------------------------------------
	void RenderTexture::Impl::destroyTexture()
	{
		if(this->texture)
		{
			// the 2D layer's RenderTexture binder material may hold a
			// TexturePtr to this incarnation - drop the material so nothing
			// keeps the dead texture's GPU memory alive (it is recreated
			// lazily against the next incarnation; strict backends like
			// Vulkan assert on allocations outliving the render system)
			Ogre::MaterialPtr binder = Ogre::MaterialManager::getSingleton()
				.getByName("DrawLayer2D/RenderTextureBinder");
			if(binder)
			{
				Ogre::TextureUnitState* unit = binder->getTechnique(0)
					->getPass(0)->getTextureUnitState(0);
				if(unit && unit->_getTexturePtr() == this->texture)
				{
					Ogre::MaterialManager::getSingleton().remove(binder);
				}
			}
			this->texture->getBuffer()->getRenderTarget()->removeAllViewports();
			Ogre::TextureManager::getSingleton().remove(this->texture);
			this->texture.reset();
		}
	}
	//---------------------------------------------------------
	void RenderTexture::Impl::recreate()
	{
		this->destroyTexture();
		this->texture = Ogre::TextureManager::getSingleton().createManual(
			this->name, Ogre::RGN_INTERNAL, Ogre::TEX_TYPE_2D,
			static_cast<Ogre::uint>(this->width),
			static_cast<Ogre::uint>(this->height), 0,
			Ogre::PF_BYTE_RGB, Ogre::TU_RENDERTARGET);
		Ogre::Camera* backendCamera = RenderBackend::ogreCamera(this->camera);
		if(!backendCamera)
		{
			return;	// viewport arrives with setCamera
		}
		Ogre::RenderTarget* renderTarget =
			this->texture->getBuffer()->getRenderTarget();
		Ogre::Viewport* viewport = renderTarget->addViewport(backendCamera);
		RenderBackend::applyRTSSScheme(viewport);
		this->applyViewportState();
		backendCamera->setAspectRatio(
			Ogre::Real(this->width) / Ogre::Real(this->height));
	}
	//---------------------------------------------------------
	void RenderTexture::Impl::applyViewportState()
	{
		if(!this->texture)
		{
			return;
		}
		Ogre::RenderTarget* renderTarget =
			this->texture->getBuffer()->getRenderTarget();
		if(renderTarget->getNumViewports() == 0)
		{
			return;
		}
		Ogre::Viewport* viewport = renderTarget->getViewport(0);
		viewport->setBackgroundColour(this->background);
		viewport->setOverlaysEnabled(this->overlaysEnabled);
		viewport->setShadowsEnabled(this->shadowsEnabled);
	}
	//---------------------------------------------------------
	RenderTexture::RenderTexture()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderTexture::~RenderTexture()
	{
		this->mImpl->destroyTexture();
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderTexture::setCamera(optr<RenderCamera> const & camera)
	{
		oAssert(camera);
		this->mImpl->camera = camera;
		// simplest correct path: rebuild target + viewport around the new
		// camera (identical cost profile to the editor's resize path)
		this->mImpl->recreate();
	}
	//---------------------------------------------------------
	void RenderTexture::setBackgroundColour(Color const & colour)
	{
		this->mImpl->background = colour;
		this->mImpl->applyViewportState();
	}
	//---------------------------------------------------------
	void RenderTexture::setOverlaysEnabled(bool enabled)
	{
		this->mImpl->overlaysEnabled = enabled;
		this->mImpl->applyViewportState();
	}
	//---------------------------------------------------------
	void RenderTexture::setShadowsEnabled(bool enabled)
	{
		this->mImpl->shadowsEnabled = enabled;
		this->mImpl->applyViewportState();
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
		// the resource handle doubles as the ImGui texture id - OGRE's
		// ImGuiOverlay resolves nonzero ids via TextureManager::getByHandle
		return this->mImpl->texture
			? static_cast<unsigned long long>(this->mImpl->texture->getHandle())
			: 0ULL;
	}
	//---------------------------------------------------------
	void RenderTexture::writeContentsToFile(String const & fileName) const
	{
		oAssert(this->mImpl->texture);
		this->mImpl->texture->getBuffer()->getRenderTarget()
			->writeContentsToFile(fileName);
	}
}
