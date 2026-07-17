/********************************************************************
	created:	Friday 2026/07/17 at 14:00
	filename: 	SpriteRunPlanner.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SpriteRunPlanner_h__17_7_2026__14_00_00__
#define __SpriteRunPlanner_h__17_7_2026__14_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <cstdint>
#include <vector>

namespace Orkige
{
	//! @brief the pure grouping half of sprite-run batching: which sprites
	//! may share one draw, and which of those groups actually changed
	//! @remarks OWNS the painter's contract for merged 2D: sprites sort by
	//! zOrder (STABLE - registration order breaks ties, so co-planar overlap
	//! keeps its creation order), and only CONTIGUOUS same-material neighbours
	//! in that sorted sequence form a RUN. An interleaved different-material
	//! sprite splits the run - batching may merge draws but may NEVER reorder
	//! across a material change, because alpha-blended painter output depends
	//! on draw order. Runs of one stay SOLO (their existing per-sprite quad
	//! already costs exactly one draw - a one-quad batch would add an object
	//! for nothing).
	//!
	//! DIRTY TRACKING: each item carries a caller-computed state hash (pose +
	//! sprite state). A run whose member sequence AND hashes are unchanged
	//! since the previous plan() reports needsRebuild=false - an unmoved run
	//! never re-uploads. One moved member dirties ONLY its own run.
	//!
	//! Pure and headless (no renderer types): the engine-side SpriteBatcher
	//! feeds it and realizes runs through the facade SpriteBatch; the unit
	//! tests drive it directly.
	class ORKIGE_CORE_DLL SpriteRunPlanner
	{
		//--- Types -------------------------------------------------
	public:
		//! one batchable sprite, in REGISTRATION order
		struct Item
		{
			std::uint64_t	id = 0;			//!< stable caller id (registration handle)
			String			materialKey;	//!< texture+sampler identity (sprites sharing it may merge)
			int				zOrder = 0;		//!< painter's sort key
			std::uint64_t	stateHash = 0;	//!< pose+state fingerprint (dirty tracking)
		};
		//! one planned draw group (always >= 2 members)
		struct Run
		{
			String			materialKey;	//!< the shared material identity
			int				zOrder = 0;		//!< the run's painter position
			std::vector<std::uint64_t>	members;	//!< member ids, draw order
			bool			needsRebuild = true;	//!< content changed since the previous plan
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		std::vector<Run>			mRuns;		//!< the current plan's runs
		std::vector<std::uint64_t>	mSolo;		//!< ids drawing individually
		//! the previous plan's run signatures: per run, the member (id, hash)
		//! sequence - the needsRebuild comparison source
		std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>>	mPreviousRuns;
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief compute the plan for this frame's items (registration
		//! order). Sorting is stable by zOrder; contiguous same-materialKey
		//! neighbours form runs; runs keep needsRebuild=false only when
		//! byte-equal (same members, same hashes) to a run of the previous
		//! plan at the same position.
		void plan(std::vector<Item> const & items);
		//! the planned runs (valid until the next plan call)
		inline std::vector<Run> const & runs() const { return this->mRuns; }
		//! ids outside every run - they keep their individual draw
		inline std::vector<std::uint64_t> const & solo() const { return this->mSolo; }
		//! forget the previous plan (every next run rebuilds) - the batcher
		//! calls this when batching re-enables after a toggle
		void reset();
	protected:
	private:
	};
}

#endif //__SpriteRunPlanner_h__17_7_2026__14_00_00__
