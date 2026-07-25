/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	DebugDraw.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __DebugDraw_h__25_7_2026__12_00_00__
#define __DebugDraw_h__25_7_2026__12_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/DebugDrawBuffer.h"
#include "core_util/Singleton.h"
#include "core_util/optr.h"

#include <vector>

namespace Orkige
{
	class RenderNode;

	//! @brief immediate-mode 3D debug drawing the Lua `draw` table drives:
	//! world-space lines, boxes and wireframe spheres, this-frame or with a TTL,
	//! flushed into ONE dynamic line mesh per frame
	//! @remarks Engine-owned and ticked by the runtime, like ScreenShake /
	//! ScreenFade: the editor never constructs one, so `draw.line`/`draw.box`/
	//! `draw.sphere` are honest no-ops in edit mode. Zero cost when unused - no
	//! line mesh exists until the first primitive is drawn. The pure collector
	//! (DebugDrawBuffer) holds the primitives; update() expands the live ones to
	//! line segments, uploads them through the facade LineMesh dynamic path (one
	//! rebuild + one draw call per frame) and then ages/drops them. These are
	//! script-driven visuals, so they ship in exported games - a game may use
	//! them - and render honestly in the game view.
	//! @remarks Ticked once per frame, LAST (a presentation effect, after the
	//! deferred-load pump and the screen fade/shake), so it captures the
	//! primitives the frame's scripts drew.
	class ORKIGE_ENGINE_DLL DebugDraw : public Singleton<DebugDraw>
	{
		DECL_OSINGLETON(DebugDraw);
		//--- Methods -----------------------------------------
	public:
		DebugDraw();
		virtual ~DebugDraw();

		//! @brief draw a world-space line p1..p2; seconds <= 0 = this frame only,
		//! else a lifetime in seconds
		void line(Vec3 const & p1, Vec3 const & p2, Color const & colour,
			float seconds);
		//! @brief draw a world-space axis-aligned wireframe box (12 edges)
		void box(Vec3 const & centre, Vec3 const & halfExtents,
			Color const & colour, float seconds);
		//! @brief draw a world-space wireframe sphere (three great circles)
		void sphere(Vec3 const & centre, float radius, Color const & colour,
			float seconds);

		//! @brief flush the queued primitives into the line mesh for THIS frame,
		//! then age their lifetimes (dropping frame-only + expired entries). No-op
		//! without a render system / world. Ticked LAST in the player loop.
		void update(float deltaTime);
		//! drop every queued primitive AND hide the mesh (teardown / explicit clear)
		void clear();

		//! primitives queued right now (selfcheck / introspection)
		std::size_t getPrimitiveCount() const;
		//! vertices in the live line mesh (0 when no mesh / empty; selfcheck probe)
		std::size_t getMeshVertexCount() const;
	protected:
	private:
		//! create the world-space line mesh + node on first use (lazy)
		void ensureMesh();

		DebugDrawBuffer					mBuffer;	//!< the pure primitive collector
		optr<LineMesh>					mMesh;		//!< the facade line mesh (NULL until first draw)
		optr<RenderNode>				mNode;		//!< world-space node the mesh rides
		std::vector<LineMesh::Vertex>	mVertices;	//!< reused segment-upload buffer
		bool							mVisible;	//!< the mesh is currently showing geometry
	};
}

#endif //__DebugDraw_h__25_7_2026__12_00_00__
