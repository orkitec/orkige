/********************************************************************
	created:	Friday 2026/07/17 at 12:00
	filename: 	StaticBakeClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file StaticBakeClassic.cpp
//! @brief the classic mobility fast path: entities on STATIC nodes bake
//! into shared Ogre::StaticGeometry regions
//! @remarks OGRE's own many-immobile-meshes machinery does the heavy
//! lifting (world transforms baked into region vertex buffers, one
//! renderable per material bucket per region); this file owns ONLY the
//! membership bookkeeping around it:
//!
//! - REGISTRATION follows the facade mobility flag: RenderNode::setStatic
//!   sweeps already-attached entities in, MeshInstance::attachTo registers
//!   content created after the flag, detach/destruction unregister.
//! - The bake is DEFERRED and COALESCED: every membership or visibility
//!   change only marks it dirty; RenderSystem::renderOneFrame flushes at
//!   most ONE rebuild per frame (scene loads register dozens of entities
//!   but pay a single build). Rebuild cost is acceptable at edit-time
//!   frequency - never per frame (the mobility contract demotes movers).
//! - Baked source entities are SUPPRESSED via visibility flags 0 (never
//!   via the visible flag, which stays the game's own show/hide state and
//!   keeps flowing through node cascades). The flush re-filters on
//!   entity->getVisible(), so hiding a static object rebuilds the regions
//!   without it and showing it folds it back in.
//! - TWO buckets, split by the entity's castShadows flag at flush time
//!   ("cast"/"nocast" StaticGeometry objects): shadow casting is
//!   region-level in OGRE, so per-object cast flags survive the bake.
//!
//! Visual note: regions re-express vertices relative to the region origin,
//! so the GPU transform order differs from individual entities by float
//! rounding - sub-pixel edge differences against the unbaked path are
//! possible (the r.staticScene toggle test compares with a tight
//! tolerance on this flavor, exact on next).

#include "engine_render_classic/ClassicBackend.h"
#include <core_debug/DebugMacros.h>

#include <map>

namespace Orkige
{
	namespace
	{
		//! one registered entity: where it sits and what the bake did to it
		struct BakeEntry
		{
			Ogre::SceneNode*	node = NULL;		//!< placement source (derived transform read at flush)
			Ogre::uint32		previousFlags = 0;	//!< visibility flags before suppression
			bool				suppressed = false;	//!< individual rendering currently off (baked)
		};

		//! the registry + built regions (process-wide like the backend hub;
		//! reset by staticBakeTeardown with the render system)
		struct BakeState
		{
			std::map<Ogre::Entity*, BakeEntry>	entries;
			Ogre::StaticGeometry*	casting = NULL;		//!< shadow-casting bucket
			Ogre::StaticGeometry*	nonCasting = NULL;	//!< non-casting bucket
			bool					dirty = false;
			size_t					bakedCount = 0;		//!< entities inside the built regions
		};
		BakeState gBake;

		//! bucket names (unique per scene manager - there is exactly one)
		char const * const BAKE_NAME_CAST = "Orkige/StaticBake/cast";
		char const * const BAKE_NAME_NOCAST = "Orkige/StaticBake/nocast";

		//! stop suppressing an entry's individual rendering
		void restoreEntry(Ogre::Entity* entity, BakeEntry & entry)
		{
			if(entry.suppressed)
			{
				entity->setVisibilityFlags(entry.previousFlags);
				entry.suppressed = false;
			}
		}

		//! destroy a built bucket (safe on NULL)
		void destroyBucket(Ogre::SceneManager* sceneManager,
			Ogre::StaticGeometry* & bucket)
		{
			if(bucket)
			{
				sceneManager->destroyStaticGeometry(bucket);
				bucket = NULL;
			}
		}
	}
	//---------------------------------------------------------
	void RenderBackend::staticBakeRegister(Ogre::Entity* entity,
		Ogre::SceneNode* node)
	{
		oAssert(entity);
		oAssert(node);
		BakeEntry & entry = gBake.entries[entity];	// idempotent insert
		entry.node = node;
		gBake.dirty = true;
	}
	//---------------------------------------------------------
	void RenderBackend::staticBakeUnregister(Ogre::Entity* entity)
	{
		std::map<Ogre::Entity*, BakeEntry>::iterator found =
			gBake.entries.find(entity);
		if(found == gBake.entries.end())
		{
			return;
		}
		// individual rendering resumes IMMEDIATELY (the region rebuild that
		// stops double-drawing it is deferred to the same frame's flush)
		restoreEntry(entity, found->second);
		gBake.entries.erase(found);
		gBake.dirty = true;
	}
	//---------------------------------------------------------
	bool RenderBackend::staticBakeContains(Ogre::Entity* entity)
	{
		return gBake.entries.find(entity) != gBake.entries.end();
	}
	//---------------------------------------------------------
	void RenderBackend::staticBakeMarkDirty()
	{
		if(!gBake.entries.empty())
		{
			gBake.dirty = true;
		}
	}
	//---------------------------------------------------------
	size_t RenderBackend::staticBakeBakedCount()
	{
		return gBake.bakedCount;
	}
	//---------------------------------------------------------
	void RenderBackend::staticBakeFlush()
	{
		if(!gBake.dirty)
		{
			return;
		}
		gBake.dirty = false;
		gBake.bakedCount = 0;
		Ogre::SceneManager* sceneManager = NULL;
		if(!gBake.entries.empty())
		{
			sceneManager = gBake.entries.begin()->second.node->getCreator();
		}
		else if(RenderBackend::system())
		{
			sceneManager = RenderBackend::system()->getWorld()
				->mImpl->sceneManager;
		}
		if(!sceneManager)
		{
			return;
		}
		// clean rebuild: regions carry baked copies, so membership changes
		// always rebuild the whole bucket set (coalesced to once per frame)
		destroyBucket(sceneManager, gBake.casting);
		destroyBucket(sceneManager, gBake.nonCasting);
		for(std::map<Ogre::Entity*, BakeEntry>::iterator each =
			gBake.entries.begin(); each != gBake.entries.end(); ++each)
		{
			Ogre::Entity* entity = each->first;
			BakeEntry & entry = each->second;
			// membership follows the game's show/hide state: a hidden static
			// object stays OUT of the regions and renders nothing
			if(!entity->getVisible() || !entity->isInScene())
			{
				restoreEntry(entity, entry);
				continue;
			}
			// the frozen placement: the node's derived transform right now
			entry.node->_update(false, true);
			Ogre::StaticGeometry* & bucket = entity->getCastShadows()
				? gBake.casting : gBake.nonCasting;
			if(!bucket)
			{
				bucket = sceneManager->createStaticGeometry(
					entity->getCastShadows() ? BAKE_NAME_CAST
						: BAKE_NAME_NOCAST);
				bucket->setCastShadows(entity->getCastShadows());
			}
			bucket->addEntity(entity, entry.node->_getDerivedPosition(),
				entry.node->_getDerivedOrientation(),
				entry.node->_getDerivedScale());
			// suppress the individual draw (visibility FLAGS, not the
			// visible flag - @see file remarks)
			if(!entry.suppressed)
			{
				entry.previousFlags = entity->getVisibilityFlags();
				entry.suppressed = true;
			}
			entity->setVisibilityFlags(0);
			++gBake.bakedCount;
		}
		if(gBake.casting)
		{
			gBake.casting->build();
		}
		if(gBake.nonCasting)
		{
			gBake.nonCasting->build();
		}
	}
	//---------------------------------------------------------
	void RenderBackend::staticBakeTeardown()
	{
		// the scene manager dies with the render system right after this -
		// only restore what an outliving entity handle could still touch
		for(std::map<Ogre::Entity*, BakeEntry>::iterator each =
			gBake.entries.begin(); each != gBake.entries.end(); ++each)
		{
			restoreEntry(each->first, each->second);
		}
		if(RenderBackend::system() && !gBake.entries.empty())
		{
			Ogre::SceneManager* sceneManager =
				gBake.entries.begin()->second.node->getCreator();
			destroyBucket(sceneManager, gBake.casting);
			destroyBucket(sceneManager, gBake.nonCasting);
		}
		gBake.casting = NULL;
		gBake.nonCasting = NULL;
		gBake.entries.clear();
		gBake.bakedCount = 0;
		gBake.dirty = false;
	}
}
