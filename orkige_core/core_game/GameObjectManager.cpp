/**************************************************************
	created:	2010/08/15 at 15:27
	filename: 	GameObjectManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/

#include "core_game/GameObjectManager.h"
#include "core_game/GameObject.h"
#include "core_event/GlobalEventManager.h"
#include "core_tween/TweenManager.h"

namespace Orkige
{
	IMPL_OSINGLETON(GameObjectManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameObjectManager::GameObjectManager() : Object(String("GameObjectManager")), numUpdatableComponents(0), currentUpdatableComponentIndex(0), enableObjectUpdates(true)
	{
	}
	//---------------------------------------------------------
	GameObjectManager::~GameObjectManager()
	{
	}
	//---------------------------------------------------------
	bool GameObjectManager::enableEvent(EventType const & eventType)
	{
		EventType::TypeId eventTypeId = eventType.getId(); 
		EventListenerMap::const_iterator it = this->globalEvents.find(eventTypeId);
		if(it == this->globalEvents.end())
		{
			EventManager* eventManager = GlobalEventManager::getSingletonPtr();
			optr<EventListener> eventListener = createEventListenerPtr(&GameObjectManager::onGlobalEvent, this);
			bool eventRegistered = eventManager->addListener(eventListener,eventType);
			if(eventRegistered)
			{
				this->globalEvents[eventTypeId] = eventListener;
				return true;
			}
			oAssert(eventRegistered);
		}
		return false;
	}
	//---------------------------------------------------------
	bool GameObjectManager::disableEvent(EventType const & eventType)
	{
		EventType::TypeId eventTypeId = eventType.getId(); 
		EventListenerMap::iterator it = this->globalEvents.find(eventTypeId);
		if(it != this->globalEvents.end())
		{
			EventManager* eventManager = GlobalEventManager::getSingletonPtr();
			optr<EventListener> eventListener = it->second;
			bool eventUnregistered = eventManager->delListener(eventListener,eventType);
			this->globalEvents.erase(it);
			oAssert(eventUnregistered);
			return true;
		}
		return false;	
	}
	//---------------------------------------------------------
	bool GameObjectManager::triggerEvent(Event const & event) const										
	{
		OPROFILE(String(__FUNCTION__) + "( " + event.getObjectID() + " )");
		bool retval = false;
		for(GameObjectMap::const_iterator it=this->objects.begin(),itend = this->objects.end(); it != itend; ++it)
		{
			//OPROFILE(it->second->getObjectID() + "->triggerEvent( " + event.getObjectID() + " )");
			if(it->second->triggerEvent(event))
				retval = true;
		}
		return retval;
	}
	//---------------------------------------------------------
	void GameObjectManager::update(float delta)
	{
		OPROFILEFUNC();

		this->processDeleteQueue();
		if(this->enableObjectUpdates)
		{
			for(this->currentUpdatableComponentIndex = 0; this->currentUpdatableComponentIndex < this->numUpdatableComponents; this->currentUpdatableComponentIndex++)
			{
				GameObjectComponent* component = this->updatableComponents[this->currentUpdatableComponentIndex];
				// deactivated objects stop ticking (Unity semantics) - the
				// cached activeInHierarchy flag makes this an O(1) gate
				GameObject* componentOwner = component->getGameObject();
				if(componentOwner && !componentOwner->isActiveInHierarchy())
				{
					continue;
				}
				component->onUpdateComponent(delta);
			}
		}
	}
	//---------------------------------------------------------
	// ================= SCENE TEARDOWN HOOK =================
	// clear() is THE single authoritative "the world goes away" point: every
	// scene switch funnels through it (SceneSerializer::loadScene clears
	// before loading, the editor's new/open document paths call it, tests
	// reset through it). Cross-object runtime state that must not outlive the
	// scene is torn down HERE and nowhere else - later features (the deferred
	// scene-load pump, #87) EXTEND this hook instead of inventing a second
	// teardown path.
	void GameObjectManager::clear()
	{
		// running tweens die with the scene: their callbacks close over
		// objects of THIS scene (core_tween/TweenManager.h lifetime rules);
		// no callbacks fire. The editor never creates a TweenManager - guard.
		if(TweenManager::getSingletonPtr() != 0)
		{
			TweenManager::getSingleton().clear();
		}

		this->numUpdatableComponents = 0;
		this->currentUpdatableComponentIndex = 0;
		this->updatableComponents.clear();
		this->childIds.clear();
		this->objects.clear();
	}
	//---------------------------------------------------------
	StringVector const & GameObjectManager::getChildren(String const & parentId) const
	{
		static const StringVector noChildren;
		ChildIdMap::const_iterator it = this->childIds.find(parentId);
		if(it == this->childIds.end())
		{
			return noChildren;
		}
		return it->second;
	}
	//---------------------------------------------------------
	StringVector GameObjectManager::getRootObjectIds() const
	{
		StringVector roots;
		for(GameObjectMap::const_iterator it = this->objects.begin(), itend = this->objects.end(); it != itend; ++it)
		{
			if(it->second->getParentId().empty())
			{
				roots.push_back(it->first);
			}
		}
		return roots;
	}
	//---------------------------------------------------------
	bool GameObjectManager::isDescendantOf(String const & id, String const & ancestorId) const
	{
		if(id.empty() || ancestorId.empty())
		{
			return false;
		}
		GameObjectMap::const_iterator it = this->objects.find(id);
		// guard against malformed parent chains (should not happen - the
		// setParent validation refuses unknown parents and cycles)
		std::size_t guard = this->objects.size() + 1;
		while(it != this->objects.end() && guard-- > 0)
		{
			String const & parentId = it->second->getParentId();
			if(parentId.empty())
			{
				return false;
			}
			if(parentId == ancestorId)
			{
				return true;
			}
			it = this->objects.find(parentId);
		}
		return false;
	}
	//---------------------------------------------------------
	StringVector GameObjectManager::collectSubtreeIds(String const & rootId) const
	{
		StringVector subtree;
		if(this->objects.find(rootId) == this->objects.end())
		{
			return subtree;
		}
		// iterative depth-first walk, root first; a pending stack keeps the
		// child order (children are pushed in reverse so they pop in order)
		StringVector pending;
		pending.push_back(rootId);
		while(!pending.empty())
		{
			const String id = pending.back();
			pending.pop_back();
			subtree.push_back(id);
			StringVector const & children = this->getChildren(id);
			for(StringVector::const_reverse_iterator it = children.rbegin(); it != children.rend(); ++it)
			{
				pending.push_back(*it);
			}
		}
		return subtree;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GameObjectManager::enableUpdates(GameObjectComponent * component)
	{
		oAssert(component);
		GameObjectComponentPtrVector::const_iterator it = std::find(this->updatableComponents.begin(), this->updatableComponents.end(), component);
		if(it == this->updatableComponents.end())
		{
			this->updatableComponents.push_back(component);
		}
		this->numUpdatableComponents = this->updatableComponents.size();
	}
	//---------------------------------------------------------
	void GameObjectManager::disableUpdates(GameObjectComponent * component)
	{
		oAssert(component);
		if(this->numUpdatableComponents > 0)
		{
			GameObjectComponentPtrVector::iterator it = std::find(this->updatableComponents.begin(), this->updatableComponents.end(), component);
			if(it != this->updatableComponents.end())
			{
				std::size_t eraseIndex = it - this->updatableComponents.begin();
				if(eraseIndex < this->currentUpdatableComponentIndex)
				{
					this->currentUpdatableComponentIndex--;
				}
				this->updatableComponents.erase(it);
			}
			this->numUpdatableComponents = this->updatableComponents.size();
		}
	}
	//---------------------------------------------------------
	void GameObjectManager::onObjectReparented(String const & childId, String const & oldParentId, String const & newParentId)
	{
		if(!oldParentId.empty())
		{
			ChildIdMap::iterator it = this->childIds.find(oldParentId);
			if(it != this->childIds.end())
			{
				StringVector & siblings = it->second;
				StringVector::iterator childIt = std::find(siblings.begin(), siblings.end(), childId);
				if(childIt != siblings.end())
				{
					siblings.erase(childIt);
				}
				if(siblings.empty())
				{
					this->childIds.erase(it);
				}
			}
		}
		if(!newParentId.empty())
		{
			this->childIds[newParentId].push_back(childId);
		}
	}
	//---------------------------------------------------------
	void GameObjectManager::processDeleteQueue()
	{
		OPROFILEFUNC();
		foreach(String const & id,  this->deleteQueue)
		{
			this->delGameObject(id);
		}
		this->deleteQueue.clear();
	}
	//---------------------------------------------------------
	bool GameObjectManager::createBeforeLoad()	
	{
		return false;
	}
	//---------------------------------------------------------
	void GameObjectManager::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		ar << objects;
	}
	//---------------------------------------------------------
	void GameObjectManager::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		ar >> objects;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(GameObjectManager)
		OSINGLETON()
		OFUNC(addGameObject)
		OFUNC(delGameObject)
		OFUNC(getGameObject)
		OFUNC(objectExists)
		OFUNC(createGameObject)
		OFUNC(enableEvent)
		OFUNC(disableEvent)
		//OVAR(objects) disabled: binding the GameObjectMap trips a compile
		//bug in vcpkg's sol2 3.3.0 associative container support
	OOBJECT_END
}
