/**************************************************************
	created:	2026/07/17 at 14:00
	filename: 	SpriteBatcher.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/SpriteBatcher.h"
#include "engine_gocomponent/SpriteComponent.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include <core_debug/CVarManager.h>

#include <algorithm>

namespace Orkige
{
	IMPL_OSINGLETON(SpriteBatcher)

	namespace
	{
		//! FNV-1a over a byte span - the pose fingerprint hash
		std::uint64_t fnv1a(std::uint64_t hash, void const * data,
			std::size_t bytes)
		{
			unsigned char const * cursor =
				static_cast<unsigned char const *>(data);
			for(std::size_t each = 0; each < bytes; ++each)
			{
				hash ^= cursor[each];
				hash *= 1099511628211ull;
			}
			return hash;
		}

		//! the sprite's dirty-tracking fingerprint: its state version plus
		//! the node's world transform (an unmoved, untouched sprite hashes
		//! identically frame over frame)
		std::uint64_t spriteStateHash(SpriteComponent const & sprite)
		{
			std::uint64_t hash = 14695981039346656037ull;
			const unsigned int version = sprite.getStateVersion();
			hash = fnv1a(hash, &version, sizeof(version));
			const Vec3 position = sprite.getNode()->getWorldPosition();
			const Quat orientation = sprite.getNode()->getWorldOrientation();
			const Vec3 scale = sprite.getNode()->getWorldScale();
			hash = fnv1a(hash, &position.x, sizeof(Real) * 3);
			hash = fnv1a(hash, &orientation.w, sizeof(Real) * 4);
			hash = fnv1a(hash, &scale.x, sizeof(Real) * 3);
			return hash;
		}
	}
	//---------------------------------------------------------
	SpriteBatcher::SpriteBatcher()
	{
	}
	//---------------------------------------------------------
	SpriteBatcher::~SpriteBatcher()
	{
		this->releaseBatches();
	}
	//---------------------------------------------------------
	void SpriteBatcher::add(SpriteComponent* sprite)
	{
		oAssert(sprite);
		for(Entry const & each : this->mSprites)
		{
			if(each.sprite == sprite)
			{
				return;	// idempotent (a reload keeps the original order slot)
			}
		}
		Entry entry;
		entry.sprite = sprite;
		entry.id = this->mNextId++;
		this->mSprites.push_back(entry);
	}
	//---------------------------------------------------------
	void SpriteBatcher::remove(SpriteComponent* sprite)
	{
		std::erase_if(this->mSprites,
			[sprite](Entry const & each) { return each.sprite == sprite; });
		if(sprite)
		{
			sprite->suppressIndividualDraw(false);
		}
	}
	//---------------------------------------------------------
	void SpriteBatcher::update()
	{
		// the live escape hatch: off releases every run and the sprites
		// draw individually again (the pixel toggle test compares the two)
		const bool enabled = !CVarManager::getSingletonPtr() ||
			CVarManager::getSingleton().getBool("r.spriteBatching", true);
		if(!enabled)
		{
			if(this->mEnabled)
			{
				this->releaseBatches();
				this->mPlanner.reset();
				this->mEnabled = false;
			}
			return;
		}
		this->mEnabled = true;

		// this frame's grouping input, in registration order; ineligible
		// sprites (no quad / hidden) stay out and untouched
		std::vector<SpriteRunPlanner::Item> items;
		items.reserve(this->mSprites.size());
		for(Entry const & entry : this->mSprites)
		{
			SpriteComponent* sprite = entry.sprite;
			if(!sprite->hasSprite() || !sprite->isEffectivelyVisible())
			{
				continue;
			}
			SpriteRunPlanner::Item item;
			item.id = entry.id;
			item.materialKey = sprite->batchMaterialKey();
			item.zOrder = sprite->getZOrder();
			item.stateHash = spriteStateHash(*sprite);
			items.push_back(std::move(item));
		}
		this->mPlanner.plan(items);

		// realize the runs: one facade SpriteBatch per run (parallel arrays,
		// possibly-empty slots on creation failure), reused while its
		// material identity holds, re-uploaded only when the run is dirty
		std::vector<SpriteRunPlanner::Run> const & runs = this->mPlanner.runs();
		this->mBatches.resize(runs.size());	// extra batches drop (RAII)
		RenderWorld* world = RenderSystem::get()
			? RenderSystem::get()->getWorld() : NULL;
		for(std::size_t index = 0; index < runs.size(); ++index)
		{
			SpriteRunPlanner::Run const & run = runs[index];
			bool freshBatch = false;
			if(!this->mBatches[index].batch ||
				this->mBatches[index].materialKey != run.materialKey)
			{
				// (re)create the run's batch under the members' EXACT
				// per-(texture,sampler) material
				this->mBatches[index] = RunBatch();
				SpriteComponent* first = NULL;
				for(Entry const & entry : this->mSprites)
				{
					if(entry.id == run.members.front())
					{
						first = entry.sprite;
						break;
					}
				}
				if(!first || !world)
				{
					continue;	// membership raced a removal; next frame heals
				}
				RunBatch runBatch;
				runBatch.batch = world->createSpriteBatch(
					first->getTextureName(), SpriteBatch::BLEND_ALPHA,
					first->getFilter(), first->getAddressing());
				if(!runBatch.batch)
				{
					continue;	// load failure already logged
				}
				runBatch.batch->attachTo(world->getRootNode());
				runBatch.materialKey = run.materialKey;
				runBatch.textureName = first->getTextureName();
				this->mBatches[index] = std::move(runBatch);
				freshBatch = true;
			}
			RunBatch & runBatch = this->mBatches[index];
			runBatch.batch->setZOrder(run.zOrder);
			if(run.needsRebuild || freshBatch)
			{
				this->mVertexScratch.clear();
				this->mVertexScratch.reserve(run.members.size() * 4);
				for(std::uint64_t member : run.members)
				{
					for(Entry const & entry : this->mSprites)
					{
						if(entry.id != member)
						{
							continue;
						}
						SpriteBatch::Vertex quad[4];
						if(entry.sprite->buildWorldQuad(quad))
						{
							this->mVertexScratch.insert(
								this->mVertexScratch.end(), quad, quad + 4);
						}
						break;
					}
				}
				runBatch.batch->setQuads(this->mVertexScratch.data(),
					this->mVertexScratch.size() / 4);
			}
			// members ride the merged draw - their individual quads hide
			for(std::uint64_t member : run.members)
			{
				for(Entry const & entry : this->mSprites)
				{
					if(entry.id == member)
					{
						entry.sprite->suppressIndividualDraw(true);
						break;
					}
				}
			}
		}
		// solo sprites (and every eligible sprite outside a run) draw
		// individually - re-assert per frame so node-visibility cascades
		// that transiently flipped the object flag self-correct pre-render
		for(std::uint64_t solo : this->mPlanner.solo())
		{
			for(Entry const & entry : this->mSprites)
			{
				if(entry.id == solo)
				{
					entry.sprite->suppressIndividualDraw(false);
					break;
				}
			}
		}
	}
	//---------------------------------------------------------
	size_t SpriteBatcher::activeRunCount() const
	{
		size_t count = 0;
		for(RunBatch const & each : this->mBatches)
		{
			if(each.batch)
			{
				++count;
			}
		}
		return count;
	}
	//---------------------------------------------------------
	void SpriteBatcher::releaseBatches()
	{
		this->mBatches.clear();	// RAII drops the backend batches
		for(Entry const & entry : this->mSprites)
		{
			entry.sprite->suppressIndividualDraw(false);
		}
	}
}
