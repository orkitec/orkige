/**************************************************************
	created:	2026/07/17 at 14:00
	filename: 	SpriteRunPlanner.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_util/SpriteRunPlanner.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	void SpriteRunPlanner::plan(std::vector<Item> const & items)
	{
		this->mRuns.clear();
		this->mSolo.clear();

		// stable painter order: zOrder ascending, registration order within
		// (index sort keeps the input untouched and the tie-break explicit)
		std::vector<std::size_t> order(items.size());
		for(std::size_t each = 0; each < order.size(); ++each)
		{
			order[each] = each;
		}
		std::stable_sort(order.begin(), order.end(),
			[&items](std::size_t left, std::size_t right)
			{
				return items[left].zOrder < items[right].zOrder;
			});

		// contiguous same-material neighbours (within one zOrder) form runs;
		// a material OR zOrder change closes the current one
		std::size_t begin = 0;
		while(begin < order.size())
		{
			std::size_t end = begin + 1;
			while(end < order.size() &&
				items[order[end]].zOrder == items[order[begin]].zOrder &&
				items[order[end]].materialKey == items[order[begin]].materialKey)
			{
				++end;
			}
			if(end - begin >= 2)
			{
				Run run;
				run.materialKey = items[order[begin]].materialKey;
				run.zOrder = items[order[begin]].zOrder;
				run.members.reserve(end - begin);
				for(std::size_t each = begin; each < end; ++each)
				{
					run.members.push_back(items[order[each]].id);
				}
				this->mRuns.push_back(std::move(run));
			}
			else
			{
				this->mSolo.push_back(items[order[begin]].id);
			}
			begin = end;
		}

		// dirty tracking: a run at the same position with the identical
		// (id, hash) member sequence needs no rebuild
		std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>>
			signatures;
		signatures.reserve(this->mRuns.size());
		for(Run & run : this->mRuns)
		{
			std::vector<std::pair<std::uint64_t, std::uint64_t>> signature;
			signature.reserve(run.members.size());
			for(std::uint64_t member : run.members)
			{
				// find the member's hash in the input (items are small per
				// frame; linear scan keeps the planner dependency-free)
				std::uint64_t hash = 0;
				for(Item const & item : items)
				{
					if(item.id == member)
					{
						hash = item.stateHash;
						break;
					}
				}
				signature.push_back({ member, hash });
			}
			const std::size_t index =
				static_cast<std::size_t>(&run - this->mRuns.data());
			run.needsRebuild = index >= this->mPreviousRuns.size() ||
				this->mPreviousRuns[index] != signature;
			signatures.push_back(std::move(signature));
		}
		this->mPreviousRuns = std::move(signatures);
	}
	//---------------------------------------------------------
	void SpriteRunPlanner::reset()
	{
		this->mPreviousRuns.clear();
		this->mRuns.clear();
		this->mSolo.clear();
	}
}
