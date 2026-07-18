/********************************************************************
	created:	Wednesday 2026/07/09 at 14:00
	filename: 	SpriteBatchNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file SpriteBatchNext.cpp
//! @brief Ogre-Next implementation of the SpriteBatch facade
//! @remarks the v2 counterpart of the classic sprite batch: one v2
//! Ogre::ManualObject (SCENE_DYNAMIC) refilled from the owner's CPU vertex
//! array (four vertices per quad, TL/TR/BR/BL), scaled to N quads so the
//! whole particle system is ONE draw. A refresh at the same quad count
//! rewrites the live VaoManager buffers in place (beginUpdate - the
//! VectorMesh dynamic idiom), which keeps the section's VAO identity
//! stable so the render queue's multi-draw merge of same-layout dynamic
//! objects survives re-uploads; only a quad-count change pays the
//! clear + begin/end reallocation. Blends through the shared per-texture
//! "Sprite/<tex>" (alpha) / "SpriteAdd/<tex>" (additive) HlmsUnlit
//! datablock.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreManualObject2.h>
#include <OgreTextureGpu.h>
#include <OgreRoot.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateSpriteBatchDatablock(
		String const & textureName, Ogre::TextureGpu* texture,
		SpriteBatch::BlendMode blendMode, SpriteQuad::FilterMode filter,
		SpriteQuad::AddressMode addressing)
	{
		// the alpha variant IS the SpriteQuad datablock, reused wholesale
		// (per-(texture,sampler) - a batched sprite run shares its members'
		// exact material)
		if(blendMode == SpriteBatch::BLEND_ALPHA)
		{
			return RenderBackend::getOrCreateSpriteDatablock(textureName,
				texture, filter, addressing);
		}
		oAssert(RenderBackend::system());
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		// additive: a DISTINCT datablock keyed under "SpriteAdd/<tex>" so it
		// never stomps the alpha datablock of the same texture
		const String name = "SpriteAdd/" + textureName + "#bilinear-clamp";
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			return existing;
		}
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsMacroblock macroblock;
		macroblock.mDepthWrite = false;
		macroblock.mCullMode = Ogre::CULL_NONE;
		// start from transparent-alpha (sets source=SBF_SOURCE_ALPHA + the
		// transparent flag), then override the destination to SBF_ONE:
		// src.rgb*src.a + dst, so a fading particle fades out instead of
		// staying full-bright
		Ogre::HlmsBlendblock blendblock;
		blendblock.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
		blendblock.mDestBlendFactor = Ogre::SBF_ONE;
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				name, name, macroblock, blendblock, Ogre::HlmsParamVec()));
		if(texture)
		{
			Ogre::HlmsSamplerblock samplerblock;
			samplerblock.setFiltering(Ogre::TFO_BILINEAR);
			samplerblock.setAddressingMode(Ogre::TAM_CLAMP);
			datablock->setTexture(0u, texture, &samplerblock);
		}
		RenderBackend::registerContentDatablock(datablock);
		return datablock;
	}
	//---------------------------------------------------------
	optr<SpriteBatch> RenderBackend::createSpriteBatch(
		Ogre::SceneManager* sceneManager, String const & textureName,
		SpriteBatch::BlendMode blendMode, SpriteQuad::FilterMode filter,
		SpriteQuad::AddressMode addressing)
	{
		oAssert(sceneManager);
		oAssert(!textureName.empty());
		Ogre::TextureGpu* texture = RenderBackend::loadTexture2D(textureName);
		if(!texture)
		{
			return optr<SpriteBatch>();	// error already logged
		}
		optr<SpriteBatch> handle(new SpriteBatch());
		RenderBackend::getOrCreateSpriteBatchDatablock(textureName, texture,
			blendMode, filter, addressing);
		handle->mImpl->creator = sceneManager;
		handle->mImpl->textureName = textureName;
		handle->mImpl->texture = texture;
		handle->mImpl->blendMode = blendMode;
		handle->mImpl->datablockName = (blendMode == SpriteBatch::BLEND_ALPHA)
			? SpriteQuad::samplerName(textureName, filter, addressing)
			: String("SpriteAdd/") + textureName + "#bilinear-clamp";
		handle->mImpl->texelWidth = static_cast<float>(texture->getWidth());
		handle->mImpl->texelHeight = static_cast<float>(texture->getHeight());
		handle->mImpl->batch =
			sceneManager->createManualObject(Ogre::SCENE_DYNAMIC);
		handle->mImpl->batch->setName(
			RenderBackend::generateName("RenderFacade/SpriteBatch"));
		handle->mImpl->batch->setQueryFlags(0);	// never picked
		// 2D content never throws shadows into a lit 3D scene
		handle->mImpl->batch->setCastShadows(false);
		// tag the 2D tier so the bloom scene split keeps it out of the glow
		RenderBackend::tagScene2D(handle->mImpl->batch);
		return handle;
	}
	//---------------------------------------------------------
	void SpriteBatch::Impl::rebuild(SpriteBatch::Vertex const * vertices,
		std::size_t quadCount)
	{
		oAssert(this->batch);
		this->quadCount = quadCount;
		if(quadCount == 0 || vertices == NULL)
		{
			this->batch->clear();
			this->allocatedQuads = 0;
			return;	// nothing to draw this frame
		}
		if(quadCount == this->allocatedQuads)
		{
			// same quad count as the live buffers: refresh IN PLACE through
			// beginUpdate (the VectorMesh dynamic idiom). Keeping the
			// VaoManager buffers alive keeps the section's VAO identity
			// stable, so the render queue's multi-draw merge of same-layout
			// dynamic objects survives a re-upload (a clear+begin/end
			// reallocates into a fresh buffer pool and permanently splits
			// the merged draw - one extra draw call per re-uploaded batch)
			// and the per-refresh buffer create/destroy churn disappears
			this->batch->beginUpdate(0);
		}
		else
		{
			this->batch->clear();
			this->batch->estimateVertexCount(quadCount * 4);
			this->batch->estimateIndexCount(quadCount * 6);
			this->batch->begin(this->datablockName, Ogre::OT_TRIANGLE_LIST);
		}
		for(std::size_t quad = 0; quad < quadCount; ++quad)
		{
			const std::size_t base = quad * 4;
			for(int corner = 0; corner < 4; ++corner)
			{
				SpriteBatch::Vertex const & vertex = vertices[base + corner];
				this->batch->position(vertex.position);
				this->batch->colour(vertex.colour);
				this->batch->textureCoord(vertex.uv);
			}
			// same winding as SpriteQuad (TL,TR,BR,BL -> (0,3,2)(0,2,1))
			const Ogre::uint32 v0 = static_cast<Ogre::uint32>(base);
			this->batch->triangle(v0 + 0, v0 + 3, v0 + 2);
			this->batch->triangle(v0 + 0, v0 + 2, v0 + 1);
		}
		this->batch->end();
		this->allocatedQuads = quadCount;
		this->batch->setRenderQueueGroup(
			RenderBackend::renderQueueForZOrder(this->zOrder));
	}
	//---------------------------------------------------------
	SpriteBatch::SpriteBatch()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	SpriteBatch::~SpriteBatch()
	{
		// late destruction guard, same rule as SpriteQuad/RenderNode
		if(this->mImpl->batch && RenderBackend::system())
		{
			if(this->mImpl->batch->isAttached())
			{
				this->mImpl->batch->detachFromParent();
			}
			this->mImpl->creator->destroyManualObject(this->mImpl->batch);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void SpriteBatch::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->batch->isAttached())
		{
			this->mImpl->batch->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->batch);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void SpriteBatch::detach()
	{
		if(this->mImpl->batch->isAttached())
		{
			this->mImpl->batch->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	String const & SpriteBatch::getTextureName() const
	{
		return this->mImpl->textureName;
	}
	//---------------------------------------------------------
	void SpriteBatch::getTextureSize(float & outWidth, float & outHeight) const
	{
		outWidth = this->mImpl->texelWidth;
		outHeight = this->mImpl->texelHeight;
	}
	//---------------------------------------------------------
	SpriteBatch::BlendMode SpriteBatch::getBlendMode() const
	{
		return this->mImpl->blendMode;
	}
	//---------------------------------------------------------
	void SpriteBatch::setQuads(SpriteBatch::Vertex const * vertices,
		std::size_t quadCount)
	{
		this->mImpl->rebuild(vertices, quadCount);
	}
	//---------------------------------------------------------
	std::size_t SpriteBatch::getQuadCount() const
	{
		return this->mImpl->quadCount;
	}
	//---------------------------------------------------------
	void SpriteBatch::setZOrder(int zOrder)
	{
		this->mImpl->zOrder = std::clamp(zOrder,
			SpriteQuad::ZORDER_MIN, SpriteQuad::ZORDER_MAX);
		this->mImpl->batch->setRenderQueueGroup(
			RenderBackend::renderQueueForZOrder(this->mImpl->zOrder));
	}
	//---------------------------------------------------------
	void SpriteBatch::setVisible(bool visible)
	{
		this->mImpl->batch->setVisible(visible);
	}
}
