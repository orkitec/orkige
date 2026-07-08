/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderTextureNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderTextureNext.cpp
//! @brief B1 STUB of the RenderTexture facade on Ogre-Next
//! @remarks No backend object exists yet (RenderSystem::
//! createRenderTexture returns NULL and logs once) - safe-default
//! no-ops so the facade links. B2 (WP-A2.3) implements the
//! TextureGpu(RenderToTexture) + workspace-per-target path per the
//! header's mapping comments.

#include "engine_render_next/NextBackend.h"

namespace Orkige
{
	//---------------------------------------------------------
	RenderTexture::RenderTexture()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderTexture::~RenderTexture()
	{
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderTexture::setCamera(optr<RenderCamera> const & camera)
	{
		this->mImpl->camera = camera;
	}
	//---------------------------------------------------------
	void RenderTexture::setBackgroundColour(Color const & colour)
	{
		(void)colour;
	}
	//---------------------------------------------------------
	void RenderTexture::setOverlaysEnabled(bool enabled)
	{
		(void)enabled;
	}
	//---------------------------------------------------------
	void RenderTexture::setShadowsEnabled(bool enabled)
	{
		(void)enabled;
	}
	//---------------------------------------------------------
	void RenderTexture::resize(unsigned int width, unsigned int height)
	{
		this->mImpl->width = width;
		this->mImpl->height = height;
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
		return 0;
	}
	//---------------------------------------------------------
	void RenderTexture::writeContentsToFile(String const & fileName) const
	{
		(void)fileName;
	}
}
