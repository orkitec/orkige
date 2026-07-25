/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	LineComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __LineComponent_h__25_7_2026__12_00_00__
#define __LineComponent_h__25_7_2026__12_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/LineMesh.h"
#include "engine_util/SceneNodeGuard.h"

#include <vector>

namespace Orkige
{
	//! @brief a 3D polyline or segment list authored on a GameObject - the
	//! component consumer of the engine_render LineMesh facade
	//! @remarks Follows LightComponent's structure: needs a sibling
	//! TransformComponent, owns a child scene node (through SceneNodeGuard) and
	//! attaches a facade LineMesh to it, so the lines follow the object's
	//! transform - the POINTS are in the object's LOCAL space (the node carries
	//! the world placement). This is AUTHORED content, not a runtime-only effect:
	//! it renders in edit mode and in Game Preview / Play (no editor-only
	//! visibility bit).
	//!
	//! The scalar look - `mode` (strip / segments), `colour` (one colour for the
	//! whole line in v1), `depthTest` (true = tested against the scene, false =
	//! an on-top overlay) - rides the ONE reflected property registry (inspector,
	//! serialization, MCP). The POINTS are bulk data, NOT per-point reflected
	//! properties: they ride a setter surface (setPoints, self.line:setPoints)
	//! and serialize compactly as a count + XYZ triples alongside the reflected
	//! block (the tag-list precedent). LINE WIDTH is intentionally absent -
	//! hairline only, @see LineMesh (a thickness upgrade is a future
	//! quad-expansion pass).
	//!
	//! DYNAMIC updates are first-class: a repeated setPoints of the SAME count
	//! rides the LineMesh dynamic fast path (no reallocation); a count/topology
	//! change rebuilds. Under a ticking runtime the upload is COALESCED to one
	//! per frame in onUpdateComponent (so the next backend never maps the buffer
	//! twice per frame); in the editor - which never ticks GameObjects - a
	//! setPoints / property change uploads synchronously so the change shows.
	class ORKIGE_ENGINE_DLL LineComponent
		: public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(LineComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief line connectivity; mirrors LineMesh::Topology for the property
		//! registry (the component owns its own enum so reflection stays
		//! decoupled from the facade, like LightComponent::LightType)
		enum LineMode
		{
			LM_STRIP = 0,		//!< a connected polyline (N points -> N-1 segments)
			LM_SEGMENTS = 1		//!< independent segments (point PAIRS)
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<LineMesh>		mMesh;			//!< the facade line mesh or NULL (detached)
		LineMode			mMode;			//!< strip (default) or segments
		Color				mColour;		//!< one colour for the whole line (default white)
		bool				mDepthTest;		//!< depth-test against the scene (default true)
		bool				mVisible;		//!< line visibility (applied to the node)
		std::vector<Vec3>	mPoints;		//!< the polyline points (LOCAL space)
		std::vector<Vec3>	mStaging;		//!< incremental builder buffer (beginPoints/addPoint/commitPoints)

		//--- upload bookkeeping ---
		std::size_t			mUploadedCount;	//!< mesh vertex count last uploaded (0 = none)
		std::size_t			mRebuildCount;	//!< topology/count-changing rebuilds (churn probe)
		LineMode			mUploadedMode;	//!< topology last uploaded (fast-path guard)
		bool				mDirty;			//!< points/colour changed, an upload is owed
		bool				mTicked;		//!< a runtime has ticked us (defer uploads to onUpdate)
		bool				mFreshBuild;	//!< a setLines mapped the buffer; defer the next dynamic update one tick (the next backend forbids mapping a buffer twice per frame)
		std::vector<LineMesh::Vertex>	mVertexScratch;	//!< reused upload buffer
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		LineComponent();
		//! destructor
		virtual ~LineComponent();

		//! is a facade line mesh currently live (false while detached)
		inline bool hasMesh() const { return this->mMesh != nullptr; }

		//! @brief replace the polyline points (object-local space). Same-count
		//! updates ride the dynamic fast path; a count/topology change rebuilds.
		void setPoints(std::vector<Vec3> const & points);
		//! @brief the Lua/script-friendly point setter: a FLAT list of XYZ
		//! coordinates ({x1,y1,z1, x2,y2,z2, ...}). A trailing partial triple is
		//! dropped. Same semantics as setPoints. (C++/native convenience - the
		//! Lua HANDLE builds points with beginPoints/addPoint/commitPoints, since
		//! the scripting bridge passes scalars, not container tables.)
		void setPointsFlat(std::vector<float> const & coords);

		//--- incremental point builder (the scalar Lua surface) ---
		//! @brief start staging a new point list (clears the staging buffer);
		//! the live line is untouched until commitPoints
		void beginPoints();
		//! @brief append one point (object-local XYZ) to the staging buffer
		void addPoint(float x, float y, float z);
		//! @brief replace the live points with the staged ones in ONE upload
		//! (rides the dynamic fast path when the count/topology are unchanged)
		void commitPoints();
		//! remove all points (the line disappears; keeps the node)
		void clearPoints();
		//! points in the polyline right now
		inline std::size_t getPointCount() const { return this->mPoints.size(); }
		//! vertices in the live mesh (0 when empty; selfcheck / introspection)
		std::size_t getVertexCount() const;
		//! @brief how many times the mesh was REBUILT (a topology/count-changing
		//! setLines) since attach - the churn probe: a same-count setPoints rides
		//! the dynamic fast path and does NOT bump this, so a script that reshapes
		//! the line every frame keeps the count at 1 (selfcheck / introspection)
		inline std::size_t getRebuildCount() const { return this->mRebuildCount; }

		//! select strip / segments (applied on the next upload)
		void setMode(LineMode mode);
		//! @see LineComponent::mMode
		inline LineMode getMode() const { return this->mMode; }
		//! set the whole-line colour
		void setColour(float red, float green, float blue, float alpha);
		//! @see LineComponent::mColour
		inline Color const & getColour() const { return this->mColour; }
		//! depth-test against the scene (true) or draw as an on-top overlay (false)
		void setDepthTest(bool depthTest);
		//! @see LineComponent::mDepthTest
		inline bool getDepthTest() const { return this->mDepthTest; }
		//! show/hide the line (the scene node's visibility)
		void setLineVisible(bool visible);
		//! is the line visible (true when no mesh exists yet - it will show)
		bool isLineVisible() const;

		//--- reflected property accessors ---
		//! reflected colour setter (Color -> the four-float setColour)
		inline void setColourValue(Color const & colour)
		{
			this->setColour(colour.r, colour.g, colour.b, colour.a);
		}
	protected:
		//! component override: create the child scene node + facade mesh
		virtual void onAdd();
		//! component override: drop the mesh + node
		virtual void onRemove();
		//! deactivated GameObjects hide their line (setLineVisible state kept)
		virtual void onSetActive(bool activeInHierarchy);
		//! @brief the SINGLE per-frame upload site under a ticking runtime: a
		//! dirty line uploads here (coalesced, next-backend-safe). Dormant unless
		//! a runtime ticks GameObjects (the editor uploads synchronously instead).
		virtual void onUpdateComponent(float deltaTime);
		//! @brief build the vertex array from the points+colour and push it to the
		//! mesh. @p forceRebuild always does a full setLines (the synchronous
		//! editor/load path, safe to repeat per frame); false lets a same-count +
		//! same-topology change ride the dynamic updateVertices fast path (the
		//! runtime onUpdateComponent path). A setLines arms the one-tick defer.
		void flushUpload(bool forceRebuild);
		//! apply the EFFECTIVE visibility to the node (own flag AND owner active)
		void applyVisibility();
		//--- SERIALIZATION ---
		//! save the reflected block (mode/colour/depthTest/visible) THEN the
		//! points as a count + XYZ triples (the bulk-data tail)
		virtual void save(optr<IArchive> const & ar);
		//! load the reflected block then the points, and upload when attached
		virtual void load(optr<IArchive> const & ar);
	private:
	};
}

#endif //__LineComponent_h__25_7_2026__12_00_00__
