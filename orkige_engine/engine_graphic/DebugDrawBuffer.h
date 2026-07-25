/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	DebugDrawBuffer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __DebugDrawBuffer_h__25_7_2026__12_00_00__
#define __DebugDrawBuffer_h__25_7_2026__12_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderMath.h"
#include "engine_render/LineMesh.h"

#include <vector>
#include <cstddef>

namespace Orkige
{
	//! @brief the PURE collector behind the immediate-mode `draw` debug table:
	//! a capped list of debug primitives (lines/boxes/spheres) with per-entry
	//! lifetime, expanded to line SEGMENTS for one dynamic line mesh per frame
	//! @remarks Headless and renderer-free (the GPU side is DebugDraw, which owns
	//! a facade LineMesh and flushes this buffer into it). Allocation-free in the
	//! steady state: the primitive vector reserves up to the capacity and entries
	//! are added/removed in place. When more primitives than the capacity are
	//! added in a frame the overflow is DROPPED with a single warn-once (a bug in
	//! the caller, not a crash). Lifetime: a primitive added with seconds <= 0 is
	//! FRAME-ONLY (rendered once, then dropped by advance); seconds > 0 gives it a
	//! TTL that advance ages down. Segment expansion is fixed and deterministic
	//! (a box is 12 edges = 24 vertices, a sphere three great circles), so the
	//! vertex counts are unit-assertable.
	class ORKIGE_ENGINE_DLL DebugDrawBuffer
	{
		//--- Types -------------------------------------------------
	public:
		//! primitive kind
		enum Kind
		{
			KIND_LINE = 0,		//!< a single segment a..b
			KIND_BOX = 1,		//!< an axis-aligned box (center + half extents)
			KIND_SPHERE = 2		//!< a wireframe sphere (center + radius)
		};
		//! one queued debug primitive
		struct Primitive
		{
			Kind	kind;
			Vec3	a;			//!< line: endpoint 1; box/sphere: centre
			Vec3	b;			//!< line: endpoint 2; box: half extents; sphere: (radius,0,0)
			Color	colour;		//!< straight RGBA
			float	remaining;	//!< seconds of TTL left (unused when frameOnly)
			bool	frameOnly;	//!< dropped after one flush (seconds <= 0 at add)
		};
		//--- Variables ---------------------------------------------
	public:
		static const std::size_t DEFAULT_CAPACITY;	//!< primitive cap (4096)
		static const std::size_t SPHERE_SEGMENTS;	//!< segments per sphere great circle (16)
	protected:
	private:
		std::vector<Primitive>	mPrimitives;	//!< live primitives
		std::size_t				mCapacity;		//!< hard primitive cap
		std::size_t				mDropped;		//!< primitives dropped over capacity (lifetime)
		bool					mOverflowWarned;//!< warn-once latch
		//--- Methods -----------------------------------------------
	public:
		//! construct with DEFAULT_CAPACITY
		DebugDrawBuffer();

		//! @brief the primitive cap (over it, new primitives are dropped). A cap
		//! of 0 means unbounded (test convenience; never used in the runtime).
		void setCapacity(std::size_t maxPrimitives);
		//! @see DebugDrawBuffer::setCapacity
		std::size_t getCapacity() const { return this->mCapacity; }

		//! @brief queue a line segment p1..p2; seconds <= 0 = this frame only
		void addLine(Vec3 const & p1, Vec3 const & p2, Color const & colour,
			float seconds);
		//! @brief queue an axis-aligned box (12 edges); seconds <= 0 = this frame
		void addBox(Vec3 const & centre, Vec3 const & halfExtents,
			Color const & colour, float seconds);
		//! @brief queue a wireframe sphere (three great circles); seconds <= 0 = frame
		void addSphere(Vec3 const & centre, float radius, Color const & colour,
			float seconds);

		//! live primitives right now
		std::size_t getPrimitiveCount() const { return this->mPrimitives.size(); }
		//! primitives dropped over capacity since construction (introspection)
		std::size_t getDroppedCount() const { return this->mDropped; }
		//! no primitives queued
		bool empty() const { return this->mPrimitives.empty(); }

		//! @brief the line-segment vertex count the current primitives expand to
		//! (line=2, box=24, sphere=3*SPHERE_SEGMENTS*2) - the exact size
		//! buildSegments fills, computed without building
		std::size_t segmentVertexCount() const;
		//! @brief expand every primitive to line-list vertices (point PAIRS) for
		//! a LineMesh::TOPOLOGY_SEGMENTS upload; clears + fills @p out
		void buildSegments(std::vector<LineMesh::Vertex> & out) const;

		//! @brief age TTL primitives by @p deltaTime and drop frame-only + expired
		//! entries (called once per frame AFTER buildSegments)
		void advance(float deltaTime);
		//! drop every primitive
		void clear();
	protected:
	private:
		//! append a primitive honouring the capacity (warn-once on overflow)
		void push(Primitive const & primitive);
	};
}

#endif //__DebugDrawBuffer_h__25_7_2026__12_00_00__
