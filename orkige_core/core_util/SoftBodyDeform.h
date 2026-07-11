/********************************************************************
	created:	Friday 2026/07/11 at 09:30
	filename: 	SoftBodyDeform.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SoftBodyDeform_h__11_7_2026__09_30_00__
#define __SoftBodyDeform_h__11_7_2026__09_30_00__

//! @file SoftBodyDeform.h
//! @brief fixed-topology weight-based deformation of a tessellated vector shape
//! - the runtime math behind soft, deformable organic shapes
//! @remarks Pure, headless, allocation-free per frame (orkige_core) so the unit
//! suite pins the deformation math without a renderer.
//!
//! FIXED-TOPOLOGY ARCHITECTURE (the GPU escape hatch): the shape is tessellated
//! ONCE at its rest pose; the triangle topology never changes at runtime. A
//! small set of CONTROL POINTS is sampled along the shape's contour, and every
//! mesh vertex is skinned to its nearest control points with a smooth
//! distance-based falloff (weights that sum to 1). Per frame ONLY the control
//! points move (from wobble springs, a physics-driven squash/stretch affine and
//! morph blending) and each vertex position is recomputed as
//!     vertex = restVertex + sum_k weight_k * (control_k.current - control_k.rest)
//! i.e. translation-only linear-blend skinning. This is a pure function of the
//! control-point displacements: it is EXACTLY the formulation a future
//! vertex-shader skinning path would consume (upload the per-control deltas as
//! uniforms, the weights/indices as vertex attributes), so the deform can move
//! to the GPU with no change to the weighting. Full re-triangulation happens
//! ONLY when the topology genuinely changes (a new shape asset) - never in the
//! per-frame path, and morph targets share topology so they never trigger it.
//!
//! COMPOSITION (all three routes move the SAME control points, so they compose
//! for free):
//!   * WOBBLE  - a per-control-point spring-damper (@see WobbleSpring) jiggles
//!               the point around its morphed rest; a contact kicks the springs.
//!   * SQUASH  - a decaying spring drives a volume-preserving anisotropic scale
//!               about the shape centroid along a contact normal (compress along
//!               the impact, bulge across it).
//!   * STRETCH - the object's velocity drives a volume-preserving scale along
//!               the motion direction (squeeze on the cross axis).
//!   * MORPH   - the control points lerp between same-topology target poses;
//!               the skinning propagates the blend to every vertex.
//! The physics BODY itself stays a rigid circle - the softness is purely visual.

#include "core_util/VectorTessellator.h"
#include "core_util/WobbleSpring.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the fixed-topology soft-shape deformer (@see SoftBodyDeform.h)
	class SoftBodyDeform
	{
		//--- Types -------------------------------------------------
	public:
		using Point = VectorTessellator::Point;
		using Region = VectorTessellator::Region;

		//! @brief tunables. The spring/amount fields are LIVE (setParams applies
		//! them without a rebuild); the structural fields (control-point count,
		//! influences) only take effect on build.
		struct Params
		{
			float	wobbleStiffness;	//!< per-control spring stiffness (k)
			float	wobbleDamping;		//!< per-control spring damping (c)
			float	wobbleAmount;		//!< scales the impulse kick into the wobble springs (0 = no jiggle)
			float	squashStiffness;	//!< squash spring stiffness
			float	squashDamping;		//!< squash spring damping
			float	squashAmount;		//!< scales a unit impact into squash depth (0 = no squash)
			float	stretchGain;		//!< speed (units/s) -> stretch fraction
			float	maxStretch;			//!< clamp on the velocity stretch fraction
			int		controlPointCount;	//!< desired control points sampled along the contour (structural)
			int		influencesPerVertex;//!< control points each vertex blends (structural, K)
			//! soft-blob defaults: an underdamped jiggle that settles in ~1s
			Params()
				: wobbleStiffness(140.0f), wobbleDamping(7.0f), wobbleAmount(1.0f),
				squashStiffness(90.0f), squashDamping(6.5f), squashAmount(0.5f),
				stretchGain(0.05f), maxStretch(0.4f),
				controlPointCount(16), influencesPerVertex(4) {}
		};
		//--- Variables ---------------------------------------------
	protected:
		//! one skinning influence: which control point, and its weight
		struct Influence
		{
			int		control;	//!< control-point index
			float	weight;		//!< normalized blend weight (all of a vertex's sum to 1)
		};
		//! per-morph-target control-point poses (same count as the rest control)
		struct MorphTarget
		{
			String				name;		//!< target name (from the asset)
			std::vector<Point>	control;	//!< this pose's control-point positions
		};

		Params				mParams;		//!< current tunables
		bool				mBuilt;			//!< a rest pose has been built

		std::vector<Point>	mRestMesh;		//!< rest vertex positions (skinning base)
		std::vector<Point>	mRestControl;	//!< rest control-point positions
		Point				mPivot;			//!< centroid of the rest control (squash/stretch pivot)
		std::vector<Influence>	mInfluences;//!< flattened K influences per vertex (mInfluencesPerVertex wide)
		int					mK;				//!< influences per vertex actually stored

		std::vector<WobbleSpring>	mSpringX;	//!< per-control x-offset springs
		std::vector<WobbleSpring>	mSpringY;	//!< per-control y-offset springs
		WobbleSpring		mSquashSpring;	//!< drives the squash depth (decays to 0)
		Point				mSquashAxis;	//!< unit axis a contact squashes along
		float				mVelX;			//!< body velocity for the stretch (units/s)
		float				mVelY;

		std::vector<MorphTarget>	mMorphTargets;	//!< same-topology morph poses
		int					mMorphTarget;	//!< target being blended toward (-1 = none)
		float				mMorphPhase;	//!< blend 0 (rest) .. 1 (target)
		float				mMorphSpeed;	//!< phase units per second
		bool				mMorphLoop;		//!< ping-pong the blend
		bool				mMorphPlaying;	//!< advancing the phase
		int					mMorphDir;		//!< +1 toward target, -1 back (loop)

		std::vector<Point>	mControlDelta;	//!< current_i - rest_i, recomputed each update (skinning input)
		float				mSquashCurrent;	//!< last applied squash fraction (introspection)
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief an unbuilt deformer (no rest pose)
		SoftBodyDeform();

		//! @brief build the rest pose: sample control points along the base
		//! shape's contours and skin every rest mesh vertex to them. restMesh is
		//! the tessellated rest vertex positions (fills + feather); baseRegions is
		//! the parsed contour set the control points are sampled from. Safe to
		//! call again (a new shape) - it resets all dynamic state. A degenerate
		//! input (no regions / no mesh) leaves the deformer unbuilt.
		void build(std::vector<Region> const & baseRegions,
			std::vector<Point> const & restMesh, Params const & params);
		//! has a rest pose been built
		inline bool isBuilt() const { return this->mBuilt; }
		//! @brief apply LIVE tunables (spring stiffness/damping, amounts, stretch)
		//! WITHOUT rebuilding; the structural fields (control count, K) are ignored
		//! here (they need a build). No-op before build.
		void setParams(Params const & params);
		//! @see SoftBodyDeform::mParams
		inline Params const & getParams() const { return this->mParams; }

		//--- morph ---
		//! @brief add a morph target from a region set that MUST share the base
		//! topology (same region count, same per-contour and per-hole vertex
		//! counts). Returns false and adds nothing on a structure mismatch (the
		//! honest error the cook also guards). The first target added is index 0.
		bool addMorphTarget(String const & name,
			std::vector<Region> const & baseRegions,
			std::vector<Region> const & targetRegions);
		//! how many morph targets are registered
		inline std::size_t morphTargetCount() const
		{
			return this->mMorphTargets.size();
		}
		//! @brief start blending toward a target index (0-based). speed is phase
		//! per second (1.0 reaches the target in a second); loop ping-pongs
		//! rest<->target. An out-of-range index or no targets is ignored.
		void playMorph(int targetIndex, float speed, bool loop);
		//! @brief stop advancing the morph (the current blend pose is held)
		void stopMorph();
		//! is a morph currently advancing
		inline bool isMorphPlaying() const { return this->mMorphPlaying; }
		//! current morph blend phase (0 = rest .. 1 = full target)
		inline float getMorphPhase() const { return this->mMorphPhase; }

		//--- drive ---
		//! @brief a physics contact: squash along the contact normal (dirX,dirY,
		//! not required normalized) with a depth from magnitude (the impact speed),
		//! and kick the wobble springs the same way. No-op before build or on a
		//! zero direction.
		void applyImpulse(float dirX, float dirY, float magnitude);
		//! @brief the owning body's linear velocity (units/s) - drives the
		//! velocity stretch. Set once per frame before update.
		void setBodyVelocity(float velocityX, float velocityY);

		//--- tick / output ---
		//! @brief advance the springs, squash and morph by deltaTime and recompute
		//! the per-control displacements. Allocation-free. No-op before build.
		void update(float deltaTime);
		//! @brief write the deformed vertex positions (rest + skinned control
		//! displacement) into out (sized to the vertex count). When the deformer
		//! is at rest this is the exact rest pose. No-op before build.
		void writePositions(std::vector<Point> & out) const;

		//! @brief is every driver settled (springs at rest, no squash, no stretch,
		//! morph at the rest pose) - the shape shows its exact rest geometry
		bool isAtRest() const;
		//! rest vertex count (0 before build)
		inline std::size_t vertexCount() const { return this->mRestMesh.size(); }
		//! control-point count (0 before build)
		inline std::size_t controlPointCount() const
		{
			return this->mRestControl.size();
		}
		//! @brief the largest current control-point displacement magnitude - a
		//! proxy for how far the mesh has deformed from rest (selfcheck probe)
		float maxControlDisplacement() const;
		//! current squash fraction applied this frame (introspection/selfcheck)
		inline float getSquash() const { return this->mSquashCurrent; }

		//--- helpers (static, shared with the component + tests) ---
		//! @brief sample up to targetCount control points evenly along the
		//! concatenated OUTER contours of regions (topology-stable: identical
		//! region/contour sizes yield identical picks, so morph targets
		//! correspond point-for-point). Appended to out.
		static void selectControlPoints(std::vector<Region> const & regions,
			int targetCount, std::vector<Point> & out);
		//! @brief do two region sets share deform topology (same region count,
		//! same outer and hole vertex counts) - the morph-target precondition
		static bool sameTopology(std::vector<Region> const & a,
			std::vector<Region> const & b);
	protected:
		//! @brief recompute mControlDelta from the current spring/squash/morph
		//! state (the per-frame math, factored so update stays readable)
		void computeControlDeltas();
		//! @brief reset all dynamic drivers to rest (springs, squash, morph, velocity)
		void resetDynamics();
	private:
	};
}

#endif //__SoftBodyDeform_h__11_7_2026__09_30_00__
