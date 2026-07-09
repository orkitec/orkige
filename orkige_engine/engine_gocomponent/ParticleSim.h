/********************************************************************
	created:	Wednesday 2026/07/09 at 14:00
	filename: 	ParticleSim.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ParticleSim_h__9_7_2026__14_00_00__
#define __ParticleSim_h__9_7_2026__14_00_00__

#include "engine_render/RenderMath.h"
#include "core_tween/EaseLibrary.h"

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Orkige
{
	//! @brief the CPU particle simulation - a pure, renderer-free, SEEDED
	//! emitter and pool. Headless-testable and deterministic: the
	//! same seed produces the same particle sequence, which the render parity
	//! screenshot test relies on.
	//! @remarks Planar 2D (XY), single texture. Emits at a rate and/or in
	//! bursts, integrates each particle under gravity + damping (semi-implicit
	//! Euler), kills it at its lifetime and swap-removes it from a fixed pool
	//! (capacity = maxParticles). Size and colour interpolate start->end over
	//! each particle's normalized life, SHAPED by a curve from the shared
	//! core_tween/EaseLibrary (the easing/tween enabler) - v1 is two-point lerps, not
	//! multi-keyframe tracks. The owning ParticleComponent turns the live
	//! particles into a SpriteBatch vertex array every frame; this class knows
	//! nothing of the renderer.
	class ParticleSim
	{
		//--- Types -------------------------------------------------
	public:
		//! blend recipe the batch should composite with (kept renderer-free
		//! here - the component maps it onto SpriteBatch::BlendMode)
		enum BlendMode
		{
			BLEND_ALPHA = 0,	//!< alpha over (order-dependent)
			BLEND_ADDITIVE = 1	//!< additive glow (order-independent; burst default)
		};
		//! @brief the authored emitter configuration (serialized whole by the
		//! component; scriptable through the component setters)
		struct EmitterParams
		{
			//--- emission ---
			float	emissionRate = 20.0f;	//!< continuous particles per second (0 = burst-only)
			int		burstCount = 24;		//!< default particles per burst() with no argument
			float	duration = 0.0f;		//!< continuous emission window in seconds (<= 0 = infinite)
			bool	looping = true;			//!< restart the duration window when it elapses
			//--- lifetime (seconds) ---
			float	lifetimeMin = 0.6f;
			float	lifetimeMax = 1.0f;
			//--- spawn placement + direction ---
			Vec2	spawnOffset = Vec2(0.0f, 0.0f);	//!< added to the emitter origin
			float	directionAngle = 90.0f;	//!< cone centre in degrees (0 = +X, 90 = +Y up)
			float	spreadAngle = 45.0f;	//!< cone half-angle in degrees
			float	speedMin = 2.0f;
			float	speedMax = 4.0f;
			//--- dynamics ---
			Vec2	gravity = Vec2(0.0f, -3.0f);	//!< world-units/s^2
			float	damping = 0.0f;			//!< per-second velocity damping (0 = none)
			float	spinMin = 0.0f;			//!< angular velocity range, degrees/second
			float	spinMax = 0.0f;
			//--- appearance (over-life, shaped by the eases) ---
			float	startSize = 0.4f;		//!< world-unit quad size at birth
			float	endSize = 0.0f;			//!< world-unit quad size at death
			Color	startColor = Color(1.0f, 0.9f, 0.4f, 1.0f);
			Color	endColor = Color(1.0f, 0.4f, 0.1f, 0.0f);
			String	sizeEase = "linear";	//!< EaseLibrary name shaping size over life
			String	colorEase = "linear";	//!< EaseLibrary name shaping colour over life
			//--- atlas (single texture, frame range within a grid) ---
			int		atlasColumns = 1;
			int		atlasRows = 1;
			int		atlasFrameMin = 0;		//!< first grid cell (inclusive)
			int		atlasFrameMax = 0;		//!< last grid cell (inclusive); a frame is chosen per particle
			//--- caps / rendering ---
			int		maxParticles = 256;		//!< pool capacity (hard cap)
			int		zOrder = 10;			//!< painter's order (SpriteBatch/SpriteQuad window)
			int		blendMode = BLEND_ADDITIVE;	//!< @see ParticleSim::BlendMode
		};
		//! @brief one live particle (world-space, planar XY)
		struct Particle
		{
			Vec2	position = Vec2(0.0f, 0.0f);
			Vec2	velocity = Vec2(0.0f, 0.0f);
			float	age = 0.0f;				//!< seconds since spawn
			float	lifetime = 1.0f;		//!< total life in seconds (> 0)
			float	rotation = 0.0f;		//!< current spin in radians
			float	angularVelocity = 0.0f;	//!< radians/second
			int		frame = 0;				//!< chosen atlas grid cell
		};
	private:
		//--- Variables ---------------------------------------------
		EmitterParams			mParams;
		std::vector<Particle>	mParticles;		//!< live pool (size = live count, reserved to maxParticles)
		bool					mEmitting;		//!< continuous emission on/off
		float					mRateAccumulator;	//!< fractional particles carried between frames
		float					mDurationTimer;	//!< elapsed seconds of the current emission window
		std::uint32_t			mSeed;			//!< the reproducible seed
		std::uint32_t			mRngState;		//!< current PRNG state
		//--- Methods -----------------------------------------------
	public:
		//! @brief construct with a fixed seed (determinism is required)
		explicit ParticleSim(std::uint32_t seed = 0x9E3779B9u)
			: mEmitting(false), mRateAccumulator(0.0f), mDurationTimer(0.0f),
			mSeed(seed), mRngState(seed ? seed : 1u)
		{
			this->mParticles.reserve(static_cast<std::size_t>(
				std::max(1, this->mParams.maxParticles)));
		}
		//! @brief adopt an emitter configuration (re-reserves the pool cap and
		//! clamps the live count to the new maximum)
		void setParams(EmitterParams const & params)
		{
			this->mParams = params;
			const std::size_t cap = static_cast<std::size_t>(
				std::max(1, this->mParams.maxParticles));
			this->mParticles.reserve(cap);
			if(this->mParticles.size() > cap)
			{
				this->mParticles.resize(cap);
			}
		}
		//! the current emitter configuration
		EmitterParams const & params() const { return this->mParams; }
		//! mutable access (the component's granular Lua setters write through it)
		EmitterParams & params() { return this->mParams; }

		//! @brief the reproducible seed (re-applied by reset)
		void setSeed(std::uint32_t seed) { this->mSeed = seed; }
		std::uint32_t seed() const { return this->mSeed; }

		//! @brief clear all particles and restore the initial deterministic
		//! state (RNG re-seeded, accumulators zeroed) - a fresh, reproducible run
		void reset()
		{
			this->mParticles.clear();
			this->mRateAccumulator = 0.0f;
			this->mDurationTimer = 0.0f;
			this->mRngState = this->mSeed ? this->mSeed : 1u;
			this->mEmitting = false;
		}

		//--- emission control ---
		void setEmitting(bool emitting) { this->mEmitting = emitting; }
		bool isEmitting() const { return this->mEmitting; }
		//! begin continuous emission from the start of a fresh duration window
		void start()
		{
			this->mEmitting = true;
			this->mDurationTimer = 0.0f;
			this->mRateAccumulator = 0.0f;
		}
		//! stop continuous emission (live particles keep flying out)
		void stop() { this->mEmitting = false; }

		//! @brief spawn a burst of exactly min(count, remaining capacity)
		//! particles at the given world origin; count <= 0 uses burstCount.
		//! @return the number actually spawned
		int burst(int count, Vec2 const & origin)
		{
			if(count <= 0)
			{
				count = this->mParams.burstCount;
			}
			int spawned = 0;
			for(int each = 0; each < count; ++each)
			{
				if(!this->spawnOne(origin))
				{
					break;	// pool full
				}
				++spawned;
			}
			return spawned;
		}

		//! @brief advance the whole system by dt seconds, emitting from origin
		//! (the emitter's current world position) and integrating + culling
		//! every live particle
		void update(float dt, Vec2 const & origin)
		{
			if(dt < 0.0f)
			{
				dt = 0.0f;
			}
			this->emitContinuous(dt, origin);
			this->integrate(dt);
		}

		//--- inspection (the component + the unit tests read these) ---
		int liveCount() const
		{
			return static_cast<int>(this->mParticles.size());
		}
		int capacity() const { return std::max(1, this->mParams.maxParticles); }
		Particle const & particleAt(int index) const
		{
			return this->mParticles[static_cast<std::size_t>(index)];
		}

		//! @brief the particle's normalized life in [0,1] (age / lifetime)
		static float normalizedLife(Particle const & particle)
		{
			if(particle.lifetime <= 0.0f)
			{
				return 1.0f;
			}
			return std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
		}
		//! @brief the particle's current world-unit size, start->end shaped by
		//! the size ease (THE EaseLibrary consumption point for size)
		float sizeAt(Particle const & particle) const
		{
			const float shaped = this->easeValue(this->mParams.sizeEase,
				normalizedLife(particle));
			return lerpScalar(this->mParams.startSize, this->mParams.endSize,
				shaped);
		}
		//! @brief the particle's current colour, start->end shaped by the
		//! colour ease (THE EaseLibrary consumption point for colour + alpha)
		Color colorAt(Particle const & particle) const
		{
			const float shaped = this->easeValue(this->mParams.colorEase,
				normalizedLife(particle));
			Color const & a = this->mParams.startColor;
			Color const & b = this->mParams.endColor;
			return Color(
				lerpScalar(a.r, b.r, shaped),
				lerpScalar(a.g, b.g, shaped),
				lerpScalar(a.b, b.b, shaped),
				lerpScalar(a.a, b.a, shaped));
		}
	private:
		//! linear interpolation of a scalar
		static float lerpScalar(float a, float b, float t)
		{
			return a + (b - a) * t;
		}
		//! resolve an EaseLibrary curve and evaluate it (unknown/empty = linear)
		float easeValue(String const & easeName, float t) const
		{
			Ease::Function ease = Ease::byName(easeName);
			if(!ease)
			{
				ease = &Ease::linear;
			}
			return ease(t);
		}
		//! next raw PRNG word (a xorshift32 - dependency-free and reproducible;
		//! std distributions are avoided so the sequence never drifts between
		//! backends/compilers on the parity test)
		std::uint32_t nextRandom()
		{
			std::uint32_t x = this->mRngState;
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			this->mRngState = x;
			return x;
		}
		//! a uniform float in [0,1)
		float nextUnit()
		{
			return static_cast<float>(this->nextRandom() >> 8) *
				(1.0f / 16777216.0f);
		}
		//! a uniform float in [a,b]
		float randRange(float a, float b)
		{
			return a + (b - a) * this->nextUnit();
		}
		//! emit the continuous stream for dt seconds (rate accumulator + the
		//! optional duration/looping window)
		void emitContinuous(float dt, Vec2 const & origin)
		{
			if(!this->mEmitting || this->mParams.emissionRate <= 0.0f)
			{
				return;
			}
			// honour a finite duration window
			if(this->mParams.duration > 0.0f)
			{
				if(this->mDurationTimer >= this->mParams.duration)
				{
					if(this->mParams.looping)
					{
						this->mDurationTimer = 0.0f;	// restart the window
					}
					else
					{
						this->mEmitting = false;		// one-shot spent
						return;
					}
				}
				this->mDurationTimer += dt;
			}
			this->mRateAccumulator += dt * this->mParams.emissionRate;
			while(this->mRateAccumulator >= 1.0f)
			{
				this->mRateAccumulator -= 1.0f;
				if(!this->spawnOne(origin))
				{
					this->mRateAccumulator = 0.0f;	// pool full, drop the rest
					break;
				}
			}
		}
		//! integrate every particle one step and swap-remove the expired ones
		void integrate(float dt)
		{
			const float dampFactor = std::max(0.0f,
				1.0f - this->mParams.damping * dt);
			std::size_t index = 0;
			while(index < this->mParticles.size())
			{
				Particle & particle = this->mParticles[index];
				particle.age += dt;
				if(particle.age >= particle.lifetime)
				{
					// swap-remove: O(1) retire, order irrelevant for additive
					this->mParticles[index] =
						this->mParticles.back();
					this->mParticles.pop_back();
					continue;	// re-test the swapped-in particle at this index
				}
				// semi-implicit Euler: velocity first, then position
				particle.velocity += this->mParams.gravity * dt;
				if(this->mParams.damping > 0.0f)
				{
					particle.velocity *= dampFactor;
				}
				particle.position += particle.velocity * dt;
				particle.rotation += particle.angularVelocity * dt;
				++index;
			}
		}
		//! spawn one particle at the origin; false when the pool is full
		bool spawnOne(Vec2 const & origin)
		{
			if(this->mParticles.size() >=
				static_cast<std::size_t>(this->capacity()))
			{
				return false;
			}
			const float degToRad = 3.14159265358979f / 180.0f;
			Particle particle;
			particle.lifetime = std::max(0.0001f,
				this->randRange(this->mParams.lifetimeMin,
					this->mParams.lifetimeMax));
			particle.age = 0.0f;
			const float angle = (this->mParams.directionAngle +
				this->randRange(-this->mParams.spreadAngle,
					this->mParams.spreadAngle)) * degToRad;
			const float speed = this->randRange(this->mParams.speedMin,
				this->mParams.speedMax);
			particle.velocity = Vec2(std::cos(angle) * speed,
				std::sin(angle) * speed);
			particle.position = origin + this->mParams.spawnOffset;
			particle.rotation = 0.0f;
			particle.angularVelocity = this->randRange(this->mParams.spinMin,
				this->mParams.spinMax) * degToRad;
			// choose an atlas frame in the inclusive [min,max] range
			int frameLow = this->mParams.atlasFrameMin;
			int frameHigh = this->mParams.atlasFrameMax;
			if(frameHigh < frameLow)
			{
				std::swap(frameLow, frameHigh);
			}
			if(frameHigh > frameLow)
			{
				const int span = frameHigh - frameLow + 1;
				const int pick = static_cast<int>(
					this->nextUnit() * static_cast<float>(span));
				particle.frame = frameLow + std::min(pick, span - 1);
			}
			else
			{
				particle.frame = frameLow;
			}
			this->mParticles.push_back(particle);
			return true;
		}
	};
}

#endif //__ParticleSim_h__9_7_2026__14_00_00__
