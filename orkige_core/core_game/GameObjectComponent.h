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
#include "core_event/EventType.h"
#include "core_event/EventListener.h"
#include "core_event/EventHandler.h"

class TiXmlDocument;
class TiXmlElement;

namespace Orkige
{
	class GameObject;

	//! Base Component for GameObjects
	class ORKIGE_DLL GameObjectComponent : public Component<GameObject>, public EventHandler
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
		virtual bool onLoadTemplate(TiXmlElement* element) { return true;};
		//! overridable to save special attributes when GameObject is saved to template
		virtual bool onSaveTemplate(TiXmlElement* element) { return true;};
		//! @brief return EventManager
		//!	calls GameObject::getEventManager
		//! @see EventHandler::getEventManager
		virtual EventManager* getEventManager();
		//! overridable to update the component
		virtual void onUpdateComponent(float deltaTime) {};
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
#define REGISTERGOCOMPONENT(Class) ::Orkige::ComponentHolder< ::Orkige::GameObjectComponent >::registerComponent<Class>();
	//put this in the OOBJECT_IMPL block of each component
#define GAMEOBJECTCOMPONENT() REGISTERGOCOMPONENT(OSelf)
}

#endif //__GameObjectComponent_h__15_8_2010__15_16_42__
