/********************************************************************
	created:	Wednesday 2026/07/09 at 14:00
	filename: 	ParticleComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ParticleComponent_h__9_7_2026__14_00_00__
#define __ParticleComponent_h__9_7_2026__14_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/SpriteBatch.h"
#include "engine_gocomponent/ParticleSim.h"
#include "core_util/StringUtil.h"

#include <vector>

namespace Orkige
{
	//! @brief a 2D particle emitter - the CPU-simulated, batched juice tier
	//! Needs a sibling TransformComponent (the emitter origin) and
	//! draws all its particles through ONE world-space SpriteBatch.
	//! @remarks WORLD-space emission: the batch hangs off the world ROOT, not
	//! the emitter's node, so particles fly independently of the emitter (the
	//! trail stays behind a moving emitter instead of dragging with it). The
	//! simulation runs in onUpdateComponent, which the editor never ticks - so
	//! the emitter is dormant in edit mode for free and only lives
	//! in the spawned player. Every frame the live particles become one
	//! SpriteBatch vertex array (setQuads once); atlas-frame UVs reuse the
	//! shared SpriteComponent::frameToUVRect primitive; zOrder uses the SAME
	//! painter's window as SpriteComponent (no parallel scheme). The
	//! pure simulation lives in ParticleSim (headless-testable, SEEDED).
	//!
	//! Lua (through the ScriptComponent `self.particles` accessor + the
	//! `world.getParticles` world API): particles:burst(n) / start() / stop()
	//! / setEmitting(bool). The emitter configuration (EmitterParams) is
	//! authored in the scene and serialized whole.
	class ORKIGE_ENGINE_DLL ParticleComponent : public GameObjectComponent
	{
		OOBJECT(ParticleComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		ParticleSim			mSim;			//!< the pure CPU simulation (seeded)
		optr<SpriteBatch>	mBatch;			//!< the world-space batch or NULL (created lazily)
		String				mTextureName;	//!< particle texture resource name or empty
		String				mTextureAssetId;	//!< stable asset id of the texture ("" = none/engine media)
		bool				mEmitOnStart;	//!< auto-begin continuous emission on the first play update
		bool				mPrimed;		//!< the first-update priming (mEmitOnStart) has run
		float				mPlaneZ;		//!< world Z plane the particles live in (emitter's Z)
		std::vector<SpriteBatch::Vertex>	mVertexScratch;	//!< per-frame vertex build buffer (4/particle)
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		ParticleComponent();
		//! destructor
		virtual ~ParticleComponent();

		//--- configuration ---
		//! @brief set the particle texture (resolved through the AUTODETECT
		//! resource group like SpriteComponent); (re)creates the batch when the
		//! component is live. A missing texture is an error log, not a crash.
		void setTexture(String const & textureName);
		//! @see ParticleComponent::mTextureName
		inline String const & getTextureName() const;
		//! the whole emitter configuration (mutable - granular script tweaks
		//! write through it; call before play or between bursts)
		inline ParticleSim::EmitterParams & params();
		//! @overload
		inline ParticleSim::EmitterParams const & params() const;
		//! auto-start continuous emission on the first play update (default true)
		inline void setEmitOnStart(bool emitOnStart);
		//! @see ParticleComponent::mEmitOnStart
		inline bool getEmitOnStart() const;

		//--- runtime control (the Lua surface) ---
		//! @brief spawn a burst of min(n, remaining capacity) particles at the
		//! emitter's current world position; n <= 0 uses the configured
		//! burstCount. @return the number actually spawned.
		int burst(int count);
		//! begin continuous emission (rate-based) from a fresh duration window
		void start();
		//! stop continuous emission (live particles keep flying out)
		void stop();
		//! turn continuous emission on/off without resetting the window
		void setEmitting(bool emitting);
		//! is continuous emission currently on
		bool isEmitting() const;
		//! how many particles are alive right now
		int getLiveCount() const;
	protected:
		//! component override - called after the component is attached
		virtual void onAdd();
		//! component override - called before the component is removed
		virtual void onRemove();
		//! deactivated GameObjects hide their batch (emission keeps its state)
		virtual void onSetActive(bool activeInHierarchy);
		//! simulate one step and refill the batch (dormant in the editor)
		virtual void onUpdateComponent(float deltaTime);

		//! create the world-space batch if the texture is set and a render
		//! system exists (idempotent); applies zOrder/visibility
		void ensureBatch();
		//! turn the live particles into the batch vertex array and submit them
		void writeQuads();
		//! the emitter's current world-space origin (sibling TransformComponent)
		Vec2 emitterOrigin();
		//--- SERIALIZATION ---
		//! save the texture (+ asset id), emit-on-start and the whole EmitterParams
		virtual void save(optr<IArchive> const & ar);
		//! load the above; the batch is (re)built on the first play update
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline String const & ParticleComponent::getTextureName() const
	{
		return this->mTextureName;
	}
	//---------------------------------------------------------------
	inline ParticleSim::EmitterParams & ParticleComponent::params()
	{
		return this->mSim.params();
	}
	//---------------------------------------------------------------
	inline ParticleSim::EmitterParams const & ParticleComponent::params() const
	{
		return this->mSim.params();
	}
	//---------------------------------------------------------------
	inline void ParticleComponent::setEmitOnStart(bool emitOnStart)
	{
		this->mEmitOnStart = emitOnStart;
	}
	//---------------------------------------------------------------
	inline bool ParticleComponent::getEmitOnStart() const
	{
		return this->mEmitOnStart;
	}
	//---------------------------------------------------------------
}

#endif //__ParticleComponent_h__9_7_2026__14_00_00__
