/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	SpriteQuadClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file SpriteQuadClassic.cpp
//! @brief classic-OGRE implementation of the SpriteQuad facade
//! @remarks Ogre::ManualObject quad + the generated "Sprite/<tex>"
//! material (unlit, alpha-blended, depth-checked/not-written, two-sided;
//! tint and flips live in the vertex data so all sprites of one texture
//! share one material; zOrder = render-queue painter's sorting). Same
//! honest v1 rules SpriteComponent renders with today; the component is
//! retargeted onto this class.

#include "engine_render_classic/ClassicBackend.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <utility>

namespace Orkige
{
	// same clamp as SpriteComponent: RENDER_QUEUE_MAIN (50) +- 40
	const int SpriteQuad::ZORDER_MIN = -40;
	const int SpriteQuad::ZORDER_MAX = 40;
	//---------------------------------------------------------
	optr<SpriteQuad> RenderBackend::createSpriteQuad(
		Ogre::SceneManager* sceneManager, String const & textureName)
	{
		oAssert(sceneManager);
		oAssert(!textureName.empty());
		// resolve through EVERY resource group (AUTODETECT): engine media
		// and project assets both work by plain file name
		Ogre::TexturePtr texture;
		try
		{
			// cooked-payload fallback (foo.png -> foo.dds/.ktx in exports)
			texture = Ogre::TextureManager::getSingleton().load(
				resolveTextureResourceName(textureName),
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "RenderWorld: sprite texture '"
				<< textureName << "' failed to load: " << e.getDescription());
			return optr<SpriteQuad>();
		}
		if(!texture)
		{
			oDebugError("engine", 0, "RenderWorld: sprite texture '"
				<< textureName << "' not found");
			return optr<SpriteQuad>();
		}
		optr<SpriteQuad> handle(new SpriteQuad());
		// default sampler (bilinear + clamp) reproduces the historic sprite
		// look; the material name carries the sampler so a later setSampler
		// rebinds onto a distinct material instead of mutating a shared one
		RenderBackend::getOrCreateSpriteMaterial(texture,
			handle->mImpl->filter, handle->mImpl->addressing);
		handle->mImpl->creator = sceneManager;
		handle->mImpl->textureName = textureName;
		handle->mImpl->texture = texture;
		handle->mImpl->materialName = SpriteQuad::samplerName(textureName,
			handle->mImpl->filter, handle->mImpl->addressing);
		handle->mImpl->texelWidth = static_cast<float>(texture->getWidth());
		handle->mImpl->texelHeight = static_cast<float>(texture->getHeight());
		handle->mImpl->quad = sceneManager->createManualObject(
			RenderBackend::generateName("RenderFacade/Sprite"));
		handle->mImpl->quad->setQueryFlags(RenderWorld::QUERYFLAG_DEFAULT);
		// the 2D layer neither casts nor receives shadows by construction
		handle->mImpl->quad->setCastShadows(false);
		// tag the 2D tier so the bloom scene split keeps it out of the glow
		RenderBackend::tagScene2D(handle->mImpl->quad);
		handle->mImpl->rebuild();
		return handle;
	}
	//---------------------------------------------------------
	void SpriteQuad::Impl::rebuild()
	{
		oAssert(this->quad);
		// resolve the world-unit size: unset dimensions derive from the
		// texture aspect (height 1); one unset side follows the other
		const float aspect =
			(this->texelWidth > 0.0f && this->texelHeight > 0.0f)
			? this->texelWidth / this->texelHeight : 1.0f;
		float resolvedWidth = this->width;
		float resolvedHeight = this->height;
		if(resolvedWidth <= 0.0f && resolvedHeight <= 0.0f)
		{
			resolvedHeight = 1.0f;
			resolvedWidth = aspect;
		}
		else if(resolvedWidth <= 0.0f)
		{
			resolvedWidth = resolvedHeight * aspect;
		}
		else if(resolvedHeight <= 0.0f)
		{
			resolvedHeight = resolvedWidth / aspect;
		}
		// UV corners, v running top-down; flips swap the coordinates
		float flippedU0 = this->u0, flippedU1 = this->u1;
		float flippedV0 = this->v0, flippedV1 = this->v1;
		if(this->flipX)
		{
			std::swap(flippedU0, flippedU1);
		}
		if(this->flipY)
		{
			std::swap(flippedV0, flippedV1);
		}
		const Ogre::Vector2 uv[4] = {
			{ flippedU0, flippedV0 },	// top-left
			{ flippedU1, flippedV0 },	// top-right
			{ flippedU1, flippedV1 },	// bottom-right
			{ flippedU0, flippedV1 },	// bottom-left
		};
		const float halfWidth = resolvedWidth * 0.5f;
		const float halfHeight = resolvedHeight * 0.5f;
		// vertex order matches the UV corners: TL, TR, BR, BL; triangles
		// (0,3,2)(0,2,1) face +Z (the material renders two-sided anyway)
		const Ogre::Vector3 corners[4] = {
			{ -halfWidth,  halfHeight, 0.0f },
			{  halfWidth,  halfHeight, 0.0f },
			{  halfWidth, -halfHeight, 0.0f },
			{ -halfWidth, -halfHeight, 0.0f },
		};
		this->quad->clear();
		this->quad->estimateVertexCount(4);
		this->quad->estimateIndexCount(6);
		this->quad->begin(this->materialName,
			Ogre::RenderOperation::OT_TRIANGLE_LIST);
		for(int each = 0; each < 4; ++each)
		{
			this->quad->position(corners[each]);
			this->quad->colour(this->tint);
			this->quad->textureCoord(uv[each]);
		}
		this->quad->triangle(0, 3, 2);
		this->quad->triangle(0, 2, 1);
		this->quad->end();
		RenderBackend::applyZOrder(this->quad, this->zOrder);
	}
	//---------------------------------------------------------
	SpriteQuad::SpriteQuad()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	SpriteQuad::~SpriteQuad()
	{
		if(this->mImpl->quad)
		{
			if(this->mImpl->quad->isAttached())
			{
				this->mImpl->quad->detachFromParent();
			}
			this->mImpl->creator->destroyManualObject(this->mImpl->quad);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void SpriteQuad::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->quad->isAttached())
		{
			this->mImpl->quad->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->quad);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void SpriteQuad::detach()
	{
		if(this->mImpl->quad->isAttached())
		{
			this->mImpl->quad->detachFromParent();
		}
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
		outWidth = this->mImpl->texelWidth;
		outHeight = this->mImpl->texelHeight;
	}
	//---------------------------------------------------------
	void SpriteQuad::setSize(float width, float height)
	{
		this->mImpl->width = width;
		this->mImpl->height = height;
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void SpriteQuad::setUVRect(float u0, float v0, float u1, float v1)
	{
		this->mImpl->u0 = u0;
		this->mImpl->v0 = v0;
		this->mImpl->u1 = u1;
		this->mImpl->v1 = v1;
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void SpriteQuad::setTint(Color const & tint)
	{
		this->mImpl->tint = tint;
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void SpriteQuad::setFlip(bool flipX, bool flipY)
	{
		this->mImpl->flipX = flipX;
		this->mImpl->flipY = flipY;
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void SpriteQuad::setSampler(FilterMode filter, AddressMode addressing)
	{
		if(this->mImpl->filter == filter &&
			this->mImpl->addressing == addressing)
		{
			return;
		}
		this->mImpl->filter = filter;
		this->mImpl->addressing = addressing;
		// rebind onto the per-(texture,sampler) material (created on demand) -
		// keying on the sampler keeps sprites that share a texture but sample
		// it differently on DISTINCT materials
		if(this->mImpl->texture)
		{
			RenderBackend::getOrCreateSpriteMaterial(this->mImpl->texture,
				filter, addressing);
		}
		this->mImpl->materialName = SpriteQuad::samplerName(
			this->mImpl->textureName, filter, addressing);
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void SpriteQuad::setZOrder(int zOrder)
	{
		this->mImpl->zOrder = std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		RenderBackend::applyZOrder(this->mImpl->quad, this->mImpl->zOrder);
	}
	//---------------------------------------------------------
	void SpriteQuad::setVisible(bool visible)
	{
		this->mImpl->quad->setVisible(visible);
	}
	//---------------------------------------------------------
	void SpriteQuad::setQueryFlags(unsigned int flags)
	{
		this->mImpl->quad->setQueryFlags(flags);
	}
}
