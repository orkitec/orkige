/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	PlayMirror.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "PlayMirror.h"

#include <cstdio>
#include <sstream>

namespace OrkigeEditor
{
	//---------------------------------------------------------
	bool parseMirrorTransform(std::string const& text, MirrorTransform& out)
	{
		std::istringstream stream(text);
		MirrorTransform parsed;
		for (float& value : parsed.m)
		{
			if (!(stream >> value))
			{
				return false;	// fewer than 10 parseable floats - reject
			}
		}
		out = parsed;
		return true;
	}
	//---------------------------------------------------------
	std::string formatMirrorTransform(MirrorTransform const& transform)
	{
		char buffer[160];
		std::snprintf(buffer, sizeof(buffer),
			"%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g",
			transform.m[0], transform.m[1], transform.m[2], transform.m[3],
			transform.m[4], transform.m[5], transform.m[6], transform.m[7],
			transform.m[8], transform.m[9]);
		return buffer;
	}
	//---------------------------------------------------------
	std::map<std::string, bool> computeEffectiveActive(
		std::vector<std::string> const& ids,
		std::vector<std::string> const& parents,
		std::vector<std::string> const& actives)
	{
		std::map<std::string, bool> result;
		// without the additive parent/active lists (an older stream) every
		// object reads as active - the historical "nothing hidden" behavior
		const bool haveParents = parents.size() == ids.size();
		const bool haveActives = actives.size() == ids.size();
		// activeSelf per id first (default true when the list is absent)
		std::map<std::string, bool> activeSelf;
		std::map<std::string, std::string> parentOf;
		for (std::size_t i = 0; i < ids.size(); ++i)
		{
			activeSelf[ids[i]] = haveActives ? (actives[i] == "1") : true;
			parentOf[ids[i]] = haveParents ? parents[i] : std::string();
		}
		// compose down the parent chain; a missing/looping parent stops the walk
		for (std::string const& id : ids)
		{
			bool effective = true;
			std::string cursor = id;
			std::set<std::string> guard;	// break a malformed parent cycle
			while (!cursor.empty() && guard.insert(cursor).second)
			{
				std::map<std::string, bool>::const_iterator self =
					activeSelf.find(cursor);
				if (self == activeSelf.end())
				{
					break;	// dangling parent - stop composing
				}
				if (!self->second)
				{
					effective = false;
					break;	// an inactive ancestor hides the whole subtree
				}
				std::map<std::string, std::string>::const_iterator up =
					parentOf.find(cursor);
				cursor = (up == parentOf.end()) ? std::string() : up->second;
			}
			result[id] = effective;
		}
		return result;
	}
	//---------------------------------------------------------
	void PlayMirror::beginIfNeeded(MirrorScene& scene)
	{
		if (this->mActive)
		{
			return;
		}
		this->mSnapshot.clear();
		this->mVisibleBaseline.clear();
		this->mVisibilityChanged.clear();
		for (std::string const& id : scene.transformableIds())
		{
			MirrorTransform authored;
			if (scene.getLocalTransform(id, authored))
			{
				this->mSnapshot[id] = authored;
				this->mVisibleBaseline[id] = scene.getBaselineVisible(id);
			}
		}
		this->mActive = true;
	}
	//---------------------------------------------------------
	void PlayMirror::applyTransforms(MirrorScene& scene,
		std::vector<std::string> const& ids,
		std::vector<std::string> const& transforms)
	{
		this->beginIfNeeded(scene);
		const std::size_t count = ids.size() < transforms.size()
			? ids.size() : transforms.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			// only mirror objects the authored scene actually holds (skip a
			// runtime-spawned id - it has no node to move; the v1 gap)
			if (this->mSnapshot.find(ids[i]) == this->mSnapshot.end())
			{
				continue;
			}
			MirrorTransform transform;
			if (parseMirrorTransform(transforms[i], transform))
			{
				scene.setLocalTransform(ids[i], transform);
			}
		}
	}
	//---------------------------------------------------------
	void PlayMirror::applyActive(MirrorScene& scene,
		std::map<std::string, bool> const& effective)
	{
		this->beginIfNeeded(scene);
		for (auto const& [id, isActive] : effective)
		{
			std::map<std::string, bool>::const_iterator baseline =
				this->mVisibleBaseline.find(id);
			if (baseline == this->mVisibleBaseline.end())
			{
				continue;	// unknown / non-authored object
			}
			if (isActive != baseline->second)
			{
				// the running game diverged from the authored visibility - drive
				// the node and remember it for the restore
				scene.setVisible(id, isActive);
				this->mVisibilityChanged.insert(id);
			}
			else if (this->mVisibilityChanged.erase(id) != 0)
			{
				// it healed back to the authored state - return it, stop tracking
				scene.setVisible(id, baseline->second);
			}
		}
	}
	//---------------------------------------------------------
	void PlayMirror::restore(MirrorScene& scene)
	{
		if (!this->mActive)
		{
			return;
		}
		// exact restore: write the snapshotted authored floats back verbatim
		for (auto const& [id, authored] : this->mSnapshot)
		{
			scene.setLocalTransform(id, authored);
		}
		// return the visibility of only what the mirror overrode
		for (std::string const& id : this->mVisibilityChanged)
		{
			std::map<std::string, bool>::const_iterator baseline =
				this->mVisibleBaseline.find(id);
			scene.setVisible(id,
				baseline == this->mVisibleBaseline.end() ? true
					: baseline->second);
		}
		this->mSnapshot.clear();
		this->mVisibleBaseline.clear();
		this->mVisibilityChanged.clear();
		this->mActive = false;
	}
	//---------------------------------------------------------
}
