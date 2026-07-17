/********************************************************************
	created:	Friday 2026/07/18 at 00:00
	filename: 	RenderDecal.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderDecal_h__18_7_2026__00_00_00__
#define __RenderDecal_h__18_7_2026__00_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief a projected surface mark placed via RenderNode - impact marks,
	//! paint splats, footprints and the blob-shadow fallback tier
	//! @remarks Created by RenderWorld::createDecal; the handle owns the backend
	//! decal and destroying it removes the mark (RAII, like RenderLight). The
	//! decal projects DOWN the owning node's local -Y onto the surface below it,
	//! so orient the node so its +Y is the surface normal (a ground mark needs no
	//! orientation). The rect footprint is world units.
	//!
	//! FLAVOR TOLERANCE (Docs/render-abstraction.md, RenderCaps::ProjectedDecals):
	//! next = a TRUE projected Ogre-Next Decal (HlmsPbs forward-clustered decal,
	//! its diffuse pooled in a fixed-size texture array; the mark wraps over any
	//! geometry inside its projection box). classic has no deferred/projective
	//! decal path, so it renders the honest subset: a surface-aligned textured
	//! QUAD floating just above the surface (depth-biased against z-fighting) -
	//! simple, mobile-cheap and visually adequate for a flat mark, but it does
	//! NOT wrap over uneven geometry. A per-flavor look, NOT a pixel-parity case.
	//!
	//! OPACITY / FADE: setOpacity smoothly dims the classic aligned quad's alpha
	//! (per-vertex). The native projected decal (next) has NO per-decal alpha
	//! uniform, so it treats opacity as a visibility THRESHOLD (> 0 shows the
	//! full-alpha mark, <= 0 hides it) - a fade there pops out at its end rather
	//! than dimming continuously (the documented capability delta).
	//!
	//! BUDGET: the world caps concurrently VISIBLE decals (RenderWorld::
	//! setMaxDecals, the `r.maxDecals` cvar). When a create pushes the live count
	//! past the cap the OLDEST decal is hidden (not destroyed - the owning handle
	//! still lives); a cap of 0 hides every decal (the mobile-budget escape hatch,
	//! byte-identical to a scene with no decals).
	class ORKIGE_ENGINE_DLL RenderDecal
	{
		//--- Types -------------------------------------------------
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	protected:
		Impl*	mImpl;	//!< backend decal guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the backend decal (leaves the
		//! world's budget registry)
		~RenderDecal();

		//--- placement ---
		//! map: classic=SceneNode::attachObject(quad) | next=SceneNode::attachObject(Decal)
		void attachTo(optr<RenderNode> const & node);
		void detach();

		//--- parameters ---
		//! @brief the diffuse (mark) texture, resolved through the resource
		//! groups. On next it must live in the shared decal diffuse pool (a
		//! fixed-size array - author decal textures at the pool resolution; a
		//! mismatch is logged once and the decal renders no mark).
		//! map: classic="Decal/<tex>" aligned-quad material | next=Decal::setDiffuseTexture (auto slice in the pooled array)
		void setDiffuseTexture(String const & textureName);
		//! @brief the projected footprint: @p width (local X) x @p depth (local Z)
		//! world units on the surface, @p projectionDepth how far along -Y the
		//! projection box reaches into geometry (next only; classic aligns a flat
		//! quad and ignores it).
		//! map: classic=quad extents (projectionDepth ignored) | next=Decal::setRectSize(Vector2(width,depth), projectionDepth)
		void setSize(Real width, Real depth, Real projectionDepth = Real(4));
		//! @brief mark opacity 0..1 (@see class remarks: classic dims smoothly,
		//! next thresholds at > 0). Cheap per-frame call - the fade site.
		void setOpacity(Real opacity);
		//! @brief show / hide the mark for owner (de)activation. Combines with the
		//! world budget's own visibility gate - the mark renders only when BOTH
		//! allow it.
		void setVisible(bool visible);
		//! the visibility last requested through setVisible (the owner gate, NOT
		//! the budget gate) - selfcheck/introspection
		bool isVisible() const;
	protected:
		//! decals are created by RenderWorld::createDecal only
		RenderDecal();
	private:
		RenderDecal(RenderDecal const &);				// non-copyable
		RenderDecal & operator=(RenderDecal const &);	// non-copyable
	};
}

#endif //__RenderDecal_h__18_7_2026__00_00_00__
