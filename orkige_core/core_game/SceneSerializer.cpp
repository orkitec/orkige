/**************************************************************
	created:	2026/07/07 at 22:10
	filename: 	SceneSerializer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/SceneSerializer.h"
#include "core_serialization/XMLArchive.h"

namespace Orkige
{
	// version 2 (2026-07): per-object parent id + activeSelf flag (Unity-style
	// GameObject tree); version 1 scenes load as all-root, all-active worlds
	const int SceneSerializer::SCENE_FORMAT_VERSION = 2;
	const String SceneSerializer::SCENE_FORMAT_MAGIC = "orkige.oscene";
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

		GameObjectManager::GameObjectMap const & objects = gameObjectManager.getGameObjects();
		unsigned int objectCount = static_cast<unsigned int>(objects.size());
		ar << objectCount;

		foreach(GameObjectManager::GameObjectMap::value_type const & objectEntry, objects)
		{
			optr<GameObject> gameObject = objectEntry.second;
			oAssert(gameObject);
			String id = gameObject->getObjectID();
			ar << id;

			// v2: the hierarchy fields ("" = root; the parent reference is the
			// object id, like every other object reference in the format)
			String parentId = gameObject->getParentId();
			ar << parentId;
			bool activeSelf = gameObject->isActiveSelf();
			ar << activeSelf;

			GameObject::ComponentMap const & components = gameObject->getComponents();
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
					oDebugMsg("scene",0,"SceneSerializer: component type \""<<componentTypeName<<"\" is not registered - cannot load scene: "<<fileName);
					loaded = false;
					break;
				}
				// dependencies may already have added this component
				// (e.g. ModelComponent pulls in TransformComponent)
				if(!gameObject->hasComponent(componentType))
				{
					if(!gameObject->addComponent(componentType))
					{
						oDebugMsg("scene",0,"SceneSerializer: could not add component \""<<componentTypeName<<"\" to GameObject: "<<id);
						loaded = false;
						break;
					}
				}
				GameObjectComponent* component = gameObject->getComponentPtr(componentType);
				oAssert(component);
				// reads the component element and calls component->load
				// (GameObjectComponent::createBeforeLoad is false)
				ar->read(static_cast<ISerializeable&>(*component));
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
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
