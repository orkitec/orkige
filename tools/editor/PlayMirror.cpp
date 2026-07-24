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
#include <cstdlib>
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
	std::vector<MirrorSpawnDesc> parseSpawnDescriptors(
		std::vector<std::string> const& ids,
		std::vector<std::string> const& parents,
		std::vector<std::string> const& components,
		std::vector<std::string> const& propObjects,
		std::vector<std::string> const& propKeys,
		std::vector<std::string> const& propKinds,
		std::vector<std::string> const& propValues,
		std::vector<std::string> const& propRefs)
	{
		std::vector<MirrorSpawnDesc> result;
		for (std::size_t i = 0; i < ids.size(); ++i)
		{
			MirrorSpawnDesc desc;
			desc.id = ids[i];
			desc.parent = i < parents.size() ? parents[i] : std::string();
			// component kinds: space-separated names (kind names carry no spaces)
			if (i < components.size())
			{
				std::istringstream kinds(components[i]);
				std::string kind;
				while (kinds >> kind)
				{
					desc.components.push_back(kind);
				}
			}
			result.push_back(desc);
		}
		// the flat per-property records (parallel quintuple, object by index)
		const std::size_t recordCount = propObjects.size() < propKeys.size()
			? propObjects.size() : propKeys.size();
		for (std::size_t i = 0; i < recordCount; ++i)
		{
			// a malformed record is skipped, never guessed at
			char* end = nullptr;
			const unsigned long objectIndex =
				std::strtoul(propObjects[i].c_str(), &end, 10);
			if (propObjects[i].empty() || end == nullptr || *end != '\0' ||
				objectIndex >= result.size())
			{
				continue;
			}
			const std::string& key = propKeys[i];
			const std::size_t dot = key.find('.');
			if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size())
			{
				continue;
			}
			MirrorSpawnProperty property;
			property.component = key.substr(0, dot);
			property.name = key.substr(dot + 1);
			property.kind = i < propKinds.size()
				? std::atoi(propKinds[i].c_str()) : 0;
			property.value = i < propValues.size() ? propValues[i]
				: std::string();
			property.reference = i < propRefs.size() ? propRefs[i]
				: std::string();
			result[objectIndex].properties.push_back(property);
		}
		return result;
	}
	//---------------------------------------------------------
	bool isMirrorVisualComponent(std::string const& componentKind)
	{
		// the VISUAL identity allowlist: what determines how an object looks in
		// the Scene view. Behavioral components (Script/RigidBody/Sound/...)
		// stay out - a stand-in must never simulate, and the editor never ticks.
		static char const* const VISUAL_KINDS[] = {
			"TransformComponent",
			"ModelComponent",
			"SpriteComponent",
			"SpriteAnimationComponent",
			"ParticleComponent",
			"VectorShapeComponent",
			"VectorAnimationComponent",
			"DecalComponent",
			"LightComponent",
			"WaterComponent",
		};
		for (char const* kind : VISUAL_KINDS)
		{
			if (componentKind == kind)
			{
				return true;
			}
		}
		return false;
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
			// move objects the authored scene holds AND the mirror stand-ins
			// (a runtime-spawned id becomes matchable once applySpawns created
			// its instance; until then it is skipped)
			if (this->mSnapshot.find(ids[i]) == this->mSnapshot.end() &&
				this->mMirrorInstances.find(ids[i]) ==
					this->mMirrorInstances.end())
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
	std::vector<std::string> PlayMirror::idsToQuery(MirrorScene& scene,
		std::vector<std::string> const& streamedIds)
	{
		this->beginIfNeeded(scene);
		std::vector<std::string> result;
		for (std::string const& id : streamedIds)
		{
			if (id.empty() || scene.hasObject(id) ||
				this->mSpawnsQueried.count(id) != 0)
			{
				continue;	// authored / already a stand-in / already asked
			}
			this->mSpawnsQueried.insert(id);
			result.push_back(id);
		}
		return result;
	}
	//---------------------------------------------------------
	void PlayMirror::applySpawns(MirrorScene& scene,
		std::vector<MirrorSpawnDesc> const& descriptors)
	{
		this->beginIfNeeded(scene);
		// parents-before-children across the batch: spawn what has a resolvable
		// (or no) parent first, repeat until a pass makes no progress, then
		// spawn the remainder as roots (an unresolvable parent never blocks the
		// stand-in itself - it just lands at the hierarchy root)
		std::vector<MirrorSpawnDesc const*> pending;
		for (MirrorSpawnDesc const& desc : descriptors)
		{
			pending.push_back(&desc);
		}
		bool requireParent = true;
		while (!pending.empty())
		{
			std::vector<MirrorSpawnDesc const*> deferred;
			bool progressed = false;
			for (MirrorSpawnDesc const* desc : pending)
			{
				if (desc->id.empty() || scene.hasObject(desc->id))
				{
					this->mSpawnsQueried.erase(desc->id);
					continue;	// raced with the streams - already there
				}
				if (requireParent && !desc->parent.empty() &&
					!scene.hasObject(desc->parent))
				{
					deferred.push_back(desc);	// its parent may spawn this batch
					continue;
				}
				if (scene.spawnObject(*desc))
				{
					this->mMirrorInstances.insert(desc->id);
					// a stand-in starts visible; the hierarchy stream's active
					// flags take over from here (applyActive)
					this->mVisibleBaseline[desc->id] = true;
					progressed = true;
				}
				this->mSpawnsQueried.erase(desc->id);	// answered
			}
			if (deferred.empty())
			{
				break;	// everything handled
			}
			if (!progressed)
			{
				// nothing new resolved a deferred parent: the next pass spawns
				// the remainder as roots (an absent parent never blocks the
				// stand-in itself)
				requireParent = false;
			}
			pending.swap(deferred);
		}
	}
	//---------------------------------------------------------
	void PlayMirror::pruneInstances(MirrorScene& scene,
		std::vector<std::string> const& streamedIds)
	{
		if (this->mMirrorInstances.empty())
		{
			return;
		}
		const std::set<std::string> live(streamedIds.begin(),
			streamedIds.end());
		for (std::set<std::string>::iterator it =
			this->mMirrorInstances.begin();
			it != this->mMirrorInstances.end();)
		{
			if (live.count(*it) != 0)
			{
				++it;
				continue;
			}
			// the running game destroyed it - the stand-in goes too, and the
			// id may be asked about again if it ever reappears
			scene.destroyObject(*it);
			this->mVisibleBaseline.erase(*it);
			this->mVisibilityChanged.erase(*it);
			this->mSpawnsQueried.erase(*it);
			it = this->mMirrorInstances.erase(it);
		}
	}
	//---------------------------------------------------------
	void PlayMirror::restore(MirrorScene& scene)
	{
		// the runtime-spawned stand-ins are DESTROYED, never restored - they
		// have no authored state to return to and must never reach a save
		for (std::string const& id : this->mMirrorInstances)
		{
			scene.destroyObject(id);
			this->mVisibleBaseline.erase(id);
			this->mVisibilityChanged.erase(id);
		}
		this->mMirrorInstances.clear();
		this->mSpawnsQueried.clear();
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
