/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	ParticleComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/ParticleComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/SpriteComponent.h"	// frameToUVRect (shared UV primitive)
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_debug/DebugMacros.h>
#include <core_project/AssetDatabase.h>

#include <cmath>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ParticleComponent::ParticleComponent()
		: mEmitOnStart(true), mPrimed(false), mPlaneZ(0.0f)
	{
		this->mTextureName = "";
		this->mTextureAssetId = "";
		this->addDependency<TransformComponent>();
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	ParticleComponent::~ParticleComponent()
	{
	}
	//---------------------------------------------------------
	void ParticleComponent::setTexture(String const & textureName)
	{
		this->mTextureName = textureName;
		this->mTextureAssetId = AssetDatabase::referenceIdForValue(
			textureName, "", AssetDatabase::REF_FILE_NAME);
		// rebuild the batch onto the new texture if we are live
		this->mBatch.reset();
		this->ensureBatch();
	}
	//---------------------------------------------------------
	int ParticleComponent::burst(int count)
	{
		this->ensureBatch();
		return this->mSim.burst(count, this->emitterOrigin());
	}
	//---------------------------------------------------------
	void ParticleComponent::start()
	{
		this->mSim.start();
	}
	//---------------------------------------------------------
	void ParticleComponent::stop()
	{
		this->mSim.stop();
	}
	//---------------------------------------------------------
	void ParticleComponent::setEmitting(bool emitting)
	{
		this->mSim.setEmitting(emitting);
	}
	//---------------------------------------------------------
	bool ParticleComponent::isEmitting() const
	{
		return this->mSim.isEmitting();
	}
	//---------------------------------------------------------
	int ParticleComponent::getLiveCount() const
	{
		return this->mSim.liveCount();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ParticleComponent::onAdd()
	{
		// nothing to do here: the batch needs a render system (present at play
		// time only) and is created lazily on the first update/burst
	}
	//---------------------------------------------------------
	void ParticleComponent::onRemove()
	{
		// RAII: dropping the handle detaches and destroys the batch geometry
		this->mBatch.reset();
	}
	//---------------------------------------------------------
	void ParticleComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mBatch)
		{
			this->mBatch->setVisible(activeInHierarchy);
		}
	}
	//---------------------------------------------------------
	void ParticleComponent::onUpdateComponent(float deltaTime)
	{
		this->ensureBatch();
		if(!this->mBatch)
		{
			return;	// no texture / no render system yet
		}
		// prime continuous emission once (mEmitOnStart): the emitter only ticks
		// in the player, so this is effectively "on play begin"
		if(!this->mPrimed)
		{
			this->mPrimed = true;
			if(this->mEmitOnStart)
			{
				this->mSim.start();
			}
		}
		this->mSim.update(deltaTime, this->emitterOrigin());
		this->writeQuads();
	}
	//---------------------------------------------------------
	void ParticleComponent::ensureBatch()
	{
		if(this->mBatch || this->mTextureName.empty())
		{
			return;
		}
		RenderSystem* renderSystem = RenderSystem::get();
		if(!renderSystem || !renderSystem->getWorld())
		{
			return;	// no renderer (headless/editor edit mode)
		}
		const SpriteBatch::BlendMode blend =
			(this->mSim.params().blendMode == ParticleSim::BLEND_ADDITIVE)
			? SpriteBatch::BLEND_ADDITIVE : SpriteBatch::BLEND_ALPHA;
		optr<SpriteBatch> batch =
			renderSystem->getWorld()->createSpriteBatch(this->mTextureName, blend);
		if(!batch)
		{
			return;	// load failure already logged
		}
		this->mBatch = batch;
		// WORLD-space: the batch hangs off the root so particles fly free of
		// the emitter node
		this->mBatch->attachTo(renderSystem->getWorld()->getRootNode());
		this->mBatch->setZOrder(this->mSim.params().zOrder);
		GameObject* owner = this->getComponentOwner();
		const bool active = !owner || owner->isActiveInHierarchy();
		this->mBatch->setVisible(active);
	}
	//---------------------------------------------------------
	void ParticleComponent::writeQuads()
	{
		oAssert(this->mBatch);
		const int live = this->mSim.liveCount();
		this->mVertexScratch.clear();
		if(live == 0)
		{
			this->mBatch->setQuads(NULL, 0);
			return;
		}
		this->mVertexScratch.reserve(static_cast<std::size_t>(live) * 4);
		float texelWidth = 0.0f, texelHeight = 0.0f;
		this->mBatch->getTextureSize(texelWidth, texelHeight);
		ParticleSim::EmitterParams const & p = this->mSim.params();
		for(int index = 0; index < live; ++index)
		{
			ParticleSim::Particle const & particle = this->mSim.particleAt(index);
			const float halfSize = this->mSim.sizeAt(particle) * 0.5f;
			const Color colour = this->mSim.colorAt(particle);
			// atlas frame -> UV rect (the SHARED SpriteComponent primitive)
			float u0, v0, u1, v1;
			SpriteComponent::frameToUVRect(particle.frame,
				p.atlasColumns, p.atlasRows, texelWidth, texelHeight,
				u0, v0, u1, v1);
			// local quad corners TL,TR,BR,BL, rotated by the particle spin
			const float cosR = std::cos(particle.rotation);
			const float sinR = std::sin(particle.rotation);
			const float localX[4] = { -halfSize,  halfSize,  halfSize, -halfSize };
			const float localY[4] = {  halfSize,  halfSize, -halfSize, -halfSize };
			const float cornerU[4] = { u0, u1, u1, u0 };
			const float cornerV[4] = { v0, v0, v1, v1 };
			for(int corner = 0; corner < 4; ++corner)
			{
				const float rx = localX[corner] * cosR - localY[corner] * sinR;
				const float ry = localX[corner] * sinR + localY[corner] * cosR;
				SpriteBatch::Vertex vertex;
				vertex.position = Vec3(particle.position.x + rx,
					particle.position.y + ry, this->mPlaneZ);
				vertex.uv = Vec2(cornerU[corner], cornerV[corner]);
				vertex.colour = colour;
				this->mVertexScratch.push_back(vertex);
			}
		}
		this->mBatch->setQuads(this->mVertexScratch.data(),
			static_cast<std::size_t>(live));
	}
	//---------------------------------------------------------
	Vec2 ParticleComponent::emitterOrigin()
	{
		GameObject* owner = this->getComponentOwner();
		if(owner)
		{
			optr<TransformComponent> transform =
				owner->getComponent<TransformComponent>().lock();
			if(transform)
			{
				const Vec3 world = transform->getWorldPosition();
				this->mPlaneZ = world.z;
				return Vec2(world.x, world.y);
			}
		}
		return Vec2(0.0f, 0.0f);
	}
	//---------------------------------------------------------
	void ParticleComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// texture name + its stable asset id (rename survival, like SpriteComponent)
		ar->writeAttributed(this->mTextureName,
			AssetDatabase::REFERENCE_ID_ATTRIBUTE,
			AssetDatabase::referenceIdForValue(this->mTextureName,
				this->mTextureAssetId, AssetDatabase::REF_FILE_NAME));
		ar << this->mEmitOnStart;
		// the whole EmitterParams, in a FIXED field order (mirrored by
		// Util/make_roller_assets.py's particles() builder - keep them in sync)
		ParticleSim::EmitterParams const & p = this->mSim.params();
		ar << p.emissionRate << p.burstCount << p.duration << p.looping;
		ar << p.lifetimeMin << p.lifetimeMax;
		ar << p.spawnOffset.x << p.spawnOffset.y;
		ar << p.directionAngle << p.spreadAngle << p.speedMin << p.speedMax;
		ar << p.gravity.x << p.gravity.y << p.damping;
		ar << p.spinMin << p.spinMax;
		ar << p.startSize << p.endSize;
		ar << p.startColor.r << p.startColor.g << p.startColor.b << p.startColor.a;
		ar << p.endColor.r << p.endColor.g << p.endColor.b << p.endColor.a;
		ar << p.sizeEase << p.colorEase;
		ar << p.atlasColumns << p.atlasRows << p.atlasFrameMin << p.atlasFrameMax;
		ar << p.maxParticles << p.zOrder << p.blendMode;
	}
	//---------------------------------------------------------
	void ParticleComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		String textureName;
		String textureAssetId;
		ar->readAttributed(textureName,
			AssetDatabase::REFERENCE_ID_ATTRIBUTE, textureAssetId);
		ar >> this->mEmitOnStart;
		ParticleSim::EmitterParams p;
		ar >> p.emissionRate >> p.burstCount >> p.duration >> p.looping;
		ar >> p.lifetimeMin >> p.lifetimeMax;
		ar >> p.spawnOffset.x >> p.spawnOffset.y;
		ar >> p.directionAngle >> p.spreadAngle >> p.speedMin >> p.speedMax;
		ar >> p.gravity.x >> p.gravity.y >> p.damping;
		ar >> p.spinMin >> p.spinMax;
		ar >> p.startSize >> p.endSize;
		ar >> p.startColor.r >> p.startColor.g >> p.startColor.b >> p.startColor.a;
		ar >> p.endColor.r >> p.endColor.g >> p.endColor.b >> p.endColor.a;
		ar >> p.sizeEase >> p.colorEase;
		ar >> p.atlasColumns >> p.atlasRows >> p.atlasFrameMin >> p.atlasFrameMax;
		ar >> p.maxParticles >> p.zOrder >> p.blendMode;
		this->mSim.setParams(p);
		// a resolving asset id wins over a stale texture name (rename survival)
		AssetDatabase::resolveReference(textureName, textureAssetId,
			AssetDatabase::REF_FILE_NAME);
		this->mTextureName = textureName;
		this->mTextureAssetId = textureAssetId;
		// the batch is (re)built on the first play update (needs a renderer)
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(ParticleComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setTexture)
		OFUNCCR(getTextureName)
		OFUNC(burst)
		OFUNC(start)
		OFUNC(stop)
		OFUNC(setEmitting)
		OFUNC(isEmitting)
		OFUNC(getLiveCount)
	OOBJECT_END
}
