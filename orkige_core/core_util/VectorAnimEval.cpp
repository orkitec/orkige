/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	VectorAnimEval.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file VectorAnimEval.cpp
//! @brief the `.oanim` pose evaluator implementation (@see VectorAnimEval.h)

#include "core_util/VectorAnimEval.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! degrees -> radians
		const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
		//! bezier time-solve tolerance (on the x/time axis)
		const float EASE_SOLVE_EPSILON = 1.0e-5f;

		//! clamp to the unit interval
		float clamp01(float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}
		//! smoothstep ease for the crossfade weight ramp (flat tangents)
		float smoothstep01(float t)
		{
			t = clamp01(t);
			return t * t * (3.0f - 2.0f * t);
		}
		float lerp(float a, float b, float t)
		{
			return a + t * (b - a);
		}

		//! @brief sample a transform channel at a frame: the segment around
		//! the frame is found and eased; before the first / after the last key
		//! the boundary value holds. Absent channels keep the defaults in out.
		void sampleChannel(VectorAnimAsset::Channel const & channel,
			float frame, int dim, float * out)
		{
			std::vector<VectorAnimAsset::Key> const & keys = channel.keys;
			if(keys.empty())
			{
				return;	// the channel default (already in out) applies
			}
			if(frame <= keys.front().frame)
			{
				for(int d = 0; d < dim; ++d)
				{
					out[d] = keys.front().value[d];
				}
				return;
			}
			std::size_t i = 0;
			while(i + 1 < keys.size() && keys[i + 1].frame <= frame)
			{
				++i;
			}
			if(i + 1 >= keys.size())
			{
				for(int d = 0; d < dim; ++d)
				{
					out[d] = keys.back().value[d];
				}
				return;
			}
			const float span = keys[i + 1].frame - keys[i].frame;
			const float u = span > 0.0f ? (frame - keys[i].frame) / span : 0.0f;
			const float eased = VectorAnimEval::evalEase(keys[i].ease, u);
			for(int d = 0; d < dim; ++d)
			{
				out[d] = lerp(keys[i].value[d], keys[i + 1].value[d], eased);
			}
		}

		//! @brief write src's region into dst element-wise (dst is resized to
		//! src's topology; steady-state this never reallocates)
		void copyRegion(VectorTessellator::Region const & src,
			VectorTessellator::Region & dst)
		{
			dst.fill = src.fill;
			dst.paintType = src.paintType;
			dst.gradientStart = src.gradientStart;
			dst.gradientEnd = src.gradientEnd;
			dst.gradientFocal = src.gradientFocal;
			dst.gradientStops = src.gradientStops;
			dst.kind = src.kind;
			dst.strokeWidth = src.strokeWidth;
			dst.strokeCap = src.strokeCap;
			dst.strokeJoin = src.strokeJoin;
			dst.strokeMiterLimit = src.strokeMiterLimit;
			dst.strokeClosed = src.strokeClosed;
			dst.outer.resize(src.outer.size());
			std::copy(src.outer.begin(), src.outer.end(), dst.outer.begin());
			dst.mask.resize(src.mask.size());
			std::copy(src.mask.begin(), src.mask.end(), dst.mask.begin());
			dst.holes.resize(src.holes.size());
			for(std::size_t h = 0; h < src.holes.size(); ++h)
			{
				dst.holes[h].resize(src.holes[h].size());
				std::copy(src.holes[h].begin(), src.holes[h].end(),
					dst.holes[h].begin());
			}
		}
		//! @brief dst = a + t * (b - a), point-wise + fill (same topology - the
		//! parser guarantees it across one shape block's keys)
		void lerpRegion(VectorTessellator::Region const & a,
			VectorTessellator::Region const & b, float t,
			VectorTessellator::Region & dst)
		{
			dst.fill = VectorTessellator::Colour(
				lerp(a.fill.r, b.fill.r, t), lerp(a.fill.g, b.fill.g, t),
				lerp(a.fill.b, b.fill.b, t), lerp(a.fill.a, b.fill.a, t));
			dst.paintType = a.paintType;
			dst.gradientStart = VectorTessellator::Point(
				lerp(a.gradientStart.x, b.gradientStart.x, t),
				lerp(a.gradientStart.y, b.gradientStart.y, t));
			dst.gradientEnd = VectorTessellator::Point(
				lerp(a.gradientEnd.x, b.gradientEnd.x, t),
				lerp(a.gradientEnd.y, b.gradientEnd.y, t));
			dst.gradientFocal = VectorTessellator::Point(
				lerp(a.gradientFocal.x, b.gradientFocal.x, t),
				lerp(a.gradientFocal.y, b.gradientFocal.y, t));
			dst.gradientStops.resize(a.gradientStops.size());
			for(std::size_t i = 0; i < a.gradientStops.size(); ++i)
			{
				dst.gradientStops[i].offset = lerp(a.gradientStops[i].offset,
					b.gradientStops[i].offset, t);
				dst.gradientStops[i].colour = VectorTessellator::Colour(
					lerp(a.gradientStops[i].colour.r, b.gradientStops[i].colour.r, t),
					lerp(a.gradientStops[i].colour.g, b.gradientStops[i].colour.g, t),
					lerp(a.gradientStops[i].colour.b, b.gradientStops[i].colour.b, t),
					lerp(a.gradientStops[i].colour.a, b.gradientStops[i].colour.a, t));
			}
			// stroke style is fixed across a block's keys (the parser enforces
			// it); only the centreline, the width and the paint animate
			dst.kind = a.kind;
			dst.strokeCap = a.strokeCap;
			dst.strokeJoin = a.strokeJoin;
			dst.strokeClosed = a.strokeClosed;
			dst.strokeWidth = lerp(a.strokeWidth, b.strokeWidth, t);
			dst.strokeMiterLimit = lerp(a.strokeMiterLimit, b.strokeMiterLimit,
				t);
			dst.outer.resize(a.outer.size());
			for(std::size_t v = 0; v < a.outer.size(); ++v)
			{
				dst.outer[v] = VectorTessellator::Point(
					lerp(a.outer[v].x, b.outer[v].x, t),
					lerp(a.outer[v].y, b.outer[v].y, t));
			}
			dst.mask.resize(a.mask.size());
			for(std::size_t v = 0; v < a.mask.size(); ++v)
			{
				dst.mask[v] = VectorTessellator::Point(
					lerp(a.mask[v].x, b.mask[v].x, t),
					lerp(a.mask[v].y, b.mask[v].y, t));
			}
			dst.holes.resize(a.holes.size());
			for(std::size_t h = 0; h < a.holes.size(); ++h)
			{
				dst.holes[h].resize(a.holes[h].size());
				for(std::size_t v = 0; v < a.holes[h].size(); ++v)
				{
					dst.holes[h][v] = VectorTessellator::Point(
						lerp(a.holes[h][v].x, b.holes[h][v].x, t),
						lerp(a.holes[h][v].y, b.holes[h][v].y, t));
				}
			}
		}
		//! @brief sample a shape block's region keys at a frame into dst
		void sampleShape(VectorAnimAsset::Shape const & shape, float frame,
			VectorTessellator::Region & dst)
		{
			std::vector<VectorAnimAsset::ShapeKey> const & keys = shape.keys;
			if(keys.empty())
			{
				return;	// unreachable for parsed documents (>= 1 key enforced)
			}
			if(keys.size() == 1 || frame <= keys.front().frame)
			{
				copyRegion(keys.front().region, dst);
				return;
			}
			std::size_t i = 0;
			while(i + 1 < keys.size() && keys[i + 1].frame <= frame)
			{
				++i;
			}
			if(i + 1 >= keys.size())
			{
				copyRegion(keys.back().region, dst);
				return;
			}
			const float span = keys[i + 1].frame - keys[i].frame;
			const float u = span > 0.0f ? (frame - keys[i].frame) / span : 0.0f;
			const float eased = VectorAnimEval::evalEase(keys[i].ease, u);
			lerpRegion(keys[i].region, keys[i + 1].region, eased, dst);
		}

		//! @brief do two regions share topology (blendability check)
		bool sameRegionTopology(VectorTessellator::Region const & a,
			VectorTessellator::Region const & b)
		{
			if(a.outer.size() != b.outer.size() ||
				a.holes.size() != b.holes.size() ||
				a.paintType != b.paintType ||
				a.gradientStops.size() != b.gradientStops.size() ||
				a.kind != b.kind || a.mask.size() != b.mask.size() ||
				a.strokeCap != b.strokeCap || a.strokeJoin != b.strokeJoin ||
				a.strokeClosed != b.strokeClosed)
			{
				return false;
			}
			for(std::size_t h = 0; h < a.holes.size(); ++h)
			{
				if(a.holes[h].size() != b.holes[h].size())
				{
					return false;
				}
			}
			return true;
		}
	}
	//---------------------------------------------------------
	VectorAnimEval::VectorAnimEval()
		: mBuilt(false), mClip(0), mCursor(0.0f), mFadeClip(-1),
		mFadeCursor(0.0f), mFadeDuration(0.0f), mFadeElapsed(0.0f)
	{
	}
	//---------------------------------------------------------
	bool VectorAnimEval::build(VectorAnimAsset::Document const & document)
	{
		this->mBuilt = false;
		this->mDoc.clear();
		this->mShapeLayer.clear();
		this->mShapeIndex.clear();
		this->mClip = 0;
		this->mCursor = 0.0f;
		this->mFadeClip = -1;

		// refuse a structurally unsound document honestly (a parsed one is
		// always sound; this guards hand-built documents)
		if(document.fps <= 0.0f || document.duration <= 0.0f ||
			document.clips.empty() || document.layers.empty())
		{
			return false;
		}
		for(std::size_t i = 0; i < document.layers.size(); ++i)
		{
			VectorAnimAsset::Layer const & layer = document.layers[i];
			if(layer.parent < -1 || layer.parent >= static_cast<int>(i))
			{
				return false;	// parents must precede their children
			}
			for(VectorAnimAsset::Shape const & shape : layer.shapes)
			{
				if(shape.keys.empty())
				{
					return false;
				}
				for(VectorAnimAsset::ShapeKey const & key : shape.keys)
				{
					if(!sameRegionTopology(shape.keys.front().region,
						key.region))
					{
						return false;	// the fixed-topology law
					}
				}
			}
		}

		this->mDoc = document;
		// flatten the shape blocks into pose slots (paint order)
		for(std::size_t i = 0; i < this->mDoc.layers.size(); ++i)
		{
			for(std::size_t s = 0; s < this->mDoc.layers[i].shapes.size(); ++s)
			{
				this->mShapeLayer.push_back(static_cast<int>(i));
				this->mShapeIndex.push_back(static_cast<int>(s));
			}
		}
		// preallocate every pose/scratch buffer ONCE - the per-tick path never
		// grows them again (the SoftBodyDeform allocation contract)
		this->resizePose(this->mPose);
		this->resizePose(this->mPoseA);
		this->resizePose(this->mPoseB);
		this->mWorld.assign(this->mDoc.layers.size(), Affine());
		this->mWorldOpacity.assign(this->mDoc.layers.size(), 1.0f);

		this->mBuilt = true;
		this->evaluateAt(this->mClip, 0.0f, this->mPose);
		return true;
	}
	//---------------------------------------------------------
	void VectorAnimEval::resizePose(Pose & pose) const
	{
		VectorAnimAsset::Document const & doc = this->mDoc;
		pose.layers.resize(doc.layers.size());
		pose.shapes.resize(this->mShapeLayer.size());
		for(std::size_t s = 0; s < this->mShapeLayer.size(); ++s)
		{
			VectorTessellator::Region const & reference =
				doc.layers[this->mShapeLayer[s]]
					.shapes[this->mShapeIndex[s]].keys.front().region;
			Region & region = pose.shapes[s];
			region.kind = reference.kind;
			region.strokeCap = reference.strokeCap;
			region.strokeJoin = reference.strokeJoin;
			region.strokeClosed = reference.strokeClosed;
			region.strokeWidth = reference.strokeWidth;
			region.strokeMiterLimit = reference.strokeMiterLimit;
			region.outer.resize(reference.outer.size());
			region.mask.resize(reference.mask.size());
			region.holes.resize(reference.holes.size());
			for(std::size_t h = 0; h < reference.holes.size(); ++h)
			{
				region.holes[h].resize(reference.holes[h].size());
			}
		}
	}
	//---------------------------------------------------------
	float VectorAnimEval::evalEase(VectorAnimAsset::Ease const & ease, float u)
	{
		u = clamp01(u);
		if(ease.mode == VectorAnimAsset::EASE_HOLD)
		{
			return 0.0f;	// stay at this key until the next one
		}
		if(ease.mode == VectorAnimAsset::EASE_LINEAR)
		{
			return u;
		}
		// cubic value bezier through (0,0) (x1,y1) (x2,y2) (1,1): solve the
		// monotone x(t) = u for t (Newton with a bisection fallback), then
		// return y(t). x handles are clamped so time stays monotone; y is
		// free (overshoot/anticipation).
		const float x1 = clamp01(ease.outX);
		const float y1 = ease.outY;
		const float x2 = clamp01(ease.inX);
		const float y2 = ease.inY;
		const float cx = 3.0f * x1;
		const float bx = 3.0f * (x2 - x1) - cx;
		const float ax = 1.0f - cx - bx;
		const float cy = 3.0f * y1;
		const float by = 3.0f * (y2 - y1) - cy;
		const float ay = 1.0f - cy - by;
		auto sampleX = [&](float t) { return ((ax * t + bx) * t + cx) * t; };

		float t = u;	// a good initial guess (x is near-diagonal)
		for(int i = 0; i < 8; ++i)
		{
			const float error = sampleX(t) - u;
			if(std::fabs(error) < EASE_SOLVE_EPSILON)
			{
				break;
			}
			const float slope = (3.0f * ax * t + 2.0f * bx) * t + cx;
			if(std::fabs(slope) < EASE_SOLVE_EPSILON)
			{
				break;	// flat spot: fall through to bisection
			}
			t = clamp01(t - error / slope);
		}
		if(std::fabs(sampleX(t) - u) >= EASE_SOLVE_EPSILON)
		{
			float lo = 0.0f, hi = 1.0f;
			for(int i = 0; i < 32; ++i)
			{
				t = 0.5f * (lo + hi);
				if(sampleX(t) < u) { lo = t; } else { hi = t; }
			}
		}
		return ((ay * t + by) * t + cy) * t;
	}
	//---------------------------------------------------------
	float VectorAnimEval::frameAt(int clipIndex, float timeSeconds) const
	{
		VectorAnimAsset::Clip const & clip = this->mDoc.clips[clipIndex];
		const float length = clip.end - clip.start;	// frames
		if(length <= 0.0f)
		{
			return clip.start;
		}
		float frame = timeSeconds * this->mDoc.fps;
		if(clip.loop)
		{
			frame = std::fmod(frame, length);
			if(frame < 0.0f)
			{
				frame += length;
			}
		}
		else
		{
			frame = std::clamp(frame, 0.0f, length);
		}
		return clip.start + frame;
	}
	//---------------------------------------------------------
	float VectorAnimEval::boundCursor(int clipIndex, float cursorSeconds) const
	{
		VectorAnimAsset::Clip const & clip = this->mDoc.clips[clipIndex];
		const float period = (clip.end - clip.start) / this->mDoc.fps;
		if(period <= 0.0f)
		{
			return 0.0f;
		}
		if(clip.loop)
		{
			float bounded = std::fmod(cursorSeconds, period);
			if(bounded < 0.0f)
			{
				bounded += period;
			}
			return bounded;
		}
		return std::clamp(cursorSeconds, 0.0f, period);
	}
	//---------------------------------------------------------
	bool VectorAnimEval::evaluateAt(int clipIndex, float timeSeconds,
		Pose & out) const
	{
		if(!this->mBuilt || clipIndex < 0 ||
			clipIndex >= static_cast<int>(this->mDoc.clips.size()))
		{
			return false;
		}
		const float frame = this->frameAt(clipIndex, timeSeconds);
		this->resizePose(out);
		for(std::size_t i = 0; i < this->mDoc.layers.size(); ++i)
		{
			VectorAnimAsset::Layer const & layer = this->mDoc.layers[i];
			LayerPose local;	// the channel defaults
			float vec2[2];
			vec2[0] = local.posX; vec2[1] = local.posY;
			sampleChannel(layer.pos, frame, 2, vec2);
			local.posX = vec2[0]; local.posY = vec2[1];
			vec2[0] = local.anchorX; vec2[1] = local.anchorY;
			sampleChannel(layer.anchor, frame, 2, vec2);
			local.anchorX = vec2[0]; local.anchorY = vec2[1];
			vec2[0] = local.scaleX; vec2[1] = local.scaleY;
			sampleChannel(layer.scale, frame, 2, vec2);
			local.scaleX = vec2[0]; local.scaleY = vec2[1];
			sampleChannel(layer.rot, frame, 1, &local.rotation);
			sampleChannel(layer.opacity, frame, 1, &local.opacity);
			out.layers[i] = local;
		}
		for(std::size_t s = 0; s < this->mShapeLayer.size(); ++s)
		{
			sampleShape(this->mDoc.layers[this->mShapeLayer[s]]
				.shapes[this->mShapeIndex[s]], frame, out.shapes[s]);
		}
		return true;
	}
	//---------------------------------------------------------
	bool VectorAnimEval::blendPose(Pose const & a, Pose const & b,
		float weight, Pose & out)
	{
		// poses must share the rig structure - different rigs never blend
		if(a.layers.size() != b.layers.size() ||
			a.shapes.size() != b.shapes.size())
		{
			return false;
		}
		for(std::size_t s = 0; s < a.shapes.size(); ++s)
		{
			if(!sameRegionTopology(a.shapes[s], b.shapes[s]))
			{
				return false;
			}
		}
		const float w = clamp01(weight);
		out.layers.resize(a.layers.size());
		for(std::size_t i = 0; i < a.layers.size(); ++i)
		{
			LayerPose const & pa = a.layers[i];
			LayerPose const & pb = b.layers[i];
			LayerPose & po = out.layers[i];
			po.posX = lerp(pa.posX, pb.posX, w);
			po.posY = lerp(pa.posY, pb.posY, w);
			po.anchorX = lerp(pa.anchorX, pb.anchorX, w);
			po.anchorY = lerp(pa.anchorY, pb.anchorY, w);
			po.scaleX = lerp(pa.scaleX, pb.scaleX, w);
			po.scaleY = lerp(pa.scaleY, pb.scaleY, w);
			po.rotation = lerp(pa.rotation, pb.rotation, w);
			po.opacity = lerp(pa.opacity, pb.opacity, w);
		}
		out.shapes.resize(a.shapes.size());
		for(std::size_t s = 0; s < a.shapes.size(); ++s)
		{
			lerpRegion(a.shapes[s], b.shapes[s], w, out.shapes[s]);
		}
		return true;
	}
	//---------------------------------------------------------
	void VectorAnimEval::composeRegions(Pose const & pose,
		std::vector<Region> & out)
	{
		if(!this->mBuilt || pose.layers.size() != this->mDoc.layers.size() ||
			pose.shapes.size() != this->mShapeLayer.size())
		{
			return;
		}
		// one forward pass: parents precede children by construction
		for(std::size_t i = 0; i < pose.layers.size(); ++i)
		{
			LayerPose const & lp = pose.layers[i];
			const float radians = lp.rotation * DEG_TO_RAD;
			const float c = std::cos(radians);
			const float s = std::sin(radians);
			// local(v) = pos + R * (S * (v - anchor))
			Affine local;
			local.a = c * lp.scaleX;
			local.b = -s * lp.scaleY;
			local.c = s * lp.scaleX;
			local.d = c * lp.scaleY;
			local.tx = lp.posX - (local.a * lp.anchorX + local.b * lp.anchorY);
			local.ty = lp.posY - (local.c * lp.anchorX + local.d * lp.anchorY);

			const int parent = this->mDoc.layers[i].parent;
			if(parent >= 0)
			{
				Affine const & p = this->mWorld[parent];
				Affine world;
				world.a = p.a * local.a + p.b * local.c;
				world.b = p.a * local.b + p.b * local.d;
				world.c = p.c * local.a + p.d * local.c;
				world.d = p.c * local.b + p.d * local.d;
				world.tx = p.a * local.tx + p.b * local.ty + p.tx;
				world.ty = p.c * local.tx + p.d * local.ty + p.ty;
				this->mWorld[i] = world;
				this->mWorldOpacity[i] =
					this->mWorldOpacity[parent] * clamp01(lp.opacity);
			}
			else
			{
				this->mWorld[i] = local;
				this->mWorldOpacity[i] = clamp01(lp.opacity);
			}
		}
		// every shape slot ALWAYS emits its region (constant region count and
		// vertex counts across frames - the tessellate-once contract)
		out.resize(pose.shapes.size());
		for(std::size_t s = 0; s < pose.shapes.size(); ++s)
		{
			const int layer = this->mShapeLayer[s];
			Affine const & world = this->mWorld[layer];
			Region const & src = pose.shapes[s];
			Region & dst = out[s];
			dst.fill = src.fill;
			dst.fill.a *= this->mWorldOpacity[layer];
			dst.paintType = src.paintType;
			dst.gradientStops = src.gradientStops;
			for(VectorTessellator::GradientStop & stop : dst.gradientStops)
			{
				stop.colour.a *= this->mWorldOpacity[layer];
			}
			auto transformPoint = [&world](Point const & point)
			{
				return Point(world.a * point.x + world.b * point.y + world.tx,
					world.c * point.x + world.d * point.y + world.ty);
			};
			dst.gradientStart = transformPoint(src.gradientStart);
			dst.gradientEnd = transformPoint(src.gradientEnd);
			dst.gradientFocal = transformPoint(src.gradientFocal);
			// a stroke's WIDTH rides the layer chain like its geometry: the
			// area scale factor of the world affine (sqrt of |determinant|) is
			// the one honest scalar for a width under a possibly non-uniform
			// scale, and it degrades to the plain factor when the scale is
			// uniform (the usual case)
			dst.kind = src.kind;
			dst.strokeCap = src.strokeCap;
			dst.strokeJoin = src.strokeJoin;
			dst.strokeClosed = src.strokeClosed;
			dst.strokeMiterLimit = src.strokeMiterLimit;
			const float determinant = std::fabs(world.a * world.d -
				world.b * world.c);
			dst.strokeWidth = src.strokeWidth * std::sqrt(determinant);
			dst.mask.resize(src.mask.size());
			for(std::size_t v = 0; v < src.mask.size(); ++v)
			{
				dst.mask[v] = transformPoint(src.mask[v]);
			}
			dst.outer.resize(src.outer.size());
			for(std::size_t v = 0; v < src.outer.size(); ++v)
			{
				Point const & point = src.outer[v];
				dst.outer[v] = Point(
					world.a * point.x + world.b * point.y + world.tx,
					world.c * point.x + world.d * point.y + world.ty);
			}
			dst.holes.resize(src.holes.size());
			for(std::size_t h = 0; h < src.holes.size(); ++h)
			{
				dst.holes[h].resize(src.holes[h].size());
				for(std::size_t v = 0; v < src.holes[h].size(); ++v)
				{
					Point const & point = src.holes[h][v];
					dst.holes[h][v] = Point(
						world.a * point.x + world.b * point.y + world.tx,
						world.c * point.x + world.d * point.y + world.ty);
				}
			}
		}
	}
	//---------------------------------------------------------
	void VectorAnimEval::setClip(int clipIndex)
	{
		if(!this->mBuilt || clipIndex < 0 ||
			clipIndex >= static_cast<int>(this->mDoc.clips.size()))
		{
			return;
		}
		this->mClip = clipIndex;
		this->mCursor = 0.0f;
		this->mFadeClip = -1;
		this->evaluateAt(this->mClip, 0.0f, this->mPose);
	}
	//---------------------------------------------------------
	void VectorAnimEval::crossFadeTo(int clipIndex, float seconds)
	{
		if(!this->mBuilt || clipIndex < 0 ||
			clipIndex >= static_cast<int>(this->mDoc.clips.size()))
		{
			return;
		}
		if(seconds <= 0.0f)
		{
			this->setClip(clipIndex);
			return;
		}
		if(this->mFadeClip >= 0)
		{
			// fade during a fade: the incoming clip becomes the new outgoing
			this->mClip = this->mFadeClip;
			this->mCursor = this->mFadeCursor;
		}
		this->mFadeClip = clipIndex;
		this->mFadeCursor = 0.0f;
		this->mFadeDuration = seconds;
		this->mFadeElapsed = 0.0f;
	}
	//---------------------------------------------------------
	void VectorAnimEval::update(float deltaTime)
	{
		if(!this->mBuilt || deltaTime <= 0.0f)
		{
			return;
		}
		this->mCursor = this->boundCursor(this->mClip,
			this->mCursor + deltaTime);
		if(this->mFadeClip >= 0)
		{
			// both clips advance during the fade (a locomotion blend stays
			// mid-stride alive); the weight ramp decides the mix
			this->mFadeCursor = this->boundCursor(this->mFadeClip,
				this->mFadeCursor + deltaTime);
			this->mFadeElapsed += deltaTime;
			if(this->mFadeElapsed >= this->mFadeDuration)
			{
				this->mClip = this->mFadeClip;
				this->mCursor = this->mFadeCursor;
				this->mFadeClip = -1;
			}
		}
		if(this->mFadeClip >= 0)
		{
			this->evaluateAt(this->mClip, this->mCursor, this->mPoseA);
			this->evaluateAt(this->mFadeClip, this->mFadeCursor, this->mPoseB);
			VectorAnimEval::blendPose(this->mPoseA, this->mPoseB,
				smoothstep01(this->mFadeElapsed / this->mFadeDuration),
				this->mPose);
		}
		else
		{
			this->evaluateAt(this->mClip, this->mCursor, this->mPose);
		}
	}
	//---------------------------------------------------------
	void VectorAnimEval::writeRegions(std::vector<Region> & out)
	{
		this->composeRegions(this->mPose, out);
	}
	//---------------------------------------------------------
	int VectorAnimEval::currentClip() const
	{
		return this->mFadeClip >= 0 ? this->mFadeClip : this->mClip;
	}
	//---------------------------------------------------------
	float VectorAnimEval::currentFrame() const
	{
		if(!this->mBuilt)
		{
			return 0.0f;
		}
		return this->mFadeClip >= 0
			? this->frameAt(this->mFadeClip, this->mFadeCursor)
			: this->frameAt(this->mClip, this->mCursor);
	}
	//---------------------------------------------------------
	bool VectorAnimEval::isAtEnd() const
	{
		if(!this->mBuilt)
		{
			return false;
		}
		const int clipIndex = this->currentClip();
		VectorAnimAsset::Clip const & clip = this->mDoc.clips[clipIndex];
		if(clip.loop)
		{
			return false;
		}
		const float cursor = this->mFadeClip >= 0
			? this->mFadeCursor : this->mCursor;
		const float period = (clip.end - clip.start) / this->mDoc.fps;
		return cursor >= period;
	}
	//---------------------------------------------------------
	float VectorAnimEval::crossFadeWeight() const
	{
		if(this->mFadeClip < 0 || this->mFadeDuration <= 0.0f)
		{
			return 1.0f;
		}
		return smoothstep01(this->mFadeElapsed / this->mFadeDuration);
	}
}
