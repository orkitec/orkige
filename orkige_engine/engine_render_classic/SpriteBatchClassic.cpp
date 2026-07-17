/********************************************************************
	created:	Wednesday 2026/07/09 at 14:00
	filename: 	SpriteBatchClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file SpriteBatchClassic.cpp
//! @brief classic-OGRE implementation of the SpriteBatch facade
//! @remarks Ogre::ManualObject rebuilt every frame from the owner's CPU
//! vertex array (four vertices per quad, TL/TR/BR/BL) - the same clear +
//! begin/end idiom SpriteQuad uses on every setUVRect, scaled to N quads so
//! the whole particle system is ONE draw. The alpha variant reuses the
//! shared "Sprite/<tex>" sprite material; the additive variant its
//! "SpriteAdd/<tex>" sibling (SBF_SOURCE_ALPHA/SBF_ONE glow).

#include "engine_render_classic/ClassicBackend.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	Ogre::MaterialPtr RenderBackend::getOrCreateSpriteBatchMaterial(
		Ogre::TexturePtr const & texture, SpriteBatch::BlendMode blendMode)
	{
		oAssert(texture);
		// the batch always samples bilinear + clamp (the historic sprite look);
		// the alpha variant IS the SpriteQuad material, reused wholesale
		if(blendMode == SpriteBatch::BLEND_ALPHA)
		{
			return RenderBackend::getOrCreateSpriteMaterial(texture,
				SpriteQuad::FILTER_BILINEAR, SpriteQuad::ADDRESS_CLAMP);
		}
		// additive: a DISTINCT material keyed under "SpriteAdd/<tex>" so it
		// never stomps the alpha material of the same texture
		const String materialName = "SpriteAdd/" + texture->getName() +
			"#bilinear-clamp";
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		if(materialManager.resourceExists(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return materialManager.getByName(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		Ogre::MaterialPtr material = materialManager.create(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		// src.rgb * src.a + dst: additive but alpha-respecting, so a fading
		// particle (alpha -> 0) fades out instead of staying full-bright
		pass->setSceneBlending(Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE);
		pass->setDepthWriteEnabled(false);
		pass->setCullingMode(Ogre::CULL_NONE);
		Ogre::TextureUnitState* textureUnit = pass->createTextureUnitState();
		textureUnit->setTexture(texture);
		textureUnit->setTextureAddressingMode(
			Ogre::TextureUnitState::TAM_CLAMP);
		textureUnit->setTextureFiltering(Ogre::FO_LINEAR, Ogre::FO_LINEAR,
			Ogre::FO_NONE);
		return material;
	}
	//---------------------------------------------------------
	optr<SpriteBatch> RenderBackend::createSpriteBatch(
		Ogre::SceneManager* sceneManager, String const & textureName,
		SpriteBatch::BlendMode blendMode)
	{
		oAssert(sceneManager);
		oAssert(!textureName.empty());
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
			oDebugError("engine", 0, "RenderWorld: sprite-batch texture '"
				<< textureName << "' failed to load: " << e.getDescription());
			return optr<SpriteBatch>();
		}
		if(!texture)
		{
			oDebugError("engine", 0, "RenderWorld: sprite-batch texture '"
				<< textureName << "' not found");
			return optr<SpriteBatch>();
		}
		optr<SpriteBatch> handle(new SpriteBatch());
		RenderBackend::getOrCreateSpriteBatchMaterial(texture, blendMode);
		handle->mImpl->creator = sceneManager;
		handle->mImpl->textureName = textureName;
		handle->mImpl->texture = texture;
		handle->mImpl->blendMode = blendMode;
		handle->mImpl->materialName = (blendMode == SpriteBatch::BLEND_ALPHA)
			? SpriteQuad::samplerName(textureName, SpriteQuad::FILTER_BILINEAR,
				SpriteQuad::ADDRESS_CLAMP)
			: String("SpriteAdd/") + textureName + "#bilinear-clamp";
		handle->mImpl->texelWidth = static_cast<float>(texture->getWidth());
		handle->mImpl->texelHeight = static_cast<float>(texture->getHeight());
		handle->mImpl->batch = sceneManager->createManualObject(
			RenderBackend::generateName("RenderFacade/SpriteBatch"));
		// particles never participate in editor picking
		handle->mImpl->batch->setQueryFlags(0);
		// dynamic: the geometry is refilled every frame
		handle->mImpl->batch->setDynamic(true);
		RenderBackend::applyZOrder(handle->mImpl->batch, 0);
		return handle;
	}
	//---------------------------------------------------------
	void SpriteBatch::Impl::rebuild(SpriteBatch::Vertex const * vertices,
		std::size_t quadCount)
	{
		oAssert(this->batch);
		this->batch->clear();
		this->quadCount = quadCount;
		if(quadCount == 0 || vertices == NULL)
		{
			return;	// nothing to draw this frame
		}
		this->batch->estimateVertexCount(quadCount * 4);
		this->batch->estimateIndexCount(quadCount * 6);
		this->batch->begin(this->materialName,
			Ogre::RenderOperation::OT_TRIANGLE_LIST);
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
		RenderBackend::applyZOrder(this->batch, this->zOrder);
	}
	//---------------------------------------------------------
	SpriteBatch::SpriteBatch()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	SpriteBatch::~SpriteBatch()
	{
		if(this->mImpl->batch)
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
		RenderBackend::applyZOrder(this->mImpl->batch, this->mImpl->zOrder);
	}
	//---------------------------------------------------------
	void SpriteBatch::setVisible(bool visible)
	{
		this->mImpl->batch->setVisible(visible);
	}
}
