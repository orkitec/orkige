/**************************************************************
	created:	2010/08/15 at 15:24
	filename: 	GameObjectManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __GameObjectManager_h__15_8_2010__15_24_56__
#define __GameObjectManager_h__15_8_2010__15_24_56__

#include "core_util/Singleton.h"
#include "core_game/GameObject.h"

namespace Orkige
{
	//! GameObject management
	class ORKIGE_DLL GameObjectManager : public Singleton<GameObjectManager>, public Object
	{
		friend class GameObject;
		OOBJECT(GameObjectManager,Object)
		DECL_OSINGLETON(GameObjectManager)
		//--- Types -------------------------------------------
	public:
		typedef std::map<String, optr<GameObject> > GameObjectMap;					//!< maps GameObject to String id
	protected:
		typedef std::map<EventType::TypeId, optr<EventListener> > EventListenerMap;	//!< maps TypeId to EventListener
		typedef std::vector< GameObjectComponent *> GameObjectComponentPtrVector;	//!< Vector of Component pointers
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		GameObjectMap					objects;						//!< managed GameObjects
		GameObjectComponentPtrVector	updatableComponents;			//!< holds components that needs their onUpdate method called
		std::size_t						numUpdatableComponents;			//!< cached number of components that should receive updates
		std::size_t						currentUpdatableComponentIndex;	//!< index of current updated component in updatableComponents vector
		bool							enableObjectUpdates;			//!< mark if updating Objects is enabled	
		EventListenerMap				globalEvents;					//!< enabled Global Events
		StringVector					deleteQueue;					//!< queue of GameObjects that should be deleted on next update
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		GameObjectManager();
		//! destructor
		virtual ~GameObjectManager();
		//! check if GameObject with given id exists
		inline bool objectExists(String const & id);
		//! get GameObject with given id
		inline woptr<GameObject> getGameObject(String const & id);
		//! add a GameObject to the list of managed GameObjects
		inline bool addGameObject(optr<GameObject> obj);
		//! remove GameObject
		inline bool delGameObject(String const & id);
		//! add objects to queue and delete them on next Application update
		inline bool queueDelGameObject(String const & id);
		//! create a GameObject with given id and add it to the list of managed GameObjects
		inline woptr<GameObject> createGameObject(String const & id); 
		//! get map with all GameObjects
		inline GameObjectMap const & getGameObjects();
		//! remove all managed GameObjects
		inline void clear();
		//! triggers an event on all GameObjects
		bool triggerEvent(Event const & event) const;
		//! forward given Event to all GameObjects
		bool enableEvent(EventType const & eventType);
		//! stop forwarding given Event
		bool disableEvent(EventType const & eventType);

		//! set GameObject updates enabled or disabled
		inline void setUpdatesEnabled(bool enabled);

		//! save to archive
		virtual void save(optr<IArchive> const & ar);
		//! load from archive
		virtual void load(optr<IArchive> const & ar);
		//! @see ISerializable::createBeforeLoad
		virtual bool createBeforeLoad();

		//! update GameObjects and components and process DeleteQueue
		void update(float delta);
	protected:
		//! enable updates for given GameObjectComponent
		void enableUpdates(GameObjectComponent * component);
		//! disable updates for given GameObjectComponent
		void disableUpdates(GameObjectComponent * component);

		//! delete GameObjects that are queued for deletion
		void processDeleteQueue();
		//! handle Global Event forwarding
		inline bool onGlobalEvent(Event const & event);
	private:
	};
	//---------------------------------------------------------
	inline bool GameObjectManager::objectExists(String const & id)
	{
		bool exists = (this->objects.find(id) != this->objects.end());
		return exists;
	}
	//---------------------------------------------------------
	inline woptr<GameObject> GameObjectManager::getGameObject(String const & id)
	{
		GameObjectMap::iterator it = this->objects.find(id);
		if(it == this->objects.end())
		{
			oDebugMsg("core",0,"GameObject: " << id << " doesn't exist!");
			return oNull<GameObject>();
		}
		return it->second;
	}
	//---------------------------------------------------------
	inline bool GameObjectManager::addGameObject(optr<GameObject> obj)
	{
		String const & id = obj->getObjectID();

		if(this->objectExists(id))
		{
			oDebugMsg("core",0,"GameObject: " << id << " does already exist");
			return false;
		}
		this->objects[id] = obj;
		return true;
	}
	//---------------------------------------------------------
	inline bool GameObjectManager::delGameObject(String const & id)
	{
		GameObjectMap::iterator it = this->objects.find(id);
		if(it == this->objects.end())
		{
			oDebugMsg("core",0,"GameObject: " << id << " doesn't exist!");
			return false;
		}
		this->objects.erase(it);		
		return true;
	}
	//---------------------------------------------------------
	inline bool GameObjectManager::queueDelGameObject(String const & id)
	{
		GameObjectMap::iterator it = this->objects.find(id);
		if(it == this->objects.end())
		{
			oDebugMsg("core",0,"GameObject: " << id << " doesn't exist!");
			return false;
		}
		if(std::find(this->deleteQueue.begin(), this->deleteQueue.end(), id) != this->deleteQueue.end())
		{
			oDebugMsg("core",0,"GameObject: " << id << " already queued for deletion!");
			return false;
		}
		this->deleteQueue.push_back(id);
		return true;
	}
	//---------------------------------------------------------
	inline woptr<GameObject> GameObjectManager::createGameObject(String const & id)
	{
		if(this->objectExists(id))
		{
			oDebugMsg("core",0,"GameObject: " << id << " does already exist");
			return oNull<GameObject>();
		}
		optr<GameObject> go = onew(new GameObject(id));
		bool gameObjectCreated = this->addGameObject(go);
		oAssert(gameObjectCreated);
		return go;
	}
	//---------------------------------------------------------
	inline GameObjectManager::GameObjectMap  const & GameObjectManager::getGameObjects()						
	{	
		return this->objects;									
	}
	//---------------------------------------------------------
	inline void GameObjectManager::clear()										
	{
		this->numUpdatableComponents = 0;
		this->currentUpdatableComponentIndex = 0;
		this->updatableComponents.clear();
		this->objects.clear();								
	}
	//---------------------------------------------------------
	inline bool GameObjectManager::onGlobalEvent(Event const & event)
	{
		return this->triggerEvent(event);
	}
	//---------------------------------------------------------
	inline void GameObjectManager::setUpdatesEnabled(bool enabled)
	{
		this->enableObjectUpdates = enabled;
	}
}

#endif //__GameObjectManager_h__15_8_2010__15_24_56__
