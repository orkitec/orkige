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
#include "core_debug/MemoryManager.h"

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
			//--- 3D mode (default OFF - while space3D is false the whole planar
			// path above stays byte-identical; the fields below drive the 3D
			// world-space path used for weather) ---
			//! spawn-placement volume for the 3D emitter
			enum EmissionVolume
			{
				VOLUME_POINT = 0,	//!< a single point at the emitter origin
				VOLUME_SPHERE = 1,	//!< inside a sphere (volumeExtents.x = radius)
				VOLUME_BOX = 2		//!< inside an axis-aligned box (volumeExtents = half-extents)
			};
			bool	space3D = false;	//!< run the 3D path instead of the planar 2D one
			bool	worldSpace = true;	//!< particles live in WORLD space (do NOT follow a moving emitter); false = emitter-local (particles ride the emitter)
			int		emissionVolume = VOLUME_POINT;	//!< @see EmissionVolume
			Vec3	volumeExtents = Vec3(0.0f, 0.0f, 0.0f);	//!< sphere: x = radius; box: per-axis half-extents
			Vec3	spawnOffset3D = Vec3(0.0f, 0.0f, 0.0f);	//!< added to the emitter origin (3D)
			Vec3	direction3D = Vec3(0.0f, 1.0f, 0.0f);	//!< emission cone axis (normalized internally)
			Vec3	gravity3D = Vec3(0.0f, -9.8f, 0.0f);	//!< world acceleration (3D)
			Vec3	wind = Vec3(0.0f, 0.0f, 0.0f);	//!< constant world acceleration added each step (weather shear)
			float	stretch = 0.0f;			//!< velocity-stretch factor for rain-streak billboards (0 = round quad)
			float	flutterAmplitude = 0.0f;	//!< snow sideways-sway acceleration amplitude (world units/s^2)
			float	flutterFrequency = 0.0f;	//!< snow sway frequency (cycles/second)
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
			//--- 3D fields (used only on the space3D path; the 2D path never
			// touches these so its behaviour stays byte-identical) ---
			Vec3	position3 = Vec3(0.0f, 0.0f, 0.0f);	//!< 3D position (world or emitter-local, @see EmitterParams::worldSpace)
			Vec3	velocity3 = Vec3(0.0f, 0.0f, 0.0f);	//!< 3D velocity
			float	flutterPhase = 0.0f;	//!< per-particle sway phase offset (snow flutter)
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

		//--- 3D world-space path (weather); active when EmitterParams::space3D ---
		//! @brief advance the 3D system by dt seconds, emitting from the 3D
		//! emitter origin and integrating every live particle under
		//! gravity3D + wind (+ the optional snow flutter). @see update
		void update3D(float dt, Vec3 const & origin)
		{
			if(dt < 0.0f)
			{
				dt = 0.0f;
			}
			this->emitContinuous3D(dt, origin);
			this->integrate3D(dt);
		}
		//! @brief spawn a 3D burst of min(count, remaining capacity) particles
		//! at the 3D emitter origin (count <= 0 uses burstCount). @see burst
		int burst3D(int count, Vec3 const & origin)
		{
			if(count <= 0)
			{
				count = this->mParams.burstCount;
			}
			int spawned = 0;
			for(int each = 0; each < count; ++each)
			{
				if(!this->spawnOne3D(origin))
				{
					break;	// pool full
				}
				++spawned;
			}
			return spawned;
		}
		//! @brief the particle's RENDER position: its stored position for
		//! world-space particles, or that position offset by the emitter's
		//! CURRENT origin for emitter-local ones (so they follow a moving
		//! emitter). This is the world-vs-local seam.
		Vec3 worldPosition3D(Particle const & particle,
			Vec3 const & currentOrigin) const
		{
			return this->mParams.worldSpace
				? particle.position3
				: (particle.position3 + currentOrigin);
		}

		//--- pure billboard math (headless-testable) -----------------------
		//! @brief camera-facing quad corners in the SpriteBatch winding
		//! (TL, TR, BR, BL) from the camera's world-space right/up axes scaled
		//! to halfSize - the CPU-billboard core.
		static void billboardCorners(Vec3 const & center, Vec3 const & right,
			Vec3 const & up, float halfSize, Vec3 outCorners[4])
		{
			const Vec3 r = right * halfSize;
			const Vec3 u = up * halfSize;
			outCorners[0] = center - r + u;	// top-left
			outCorners[1] = center + r + u;	// top-right
			outCorners[2] = center + r - u;	// bottom-right
			outCorners[3] = center - r - u;	// bottom-left
		}
		//! @brief velocity-stretched streak corners (rain): the quad stays in
		//! the camera plane, but its long axis follows the particle velocity's
		//! ON-SCREEN direction stretched to halfLength, with halfWidth across.
		//! Falls back to a plain billboard when the velocity projects to ~zero
		//! on screen (a particle moving straight at/away from the camera).
		static void streakCorners(Vec3 const & center, Vec3 const & right,
			Vec3 const & up, Vec3 const & velocity, float halfWidth,
			float halfLength, Vec3 outCorners[4])
		{
			const float vx = velocity.dotProduct(right);
			const float vy = velocity.dotProduct(up);
			const float lenSq = vx * vx + vy * vy;
			if(lenSq < 1e-8f)
			{
				billboardCorners(center, right, up, halfWidth, outCorners);
				return;
			}
			const float inv = 1.0f / std::sqrt(lenSq);
			// long axis = screen-projected velocity direction (in world)
			const Vec3 axis = (right * (vx * inv) + up * (vy * inv)) * halfLength;
			// cross axis = perpendicular to it, still in the camera plane
			const Vec3 perp = (right * (-vy * inv) + up * (vx * inv)) * halfWidth;
			outCorners[0] = center - perp + axis;	// top-left
			outCorners[1] = center + perp + axis;	// top-right
			outCorners[2] = center + perp - axis;	// bottom-right
			outCorners[3] = center - perp - axis;	// bottom-left
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
			// the pool is reserved to capacity, so this push never grows in
			// steady state - the probe guards the contract
			const std::size_t capacityBefore = this->mParticles.capacity();
			this->mParticles.push_back(particle);
			MemoryManager::countGrowth(MemoryManager::TAG_PARTICLES,
				capacityBefore, this->mParticles.capacity());
			return true;
		}
		//! choose an atlas grid cell in the inclusive [min,max] range (shared by
		//! the 2D and 3D spawn paths)
		int pickAtlasFrame()
		{
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
				return frameLow + std::min(pick, span - 1);
			}
			return frameLow;
		}
		//! a random unit direction within a cone of the given half-angle around
		//! @p axis (a 180-degree half-angle samples the whole sphere uniformly)
		Vec3 randomConeDirection(Vec3 const & axis, float halfAngleDegrees)
		{
			const float degToRad = 3.14159265358979f / 180.0f;
			Vec3 a = axis;
			const float len = a.length();
			a = (len > 1e-6f) ? (a / len) : Vec3(0.0f, 1.0f, 0.0f);
			const float cosHalf = std::cos(halfAngleDegrees * degToRad);
			// uniform on the spherical cap: cos(theta) uniform in [cosHalf, 1]
			const float cosTheta = this->randRange(cosHalf, 1.0f);
			const float sinTheta = std::sqrt(std::max(0.0f,
				1.0f - cosTheta * cosTheta));
			const float phi = this->randRange(0.0f, 2.0f * 3.14159265358979f);
			// an orthonormal basis (t1, t2) spanning the plane perpendicular to a
			Vec3 helper = (std::fabs(a.x) < 0.9f)
				? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
			Vec3 t1 = a.crossProduct(helper);
			t1.normalise();
			const Vec3 t2 = a.crossProduct(t1);	// unit (a, t1 orthonormal)
			Vec3 dir = a * cosTheta +
				(t1 * std::cos(phi) + t2 * std::sin(phi)) * sinTheta;
			dir.normalise();
			return dir;
		}
		//! sample an emitter-local offset inside the configured emission volume
		Vec3 sampleVolume()
		{
			switch(this->mParams.emissionVolume)
			{
			case EmitterParams::VOLUME_SPHERE:
			{
				const float radius = this->mParams.volumeExtents.x;
				// uniform inside the sphere: a uniform direction scaled by
				// radius * cbrt(u) (constant volumetric density)
				const Vec3 dir = this->randomConeDirection(
					Vec3(0.0f, 1.0f, 0.0f), 180.0f);
				return dir * (radius * std::cbrt(this->nextUnit()));
			}
			case EmitterParams::VOLUME_BOX:
			{
				Vec3 const & e = this->mParams.volumeExtents;
				return Vec3(this->randRange(-e.x, e.x),
					this->randRange(-e.y, e.y),
					this->randRange(-e.z, e.z));
			}
			default:
				return Vec3(0.0f, 0.0f, 0.0f);
			}
		}
		//! emit the 3D continuous stream for dt seconds (rate accumulator + the
		//! optional duration/looping window - the 3D twin of emitContinuous)
		void emitContinuous3D(float dt, Vec3 const & origin)
		{
			if(!this->mEmitting || this->mParams.emissionRate <= 0.0f)
			{
				return;
			}
			if(this->mParams.duration > 0.0f)
			{
				if(this->mDurationTimer >= this->mParams.duration)
				{
					if(this->mParams.looping)
					{
						this->mDurationTimer = 0.0f;
					}
					else
					{
						this->mEmitting = false;
						return;
					}
				}
				this->mDurationTimer += dt;
			}
			this->mRateAccumulator += dt * this->mParams.emissionRate;
			while(this->mRateAccumulator >= 1.0f)
			{
				this->mRateAccumulator -= 1.0f;
				if(!this->spawnOne3D(origin))
				{
					this->mRateAccumulator = 0.0f;
					break;
				}
			}
		}
		//! integrate every 3D particle one step under gravity3D + wind (+ the
		//! optional snow flutter) and swap-remove the expired ones
		void integrate3D(float dt)
		{
			const float dampFactor = std::max(0.0f,
				1.0f - this->mParams.damping * dt);
			const Vec3 accel = this->mParams.gravity3D + this->mParams.wind;
			const float twoPi = 2.0f * 3.14159265358979f;
			const bool flutter = this->mParams.flutterAmplitude > 0.0f;
			std::size_t index = 0;
			while(index < this->mParticles.size())
			{
				Particle & particle = this->mParticles[index];
				particle.age += dt;
				if(particle.age >= particle.lifetime)
				{
					this->mParticles[index] = this->mParticles.back();
					this->mParticles.pop_back();
					continue;
				}
				particle.velocity3 += accel * dt;
				if(flutter)
				{
					// a sideways sway on the horizontal plane (X and Z), 90 deg
					// out of phase so flakes trace little circles as they drift
					const float t = twoPi * this->mParams.flutterFrequency *
						particle.age + particle.flutterPhase;
					particle.velocity3.x +=
						this->mParams.flutterAmplitude * std::sin(t) * dt;
					particle.velocity3.z +=
						this->mParams.flutterAmplitude * std::cos(t) * dt;
				}
				if(this->mParams.damping > 0.0f)
				{
					particle.velocity3 *= dampFactor;
				}
				particle.position3 += particle.velocity3 * dt;
				++index;
			}
		}
		//! spawn one 3D particle from the emitter origin; false when full
		bool spawnOne3D(Vec3 const & origin)
		{
			if(this->mParticles.size() >=
				static_cast<std::size_t>(this->capacity()))
			{
				return false;
			}
			Particle particle;
			particle.lifetime = std::max(0.0001f,
				this->randRange(this->mParams.lifetimeMin,
					this->mParams.lifetimeMax));
			particle.age = 0.0f;
			const Vec3 dir = this->randomConeDirection(this->mParams.direction3D,
				this->mParams.spreadAngle);
			const float speed = this->randRange(this->mParams.speedMin,
				this->mParams.speedMax);
			particle.velocity3 = dir * speed;
			const Vec3 localOffset = this->mParams.spawnOffset3D +
				this->sampleVolume();
			particle.position3 = this->mParams.worldSpace
				? (origin + localOffset) : localOffset;
			particle.flutterPhase = this->nextUnit() *
				(2.0f * 3.14159265358979f);
			particle.frame = this->pickAtlasFrame();
			const std::size_t capacityBefore = this->mParticles.capacity();
			this->mParticles.push_back(particle);
			MemoryManager::countGrowth(MemoryManager::TAG_PARTICLES,
				capacityBefore, this->mParticles.capacity());
			return true;
		}
	};
}

#endif //__ParticleSim_h__9_7_2026__14_00_00__
