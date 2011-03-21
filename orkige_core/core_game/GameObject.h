/**************************************************************
	created:	2010/08/15 at 14:41
	filename: 	GameObject.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/
#ifndef __GameObject_h__15_8_2010__14_41_41__
#define __GameObject_h__15_8_2010__14_41_41__

#include "core_util/ComponentHolder.h"
#include "core_event/EventManager.h"

#include "core_game/GameObjectComponent.h"

namespace Orkige
{
	//! Component Based GameObject
	class ORKIGE_DLL GameObject : public ComponentHolder<GameObjectComponent>
	{
		OOBJECT(GameObject,ComponentHolder<GameObjectComponent>)
		//--- Types -------------------------------------------
	public:
		class GameObjectComponentUpdateBreak
		{

		};
		class GameObjectUpdateBreak
		{

		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		EventManager*								eventManager;			//!< EventManager assigned Components can listen to
		std::vector< optr<GameObjectComponent> >	updatableComponents;	//!< holds components that needs ther onUpdate method called
	private:
		//--- Methods -----------------------------------------
	public:
		//! create  GameObject with given id
		explicit GameObject(String const & id);
		//! destructor
		virtual ~GameObject();
		//! get EventManager
		inline EventManager* getEventManager();
		//! trigger a Event on this GameObject
		inline bool triggerEvent(Event const & event) const;
		//! load GameObject and GameObjectComponents from Template
		bool loadTemplate(String const & templateFileName);
		//! load GameObject and GameObjectComponents from Template
		bool saveTemplate(String const & templateFileName);
		//! enable updates for component of given type
		void enableUpdates(TypeInfo const & componentType);
		//! disable updates for component of given type
		void disableUpdates(TypeInfo const & componentType);
		//! update components that wants Updates
		inline void updateComponents(float delta);
	protected:
		//! called when a component is added
		virtual void onComponentAdded(TypeInfo const & componentType);
		//! called when a component is removed
		virtual void onComponentRemoved(TypeInfo const & componentType);
		//--- SERIALIZATION ---
		//! load from Archive
		virtual void save(optr<IArchive> const & ar);
		//! save to Archive
		virtual void load(optr<IArchive> const & ar);
		//! default constructor
		GameObject(){}
	private:
	};
	//---------------------------------------------------------
	inline EventManager* GameObject::getEventManager()
	{
		return this->eventManager;
	}
	//---------------------------------------------------------
	inline bool GameObject::triggerEvent(Event const & event) const
	{
		return this->eventManager->trigger(event);
	}
	//---------------------------------------------------------
	inline void GameObject::updateComponents(float delta)
	{
		try
		{
			foreach(optr<GameObjectComponent> const & goc, this->updatableComponents)
			{
				goc->onUpdateComponent(delta);
			}
		}
		catch (GameObjectComponentUpdateBreak const &/* e*/)
		{

		}
	}
}

#endif //__GameObject_h__15_8_2010__14_41_41__
