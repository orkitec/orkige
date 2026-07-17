/********************************************************************
	created:	Friday 2026/07/17 at 14:00
	filename: 	SpriteBatcher.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SpriteBatcher_h__17_7_2026__14_00_00__
#define __SpriteBatcher_h__17_7_2026__14_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/SpriteBatch.h"
#include <core_util/Singleton.h>
#include <core_util/SpriteRunPlanner.h>

#include <cstdint>
#include <vector>

namespace Orkige
{
	class SpriteComponent;

	//! @brief merges contiguous same-material sprite runs into shared
	//! facade SpriteBatches - one draw per run instead of one per sprite
	//! @remarks The engine-side half of sprite-run batching: SpriteComponents
	//! register on sprite load (registration order IS the within-zOrder
	//! painter order), the pure core_util/SpriteRunPlanner owns the grouping
	//! contract (stable zOrder sort, contiguous same-(texture,sampler) runs,
	//! never reordering across a material change, dirty tracking), and each
	//! run realizes as ONE facade SpriteBatch on the world root filled with
	//! the members' world-space quads (SpriteComponent::buildWorldQuad -
	//! byte-equal geometry to the individual quads, so merging changes
	//! nothing but the draw count; the render-toggle pixel test enforces
	//! that). Batched members hide their individual quad (object-level flag
	//! only - node visibility stays the game's state); solo sprites keep it.
	//! An unmoved run never re-uploads; one moved member re-uploads only its
	//! own run.
	//!
	//! Runtime-owned like TweenManager: the player creates it and calls
	//! update() once per frame right before rendering (also while the sim is
	//! paused, so debug-protocol edits keep landing). The editor never
	//! creates one, so edit mode keeps the plain per-quad path (WYSIWYG holds
	//! because merged pixels equal per-quad pixels). The r.spriteBatching
	//! cvar (default on) is the live escape hatch: off releases every run
	//! and restores the individual draws the next update.
	class ORKIGE_ENGINE_DLL SpriteBatcher : public Singleton<SpriteBatcher>
	{
		DECL_OSINGLETON(SpriteBatcher)
		//--- Types -------------------------------------------------
	public:
	protected:
		//! one registered sprite (registration order preserved)
		struct Entry
		{
			SpriteComponent*	sprite = NULL;
			std::uint64_t		id = 0;			//!< stable planner id
		};
		//! one realized run: the facade batch + the identity it was built for
		struct RunBatch
		{
			optr<SpriteBatch>	batch;
			String				materialKey;	//!< the batch's texture+sampler identity
			String				textureName;	//!< creation texture (recreate detection)
		};
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		std::vector<Entry>		mSprites;		//!< registration order
		std::uint64_t			mNextId = 1;	//!< planner id source
		SpriteRunPlanner		mPlanner;		//!< the pure grouping core
		std::vector<RunBatch>	mBatches;		//!< parallel to the planner's runs
		std::vector<SpriteBatch::Vertex>	mVertexScratch;	//!< reused upload buffer
		bool					mEnabled = true;	//!< the cvar state last update
	private:
		//--- Methods -----------------------------------------------
	public:
		SpriteBatcher();
		virtual ~SpriteBatcher();

		//! register a sprite with a live quad (idempotent; order = painter
		//! tie-break within a zOrder)
		void add(SpriteComponent* sprite);
		//! drop a sprite (its individual draw is restored immediately)
		void remove(SpriteComponent* sprite);

		//! @brief group, dirty-check and (re)upload the runs for the frame
		//! about to render - called once per frame by the runtime loop,
		//! after all gameplay mutations, before renderOneFrame
		void update();

		//! runs currently realized as merged batches (selfcheck introspection)
		size_t activeRunCount() const;
	protected:
		//! release every merged batch and restore all individual draws (the
		//! toggle-off / teardown path)
		void releaseBatches();
	private:
	};
}

#endif //__SpriteBatcher_h__17_7_2026__14_00_00__
