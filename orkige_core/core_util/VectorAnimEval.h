/********************************************************************
	created:	Saturday 2026/07/12 at 10:00
	filename: 	VectorAnimEval.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorAnimEval_h__12_7_2026__10_00_00__
#define __VectorAnimEval_h__12_7_2026__10_00_00__

//! @file VectorAnimEval.h
//! @brief the pure pose evaluator for `.oanim` vector animation rigs:
//! keyframe interpolation, parent-chain composition, clip playback and
//! pose-level blending
//! @remarks Pure, headless, allocation-free per tick (orkige_core): every
//! buffer is sized once at build - the SoftBodyDeform architecture - so the
//! unit suite pins the math without a renderer and the steady-state tick
//! costs no allocations.
//!
//! EVALUATION MODEL: a clip maps a time in seconds onto the document's frame
//! timeline (looping clips wrap, `once` clips clamp). At a frame, every
//! layer's transform channels and every shape block's region keys are sampled
//! (linear / hold / cubic value-bezier easing between neighbouring keys; path
//! keys share a fixed vertex count, so they lerp point-for-point - the morph
//! discipline) into a Pose: per-layer LOCAL transforms + opacity, plus every
//! shape's region in its layer's own space.
//!
//! BLENDING happens at the pose level, BEFORE parent composition: local
//! transforms lerp component-wise (2D rotation is a scalar, so the lerp is
//! singularity-free), path vertices lerp point-wise, colours and opacities
//! lerp. Composing the parent chain AFTER the lerp is what makes a blend read
//! as one rig moving between poses rather than two drawings dissolving.
//!
//! COMPOSITION: each layer's local transform is
//!     local(v) = pos + R(rot) * (scale * (v - anchor))
//! and world = parentWorld composed with local (parents always precede
//! children in the layer list, so one forward pass suffices). Layer opacity
//! multiplies down the chain and into the owning shape's fill alpha. The
//! output is a VectorTessellator::Region list in paint order with CONSTANT
//! topology across frames - a consumer tessellates once and then only moves
//! vertices.
//!
//! CROSSFADE: crossFadeTo() keeps the outgoing clip ADVANCING while the
//! incoming clip ramps in (weight 0 -> 1, smoothstepped), so a locomotion
//! blend stays mid-stride alive; when the ramp completes the outgoing clip is
//! dropped and evaluation is single-clip again.

#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorTessellator.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the `.oanim` pose evaluator (@see VectorAnimEval.h)
	class VectorAnimEval
	{
		//--- Types -------------------------------------------------
	public:
		using Point = VectorTessellator::Point;
		using Colour = VectorTessellator::Colour;
		using Region = VectorTessellator::Region;

		//! the evaluated LOCAL transform + opacity of one layer, before any
		//! parent composition (the blendable representation)
		struct LayerPose
		{
			float	posX;		//!< local position
			float	posY;
			float	anchorX;	//!< transform origin in layer space
			float	anchorY;
			float	scaleX;		//!< local scale (1 = identity)
			float	scaleY;
			float	rotation;	//!< local rotation, degrees CCW (+y up)
			float	opacity;	//!< local opacity 0..1
			LayerPose()
				: posX(0.0f), posY(0.0f), anchorX(0.0f), anchorY(0.0f),
				scaleX(1.0f), scaleY(1.0f), rotation(0.0f), opacity(1.0f) {}
		};
		//! a full evaluated pose in LOCAL terms: per-layer transforms plus
		//! every shape's region in its layer's space, in paint order (layer
		//! order, shape blocks in file order). Fixed-size once built.
		struct Pose
		{
			std::vector<LayerPose>	layers;	//!< one per document layer
			std::vector<Region>		shapes;	//!< one per shape block, paint order
		};
		//--- Variables ---------------------------------------------
	protected:
		//! a 2D affine: x' = a*x + b*y + tx, y' = c*x + d*y + ty
		struct Affine
		{
			float	a, b, c, d, tx, ty;
			Affine() : a(1.0f), b(0.0f), c(0.0f), d(1.0f),
				tx(0.0f), ty(0.0f) {}
		};

		VectorAnimAsset::Document	mDoc;	//!< the rig being evaluated
		bool				mBuilt;			//!< a document has been built
		std::vector<int>	mShapeLayer;	//!< pose shape slot -> layer index
		std::vector<int>	mShapeIndex;	//!< pose shape slot -> index in its layer

		int					mClip;			//!< playing clip (outgoing during a fade)
		float				mCursor;		//!< seconds into mClip
		int					mFadeClip;		//!< incoming clip during a crossfade (-1 = none)
		float				mFadeCursor;	//!< seconds into the incoming clip
		float				mFadeDuration;	//!< crossfade length in seconds
		float				mFadeElapsed;	//!< crossfade time so far

		Pose				mPose;			//!< current output pose (blended during fades)
		Pose				mPoseA;			//!< outgoing-clip scratch pose
		Pose				mPoseB;			//!< incoming-clip scratch pose
		std::vector<Affine>	mWorld;			//!< per-layer world transforms (compose scratch)
		std::vector<float>	mWorldOpacity;	//!< per-layer composed opacity (compose scratch)
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief an unbuilt evaluator (no rig)
		VectorAnimEval();

		//! @brief take a parsed document and preallocate every pose buffer to
		//! its fixed topology; the current pose becomes clip 0 at time 0. A
		//! structurally invalid document (no layers/clips, a forward parent, a
		//! shape without keys, fps <= 0) is refused and leaves the evaluator
		//! unbuilt. Safe to call again with a new document.
		bool build(VectorAnimAsset::Document const & document);
		//! has a rig been built
		inline bool isBuilt() const { return this->mBuilt; }
		//! the built document (empty before build)
		inline VectorAnimAsset::Document const & document() const
		{
			return this->mDoc;
		}
		//! layer count of the built rig
		inline std::size_t layerCount() const { return this->mDoc.layers.size(); }
		//! shape-block count of the built rig (pose slots, paint order)
		inline std::size_t shapeCount() const { return this->mShapeLayer.size(); }
		//! index of the named clip, or -1 (@see VectorAnimAsset::Document)
		inline int findClip(String const & name) const
		{
			return this->mDoc.findClip(name);
		}

		//--- stateless evaluation ---
		//! @brief evaluate one clip at a time (seconds since clip start) into
		//! out - STATELESS: playback is untouched (the preview/scrub/thumbnail
		//! entry). Looping clips wrap the time, `once` clips clamp it. out is
		//! resized to the rig's fixed topology (no reallocation once warm).
		//! @return false before build or on a bad clip index (out untouched)
		bool evaluateAt(int clipIndex, float timeSeconds, Pose & out) const;
		//! @brief pose-level lerp: out = a + weight * (b - a), weight clamped
		//! to 0..1. Local transforms lerp component-wise (rotation as a
		//! scalar), path vertices point-wise, fills and opacities per channel.
		//! @return false on a structure mismatch (different layer/shape/vertex
		//! counts - poses of different rigs never blend); out is only written
		//! on success
		static bool blendPose(Pose const & a, Pose const & b, float weight,
			Pose & out);
		//! @brief compose a pose's parent chain and write the world-space
		//! region list (paint order, layer opacity folded into fill alpha) -
		//! the VectorTessellator-ready output a consumer tessellates once and
		//! streams vertex updates from. out is resized to the fixed topology.
		//! No-op before build.
		void composeRegions(Pose const & pose, std::vector<Region> & out);

		//--- playback ---
		//! @brief jump to a clip at time 0 (any running crossfade is dropped)
		//! and evaluate its first pose. An invalid index is ignored.
		void setClip(int clipIndex);
		//! @brief start a crossfade to a clip: the current clip keeps playing
		//! while the new one ramps in over seconds (smoothstepped 0 -> 1);
		//! when the ramp completes the outgoing clip is dropped. seconds <= 0
		//! is an immediate setClip. Starting a fade during a fade promotes the
		//! current incoming clip to outgoing (the blended history is not
		//! preserved). An invalid index is ignored.
		void crossFadeTo(int clipIndex, float seconds);
		//! @brief advance playback by deltaTime (both clips during a fade) and
		//! re-evaluate the current pose. Allocation-free. No-op before build
		//! or for deltaTime <= 0.
		void update(float deltaTime);
		//! @brief compose the CURRENT pose into a world-space region list
		//! (@see composeRegions). No-op before build.
		void writeRegions(std::vector<Region> & out);
		//! the current local pose (blended during a fade) - introspection
		inline Pose const & pose() const { return this->mPose; }

		//! the clip playback is heading into (the incoming clip during a fade)
		int currentClip() const;
		//! @brief the current absolute timeline frame of currentClip()
		float currentFrame() const;
		//! @brief has a `once` clip reached its end (loops never end)
		bool isAtEnd() const;
		//! is a crossfade in progress
		inline bool isCrossFading() const { return this->mFadeClip >= 0; }
		//! current crossfade weight (0 = outgoing, 1 = incoming; 1 when idle)
		float crossFadeWeight() const;

		//--- helpers (static, shared with tests) ---
		//! @brief evaluate a key's easing at segment fraction u (0..1): the
		//! eased value fraction. Hold returns 0, linear returns u, bezier
		//! solves the cubic value curve (handle x components clamped to 0..1
		//! so time stays monotone; y is free for overshoot).
		static float evalEase(VectorAnimAsset::Ease const & ease, float u);
	protected:
		//! @brief map a clip-relative time (seconds) onto an absolute timeline
		//! frame, wrapping (loop) or clamping (once) into the clip window
		float frameAt(int clipIndex, float timeSeconds) const;
		//! @brief wrap/clamp a clip cursor (seconds) into the clip's period so
		//! it stays bounded over long playback
		float boundCursor(int clipIndex, float cursorSeconds) const;
		//! @brief size a pose to the rig's fixed topology (layers, shape slots,
		//! per-region vertex counts) - idempotent, reallocation-free once warm
		void resizePose(Pose & pose) const;
	private:
	};
}

#endif //__VectorAnimEval_h__12_7_2026__10_00_00__
