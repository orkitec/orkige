/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	PrefabSerializer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/PrefabSerializer.h"
#include "core_game/SceneSerializer.h"
#include "core_project/AssetDatabase.h"
#include "core_serialization/XMLArchive.h"

#include <algorithm>
#include <filesystem>
#include <map>

namespace Orkige
{
	// version 1 (2026-07): magic, version, rootLocalId, objectCount, then per
	// object the scene v3 per-object block under prefab-LOCAL ids (parent
	// links local, prefabRef always "" - nested prefabs are refused)
	const int PrefabSerializer::PREFAB_FORMAT_VERSION = 1;
	const String PrefabSerializer::PREFAB_FORMAT_MAGIC = "orkige.oprefab";
	const String PrefabSerializer::INSTANCE_ID_SEPARATOR = "/";
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	bool PrefabSerializer::savePrefab(String const & fileName,
		GameObjectManager & gameObjectManager, String const & rootId)
	{
		oAssert(!fileName.empty());
		optr<GameObject> root = gameObjectManager.getGameObject(rootId).lock();
		if(!root)
		{
			oDebugMsg("scene",0,"PrefabSerializer: cannot save prefab - GameObject \""<<rootId<<"\" does not exist");
			return false;
		}
		const StringVector subtreeIds = gameObjectManager.collectSubtreeIds(rootId);
		oAssert(!subtreeIds.empty());

		// derive the deterministic prefab-local ids (root first) and refuse
		// nested prefabs: an instance root BELOW the saved root cannot be
		// represented in the v1 format. The saved root's OWN prefabRef is
		// deliberately ignored - overwriting an instance's source prefab is
		// the v1 "edit a prefab" loop.
		typedef std::map<String, String> LocalIdMap;	// object id -> prefab-local id
		LocalIdMap localIds;
		foreach(String const & objectId, subtreeIds)
		{
			optr<GameObject> gameObject = gameObjectManager.getGameObject(objectId).lock();
			oAssert(gameObject);
			if(objectId != rootId && !gameObject->getPrefabRef().empty())
			{
				oDebugMsg("scene",0,"PrefabSerializer: cannot save prefab \""<<fileName
					<<"\" - the subtree of \""<<rootId<<"\" contains the prefab instance \""
					<<objectId<<"\" (nested prefabs are not supported)");
				return false;
			}
			String localId = (objectId == rootId)
				? rootId : PrefabSerializer::localIdForChild(rootId, objectId);
			// stripping the root prefix must never collide two locals; fall
			// back to the (unique) full object id when it would
			bool collides = false;
			for(LocalIdMap::const_iterator it = localIds.begin(); it != localIds.end(); ++it)
			{
				if(it->second == localId)
				{
					collides = true;
					break;
				}
			}
			if(collides)
			{
				localId = objectId;
			}
			localIds[objectId] = localId;
		}

		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startWriting(fileName))
		{
			oDebugMsg("scene",0,"PrefabSerializer: could not start writing prefab file: "<<fileName);
			return false;
		}
		ar << PREFAB_FORMAT_MAGIC;
		int version = PREFAB_FORMAT_VERSION;
		ar << version;
		String rootLocalId = localIds[rootId];
		ar << rootLocalId;
		unsigned int objectCount = static_cast<unsigned int>(subtreeIds.size());
		ar << objectCount;
		foreach(String const & objectId, subtreeIds)
		{
			optr<GameObject> gameObject = gameObjectManager.getGameObject(objectId).lock();
			oAssert(gameObject);
			String localId = localIds[objectId];
			ar << localId;
			// parent links are LOCAL inside the file; the root is always a
			// root in the file (its scene parent is instance state)
			String parentLocalId;
			if(objectId != rootId)
			{
				LocalIdMap::const_iterator parentIt =
					localIds.find(gameObject->getParentId());
				oAssert(parentIt != localIds.end());
				parentLocalId = parentIt->second;
			}
			ar << parentLocalId;
			bool activeSelf = gameObject->isActiveSelf();
			ar << activeSelf;
			// the prefabRef slot of the scene v3 per-object block - always ""
			// in a prefab file (see the nested-prefab refusal above)
			String noPrefabRef;
			ar->writeAttributed(noPrefabRef,
				AssetDatabase::REFERENCE_ID_ATTRIBUTE, String());
			SceneSerializer::writeComponents(ar, *gameObject);
		}
		bool written = ar->stopWriting();
		if(!written)
		{
			oDebugMsg("scene",0,"PrefabSerializer: error while writing prefab file: "<<fileName);
		}
		return written;
	}
	//---------------------------------------------------------
	PrefabSerializer::InstantiateResult PrefabSerializer::instantiatePrefab(
		String const & fileName, GameObjectManager & gameObjectManager,
		String const & instanceRootId, StringVector const & suppressedChildren)
	{
		oAssert(!fileName.empty());
		oAssert(!instanceRootId.empty());
		std::error_code ignored;
		if(!std::filesystem::exists(fileName, ignored))
		{
			return INSTANTIATE_FILE_MISSING;
		}
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startReading(fileName))
		{
			oDebugMsg("scene",0,"PrefabSerializer: could not open prefab file: "<<fileName);
			return INSTANTIATE_ERROR;
		}
		String magic;
		ar >> magic;
		if(magic != PREFAB_FORMAT_MAGIC)
		{
			oDebugMsg("scene",0,"PrefabSerializer: "<<fileName<<" is not an orkige prefab file (magic: \""<<magic<<"\")");
			ar->stopReading();
			return INSTANTIATE_ERROR;
		}
		int version = 0;
		ar >> version;
		if(version > PREFAB_FORMAT_VERSION)
		{
			oDebugMsg("scene",0,"PrefabSerializer: prefab file "<<fileName<<" has unsupported version "<<version<<" (supported: "<<PREFAB_FORMAT_VERSION<<")");
			ar->stopReading();
			return INSTANTIATE_ERROR;
		}
		String rootLocalId;
		ar >> rootLocalId;
		unsigned int objectCount = 0;
		ar >> objectCount;

		// the instance root may already exist (the scene loader creates it
		// before it knows the object is an instance); everything else is
		// created fresh in the "<instanceRootId>/" namespace
		bool loaded = true;
		StringVector createdIds;	// fresh objects, for the error rollback
		typedef std::pair<String, String> ParentLink;	// instance id -> instance parent id
		std::vector<ParentLink> parentLinks;
		typedef std::pair<String, bool> ActiveState;	// instance id -> activeSelf
		std::vector<ActiveState> activeStates;
		for(unsigned int objectIndex = 0; objectIndex < objectCount && loaded; ++objectIndex)
		{
			String localId;
			ar >> localId;
			String parentLocalId;
			ar >> parentLocalId;
			bool activeSelf = true;
			ar >> activeSelf;
			String nestedPrefabRef;
			String nestedPrefabAssetId;
			ar->readAttributed(nestedPrefabRef,
				AssetDatabase::REFERENCE_ID_ATTRIBUTE, nestedPrefabAssetId);
			if(!nestedPrefabRef.empty())
			{
				// nested prefabs are forbidden in v1 - a hard error by design
				// (savePrefab refuses to produce such a file; this guards
				// hand-edited ones)
				oDebugMsg("scene",0,"PrefabSerializer: prefab file "<<fileName
					<<" contains a nested prefab reference (\""<<localId<<"\" -> \""
					<<nestedPrefabRef<<"\") - nested prefabs are not supported");
				loaded = false;
				break;
			}
			const bool isRoot = (localId == rootLocalId);
			const String instanceId = isRoot
				? instanceRootId
				: PrefabSerializer::instanceChildId(instanceRootId, localId);
			optr<GameObject> gameObject = gameObjectManager.objectExists(instanceId)
				? gameObjectManager.getGameObject(instanceId).lock()
				: gameObjectManager.createGameObject(instanceId).lock();
			if(!gameObject)
			{
				oDebugMsg("scene",0,"PrefabSerializer: could not create GameObject \""<<instanceId<<"\" for prefab: "<<fileName);
				loaded = false;
				break;
			}
			if(!isRoot)
			{
				createdIds.push_back(instanceId);
				oAssert(!parentLocalId.empty());
				parentLinks.push_back(ParentLink(instanceId, (parentLocalId == rootLocalId)
					? instanceRootId
					: PrefabSerializer::instanceChildId(instanceRootId, parentLocalId)));
			}
			if(!activeSelf)
			{
				activeStates.push_back(ActiveState(instanceId, activeSelf));
			}
			if(!SceneSerializer::readComponents(ar, gameObject, fileName))
			{
				loaded = false;
				break;
			}
			// capture the PRISTINE per-component state of every prefab-provided
			// child right after it is read (before any scene-side override is
			// overlaid): this baseline is what SceneSerializer::saveScene diffs
			// the live child against so an unmodified child stores no override
			// (the root is not a provided child - its own block is the v1 root
			// override, handled by the scene loader)
			if(!isRoot)
			{
				GameObject::ComponentStateMap baseline;
				GameObject::ComponentMap const & childComponents = gameObject->getComponents();
				foreach(GameObject::ComponentMap::value_type const & componentEntry, childComponents)
				{
					baseline[componentEntry.first.getName()] =
						SceneSerializer::serializeComponentState(*componentEntry.second);
				}
				gameObject->setPrefabComponentBaseline(baseline);
			}
		}
		ar->stopReading();

		if(loaded)
		{
			// parent pass (deferred like the scene loader's: a parent may
			// appear later in the file; serialized transforms are LOCAL)
			foreach(ParentLink const & link, parentLinks)
			{
				optr<GameObject> child = gameObjectManager.getGameObject(link.first).lock();
				oAssert(child);
				if(!child->setParent(link.second, false))
				{
					oDebugMsg("scene",0,"PrefabSerializer: could not parent \""<<link.first<<"\" to \""<<link.second<<"\" (prefab "<<fileName<<")");
					loaded = false;
					break;
				}
			}
		}
		if(loaded)
		{
			// drop the suppressed children (structural overrides) SUBTREE-DEEP
			// before the active pass; an id the prefab does not provide is
			// logged and kept in the instance's list (it may heal on a later
			// prefab edit - same keep-the-data stance as a missing prefab)
			foreach(String const & suppressedLocalId, suppressedChildren)
			{
				const String suppressedId =
					PrefabSerializer::instanceChildId(instanceRootId, suppressedLocalId);
				if(!gameObjectManager.objectExists(suppressedId))
				{
					oDebugMsg("scene",0,"PrefabSerializer: suppressed child \""<<suppressedLocalId
						<<"\" does not exist in prefab "<<fileName<<" - ignored");
					continue;
				}
				const StringVector doomed = gameObjectManager.collectSubtreeIds(suppressedId);
				for(StringVector::const_reverse_iterator it = doomed.rbegin(); it != doomed.rend(); ++it)
				{
					gameObjectManager.delGameObject(*it);
					createdIds.erase(std::remove(createdIds.begin(), createdIds.end(), *it), createdIds.end());
				}
			}
			// active pass AFTER parenting, like the scene loader
			foreach(ActiveState const & state, activeStates)
			{
				if(optr<GameObject> gameObject = gameObjectManager.getGameObject(state.first).lock())
				{
					gameObject->setActive(state.second);
				}
			}
			return INSTANTIATE_OK;
		}
		// error rollback: remove the children this call created (deepest
		// first); the root object belongs to the caller
		for(StringVector::const_reverse_iterator it = createdIds.rbegin(); it != createdIds.rend(); ++it)
		{
			gameObjectManager.delGameObject(*it);
		}
		return INSTANTIATE_ERROR;
	}
	//---------------------------------------------------------
	String PrefabSerializer::instanceChildId(String const & instanceRootId,
		String const & localId)
	{
		return instanceRootId + INSTANCE_ID_SEPARATOR + localId;
	}
	//---------------------------------------------------------
	bool PrefabSerializer::isInstanceChildId(String const & instanceRootId,
		String const & id)
	{
		const String prefix = instanceRootId + INSTANCE_ID_SEPARATOR;
		return id.size() > prefix.size() && id.compare(0, prefix.size(), prefix) == 0;
	}
	//---------------------------------------------------------
	String PrefabSerializer::localIdForChild(String const & rootId,
		String const & childId)
	{
		if(childId.size() > rootId.size() && childId.compare(0, rootId.size(), rootId) == 0)
		{
			String local = childId.substr(rootId.size());
			// eat ONE separator character between the root prefix and the
			// local part ("TileA_Frame" -> "Frame", "TileA/Frame" -> "Frame")
			if(!local.empty() && (local[0] == '/' || local[0] == '_' ||
				local[0] == '.' || local[0] == '-' || local[0] == ' '))
			{
				local.erase(0, 1);
			}
			if(!local.empty())
			{
				return local;
			}
		}
		return childId;
	}
	//---------------------------------------------------------
	bool PrefabSerializer::isPrefabProvided(GameObjectManager & gameObjectManager,
		GameObject const & gameObject)
	{
		return !PrefabSerializer::instanceRootIdOf(gameObjectManager, gameObject).empty();
	}
	//---------------------------------------------------------
	String PrefabSerializer::instanceRootIdOf(GameObjectManager & gameObjectManager,
		GameObject const & gameObject)
	{
		String const & id = gameObject.getObjectID();
		// walk the ancestor chain: provided means SOME ancestor is an
		// instance root AND the id lies inside that ancestor's namespace
		// (extra children under an instance root live outside it)
		String ancestorId = gameObject.getParentId();
		std::size_t guard = gameObjectManager.getGameObjects().size() + 1;
		while(!ancestorId.empty() && guard-- > 0)
		{
			optr<GameObject> ancestor = gameObjectManager.getGameObject(ancestorId).lock();
			if(!ancestor)
			{
				break;
			}
			if(!ancestor->getPrefabRef().empty() &&
				PrefabSerializer::isInstanceChildId(ancestorId, id))
			{
				return ancestorId;
			}
			ancestorId = ancestor->getParentId();
		}
		return String();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
