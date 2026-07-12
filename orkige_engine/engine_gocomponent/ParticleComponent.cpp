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
#include "engine_gocomponent/ComponentPropertyReflect.h"	// Vec3 pack/unpack for OPROPERTY
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderCamera.h"	// view matrix -> camera billboard axes
#include <core_game/GameObject.h>
#include <core_debug/DebugMacros.h>
#include <core_debug/MemoryManager.h>
#include <core_project/AssetDatabase.h>

#include <algorithm>
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
		if(this->mSim.params().space3D)
		{
			return this->mSim.burst3D(count, this->emitterOrigin3D());
		}
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
	bool ParticleComponent::getSpace3D() const
	{
		return this->mSim.params().space3D;
	}
	//---------------------------------------------------------
	void ParticleComponent::setSpace3D(bool space3D)
	{
		this->mSim.params().space3D = space3D;
	}
	//---------------------------------------------------------
	bool ParticleComponent::getWorldSpace() const
	{
		return this->mSim.params().worldSpace;
	}
	//---------------------------------------------------------
	void ParticleComponent::setWorldSpace(bool worldSpace)
	{
		this->mSim.params().worldSpace = worldSpace;
	}
	//---------------------------------------------------------
	int ParticleComponent::getEmissionVolume() const
	{
		return this->mSim.params().emissionVolume;
	}
	//---------------------------------------------------------
	void ParticleComponent::setEmissionVolume(int volume)
	{
		this->mSim.params().emissionVolume = std::clamp(volume, 0,
			static_cast<int>(ParticleSim::EmitterParams::VOLUME_BOX));
	}
	//---------------------------------------------------------
	Vec3 ParticleComponent::getVolumeExtents() const
	{
		return this->mSim.params().volumeExtents;
	}
	//---------------------------------------------------------
	void ParticleComponent::setVolumeExtents(Vec3 const & extents)
	{
		this->mSim.params().volumeExtents = extents;
	}
	//---------------------------------------------------------
	Vec3 ParticleComponent::getGravity3D() const
	{
		return this->mSim.params().gravity3D;
	}
	//---------------------------------------------------------
	void ParticleComponent::setGravity3D(Vec3 const & gravity)
	{
		this->mSim.params().gravity3D = gravity;
	}
	//---------------------------------------------------------
	Vec3 ParticleComponent::getWind() const
	{
		return this->mSim.params().wind;
	}
	//---------------------------------------------------------
	void ParticleComponent::setWind(Vec3 const & wind)
	{
		this->mSim.params().wind = wind;
	}
	//---------------------------------------------------------
	Vec3 ParticleComponent::getDirection3D() const
	{
		return this->mSim.params().direction3D;
	}
	//---------------------------------------------------------
	void ParticleComponent::setDirection3D(Vec3 const & direction)
	{
		this->mSim.params().direction3D = direction;
	}
	//---------------------------------------------------------
	float ParticleComponent::getStretch() const
	{
		return this->mSim.params().stretch;
	}
	//---------------------------------------------------------
	void ParticleComponent::setStretch(float stretch)
	{
		this->mSim.params().stretch = std::max(0.0f, stretch);
	}
	//---------------------------------------------------------
	float ParticleComponent::getFlutterAmplitude() const
	{
		return this->mSim.params().flutterAmplitude;
	}
	//---------------------------------------------------------
	void ParticleComponent::setFlutterAmplitude(float amplitude)
	{
		this->mSim.params().flutterAmplitude = std::max(0.0f, amplitude);
	}
	//---------------------------------------------------------
	float ParticleComponent::getFlutterFrequency() const
	{
		return this->mSim.params().flutterFrequency;
	}
	//---------------------------------------------------------
	void ParticleComponent::setFlutterFrequency(float frequency)
	{
		this->mSim.params().flutterFrequency = std::max(0.0f, frequency);
	}
	//---------------------------------------------------------
	bool ParticleComponent::getAdditive() const
	{
		return this->mSim.params().blendMode == ParticleSim::BLEND_ADDITIVE;
	}
	//---------------------------------------------------------
	void ParticleComponent::setAdditive(bool additive)
	{
		const int blend = additive
			? ParticleSim::BLEND_ADDITIVE : ParticleSim::BLEND_ALPHA;
		if(this->mSim.params().blendMode == blend)
		{
			return;
		}
		this->mSim.params().blendMode = blend;
		// the blend mode is baked into the batch material/datablock at creation,
		// so a change rebuilds the batch onto the matching one
		this->mBatch.reset();
		this->ensureBatch();
	}
	//---------------------------------------------------------
	int ParticleComponent::getMaxParticles() const
	{
		return this->mSim.params().maxParticles;
	}
	//---------------------------------------------------------
	void ParticleComponent::setMaxParticles(int maxParticles)
	{
		// route through setParams so the pool re-reserves and the live count is
		// clamped to the new hard cap
		ParticleSim::EmitterParams params = this->mSim.params();
		params.maxParticles = std::max(1, maxParticles);
		this->mSim.setParams(params);
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
		if(this->mSim.params().space3D)
		{
			this->mSim.update3D(deltaTime, this->emitterOrigin3D());
			this->writeQuads3D();
		}
		else
		{
			this->mSim.update(deltaTime, this->emitterOrigin());
			this->writeQuads();
		}
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
		const std::size_t scratchCapacityBefore = this->mVertexScratch.capacity();
		this->mVertexScratch.reserve(static_cast<std::size_t>(live) * 4);
		MemoryManager::countGrowth(MemoryManager::TAG_PARTICLES,
			scratchCapacityBefore, this->mVertexScratch.capacity());
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
	Vec3 ParticleComponent::emitterOrigin3D()
	{
		GameObject* owner = this->getComponentOwner();
		if(owner)
		{
			optr<TransformComponent> transform =
				owner->getComponent<TransformComponent>().lock();
			if(transform)
			{
				return transform->getWorldPosition();
			}
		}
		return Vec3(0.0f, 0.0f, 0.0f);
	}
	//---------------------------------------------------------
	void ParticleComponent::writeQuads3D()
	{
		oAssert(this->mBatch);
		const int live = this->mSim.liveCount();
		this->mVertexScratch.clear();
		if(live == 0)
		{
			this->mBatch->setQuads(NULL, 0);
			return;
		}
		const std::size_t scratchCapacityBefore = this->mVertexScratch.capacity();
		this->mVertexScratch.reserve(static_cast<std::size_t>(live) * 4);
		MemoryManager::countGrowth(MemoryManager::TAG_PARTICLES,
			scratchCapacityBefore, this->mVertexScratch.capacity());
		float texelWidth = 0.0f, texelHeight = 0.0f;
		this->mBatch->getTextureSize(texelWidth, texelHeight);
		ParticleSim::EmitterParams const & p = this->mSim.params();
		// the CPU billboard axes: the window camera's world-space right/up, read
		// from its view matrix (its first two rows ARE those axes in world space)
		Vec3 cameraRight(1.0f, 0.0f, 0.0f);
		Vec3 cameraUp(0.0f, 1.0f, 0.0f);
		if(RenderSystem* renderSystem = RenderSystem::get())
		{
			optr<RenderCamera> camera = renderSystem->getWindowCamera();
			if(camera)
			{
				const Mat4 view = camera->getViewMatrix();
				cameraRight = Vec3(view[0][0], view[0][1], view[0][2]);
				cameraUp = Vec3(view[1][0], view[1][1], view[1][2]);
			}
		}
		const Vec3 origin = this->emitterOrigin3D();
		Vec3 corners[4];
		for(int index = 0; index < live; ++index)
		{
			ParticleSim::Particle const & particle = this->mSim.particleAt(index);
			const float halfSize = this->mSim.sizeAt(particle) * 0.5f;
			const Color colour = this->mSim.colorAt(particle);
			float u0, v0, u1, v1;
			SpriteComponent::frameToUVRect(particle.frame,
				p.atlasColumns, p.atlasRows, texelWidth, texelHeight,
				u0, v0, u1, v1);
			const Vec3 center = this->mSim.worldPosition3D(particle, origin);
			if(p.stretch > 0.0f)
			{
				// velocity-stretch (rain streaks): longer along the motion
				const float speed = particle.velocity3.length();
				const float halfLength = halfSize * (1.0f + p.stretch * speed);
				ParticleSim::streakCorners(center, cameraRight, cameraUp,
					particle.velocity3, halfSize, halfLength, corners);
			}
			else
			{
				ParticleSim::billboardCorners(center, cameraRight, cameraUp,
					halfSize, corners);
			}
			const float cornerU[4] = { u0, u1, u1, u0 };
			const float cornerV[4] = { v0, v0, v1, v1 };
			for(int corner = 0; corner < 4; ++corner)
			{
				SpriteBatch::Vertex vertex;
				vertex.position = corners[corner];
				vertex.uv = Vec2(cornerU[corner], cornerV[corner]);
				vertex.colour = colour;
				this->mVertexScratch.push_back(vertex);
			}
		}
		this->mBatch->setQuads(this->mVertexScratch.data(),
			static_cast<std::size_t>(live));
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
		// 3D / weather extension block (space3D default false = the 2D path);
		// FIXED field order, mirrored by Util/make_roller_assets.py particles()
		ar << p.space3D << p.worldSpace << p.emissionVolume;
		ar << p.volumeExtents.x << p.volumeExtents.y << p.volumeExtents.z;
		ar << p.spawnOffset3D.x << p.spawnOffset3D.y << p.spawnOffset3D.z;
		ar << p.direction3D.x << p.direction3D.y << p.direction3D.z;
		ar << p.gravity3D.x << p.gravity3D.y << p.gravity3D.z;
		ar << p.wind.x << p.wind.y << p.wind.z;
		ar << p.stretch << p.flutterAmplitude << p.flutterFrequency;
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
		// 3D / weather extension block (mirrors save, above)
		ar >> p.space3D >> p.worldSpace >> p.emissionVolume;
		ar >> p.volumeExtents.x >> p.volumeExtents.y >> p.volumeExtents.z;
		ar >> p.spawnOffset3D.x >> p.spawnOffset3D.y >> p.spawnOffset3D.z;
		ar >> p.direction3D.x >> p.direction3D.y >> p.direction3D.z;
		ar >> p.gravity3D.x >> p.gravity3D.y >> p.gravity3D.z;
		ar >> p.wind.x >> p.wind.y >> p.wind.z;
		ar >> p.stretch >> p.flutterAmplitude >> p.flutterFrequency;
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
		// reflected 3D / weather tunables: the ONE property registry feeds the
		// inspector, scene overrides, Lua self.<name>, the debug protocol and MCP
		// (no new verbs, no new Lua tables). The emission volume rides as a plain
		// int (0 point / 1 sphere / 2 box, @see ParticleSim::EmissionVolume).
		OPROPERTY("space3D", Orkige::PropertyKind::Bool, getSpace3D, setSpace3D, Orkige::PROP_NONE)
		OPROPERTY("worldSpace", Orkige::PropertyKind::Bool, getWorldSpace, setWorldSpace, Orkige::PROP_NONE)
		OPROPERTY("emissionVolume", Orkige::PropertyKind::Int, getEmissionVolume, setEmissionVolume, Orkige::PROP_NONE)
		OPROPERTY("volumeExtents", Orkige::PropertyKind::Vec3, getVolumeExtents, setVolumeExtents, Orkige::PROP_NONE)
		OPROPERTY("gravity3D", Orkige::PropertyKind::Vec3, getGravity3D, setGravity3D, Orkige::PROP_NONE)
		OPROPERTY("wind", Orkige::PropertyKind::Vec3, getWind, setWind, Orkige::PROP_NONE)
		OPROPERTY("direction3D", Orkige::PropertyKind::Vec3, getDirection3D, setDirection3D, Orkige::PROP_NONE)
		OPROPERTY("stretch", Orkige::PropertyKind::Float, getStretch, setStretch, Orkige::PROP_NONE)
		OPROPERTY("flutterAmplitude", Orkige::PropertyKind::Float, getFlutterAmplitude, setFlutterAmplitude, Orkige::PROP_NONE)
		OPROPERTY("flutterFrequency", Orkige::PropertyKind::Float, getFlutterFrequency, setFlutterFrequency, Orkige::PROP_NONE)
		OPROPERTY("additive", Orkige::PropertyKind::Bool, getAdditive, setAdditive, Orkige::PROP_NONE)
		OPROPERTY("maxParticles", Orkige::PropertyKind::Int, getMaxParticles, setMaxParticles, Orkige::PROP_NONE)
	OOBJECT_END
}
