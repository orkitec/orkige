/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	SpriteQuadNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file SpriteQuadNext.cpp
//! @brief B1 STUB of the SpriteQuad facade on Ogre-Next
//! @remarks No backend object exists yet (RenderWorld::createSpriteQuad
//! returns NULL and logs once) - safe-default no-ops so the facade
//! links. B2 (WP-A2.3) implements the v2 quad + HlmsUnlit datablock
//! per the header's mapping comments.

#include "engine_render_next/NextBackend.h"

namespace Orkige
{
	//---------------------------------------------------------
	const int SpriteQuad::ZORDER_MIN = -40;	//!< matches the classic backend
	const int SpriteQuad::ZORDER_MAX = 40;
	//---------------------------------------------------------
	SpriteQuad::SpriteQuad()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	SpriteQuad::~SpriteQuad()
	{
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void SpriteQuad::attachTo(optr<RenderNode> const & node)
	{
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void SpriteQuad::detach()
	{
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	String const & SpriteQuad::getTextureName() const
	{
		return this->mImpl->textureName;
	}
	//---------------------------------------------------------
	void SpriteQuad::getTextureSize(float & outWidth, float & outHeight) const
	{
		outWidth = 0.0f;
		outHeight = 0.0f;
	}
	//---------------------------------------------------------
	void SpriteQuad::setSize(float width, float height)
	{
		(void)width; (void)height;
	}
	//---------------------------------------------------------
	void SpriteQuad::setUVRect(float u0, float v0, float u1, float v1)
	{
		(void)u0; (void)v0; (void)u1; (void)v1;
	}
	//---------------------------------------------------------
	void SpriteQuad::setTint(Color const & tint)
	{
		(void)tint;
	}
	//---------------------------------------------------------
	void SpriteQuad::setFlip(bool flipX, bool flipY)
	{
		(void)flipX; (void)flipY;
	}
	//---------------------------------------------------------
	void SpriteQuad::setZOrder(int zOrder)
	{
		(void)zOrder;
	}
	//---------------------------------------------------------
	void SpriteQuad::setVisible(bool visible)
	{
		(void)visible;
	}
	//---------------------------------------------------------
	void SpriteQuad::setQueryFlags(unsigned int flags)
	{
		(void)flags;
	}
}
