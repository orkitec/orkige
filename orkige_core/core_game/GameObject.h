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
	class ORKIGE_CORE_DLL GameObject : public ComponentHolder<GameObjectComponent>
	{
		OOBJECT(GameObject,ComponentHolder<GameObjectComponent>)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		EventManager*								eventManager;			//!< EventManager assigned Components can listen to
		std::vector< optr<GameObjectComponent> >	updatableComponents;	//!< holds components that needs their onUpdate method called
		String										parentId;				//!< id of the parent GameObject ("" = root, Unity-style tree)
		bool										activeSelf;				//!< own active flag (Unity activeSelf, default true)
		bool										activeInHierarchy;		//!< cached effective state: activeSelf AND all ancestors active
	private:
		//--- Methods -----------------------------------------
	public:
		//! create GameObject with given id
		explicit GameObject(String const & id);
		//! destructor
		virtual ~GameObject();
		//! get EventManager
		inline EventManager* getEventManager();
		//! trigger an Event on this GameObject
		inline bool triggerEvent(Event const & event) const;
		//! load GameObject and GameObjectComponents from Template
		bool loadTemplate(String const & templateFileName);
		//! load GameObject and GameObjectComponents from Template
		bool saveTemplate(String const & templateFileName);
		//! enable updates for component of given type
		void enableUpdates(TypeInfo const & componentType);
		//! disable updates for component of given type
		void disableUpdates(TypeInfo const & componentType);
		//--- HIERARCHY ---
		//! id of the parent GameObject ("" while this object is a root)
		inline String const & getParentId() const;
		//! the parent GameObject (NULL while this object is a root)
		//! (non-const so the Lua OFUNCWEAK wrapper can bind it)
		woptr<GameObject> getParent();
		//! @brief re-parent this GameObject ("" = make it a root)
		//! @remarks refuses (and logs) unknown parents, self-parenting and
		//! re-parenting onto an own descendant (cycle guard). Components get
		//! GameObjectComponent::onParentChanged - with keepWorldTransform the
		//! TransformComponent recomputes its LOCAL transform so the WORLD
		//! transform is preserved (Unity semantics); scene loading passes
		//! false because serialized transforms are already local.
		bool setParent(String const & newParentId, bool keepWorldTransform);
		//! @brief setParent overload with the Unity default (keep the world
		//! transform) - the form the Lua binding exposes
		inline bool setParent(String const & newParentId);
		//! ids of the direct children (@see GameObjectManager::getChildren)
		StringVector const & getChildIds() const;
		//--- ACTIVE STATE ---
		//! own active flag (Unity activeSelf)
		inline bool isActiveSelf() const;
		//! effective active state: activeSelf AND all ancestors active
		inline bool isActiveInHierarchy() const;
		//! @brief set the own active flag (Unity SetActive)
		//! @remarks where the effective state (activeInHierarchy) of this
		//! object or a descendant changes, every affected component receives
		//! GameObjectComponent::onSetActive - inactive objects stop ticking,
		//! engine components hide/silence/park their scene state
		void setActive(bool active);
	protected:
		//! @brief recompute the cached activeInHierarchy state of this object
		//! and (on change) of all descendants, dispatching onSetActive
		void refreshActiveInHierarchy();
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
	inline String const & GameObject::getParentId() const
	{
		return this->parentId;
	}
	//---------------------------------------------------------
	inline bool GameObject::setParent(String const & newParentId)
	{
		return this->setParent(newParentId, true);
	}
	//---------------------------------------------------------
	inline bool GameObject::isActiveSelf() const
	{
		return this->activeSelf;
	}
	//---------------------------------------------------------
	inline bool GameObject::isActiveInHierarchy() const
	{
		return this->activeInHierarchy;
	}
}

#endif //__GameObject_h__15_8_2010__14_41_41__
