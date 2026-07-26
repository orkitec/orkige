/**************************************************************
	created:	2010/08/15 at 15:20
	filename: 	GameObjectComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/GameObjectComponent.h"
#include "core_game/GameObject.h"
#include "core_base/TypeManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	PropertySchema getComponentSchema(GameObjectComponent const & component)
	{
		// the static per-type half COMPOSED along the base chain with the disable
		// opt-out applied (@see getComponentStaticSchema), so the generic base
		// `enabled` inherits onto every kind that supports it
		PropertySchema schema = getComponentStaticSchema(component);
		// the dynamic per-instance half (empty for a fully-static component) -
		// appended AFTER the static ones so a script's exports render below the
		// component's own fields, and REPLACE a static of the same name
		PropertySchema const dynamicSchema = component.getInstancePropertySchema();
		for (PropertyDesc const & desc : dynamicSchema.properties())
		{
			schema.add(desc);
		}
		return schema;
	}
	//---------------------------------------------------------
	PropertySchema getComponentStaticSchema(GameObjectComponent const & component)
	{
		// the STATIC per-type schema COMPOSED along the reflection base chain, so
		// a base-declared property (the generic `enabled` on GameObjectComponent)
		// surfaces on every subclass with no per-kind redeclaration. A frozen
		// kind (ScriptComponent/AtmosphereComponent) that declares its OWN
		// `enabled` shadows the inherited one by name (base-first composition).
		PropertySchema schema =
			TypeManager::getSingleton().getInheritedPropertySchema(
				component.getTypeInfo().getId());
		// opt-out: a kind with no coherent disable (TransformComponent, ...) drops
		// the inherited `enabled` so no lying checkbox reaches the inspector / MCP
		// / serialization. Frozen kinds keep THEIR own enabled - they support
		// disable, so this never fires for them.
		if (!component.supportsDisable())
		{
			schema.remove(GameObjectComponent::ENABLED_PROPERTY);
		}
		return schema;
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	const char * const GameObjectComponent::ENABLED_PROPERTY = "enabled";
	//---------------------------------------------------------
	GameObjectComponent::GameObjectComponent() : wantsUpdates(false), mEnabled(true)
	{
	}
	//---------------------------------------------------------
	GameObjectComponent::~GameObjectComponent()
	{
	}
	//---------------------------------------------------------
	EventManager* GameObjectComponent::getEventManager()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		EventManager* eventManager = componentOwner->getEventManager();
		oAssert(eventManager);
		return eventManager;
	}
	//---------------------------------------------------------
	bool GameObjectComponent::createBeforeLoad()
	{
		return false;
	}
	//---------------------------------------------------------
	TypeInfo GameObjectComponent::getComponentKey() const
	{
		// discover the container key by finding THIS instance in the owner's
		// component map: a name-aliased kind (a script component) is stored under
		// its script name, not its C++ class, and that key is the identity every
		// consumer uses. No per-component storage keeps the class ABI-stable.
		GameObject* owner = const_cast<GameObjectComponent*>(this)->getComponentOwner();
		if (owner)
		{
			for (auto const & entry : owner->getComponents())
			{
				if (entry.second.get() == this)
				{
					return TypeInfo(entry.first);
				}
			}
		}
		// not (yet) owned: the C++ TypeInfo IS the key (they coincide for every
		// plain component). TypeInfo's copy ctor is explicit - direct-initialise.
		return TypeInfo(this->getTypeInfo());
	}
	//---------------------------------------------------------
	void GameObjectComponent::setEnabled(bool enabled)
	{
		// a kind with no coherent disable (TransformComponent, ...) is inert -
		// its schema never exposes `enabled`, so this only guards a direct call
		if (!this->supportsDisable())
		{
			return;
		}
		if (this->mEnabled == enabled)
		{
			return;
		}
		this->mEnabled = enabled;
		// funnel into the ONE per-kind suspend/resume path - the same one an
		// owner active-state change takes, so the two axes compose (a disabled
		// owner keeps content suspended even as the component flag flips)
		this->applyEffectiveEnabled();
	}
	//---------------------------------------------------------
	bool GameObjectComponent::effectivelyEnabled() const
	{
		if (!this->mEnabled)
		{
			return false;
		}
		GameObject* owner = const_cast<GameObjectComponent*>(this)->getComponentOwner();
		return !owner || owner->isActiveInHierarchy();
	}
	//---------------------------------------------------------
	void GameObjectComponent::setWantsUpdates(bool wantsUpdates)
	{
		this->wantsUpdates = wantsUpdates;
		if(this->getComponentOwner())
		{
			// key off the container KIND, so a name-aliased script component
			// (stored under its script name, not "ScriptComponent") registers
			const TypeInfo key = this->getComponentKey();
			if(this->wantsUpdates)
			{
				this->getComponentOwner()->enableUpdates(key);
			}
			else
			{
				this->getComponentOwner()->disableUpdates(key);
			}
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	IMPLEMENT_COMPONENT(GameObject)

	OOBJECT_IMPL(GameObjectComponent)
		OFUNCIR(getDependencies)
		OFUNC(setEnabled)
		OFUNC(isEnabled)
		// the ONE generic per-component enable switch, declared ONCE on the base:
		// every subclass inherits the reflected `enabled` property through schema
		// inheritance (@see getComponentStaticSchema), and a kind that cannot be
		// disabled opts out of exposing it via supportsDisable. It serializes
		// through the ONE schema path, but ONLY when false (the SceneSerializer
		// omits the generic `enabled` at its default true), so an untouched scene
		// stays byte-identical. A frozen kind (Script/Atmosphere) declares its OWN
		// `enabled` which shadows this one by name and serializes unconditionally.
		OPROPERTY("enabled", Orkige::PropertyKind::Bool, isEnabled, setEnabled, Orkige::PROP_NONE)
	OOBJECT_END
}
