/**************************************************************
	created:	2010/08/15 at 15:16
	filename: 	GameObjectComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/
#ifndef __GameObjectComponent_h__15_8_2010__15_16_42__
#define __GameObjectComponent_h__15_8_2010__15_16_42__

#include "core_util/ComponentHolder.h"
// EventHandler.h (the EventHandler base class) already provides the
// EventManager/EventListener/EventType surface this header's users need
#include "core_event/EventHandler.h"
#include "core_base/PropertySchema.h"

namespace tinyxml2
{
	class XMLDocument;
	class XMLElement;
}

namespace Orkige
{
	class GameObject;

	//! Base Component for GameObjects
	class ORKIGE_CORE_DLL GameObjectComponent : public Component<GameObject>, public EventHandler
	{
		OOBJECT(GameObjectComponent,Component<GameObject>)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		bool wantsUpdates; //!< mark if this Componenets ::onUpdate method should be called when the GameObject is updated!
		//--- Methods -----------------------------------------
	public:
		//! constructor
		GameObjectComponent();
		//! destructor
		virtual ~GameObjectComponent();
		//! overridable to load special attributes when GameObject is loaded from template
		virtual bool onLoadTemplate(tinyxml2::XMLElement* element) { return true;};
		//! overridable to save special attributes when GameObject is saved to template
		virtual bool onSaveTemplate(tinyxml2::XMLElement* element) { return true;};
		//! @brief return EventManager
		//!	calls GameObject::getEventManager
		//! @see EventHandler::getEventManager
		virtual EventManager* getEventManager();
		//! overridable to update the component
		virtual void onUpdateComponent(float deltaTime) {};
		//! @brief overridable - called after the owning GameObject was
		//! re-parented (@see GameObject::setParent)
		//! @param newParent the new parent GameObject (NULL = became a root)
		//! @param keepWorldTransform true = editor/runtime re-parent that must
		//! preserve the WORLD transform (recompute the local one); false =
		//! scene loading, the serialized LOCAL transform is authoritative
		virtual void onParentChanged(GameObject * newParent, bool keepWorldTransform) {};
		//! @brief overridable - called when the owning GameObject's EFFECTIVE
		//! active state (activeInHierarchy) changed (@see GameObject::setActive)
		//! @remarks components gate their own scene state here: render content
		//! hides, physics bodies leave the simulation, sounds stop; ticking is
		//! gated centrally in GameObjectManager::update
		virtual void onSetActive(bool activeInHierarchy) {};
		//--- SERIALIZATION ---
		//! @brief components are created through the ComponentHolder factory
		//! (GameObject::addComponent) before their state is loaded, never by
		//! the TypeManager - @see ISerializeable::createBeforeLoad
		virtual bool createBeforeLoad();
		//! @brief the DYNAMIC per-INSTANCE property schema of this component
		//! The static reflection half is a per-TYPE schema
		//! (TypeManager::getPropertySchema by TypeId); some components ALSO
		//! carry properties known only once a specific resource is attached -
		//! ScriptComponent's exported script properties are the case that drives
		//! this hook. The default is empty (a component's schema is fully
		//! static). The returned descriptors' get/set close over THIS instance,
		//! so a consumer reads/writes them exactly like a static property - it
		//! cannot tell the two apart. @see getComponentSchema for the union the
		//! consumers actually iterate.
		virtual PropertySchema getInstancePropertySchema() const
		{
			return PropertySchema();
		}
		//! does this component wants updates?
		inline bool getWantsUpdates();
		//! set if this component should receive updates
		void setWantsUpdates(bool wantsUpdates);
		//! get GameObject this Component belongs to
		inline GameObject* getGameObject();
	protected:
	private:
	};
	//---------------------------------------------------------
	inline bool GameObjectComponent::getWantsUpdates()
	{
		return this->wantsUpdates;
	}
	//---------------------------------------------------------
	inline GameObject* GameObjectComponent::getGameObject()
	{
		return this->getComponentOwner();
	}
	//---------------------------------------------------------
	//! @brief the full property schema of a component instance: the STATIC
	//! per-type schema (TypeManager, by the instance's dynamic TypeId) UNION the
	//! DYNAMIC per-instance schema (getInstancePropertySchema). This is the ONE
	//! query every reflection consumer (serialization, the debug protocol, the
	//! editor inspector, MCP) routes through, so a C++ component's static
	//! properties and a script's exported properties surface everywhere through
	//! the same path with zero per-consumer special-casing.
	//! Returned by value (the union is a fresh list); the dynamic half is cheap
	//! (a handful of descriptors), so consumers may call it per selected object.
	//! A dynamic property REPLACES a static one of the same name (PropertySchema
	//! ::add is by-name idempotent) - in practice the two name spaces are
	//! disjoint (a script component's small static schema vs. its exports).
	ORKIGE_CORE_DLL PropertySchema getComponentSchema(
		GameObjectComponent const & component);
	//---------------------------------------------------------
#define REGISTERGOCOMPONENT(Class) ::Orkige::ComponentHolder< ::Orkige::GameObjectComponent >::registerComponent<Class>();
	//put this in the OOBJECT_IMPL block of each component
#define GAMEOBJECTCOMPONENT() REGISTERGOCOMPONENT(OSelf)
}

#endif //__GameObjectComponent_h__15_8_2010__15_16_42__
