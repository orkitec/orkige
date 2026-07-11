/**************************************************************
	created:	2026/07/11 at 09:30
	filename: 	SoftBodyDeform.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file SoftBodyDeform.cpp
//! @brief the fixed-topology weight-based deform implementation
//! (@see SoftBodyDeform.h)

#include "core_util/SoftBodyDeform.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! below this a direction/speed is treated as zero (no axis to squash on)
		const float SOFTBODY_EPSILON = 1.0e-5f;
		//! velocity stretch deadzone (units/s): below it the body is treated as
		//! still, so a shape resting on the ground (tiny solver jitter) shows NO
		//! stretch and reaches an exact rest instead of shimmering forever
		const float STRETCH_MIN_SPEED = 0.05f;
		//! clamp on the squash fraction so the volume-preserving 1/(1-s) stays finite
		const float SQUASH_CLAMP = 0.85f;
		//! Gaussian falloff width, as a fraction of the control bounds diagonal
		//! scaled by the point spacing - wide enough that neighbours blend smoothly
		const float FALLOFF_SIGMA_FACTOR = 0.9f;

		//! smoothstep ease for the morph blend (0 at 0, 1 at 1, flat tangents)
		float smoothstep01(float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}
	//---------------------------------------------------------
	SoftBodyDeform::SoftBodyDeform()
		: mBuilt(false), mPivot(0.0f, 0.0f), mK(0), mSquashAxis(0.0f, 1.0f),
		mVelX(0.0f), mVelY(0.0f), mMorphTarget(-1), mMorphPhase(0.0f),
		mMorphSpeed(1.0f), mMorphLoop(false), mMorphPlaying(false), mMorphDir(1),
		mSquashCurrent(0.0f)
	{
	}
	//---------------------------------------------------------
	void SoftBodyDeform::selectControlPoints(std::vector<Region> const & regions,
		int targetCount, std::vector<Point> & out)
	{
		// flatten every region's OUTER contour in region order (topology-stable:
		// two shapes with matching contour sizes flatten to the same length, so
		// the even-spaced picks below land on corresponding vertices)
		std::vector<Point> flat;
		for(Region const & region : regions)
		{
			for(Point const & point : region.outer)
			{
				flat.push_back(point);
			}
		}
		const int n = static_cast<int>(flat.size());
		if(n <= 0)
		{
			return;
		}
		const int count = std::min(targetCount > 0 ? targetCount : 1, n);
		for(int k = 0; k < count; ++k)
		{
			// evenly spaced index across the flat contour list
			const int idx = static_cast<int>(
				(static_cast<long long>(k) * n) / count);
			out.push_back(flat[idx < n ? idx : n - 1]);
		}
	}
	//---------------------------------------------------------
	bool SoftBodyDeform::sameTopology(std::vector<Region> const & a,
		std::vector<Region> const & b)
	{
		if(a.size() != b.size())
		{
			return false;
		}
		for(std::size_t r = 0; r < a.size(); ++r)
		{
			if(a[r].outer.size() != b[r].outer.size() ||
				a[r].holes.size() != b[r].holes.size())
			{
				return false;
			}
			for(std::size_t h = 0; h < a[r].holes.size(); ++h)
			{
				if(a[r].holes[h].size() != b[r].holes[h].size())
				{
					return false;
				}
			}
		}
		return true;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::build(std::vector<Region> const & baseRegions,
		std::vector<Point> const & restMesh, Params const & params)
	{
		this->mBuilt = false;
		this->mParams = params;
		this->mRestMesh.clear();
		this->mRestControl.clear();
		this->mInfluences.clear();
		this->mSpringX.clear();
		this->mSpringY.clear();
		this->mMorphTargets.clear();
		this->mControlDelta.clear();

		if(baseRegions.empty() || restMesh.empty())
		{
			return;	// nothing to deform
		}
		this->mRestControl.reserve(params.controlPointCount);
		SoftBodyDeform::selectControlPoints(baseRegions,
			params.controlPointCount, this->mRestControl);
		const int controlCount = static_cast<int>(this->mRestControl.size());
		if(controlCount == 0)
		{
			return;
		}

		this->mRestMesh = restMesh;
		this->mControlDelta.assign(controlCount, Point(0.0f, 0.0f));

		// pivot = centroid of the rest control points (the squash/stretch anchor)
		float px = 0.0f, py = 0.0f;
		for(Point const & control : this->mRestControl)
		{
			px += control.x;
			py += control.y;
		}
		this->mPivot = Point(px / controlCount, py / controlCount);

		// falloff width from the control-point spread: a Gaussian sigma that
		// spans roughly the spacing between neighbouring control points, so a
		// vertex blends its few nearest smoothly rather than snapping to one
		float minX = this->mRestControl[0].x, maxX = minX;
		float minY = this->mRestControl[0].y, maxY = minY;
		for(Point const & control : this->mRestControl)
		{
			minX = std::min(minX, control.x);
			maxX = std::max(maxX, control.x);
			minY = std::min(minY, control.y);
			maxY = std::max(maxY, control.y);
		}
		const float diagonal = std::sqrt((maxX - minX) * (maxX - minX) +
			(maxY - minY) * (maxY - minY));
		float sigma = FALLOFF_SIGMA_FACTOR * diagonal /
			std::sqrt(static_cast<float>(controlCount));
		if(sigma < SOFTBODY_EPSILON)
		{
			sigma = 1.0f;	// degenerate (all control points coincident)
		}
		const float invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);

		// skin each mesh vertex to its K nearest control points, Gaussian-weighted
		this->mK = std::clamp(params.influencesPerVertex, 1, controlCount);
		this->mInfluences.assign(this->mRestMesh.size() * this->mK, Influence());
		std::vector<std::pair<float, int> > byDistance;
		byDistance.reserve(controlCount);
		for(std::size_t v = 0; v < this->mRestMesh.size(); ++v)
		{
			Point const & vertex = this->mRestMesh[v];
			byDistance.clear();
			for(int c = 0; c < controlCount; ++c)
			{
				const float dx = vertex.x - this->mRestControl[c].x;
				const float dy = vertex.y - this->mRestControl[c].y;
				byDistance.push_back(std::make_pair(dx * dx + dy * dy, c));
			}
			std::partial_sort(byDistance.begin(), byDistance.begin() + this->mK,
				byDistance.end());
			float sum = 0.0f;
			for(int k = 0; k < this->mK; ++k)
			{
				const float weight =
					std::exp(-byDistance[k].first * invTwoSigmaSq);
				this->mInfluences[v * this->mK + k].control = byDistance[k].second;
				this->mInfluences[v * this->mK + k].weight = weight;
				sum += weight;
			}
			// normalize so a rigid control translation reproduces exactly
			const float inv = sum > SOFTBODY_EPSILON ? 1.0f / sum : 0.0f;
			for(int k = 0; k < this->mK; ++k)
			{
				this->mInfluences[v * this->mK + k].weight *= inv;
			}
		}

		// per-control springs (2 each) at the requested dynamics
		this->mSpringX.resize(controlCount);
		this->mSpringY.resize(controlCount);
		for(int c = 0; c < controlCount; ++c)
		{
			this->mSpringX[c].setParams(params.wobbleStiffness,
				params.wobbleDamping);
			this->mSpringY[c].setParams(params.wobbleStiffness,
				params.wobbleDamping);
		}
		this->mSquashSpring.setParams(params.squashStiffness,
			params.squashDamping);
		this->resetDynamics();
		this->mBuilt = true;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::setParams(Params const & params)
	{
		if(!this->mBuilt)
		{
			return;
		}
		// keep the structural fields (control count, K) from the build; only the
		// live dynamics change
		this->mParams.wobbleStiffness = params.wobbleStiffness;
		this->mParams.wobbleDamping = params.wobbleDamping;
		this->mParams.wobbleAmount = params.wobbleAmount;
		this->mParams.squashStiffness = params.squashStiffness;
		this->mParams.squashDamping = params.squashDamping;
		this->mParams.squashAmount = params.squashAmount;
		this->mParams.stretchGain = params.stretchGain;
		this->mParams.maxStretch = params.maxStretch;
		for(std::size_t c = 0; c < this->mSpringX.size(); ++c)
		{
			this->mSpringX[c].setParams(params.wobbleStiffness,
				params.wobbleDamping);
			this->mSpringY[c].setParams(params.wobbleStiffness,
				params.wobbleDamping);
		}
		this->mSquashSpring.setParams(params.squashStiffness,
			params.squashDamping);
	}
	//---------------------------------------------------------
	bool SoftBodyDeform::addMorphTarget(String const & name,
		std::vector<Region> const & baseRegions,
		std::vector<Region> const & targetRegions)
	{
		if(!this->mBuilt)
		{
			return false;
		}
		if(!SoftBodyDeform::sameTopology(baseRegions, targetRegions))
		{
			return false;	// a morph target MUST share the deform topology
		}
		MorphTarget target;
		target.name = name;
		SoftBodyDeform::selectControlPoints(targetRegions,
			this->mParams.controlPointCount, target.control);
		if(target.control.size() != this->mRestControl.size())
		{
			return false;	// selection disagreed (shouldn't happen given topology)
		}
		this->mMorphTargets.push_back(target);
		return true;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::playMorph(int targetIndex, float speed, bool loop)
	{
		if(targetIndex < 0 ||
			targetIndex >= static_cast<int>(this->mMorphTargets.size()))
		{
			return;
		}
		this->mMorphTarget = targetIndex;
		this->mMorphSpeed = speed > 0.0f ? speed : 1.0f;
		this->mMorphLoop = loop;
		this->mMorphPlaying = true;
		this->mMorphDir = 1;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::stopMorph()
	{
		this->mMorphPlaying = false;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::applyImpulse(float dirX, float dirY, float magnitude)
	{
		if(!this->mBuilt)
		{
			return;
		}
		const float length = std::sqrt(dirX * dirX + dirY * dirY);
		if(length < SOFTBODY_EPSILON || magnitude <= 0.0f)
		{
			return;
		}
		const float nx = dirX / length;
		const float ny = dirY / length;
		this->mSquashAxis = Point(nx, ny);
		// squash depth from the impact: the spring value is a squash fraction,
		// so kicking its velocity lets it grow then oscillate back to zero
		this->mSquashSpring.kick(magnitude * this->mParams.squashAmount);
		// the same contact kicks the wobble springs along the impact direction
		const float wobbleKick = magnitude * this->mParams.wobbleAmount;
		for(std::size_t c = 0; c < this->mSpringX.size(); ++c)
		{
			this->mSpringX[c].kick(nx * wobbleKick);
			this->mSpringY[c].kick(ny * wobbleKick);
		}
	}
	//---------------------------------------------------------
	void SoftBodyDeform::setBodyVelocity(float velocityX, float velocityY)
	{
		this->mVelX = velocityX;
		this->mVelY = velocityY;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::update(float deltaTime)
	{
		if(!this->mBuilt || deltaTime <= 0.0f)
		{
			return;
		}
		// morph phase advance (ping-pong when looping, else clamp/stop at 1)
		if(this->mMorphPlaying && this->mMorphTarget >= 0)
		{
			this->mMorphPhase += this->mMorphDir * this->mMorphSpeed * deltaTime;
			if(this->mMorphPhase >= 1.0f)
			{
				if(this->mMorphLoop)
				{
					this->mMorphPhase = 1.0f;
					this->mMorphDir = -1;
				}
				else
				{
					this->mMorphPhase = 1.0f;
					this->mMorphPlaying = false;
				}
			}
			else if(this->mMorphPhase <= 0.0f)
			{
				this->mMorphPhase = 0.0f;
				if(this->mMorphLoop)
				{
					this->mMorphDir = 1;
				}
				else
				{
					this->mMorphPlaying = false;
				}
			}
		}
		// integrate the springs
		for(std::size_t c = 0; c < this->mSpringX.size(); ++c)
		{
			this->mSpringX[c].update(deltaTime);
			this->mSpringY[c].update(deltaTime);
		}
		this->mSquashSpring.update(deltaTime);
		this->computeControlDeltas();
	}
	//---------------------------------------------------------
	void SoftBodyDeform::computeControlDeltas()
	{
		const int controlCount = static_cast<int>(this->mRestControl.size());
		// --- build the anisotropic affine M (about the pivot): squash then
		// stretch, both volume-preserving axis-aligned scales stacked as
		//   S(axis, sa, sp) = sp*I + (sa - sp) * axis (x) axis
		// (sa scales along the axis, sp across it). M = Mstretch * Msquash.
		float sm00 = 1.0f, sm01 = 0.0f, sm10 = 0.0f, sm11 = 1.0f;	// squash
		float squash = std::clamp(this->mSquashSpring.value(),
			-SQUASH_CLAMP, SQUASH_CLAMP);
		this->mSquashCurrent = squash;
		if(std::fabs(squash) > SOFTBODY_EPSILON)
		{
			const float sa = 1.0f - squash;			// compress along the impact
			const float sp = 1.0f / sa;				// bulge across it (area kept)
			const float ax = this->mSquashAxis.x, ay = this->mSquashAxis.y;
			sm00 = sp + (sa - sp) * ax * ax;
			sm01 = (sa - sp) * ax * ay;
			sm10 = (sa - sp) * ay * ax;
			sm11 = sp + (sa - sp) * ay * ay;
		}
		float tm00 = 1.0f, tm01 = 0.0f, tm10 = 0.0f, tm11 = 1.0f;	// stretch
		const float speed = std::sqrt(this->mVelX * this->mVelX +
			this->mVelY * this->mVelY);
		if(speed > STRETCH_MIN_SPEED)
		{
			const float stretch = std::min(speed * this->mParams.stretchGain,
				this->mParams.maxStretch);
			if(stretch > SOFTBODY_EPSILON)
			{
				const float sa = 1.0f + stretch;		// elongate along motion
				const float sp = 1.0f / sa;				// squeeze across it
				const float ax = this->mVelX / speed, ay = this->mVelY / speed;
				tm00 = sp + (sa - sp) * ax * ax;
				tm01 = (sa - sp) * ax * ay;
				tm10 = (sa - sp) * ay * ax;
				tm11 = sp + (sa - sp) * ay * ay;
			}
		}
		// M = stretch * squash
		const float m00 = tm00 * sm00 + tm01 * sm10;
		const float m01 = tm00 * sm01 + tm01 * sm11;
		const float m10 = tm10 * sm00 + tm11 * sm10;
		const float m11 = tm10 * sm01 + tm11 * sm11;

		// morph blend factor (eased); the target's control positions lerp in
		const float phase = smoothstep01(this->mMorphPhase);
		const bool morphing = this->mMorphTarget >= 0 && phase > 0.0f &&
			this->mMorphTarget < static_cast<int>(this->mMorphTargets.size());
		std::vector<Point> const * targetControl = morphing
			? &this->mMorphTargets[this->mMorphTarget].control : nullptr;

		for(int c = 0; c < controlCount; ++c)
		{
			// morphed rest position (control lerp) + the wobble spring offset
			Point rest = this->mRestControl[c];
			if(morphing)
			{
				Point const & tgt = (*targetControl)[c];
				rest.x += phase * (tgt.x - rest.x);
				rest.y += phase * (tgt.y - rest.y);
			}
			const float cx = rest.x + this->mSpringX[c].value();
			const float cy = rest.y + this->mSpringY[c].value();
			// apply the affine about the pivot
			const float rx = cx - this->mPivot.x;
			const float ry = cy - this->mPivot.y;
			const float curX = this->mPivot.x + m00 * rx + m01 * ry;
			const float curY = this->mPivot.y + m10 * rx + m11 * ry;
			// skinning consumes the displacement from the ORIGINAL rest control
			this->mControlDelta[c] = Point(curX - this->mRestControl[c].x,
				curY - this->mRestControl[c].y);
		}
	}
	//---------------------------------------------------------
	void SoftBodyDeform::writePositions(std::vector<Point> & out) const
	{
		if(!this->mBuilt)
		{
			return;
		}
		out.resize(this->mRestMesh.size());
		for(std::size_t v = 0; v < this->mRestMesh.size(); ++v)
		{
			float dx = 0.0f, dy = 0.0f;
			const std::size_t base = v * this->mK;
			for(int k = 0; k < this->mK; ++k)
			{
				Influence const & influence = this->mInfluences[base + k];
				Point const & delta = this->mControlDelta[influence.control];
				dx += influence.weight * delta.x;
				dy += influence.weight * delta.y;
			}
			out[v] = Point(this->mRestMesh[v].x + dx, this->mRestMesh[v].y + dy);
		}
	}
	//---------------------------------------------------------
	bool SoftBodyDeform::isAtRest() const
	{
		if(!this->mBuilt)
		{
			return true;
		}
		if(this->mMorphPlaying || this->mMorphPhase > 0.0f)
		{
			return false;
		}
		if(std::sqrt(this->mVelX * this->mVelX + this->mVelY * this->mVelY) >
			STRETCH_MIN_SPEED)
		{
			return false;
		}
		if(!this->mSquashSpring.atRest(1.0e-4f))
		{
			return false;
		}
		for(std::size_t c = 0; c < this->mSpringX.size(); ++c)
		{
			if(!this->mSpringX[c].atRest(1.0e-4f) ||
				!this->mSpringY[c].atRest(1.0e-4f))
			{
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	float SoftBodyDeform::maxControlDisplacement() const
	{
		float maximum = 0.0f;
		for(Point const & delta : this->mControlDelta)
		{
			const float magnitude = std::sqrt(delta.x * delta.x +
				delta.y * delta.y);
			maximum = std::max(maximum, magnitude);
		}
		return maximum;
	}
	//---------------------------------------------------------
	void SoftBodyDeform::resetDynamics()
	{
		for(std::size_t c = 0; c < this->mSpringX.size(); ++c)
		{
			this->mSpringX[c].reset();
			this->mSpringY[c].reset();
		}
		this->mSquashSpring.reset();
		this->mSquashAxis = Point(0.0f, 1.0f);
		this->mVelX = 0.0f;
		this->mVelY = 0.0f;
		this->mMorphTarget = -1;
		this->mMorphPhase = 0.0f;
		this->mMorphPlaying = false;
		this->mMorphDir = 1;
		this->mSquashCurrent = 0.0f;
		std::fill(this->mControlDelta.begin(), this->mControlDelta.end(),
			Point(0.0f, 0.0f));
	}
}
