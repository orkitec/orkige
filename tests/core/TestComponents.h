/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	TestComponents.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TestComponents_h__7_7_2026__14_00_00__
#define __TestComponents_h__7_7_2026__14_00_00__

#include <core_game/GameObject.h>
#include <core_game/GameObjectComponent.h>

namespace Orkige
{
	//! @brief minimal stateful GameObjectComponent for the unit tests: one
	//! serialized int plus lifecycle counters. Registered through the same
	//! OOBJECT_IMPL/GAMEOBJECTCOMPONENT macro dance every real component uses.
	class TestHealthComponent : public GameObjectComponent
	{
		OOBJECT(TestHealthComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	public:
		static int addCount;	//!< number of onAdd calls process-wide
		static int removeCount;	//!< number of onRemove calls process-wide
	private:
		int health;				//!< the serialized test payload
		//--- Methods -----------------------------------------
	public:
		TestHealthComponent() : health(100) {}
		virtual ~TestHealthComponent() {}
		int getHealth() { return this->health; }
		void setHealth(int value) { this->health = value; }
		virtual void onAdd() { ++TestHealthComponent::addCount; }
		virtual void onRemove() { ++TestHealthComponent::removeCount; }
		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar)
		{
			OParent::save(ar);
			ar << this->health;
		}
		virtual void load(optr<IArchive> const & ar)
		{
			OParent::load(ar);
			ar >> this->health;
		}
	};

	//! @brief test component that declares a dependency on
	//! TestHealthComponent (like ModelComponent depends on TransformComponent)
	class TestArmorComponent : public GameObjectComponent
	{
		OOBJECT(TestArmorComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	private:
		int armor;				//!< the serialized test payload
		//--- Methods -----------------------------------------
	public:
		TestArmorComponent() : armor(0)
		{
			this->addDependency<TestHealthComponent>();
		}
		virtual ~TestArmorComponent() {}
		int getArmor() { return this->armor; }
		void setArmor(int value) { this->armor = value; }
		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar)
		{
			OParent::save(ar);
			ar << this->armor;
		}
		virtual void load(optr<IArchive> const & ar)
		{
			OParent::load(ar);
			ar >> this->armor;
		}
	};

	//! register both test components once per process (component factory,
	//! TypeManager and Lua usertype - exactly what a module init does)
	void registerOrkigeTestComponents();
}

#endif //__TestComponents_h__7_7_2026__14_00_00__
