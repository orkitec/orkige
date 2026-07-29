/**************************************************************
	created:	2010/08/15 at 15:27
	filename: 	GameObjectManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec	
***************************************************************/

#include "core_game/GameObjectManager.h"
#include "core_game/GameObject.h"
#include "core_event/GlobalEventManager.h"
#include "core_tween/TweenManager.h"
#include "core_tween/TimerManager.h"
#include "core_debug/Profile.h"
#include <algorithm>

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
		// detach the world through the teardown hook BEFORE any member dies.
		// A GameObject's destructor removes its components, and component
		// removal calls back into disableUpdates() to drop the component from
		// updatableComponents. But updatableComponents is declared before the
		// objects map, so plain member destruction frees the update vector
		// FIRST and the objects map (destroyed last) would then re-enter
		// disableUpdates() on freed storage. clear() empties the update list
		// (and zeroes numUpdatableComponents, making disableUpdates a no-op)
		// while every member is still alive, so no object destructor can reach
		// a dead list.
		this->clear();
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
		OPROFILE("objects.triggerEvent");
		bool retval = false;
		for(GameObjectMap::const_iterator it=this->objects.begin(),itend = this->objects.end(); it != itend; ++it)
		{
			if(it->second->triggerEvent(event))
				retval = true;
		}
		return retval;
	}
	//---------------------------------------------------------
	void GameObjectManager::update(float delta)
	{
		OPROFILE("objects.update");

		this->processDeleteQueue();
		if(this->enableObjectUpdates)
		{
			for(this->currentUpdatableComponentIndex = 0; this->currentUpdatableComponentIndex < this->numUpdatableComponents; this->currentUpdatableComponentIndex++)
			{
				GameObjectComponent* component = this->updatableComponents[this->currentUpdatableComponentIndex];
				// deactivated objects AND disabled components stop ticking - the
				// composed effectivelyEnabled() query (cached activeInHierarchy
				// flag AND the component enable flag) keeps this an O(1) gate. A
				// disabled component that must wind down gracefully opts back in
				// via ticksWhileDisabled and self-gates its emission (Particle);
				// an inactive OWNER always suspends, regardless.
				GameObject* componentOwner = component->getGameObject();
				const bool ownerActive = !componentOwner || componentOwner->isActiveInHierarchy();
				if(!ownerActive)
				{
					continue;
				}
				if(!component->isEnabled() && !component->ticksWhileDisabled())
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
	// scene-load pump) EXTEND this hook instead of inventing a second
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
		// scheduled timers die with the scene too, for the same reason (their
		// callbacks close over this scene's objects - core_tween/TimerManager.h
		// lifetime rules); no callbacks fire. The editor never creates one.
		if(TimerManager::getSingletonPtr() != 0)
		{
			TimerManager::getSingleton().clear();
		}

		this->numUpdatableComponents = 0;
		this->currentUpdatableComponentIndex = 0;
		this->updatableComponents.clear();
		this->childIds.clear();
		this->tagIds.clear();
		this->objects.clear();
	}
	//---------------------------------------------------------
	// The persistence-aware teardown: the level system's deferred scene switch
	// funnels through HERE instead of clear() so a persistent object survives
	// with its WHOLE live state. Because a survivor is never destroyed, its
	// components are never removed - the render node, physics body, sound
	// source and script sandbox (its counters, subscriptions) all live on by
	// construction; only the non-survivors run their component teardown. This
	// EXTENDS the one scene-teardown hook rather than inventing a second path.
	void GameObjectManager::clearExceptPersistent()
	{
		// running tweens/timers die with the OUTGOING scene exactly as in
		// clear(): their callbacks close over objects that may be torn down
		// here. A persistent object's SCRIPT state survives, but an in-flight
		// tween/timer it started does not (a documented first-version limit).
		if(TweenManager::getSingletonPtr() != 0)
		{
			TweenManager::getSingleton().clear();
		}
		if(TimerManager::getSingletonPtr() != 0)
		{
			TimerManager::getSingleton().clear();
		}

		// the survivor set: an object survives iff it (or any ancestor) is
		// persistent, so a persistent parent keeps its whole subtree
		std::set<String> survivors;
		foreach(GameObjectMap::value_type const & entry, this->objects)
		{
			if(this->isPersistentInHierarchy(entry.first))
			{
				survivors.insert(entry.first);
			}
		}

		// re-root every survivor whose parent will NOT survive (a persistent
		// child of a dying parent): it becomes a scene root and lives on,
		// keeping its world transform. A survivor inside a surviving subtree
		// keeps its parent link untouched.
		foreach(String const & survivorId, survivors)
		{
			GameObjectMap::iterator it = this->objects.find(survivorId);
			if(it == this->objects.end())
			{
				continue;
			}
			String const parentId = it->second->getParentId();
			if(!parentId.empty() && survivors.find(parentId) == survivors.end())
			{
				oDebugMsg("core",0,"GameObjectManager: persistent object "
					<< survivorId << " re-roots to the scene root (its parent "
					<< parentId << " does not survive the scene switch)");
				it->second->setParent(String(), true);
			}
		}

		// destroy every non-survivor. Erasing it from the objects map runs its
		// component teardown (render nodes, physics bodies) and keeps the
		// update list in sync through disableUpdates; its tag + child index
		// entries are pruned per id so the survivors' entries - and their
		// child ORDER - stay intact. After the re-root pass every survivor's
		// parent is a survivor and every doomed object's children are doomed,
		// so no survivor is ever orphaned here.
		StringVector doomed;
		foreach(GameObjectMap::value_type const & entry, this->objects)
		{
			if(survivors.find(entry.first) == survivors.end())
			{
				doomed.push_back(entry.first);
			}
		}
		foreach(String const & id, doomed)
		{
			GameObjectMap::iterator it = this->objects.find(id);
			if(it == this->objects.end())
			{
				continue;
			}
			optr<GameObject> gameObject = it->second;
			this->onObjectTagsChanged(id, gameObject->getTags(), StringVector());
			this->unregisterFromChildIndex(id, gameObject->getParentId());
			this->objects.erase(it);
		}
	}
	//---------------------------------------------------------
	bool GameObjectManager::isPersistentInHierarchy(String const & id) const
	{
		GameObjectMap::const_iterator it = this->objects.find(id);
		// guard against a malformed parent chain (setParent refuses cycles)
		std::size_t guard = this->objects.size() + 1;
		while(it != this->objects.end() && guard-- > 0)
		{
			if(it->second->isPersistent())
			{
				return true;
			}
			String const & parentId = it->second->getParentId();
			if(parentId.empty())
			{
				return false;
			}
			it = this->objects.find(parentId);
		}
		return false;
	}
	//---------------------------------------------------------
	StringVector GameObjectManager::findByTag(String const & tag) const
	{
		StringVector result;
		TagIdMap::const_iterator it = this->tagIds.find(tag);
		if(it != this->tagIds.end())
		{
			// the set is already sorted; hand the ids back as a plain vector
			result.assign(it->second.begin(), it->second.end());
		}
		return result;
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
		// the root sequence IS the "" entry of the child index (a copy: callers
		// iterate it while tree edits mutate the index)
		return this->getChildren(String());
	}
	//---------------------------------------------------------
	int GameObjectManager::getChildIndex(String const & id) const
	{
		GameObjectMap::const_iterator objectIt = this->objects.find(id);
		if(objectIt == this->objects.end())
		{
			return -1;
		}
		StringVector const & siblings = this->getChildren(objectIt->second->getParentId());
		StringVector::const_iterator it = std::find(siblings.begin(), siblings.end(), id);
		if(it == siblings.end())
		{
			return -1;
		}
		return static_cast<int>(it - siblings.begin());
	}
	//---------------------------------------------------------
	bool GameObjectManager::moveChildToIndex(String const & id, int index)
	{
		GameObjectMap::const_iterator objectIt = this->objects.find(id);
		if(objectIt == this->objects.end())
		{
			return false;
		}
		ChildIdMap::iterator listIt = this->childIds.find(objectIt->second->getParentId());
		if(listIt == this->childIds.end())
		{
			return false;
		}
		StringVector & siblings = listIt->second;
		StringVector::iterator from = std::find(siblings.begin(), siblings.end(), id);
		if(from == siblings.end())
		{
			return false;
		}
		siblings.erase(from);
		// clamp into the post-removal range: feeding back a getChildIndex value
		// lands the object exactly where it was
		int target = index;
		if(target < 0)
		{
			target = 0;
		}
		if(target > static_cast<int>(siblings.size()))
		{
			target = static_cast<int>(siblings.size());
		}
		siblings.insert(siblings.begin() + target, id);
		return true;
	}
	//---------------------------------------------------------
	bool GameObjectManager::reorderChild(String const & childId,
		String const & anchorId, bool after)
	{
		if(childId.empty() || anchorId.empty() || childId == anchorId)
		{
			return false;
		}
		GameObjectMap::const_iterator childIt = this->objects.find(childId);
		GameObjectMap::const_iterator anchorIt = this->objects.find(anchorId);
		if(childIt == this->objects.end() || anchorIt == this->objects.end())
		{
			return false;
		}
		String const & parentId = childIt->second->getParentId();
		if(parentId != anchorIt->second->getParentId())
		{
			// a cross-parent move is a REPARENT, not a reorder
			return false;
		}
		ChildIdMap::iterator listIt = this->childIds.find(parentId);
		if(listIt == this->childIds.end())
		{
			return false;
		}
		StringVector & siblings = listIt->second;
		StringVector::iterator from = std::find(siblings.begin(), siblings.end(), childId);
		StringVector::const_iterator anchorSlot =
			std::find(siblings.begin(), siblings.end(), anchorId);
		if(from == siblings.end() || anchorSlot == siblings.end())
		{
			return false;
		}
		siblings.erase(from);
		// the anchor may have shifted one slot left by the erase - locate it again
		anchorSlot = std::find(siblings.begin(), siblings.end(), anchorId);
		int target = static_cast<int>(anchorSlot - siblings.begin());
		if(after)
		{
			++target;
		}
		siblings.insert(siblings.begin() + target, childId);
		return true;
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
		// "" is a real key here (the scene root sequence), so an object leaving
		// or joining the roots is indexed exactly like any other move
		this->unregisterFromChildIndex(childId, oldParentId);
		this->childIds[newParentId].push_back(childId);
	}
	//---------------------------------------------------------
	void GameObjectManager::unregisterFromChildIndex(String const & id, String const & parentId)
	{
		ChildIdMap::iterator it = this->childIds.find(parentId);
		if(it == this->childIds.end())
		{
			return;
		}
		StringVector & siblings = it->second;
		StringVector::iterator childIt = std::find(siblings.begin(), siblings.end(), id);
		if(childIt != siblings.end())
		{
			siblings.erase(childIt);
		}
		if(siblings.empty())
		{
			this->childIds.erase(it);
		}
	}
	//---------------------------------------------------------
	void GameObjectManager::onObjectTagsChanged(String const & objectId, StringVector const & oldTags, StringVector const & newTags)
	{
		// remove the object from every tag it no longer has (mirror of the
		// oldParent unlink in onObjectReparented)
		foreach(String const & tag, oldTags)
		{
			if(std::find(newTags.begin(), newTags.end(), tag) != newTags.end())
			{
				continue;
			}
			TagIdMap::iterator it = this->tagIds.find(tag);
			if(it != this->tagIds.end())
			{
				it->second.erase(objectId);
				if(it->second.empty())
				{
					this->tagIds.erase(it);
				}
			}
		}
		// add the object to every tag it gained (mirror of the newParent add)
		foreach(String const & tag, newTags)
		{
			if(std::find(oldTags.begin(), oldTags.end(), tag) != oldTags.end())
			{
				continue;
			}
			this->tagIds[tag].insert(objectId);
		}
	}
	//---------------------------------------------------------
	void GameObjectManager::processDeleteQueue()
	{
		OPROFILE("objects.deleteQueue");
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
