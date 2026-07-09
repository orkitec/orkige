/**************************************************************
	created:	2026/07/07 at 22:10
	filename: 	SceneSerializer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/SceneSerializer.h"
#include "core_game/PrefabSerializer.h"
#include "core_project/AssetDatabase.h"
#include "core_serialization/XMLArchive.h"

#include <filesystem>

namespace Orkige
{
	// version 4 (2026-07): per-object tag list (free-form multi-tag labels,
	// GameObjectManager tag index); version 3 added the per-object prefabRef
	// (+assetId attribute, like every asset reference) and, on prefab instance
	// roots, the list of suppressed prefab children (structural overrides - see
	// core_game/PrefabSerializer.h); version 2 added the per-object parent id +
	// activeSelf flag (Unity-style GameObject tree); version 1 scenes load as
	// all-root, all-active, prefab-less, tag-less worlds
	const int SceneSerializer::SCENE_FORMAT_VERSION = 4;
	const String SceneSerializer::SCENE_FORMAT_MAGIC = "orkige.oscene";
	//---------------------------------------------------------
	namespace
	{
		//! @brief resolve a project-relative prefabRef to a loadable file
		//! path: through the active AssetDatabase's project root when there
		//! is one, otherwise relative to the scene file's directory and (the
		//! standard <project>/scenes/ + <project>/assets/ layout) to its
		//! parent. "" when nothing exists - the caller creates a placeholder.
		String resolvePrefabPath(String const & prefabRef, String const & sceneFileName)
		{
			namespace fs = std::filesystem;
			std::error_code ignored;
			std::vector<fs::path> candidates;
			if(optr<AssetDatabase> const & database = AssetDatabase::getActive())
			{
				if(!database->getRootDirectory().empty())
				{
					candidates.push_back(fs::path(database->getRootDirectory()) / prefabRef);
				}
			}
			const fs::path sceneDirectory = fs::path(sceneFileName).parent_path();
			candidates.push_back(sceneDirectory / prefabRef);
			candidates.push_back(sceneDirectory.parent_path() / prefabRef);
			foreach(fs::path const & candidate, candidates)
			{
				if(fs::exists(candidate, ignored))
				{
					return candidate.string();
				}
			}
			return String();
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	bool SceneSerializer::saveScene(String const & fileName, GameObjectManager & gameObjectManager)
	{
		oAssert(!fileName.empty());
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startWriting(fileName))
		{
			oDebugMsg("scene",0,"SceneSerializer: could not start writing scene file: "<<fileName);
			return false;
		}

		ar << SCENE_FORMAT_MAGIC;
		int version = SCENE_FORMAT_VERSION;
		ar << version;

		// prefab-PROVIDED objects (the "<instanceRoot>/" id namespace) are
		// NOT serialized - they are reconstructed from their .oprefab at
		// load time; only the instance root (with its overrides) and the
		// scene-side extra children ride in the scene file
		GameObjectManager::GameObjectMap const & objects = gameObjectManager.getGameObjects();
		std::vector< optr<GameObject> > savedObjects;
		savedObjects.reserve(objects.size());
		foreach(GameObjectManager::GameObjectMap::value_type const & objectEntry, objects)
		{
			optr<GameObject> gameObject = objectEntry.second;
			oAssert(gameObject);
			if(!PrefabSerializer::isPrefabProvided(gameObjectManager, *gameObject))
			{
				savedObjects.push_back(gameObject);
			}
		}
		unsigned int objectCount = static_cast<unsigned int>(savedObjects.size());
		ar << objectCount;

		foreach(optr<GameObject> const & gameObject, savedObjects)
		{
			String id = gameObject->getObjectID();
			ar << id;

			// v2: the hierarchy fields ("" = root; the parent reference is the
			// object id, like every other object reference in the format)
			String parentId = gameObject->getParentId();
			ar << parentId;
			bool activeSelf = gameObject->isActiveSelf();
			ar << activeSelf;

			// v4: the free-form tag list (ids into the manager's tag index)
			StringVector const & tags = gameObject->getTags();
			unsigned int tagCount = static_cast<unsigned int>(tags.size());
			ar << tagCount;
			foreach(String const & tag, tags)
			{
				String tagValue = tag;
				ar << tagValue;
			}

			// v3: the prefab reference ("" on plain objects) with its stable
			// asset id riding as a side attribute (the same rename-survival
			// plumbing every asset reference uses - see ModelComponent)
			String const & prefabRef = gameObject->getPrefabRef();
			ar->writeAttributed(prefabRef,
				AssetDatabase::REFERENCE_ID_ATTRIBUTE,
				AssetDatabase::referenceIdForValue(prefabRef,
					gameObject->getPrefabAssetId(), AssetDatabase::REF_PROJECT_PATH));
			if(!prefabRef.empty())
			{
				// structural overrides: the prefab-LOCAL ids this instance drops
				StringVector const & suppressed = gameObject->getSuppressedPrefabChildren();
				unsigned int suppressedCount = static_cast<unsigned int>(suppressed.size());
				ar << suppressedCount;
				foreach(String const & localId, suppressed)
				{
					String suppressedId = localId;
					ar << suppressedId;
				}
			}

			// the instance ROOT's own component block doubles as its override
			// state: at load it overlays the prefab defaults
			SceneSerializer::writeComponents(ar, *gameObject);
		}

		bool written = ar->stopWriting();
		if(!written)
		{
			oDebugMsg("scene",0,"SceneSerializer: error while writing scene file: "<<fileName);
		}
		return written;
	}
	//---------------------------------------------------------
	bool SceneSerializer::loadScene(String const & fileName, GameObjectManager & gameObjectManager)
	{
		oAssert(!fileName.empty());
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startReading(fileName))
		{
			oDebugMsg("scene",0,"SceneSerializer: could not open scene file: "<<fileName);
			return false;
		}

		String magic;
		ar >> magic;
		if(magic != SCENE_FORMAT_MAGIC)
		{
			oDebugMsg("scene",0,"SceneSerializer: "<<fileName<<" is not an orkige scene file (magic: \""<<magic<<"\")");
			ar->stopReading();
			return false;
		}
		int version = 0;
		ar >> version;
		if(version > SCENE_FORMAT_VERSION)
		{
			oDebugMsg("scene",0,"SceneSerializer: scene file "<<fileName<<" has unsupported version "<<version<<" (supported: "<<SCENE_FORMAT_VERSION<<")");
			ar->stopReading();
			return false;
		}

		// replace the current world; component removal tears down the
		// scene-side state (e.g. TransformComponent destroys its scene nodes)
		gameObjectManager.clear();

		bool loaded = true;
		unsigned int objectCount = 0;
		ar >> objectCount;
		// hierarchy fields are applied AFTER the object loop: a parent may
		// appear later in the file than its children, and the components
		// (transforms especially) must exist before the links re-attach nodes
		typedef std::pair<String, String> ParentLink;		// object id -> parent id
		std::vector<ParentLink> parentLinks;
		typedef std::pair<String, bool> ActiveState;		// object id -> activeSelf
		std::vector<ActiveState> activeStates;
		for(unsigned int objectIndex = 0; objectIndex < objectCount && loaded; ++objectIndex)
		{
			String id;
			ar >> id;
			optr<GameObject> gameObject = gameObjectManager.createGameObject(id).lock();
			if(!gameObject)
			{
				oDebugMsg("scene",0,"SceneSerializer: could not create GameObject: "<<id);
				loaded = false;
				break;
			}

			if(version >= 2)
			{
				String parentId;
				ar >> parentId;
				bool activeSelf = true;
				ar >> activeSelf;
				if(!parentId.empty())
				{
					parentLinks.push_back(ParentLink(id, parentId));
				}
				if(!activeSelf)
				{
					activeStates.push_back(ActiveState(id, activeSelf));
				}
			}

			if(version >= 4)
			{
				// tags apply immediately (independent of the hierarchy links
				// resolved after the loop); setTags registers them in the
				// manager's tag index
				StringVector tags;
				unsigned int tagCount = 0;
				ar >> tagCount;
				for(unsigned int tagIndex = 0; tagIndex < tagCount; ++tagIndex)
				{
					String tag;
					ar >> tag;
					tags.push_back(tag);
				}
				if(!tags.empty())
				{
					gameObject->setTags(tags);
				}
			}

			if(version >= 3)
			{
				String prefabRef;
				String prefabAssetId;
				ar->readAttributed(prefabRef,
					AssetDatabase::REFERENCE_ID_ATTRIBUTE, prefabAssetId);
				// a resolving asset id wins over a stale path (rename
				// survival); scenes without ids keep loading via the path
				AssetDatabase::resolveReference(prefabRef, prefabAssetId,
					AssetDatabase::REF_PROJECT_PATH);
				if(!prefabRef.empty())
				{
					StringVector suppressed;
					unsigned int suppressedCount = 0;
					ar >> suppressedCount;
					for(unsigned int suppressedIndex = 0; suppressedIndex < suppressedCount; ++suppressedIndex)
					{
						String localId;
						ar >> localId;
						suppressed.push_back(localId);
					}
					// the instance keeps its reference and overrides even when
					// the prefab file cannot be found right now (Unity-style:
					// a re-save must not strip the link, a later load with the
					// asset back in place heals the instance)
					gameObject->setPrefabRef(prefabRef, prefabAssetId);
					gameObject->setSuppressedPrefabChildren(suppressed);

					// the prefab subtree comes FIRST; the root's own component
					// block (read below) then overlays the prefab defaults
					const String prefabPath = resolvePrefabPath(prefabRef, fileName);
					if(prefabPath.empty())
					{
						oDebugMsg("scene",0,"SceneSerializer: PREFAB MISSING - \""<<prefabRef
							<<"\" (instance \""<<id<<"\" in "<<fileName
							<<") could not be found; the instance loads as a PLACEHOLDER root "
							"keeping its reference and overrides");
					}
					else
					{
						const PrefabSerializer::InstantiateResult result =
							PrefabSerializer::instantiatePrefab(prefabPath,
								gameObjectManager, id, suppressed);
						if(result == PrefabSerializer::INSTANTIATE_FILE_MISSING)
						{
							// raced away between resolve and open - same placeholder policy
							oDebugMsg("scene",0,"SceneSerializer: PREFAB MISSING - \""<<prefabPath
								<<"\" vanished while loading "<<fileName
								<<"; instance \""<<id<<"\" loads as a placeholder root");
						}
						else if(result != PrefabSerializer::INSTANTIATE_OK)
						{
							// corrupt or nested prefab: a hard error by design
							oDebugMsg("scene",0,"SceneSerializer: could not instantiate prefab \""
								<<prefabPath<<"\" for instance \""<<id<<"\" - cannot load scene: "<<fileName);
							loaded = false;
							break;
						}
					}
				}
			}

			if(!SceneSerializer::readComponents(ar, gameObject, fileName))
			{
				loaded = false;
				break;
			}
		}

		if(loaded)
		{
			// apply the hierarchy: keepWorldTransform=false because the
			// serialized transforms ARE the local transforms (identical to
			// world for roots - which keeps version 1 scenes semantically
			// untouched). A refused link (missing parent, cycle in a
			// hand-edited file) is logged by setParent; the object stays a
			// root rather than failing the whole scene.
			foreach(ParentLink const & link, parentLinks)
			{
				optr<GameObject> child = gameObjectManager.getGameObject(link.first).lock();
				oAssert(child);
				if(!child->setParent(link.second, false))
				{
					oDebugMsg("scene",0,"SceneSerializer: could not parent GameObject "<<link.first<<" to "<<link.second<<" - it stays a root");
				}
			}
			// deactivate AFTER parenting so the effective state propagates
			// through the final tree (components get their onSetActive)
			foreach(ActiveState const & state, activeStates)
			{
				optr<GameObject> gameObject = gameObjectManager.getGameObject(state.first).lock();
				oAssert(gameObject);
				gameObject->setActive(state.second);
			}
		}

		ar->stopReading();
		if(!loaded)
		{
			// don't leave a half-loaded world behind
			gameObjectManager.clear();
		}
		return loaded;
	}
	//---------------------------------------------------------
	void SceneSerializer::writeComponents(optr<XMLArchive> const & ar, GameObject & gameObject)
	{
		GameObject::ComponentMap const & components = gameObject.getComponents();
		unsigned int componentCount = static_cast<unsigned int>(components.size());
		ar << componentCount;
		foreach(GameObject::ComponentMap::value_type const & componentEntry, components)
		{
			String componentTypeName = componentEntry.first.getName();
			ar << componentTypeName;
			// writes an element named after the component type whose
			// children are the component's serialized state
			ar->write(static_cast<ISerializeable&>(*componentEntry.second));
		}
	}
	//---------------------------------------------------------
	bool SceneSerializer::readComponents(optr<XMLArchive> const & ar,
		optr<GameObject> const & gameObject, String const & fileName)
	{
		oAssert(gameObject);
		unsigned int componentCount = 0;
		ar >> componentCount;
		for(unsigned int componentIndex = 0; componentIndex < componentCount; ++componentIndex)
		{
			String componentTypeName;
			ar >> componentTypeName;
			TypeInfo componentType(componentTypeName);
			if(!GameObject::isComponentRegistered(componentType))
			{
				// the archive cursor cannot skip the unknown component's
				// state element, so this is a hard error by design
				oDebugMsg("scene",0,"SceneSerializer: component type \""<<componentTypeName<<"\" is not registered - cannot load: "<<fileName);
				return false;
			}
			// dependencies may already have added this component
			// (e.g. ModelComponent pulls in TransformComponent) - and on a
			// prefab instance ROOT the components already exist as prefab
			// defaults; reading overlays the serialized (override) state
			if(!gameObject->hasComponent(componentType))
			{
				if(!gameObject->addComponent(componentType))
				{
					oDebugMsg("scene",0,"SceneSerializer: could not add component \""<<componentTypeName<<"\" to GameObject: "<<gameObject->getObjectID());
					return false;
				}
			}
			GameObjectComponent* component = gameObject->getComponentPtr(componentType);
			oAssert(component);
			// reads the component element and calls component->load
			// (GameObjectComponent::createBeforeLoad is false)
			ar->read(static_cast<ISerializeable&>(*component));
		}
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
