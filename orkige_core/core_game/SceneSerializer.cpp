/**************************************************************
	created:	2026/07/07 at 22:10
	filename: 	SceneSerializer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/SceneSerializer.h"
#include "core_game/GameObjectComponent.h"
#include "core_game/PrefabSerializer.h"
#include "core_project/AssetDatabase.h"
#include "core_serialization/XMLArchive.h"
#include "core_base/TypeManager.h"
#include "core_base/PropertyValue.h"
#include "core_base/PropertySchema.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <vector>

namespace Orkige
{
	// version 5 (2026-07): per prefab-instance-root, per prefab-PROVIDED child,
	// component-property OVERRIDES (prefab-local id -> component type name ->
	// the component's serialized state, diffed against the pristine prefab
	// default so unmodified children store nothing; re-applied over the prefab
	// on load - see GameObject::getPrefabChildOverrides); version 4 added the
	// per-object tag list (free-form multi-tag labels, GameObjectManager tag
	// index); version 3 added the per-object prefabRef (+assetId attribute, like
	// every asset reference) and, on prefab instance roots, the list of
	// suppressed prefab children (structural overrides - see
	// core_game/PrefabSerializer.h); version 2 added the per-object parent id +
	// activeSelf flag (Unity-style GameObject tree); version 1 scenes load as
	// all-root, all-active, prefab-less, tag-less worlds
	// version 6 (2026-07, task #94 P2): reflection-driven NAMED component
	// serialization. Each component's declared PropertySchema (TypeManager) is
	// written as a name->value field block instead of a positional field list,
	// so reordering/adding/removing a component field no longer needs an archive
	// version gate - a clean single current format with no positional-reader
	// fallback (the owner keeps no legacy scenes; every asset was rewritten).
	// The per-object envelope (id, parent, activeSelf, tags, prefabRef +
	// suppressed children + per-child overrides, component list) stays
	// hand-written/structured - reflection covers component FIELDS, not the
	// object graph.
	// version 7 (2026-07): PER-PROPERTY prefab overrides. A prefab child override
	// used to store the whole opaque component block (the honest unit under
	// opaque serialization); reflection makes it the subset of NAMED fields whose
	// live value differs from the prefab default. The on-disk override block
	// changed shape (child -> component -> a count of
	// {propertyName, kind, value, reference} records instead of one
	// component-block string), so the format-era marker bumps. Clean cutover -
	// every scene asset was rewritten, no legacy override reader survives.
	const int SceneSerializer::SCENE_FORMAT_VERSION = 7;
	const String SceneSerializer::SCENE_FORMAT_MAGIC = "orkige.oscene";
	//---------------------------------------------------------
	namespace
	{
		//! @brief which AssetDatabase reference flavour a reflected AssetRef
		//! property uses, decided by its asset-kind hint: script paths are
		//! project-relative, every other asset (mesh/texture/sound) is a bare
		//! resource file name (@see AssetDatabase::ReferenceKind).
		AssetDatabase::ReferenceKind refKindForHint(String const & hint)
		{
			return (hint == "script")
				? AssetDatabase::REF_PROJECT_PATH
				: AssetDatabase::REF_FILE_NAME;
		}
		//! @brief a default-constructed PropertyValue of a descriptor's kind, so
		//! PropertyValue::fromString has the right variant tag (and enum/reference
		//! hint) to parse the stored string into.
		PropertyValue defaultValueForDesc(PropertyDesc const & desc)
		{
			switch(desc.kind)
			{
			case PropertyKind::Int:		return PropertyValue::makeInt(0);
			case PropertyKind::Float:	return PropertyValue::makeFloat(0.0);
			case PropertyKind::Bool:	return PropertyValue::makeBool(false);
			case PropertyKind::String:	return PropertyValue::makeString("");
			case PropertyKind::Enum:	return PropertyValue::makeEnum(desc.enumTypeName, 0);
			case PropertyKind::Vec3:	return PropertyValue::makeVec3(PropVec3());
			case PropertyKind::Quat:	return PropertyValue::makeQuat(PropQuat());
			case PropertyKind::Color:	return PropertyValue::makeColor(PropColor());
			case PropertyKind::AssetRef:	return PropertyValue::makeAssetRef(desc.referenceHint, "");
			case PropertyKind::ObjectRef:	return PropertyValue::makeObjectRef(desc.referenceHint, "");
			default:					return PropertyValue::makeString("");
			}
		}
		//! @brief is a declared property actually serialized: skip transient
		//! (runtime-only) state and computed/read-only properties (no setter to
		//! restore them through)
		bool isSerializedProperty(PropertyDesc const & desc)
		{
			return !desc.hasFlag(PROP_TRANSIENT) && static_cast<bool>(desc.set);
		}
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
		//! @brief diff every prefab-PROVIDED child of the instance root against
		//! its instantiate-time baseline and collect only the reflected
		//! PROPERTIES that differ. An override is the subset of a
		//! component's named fields whose live value differs from the pristine
		//! prefab default - an unmodified property (and an unmodified component,
		//! and an unmodified child) stores NOTHING. A component with no baseline
		//! (an editor-added component the prefab does not provide) counts as an
		//! override of ALL its properties; a suppressed child is skipped (it is
		//! gone anyway). Order is deterministic (std::map keys), so re-saves are
		//! byte-stable.
		GameObject::ChildOverrideMap computeChildOverrides(
			GameObjectManager & gameObjectManager, GameObject & root)
		{
			GameObject::ChildOverrideMap overrides;
			String const & rootId = root.getObjectID();
			StringVector const & suppressed = root.getSuppressedPrefabChildren();
			const StringVector subtreeIds = gameObjectManager.collectSubtreeIds(rootId);
			foreach(String const & childId, subtreeIds)
			{
				if(childId == rootId ||
					!PrefabSerializer::isInstanceChildId(rootId, childId))
				{
					// the root itself and scene-side extra children (outside the
					// "<rootId>/" namespace) are serialized normally, not diffed
					continue;
				}
				optr<GameObject> child = gameObjectManager.getGameObject(childId).lock();
				if(!child)
				{
					continue;
				}
				const String localId = PrefabSerializer::localIdForChild(rootId, childId);
				if(std::find(suppressed.begin(), suppressed.end(), localId) != suppressed.end())
				{
					continue;
				}
				GameObject::ComponentStateMap const & baseline = child->getPrefabComponentBaseline();
				GameObject::ComponentStateMap changed;
				GameObject::ComponentMap const & components = child->getComponents();
				foreach(GameObject::ComponentMap::value_type const & componentEntry, components)
				{
					String const & typeName = componentEntry.first.getName();
					GameObject::ComponentPropertyMap live =
						SceneSerializer::captureComponentProperties(*componentEntry.second);
					GameObject::ComponentStateMap::const_iterator baseIt = baseline.find(typeName);
					GameObject::ComponentPropertyMap changedProperties;
					if(baseIt == baseline.end())
					{
						// no baseline: an editor-added component the prefab does not
						// provide - every one of its properties is an override
						changedProperties = live;
					}
					else
					{
						// keep ONLY the properties whose live value differs from the
						// pristine prefab default (or that the baseline lacks)
						GameObject::ComponentPropertyMap const & baseProperties = baseIt->second;
						foreach(GameObject::ComponentPropertyMap::value_type const & propertyEntry, live)
						{
							GameObject::ComponentPropertyMap::const_iterator baseProperty =
								baseProperties.find(propertyEntry.first);
							if(baseProperty == baseProperties.end() ||
								baseProperty->second != propertyEntry.second)
							{
								changedProperties[propertyEntry.first] = propertyEntry.second;
							}
						}
					}
					if(!changedProperties.empty())
					{
						changed[typeName] = changedProperties;
					}
				}
				if(!changed.empty())
				{
					overrides[localId] = changed;
				}
			}
			return overrides;
		}
		//! @brief re-apply the loaded per-child overrides over the freshly
		//! instantiated prefab subtree (load order: prefab subtree -> suppressed
		//! drop -> HERE -> root overlay). A child that no longer exists (a
		//! prefab that dropped it, or a suppressed one) is left logged; its
		//! override stays in the instance's map so a later heal re-applies it.
		void applyChildOverrides(GameObjectManager & gameObjectManager,
			String const & instanceRootId,
			GameObject::ChildOverrideMap const & overrides, String const & fileName)
		{
			foreach(GameObject::ChildOverrideMap::value_type const & childEntry, overrides)
			{
				const String childId =
					PrefabSerializer::instanceChildId(instanceRootId, childEntry.first);
				optr<GameObject> child = gameObjectManager.getGameObject(childId).lock();
				if(!child)
				{
					oDebugMsg("scene",0,"SceneSerializer: prefab child override for \""
						<<childEntry.first<<"\" (instance \""<<instanceRootId<<"\" in "
						<<fileName<<") has no target child - kept for a later heal");
					continue;
				}
				foreach(GameObject::ComponentStateMap::value_type const & componentEntry, childEntry.second)
				{
					TypeInfo componentType(componentEntry.first);
					if(!GameObject::isComponentRegistered(componentType))
					{
						oDebugMsg("scene",0,"SceneSerializer: prefab child override component \""
							<<componentEntry.first<<"\" is not registered - ignored");
						continue;
					}
					if(!child->hasComponent(componentType) &&
						!child->addComponent(componentType))
					{
						continue;
					}
					GameObjectComponent* component = child->getComponentPtr(componentType);
					if(component)
					{
						SceneSerializer::applyComponentProperties(componentEntry.second, *component);
					}
				}
			}
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

				// v5: per prefab-provided-child component-property overrides.
				// When the prefab resolves, diff every provided child's live
				// components against the pristine baseline captured at
				// instantiate (an unmodified component stores NOTHING); when it
				// does not (a placeholder instance), keep the overrides loaded
				// from the scene so a re-save loses none of them (Unity-style)
				GameObject::ChildOverrideMap overrides;
				const String prefabPath = resolvePrefabPath(prefabRef, fileName);
				if(!prefabPath.empty())
				{
					overrides = computeChildOverrides(gameObjectManager, *gameObject);
					gameObject->setPrefabChildOverrides(overrides);
				}
				else
				{
					overrides = gameObject->getPrefabChildOverrides();
				}
				unsigned int overrideChildCount = static_cast<unsigned int>(overrides.size());
				ar << overrideChildCount;
				foreach(GameObject::ChildOverrideMap::value_type const & childEntry, overrides)
				{
					String childLocalId = childEntry.first;
					ar << childLocalId;
					unsigned int componentOverrideCount =
						static_cast<unsigned int>(childEntry.second.size());
					ar << componentOverrideCount;
					foreach(GameObject::ComponentStateMap::value_type const & componentEntry, childEntry.second)
					{
						String componentTypeName = componentEntry.first;
						ar << componentTypeName;
						// v7: only the CHANGED properties (name, kind, value,
						// reference) instead of a whole opaque component block
						unsigned int propertyOverrideCount =
							static_cast<unsigned int>(componentEntry.second.size());
						ar << propertyOverrideCount;
						foreach(GameObject::ComponentPropertyMap::value_type const & propertyEntry, componentEntry.second)
						{
							String propertyName = propertyEntry.first;
							ar << propertyName;
							int kind = propertyEntry.second.kind;
							ar << kind;
							String value = propertyEntry.second.value;
							ar << value;
							String reference = propertyEntry.second.reference;
							ar << reference;
						}
					}
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
		// clean single current format (task #94 P2): no positional-reader
		// fallback, no per-version field gates - only the current version loads
		if(version != SCENE_FORMAT_VERSION)
		{
			oDebugMsg("scene",0,"SceneSerializer: scene file "<<fileName<<" has unsupported version "<<version<<" (this build reads only version "<<SCENE_FORMAT_VERSION<<")");
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

			// the per-object envelope (hierarchy, tags, prefab reference) is
			// always present in the current format - no version gates
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

					// the per-child component-property overrides ride here in the
					// file (must be consumed even for a placeholder so the cursor
					// stays aligned); they are re-APPLIED after the prefab subtree
					// instantiates (load order: subtree -> suppressed drop -> child
					// overrides -> root overlay). v7: an override is the subset of
					// CHANGED named properties, not a whole component block
					GameObject::ChildOverrideMap overrides;
					{
						unsigned int overrideChildCount = 0;
						ar >> overrideChildCount;
						for(unsigned int childIndex = 0; childIndex < overrideChildCount; ++childIndex)
						{
							String localId;
							ar >> localId;
							unsigned int componentOverrideCount = 0;
							ar >> componentOverrideCount;
							GameObject::ComponentStateMap componentStates;
							for(unsigned int componentIndex = 0; componentIndex < componentOverrideCount; ++componentIndex)
							{
								String componentTypeName;
								ar >> componentTypeName;
								unsigned int propertyOverrideCount = 0;
								ar >> propertyOverrideCount;
								GameObject::ComponentPropertyMap properties;
								for(unsigned int propertyIndex = 0; propertyIndex < propertyOverrideCount; ++propertyIndex)
								{
									String propertyName;
									ar >> propertyName;
									GameObject::ComponentPropertyRecord record;
									ar >> record.kind;
									ar >> record.value;
									ar >> record.reference;
									properties[propertyName] = record;
								}
								componentStates[componentTypeName] = properties;
							}
							overrides[localId] = componentStates;
						}
						gameObject->setPrefabChildOverrides(overrides);
					}

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
						else
						{
							// the prefab-provided children now exist (minus the
							// suppressed): overlay the per-child overrides before
							// the root's own block reads below
							applyChildOverrides(gameObjectManager, id, overrides, fileName);
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
	GameObject::ComponentPropertyMap SceneSerializer::captureComponentProperties(
		GameObjectComponent & component)
	{
		// mirrors saveComponentProperties' per-field capture (the same schema,
		// the same transient/read-only skip, the same AssetRef id resolution) but
		// into an in-memory name->record map instead of onto an archive - so a
		// prefab baseline and a live child speak the identical per-property
		// dialect and diff cleanly
		GameObject::ComponentPropertyMap properties;
		const PropertySchema schema = getComponentSchema(component);
		void const * instance = static_cast<void const *>(&component);
		foreach(PropertyDesc const & desc, schema.properties())
		{
			if(!isSerializedProperty(desc))
			{
				continue;
			}
			GameObject::ComponentPropertyRecord record;
			record.kind = static_cast<int>(desc.kind);
			PropertyValue value = desc.get(instance);
			record.value = value.toString();
			if(desc.kind == PropertyKind::AssetRef)
			{
				record.reference = AssetDatabase::referenceIdForValue(record.value,
					"", refKindForHint(desc.referenceHint));
			}
			properties[desc.name] = record;
		}
		return properties;
	}
	//---------------------------------------------------------
	void SceneSerializer::applyComponentProperties(
		GameObject::ComponentPropertyMap const & properties,
		GameObjectComponent & component)
	{
		void * instance = static_cast<void *>(&component);
		// assign the present override properties in DECLARATION order (like
		// loadComponentProperties, so a setter cascade sees scalars before a
		// dependent reference); a property absent from the override map is left
		// untouched - it keeps the freshly-instantiated prefab default
		auto assignProperty = [&properties, instance](PropertyDesc const & desc)
		{
			if(!isSerializedProperty(desc))
			{
				return;
			}
			GameObject::ComponentPropertyMap::const_iterator found =
				properties.find(desc.name);
			if(found == properties.end())
			{
				return;
			}
			if(desc.kind == PropertyKind::AssetRef)
			{
				String name = found->second.value;
				String assetId = found->second.reference;
				AssetDatabase::resolveReference(name, assetId,
					refKindForHint(desc.referenceHint));
				desc.set(instance,
					PropertyValue::makeAssetRef(desc.referenceHint, name));
			}
			else
			{
				PropertyValue value = defaultValueForDesc(desc);
				value.fromString(found->second.value);
				desc.set(instance, value);
			}
		};
		// PASS 1 static per-type schema (a ScriptComponent's script path lands
		// here, discovering its dynamic exports); PASS 2 the now-populated
		// dynamic per-instance schema - the same two-pass order the named field
		// loader uses (@see loadComponentProperties)
		if(PropertySchema const * staticSchema =
			TypeManager::getSingleton().getPropertySchema(
				component.getTypeInfo().getId()))
		{
			foreach(PropertyDesc const & desc, staticSchema->properties())
			{
				assignProperty(desc);
			}
		}
		const PropertySchema dynamicSchema = component.getInstancePropertySchema();
		foreach(PropertyDesc const & desc, dynamicSchema.properties())
		{
			assignProperty(desc);
		}
	}
	//---------------------------------------------------------
	void SceneSerializer::saveComponentProperties(optr<IArchive> const & ar,
		GameObjectComponent & component)
	{
		// the FULL schema: static per-type UNION dynamic per-instance (task #94
		// P5b) - so a ScriptComponent's exported script properties serialize
		// per-instance alongside its static fields, through this one named path
		const PropertySchema schema = getComponentSchema(component);
		std::vector<PropertyDesc const *> fields;
		foreach(PropertyDesc const & desc, schema.properties())
		{
			if(isSerializedProperty(desc))
			{
				fields.push_back(&desc);
			}
		}
		unsigned int fieldCount = static_cast<unsigned int>(fields.size());
		ar << fieldCount;
		void const * instance = static_cast<void const *>(&component);
		foreach(PropertyDesc const * desc, fields)
		{
			String name = desc->name;
			ar << name;
			int kind = static_cast<int>(desc->kind);
			ar << kind;
			PropertyValue value = desc->get(instance);
			String text = value.toString();
			ar << text;
			// AssetRef properties carry the resolving asset id (rename survival)
			// in the trailing record field; every other kind writes "" there
			String reference;
			if(desc->kind == PropertyKind::AssetRef)
			{
				reference = AssetDatabase::referenceIdForValue(text, "",
					refKindForHint(desc->referenceHint));
			}
			ar << reference;
		}
	}
	//---------------------------------------------------------
	void SceneSerializer::loadComponentProperties(optr<IArchive> const & ar,
		GameObjectComponent & component)
	{
		struct FieldRecord
		{
			int kind;
			String text;
			String reference;
		};
		unsigned int fieldCount = 0;
		ar >> fieldCount;
		std::map<String, FieldRecord> records;
		for(unsigned int fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex)
		{
			String name;
			ar >> name;
			int kind = 0;
			ar >> kind;
			String text;
			ar >> text;
			String reference;
			ar >> reference;
			FieldRecord record;
			record.kind = kind;
			record.text = text;
			record.reference = reference;
			records[name] = record;
		}
		void * instance = static_cast<void *>(&component);
		// assign in DECLARATION order (deterministic, so a setter cascade like the
		// sprite quad rebuild sees the scalar state before the texture reference)
		auto assignField = [&records, instance](PropertyDesc const & desc)
		{
			if(!isSerializedProperty(desc))
			{
				return;
			}
			std::map<String, FieldRecord>::const_iterator found =
				records.find(desc.name);
			if(found == records.end())
			{
				// a field absent from the file keeps the constructed default
				return;
			}
			if(desc.kind == PropertyKind::AssetRef)
			{
				// resolve the reference against the active database (a resolving
				// id wins over a stale name - rename survival) then set the name
				String name = found->second.text;
				String assetId = found->second.reference;
				AssetDatabase::resolveReference(name, assetId,
					refKindForHint(desc.referenceHint));
				desc.set(instance,
					PropertyValue::makeAssetRef(desc.referenceHint, name));
			}
			else
			{
				PropertyValue value = defaultValueForDesc(desc);
				value.fromString(found->second.text);
				desc.set(instance, value);
			}
		};
		// PASS 1 - the STATIC per-type schema. Assigning it sets the authoring
		// state INCLUDING a ScriptComponent's script path, whose setter discovers
		// the dynamic export schema (task #94 P5b) - so pass 2 can see it.
		if(PropertySchema const * staticSchema =
			TypeManager::getSingleton().getPropertySchema(
				component.getTypeInfo().getId()))
		{
			foreach(PropertyDesc const & desc, staticSchema->properties())
			{
				assignField(desc);
			}
		}
		// PASS 2 - the DYNAMIC per-instance schema, now populated (the script's
		// exported property values restore per-instance from the same records).
		// A fully-static component's dynamic schema is empty, so this is a no-op.
		const PropertySchema dynamicSchema = component.getInstancePropertySchema();
		foreach(PropertyDesc const & desc, dynamicSchema.properties())
		{
			assignField(desc);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
