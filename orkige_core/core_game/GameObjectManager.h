/**************************************************************
	created:	2010/08/15 at 15:24
	filename: 	GameObjectManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
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
		OOBJECT(GameObjectManager,Object)
		DECL_OSINGLETON(GameObjectManager)
		//--- Types -------------------------------------------
	public:
		typedef std::map<String, optr<GameObject> > GameObjectMap;					//!< maps GameObject to String id
	protected:
		typedef std::map<EventType::TypeId, optr<EventListener> > EventListenerMap;	//!< maps TypeId to EventListener
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		GameObjectMap objects;			//!< managed GameObject's
		EventListenerMap globalEvents;	//!< enabled Global Event's
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		GameObjectManager();
		//! destructor
		virtual ~GameObjectManager();
		//! check if GameObject with given id exists
		inline bool objectExists(String const & id);
		//! get gameObject with given id
		inline woptr<GameObject> getGameObject(String const & id);
		//! add a gameObject to the list of managed Objects
		inline bool addGameObject(optr<GameObject> obj);
		//! remove GameObject
		inline bool delGameObject(String const & id);
		//! create a gameObject with given id and add it to the list of managed GameObject's
		inline woptr<GameObject> createGameObject(String const & id); 
		//! get map with all GameObject's
		inline GameObjectMap getGameObjects();
		//! remove alle managed GameObject's
		inline void clear();
		//! triggers an event on all gameobjects
		inline bool triggerEvent(Event const & event) const;
		//! forward given Event to all GameObject's
		bool enableEvent(EventType const & eventType);
		//! stop forwarding given Event
		bool disableEvent(EventType const & eventType);

		//! save to archive
		virtual void save(optr<IArchive> const & ar);
		//! load from archive
		virtual void load(optr<IArchive> const & ar);
		//! @see ISerializable::createBeforeLoad
		virtual bool createBeforeLoad();
	protected:
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
			oDebugMsg("world",0,"GameObject: " << id << " doesn't exist!");
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
			oDebugMsg("world",0,"GameObject: " << id << " does already exist");
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
			oDebugMsg("world",0,"GameObject: " << id << " doesn't exist!");
			return false;
		}
		this->objects.erase(it);
		return true;
	}
	//---------------------------------------------------------
	inline woptr<GameObject> GameObjectManager::createGameObject(String const & id)
	{
		if(this->objectExists(id))
		{
			oDebugMsg("world",0,"GameObject: " << id << " does already exist");
			return oNull<GameObject>();
		}
		optr<GameObject> go = onew(new GameObject(id));
		bool gameObjectCreated = this->addGameObject(go);
		oAssert(gameObjectCreated);
		return go;
	}
	//---------------------------------------------------------
	inline GameObjectManager::GameObjectMap GameObjectManager::getGameObjects()						
	{	
		return this->objects;									
	}
	//---------------------------------------------------------
	inline void GameObjectManager::clear()										
	{	
		this->objects.clear();								
	}
	//---------------------------------------------------------
	inline bool GameObjectManager::triggerEvent(Event const & event) const										
	{
		bool retval = false;
		for(GameObjectMap::const_iterator it=this->objects.begin(),itend = this->objects.end(); it != itend; ++it)
		{
			if(it->second->triggerEvent(event))
				retval = true;
		}
		return retval;
	}
	//---------------------------------------------------------
	inline bool GameObjectManager::onGlobalEvent(Event const & event)
	{
		return this->triggerEvent(event);
	}
}

#endif //__GameObjectManager_h__15_8_2010__15_24_56__
