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
#include <core_project/AssetDatabase.h>

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

	//! @brief test component that references an asset by project-relative
	//! path + stable asset id - the same serialization pattern the engine
	//! components (Model/Sprite/ScriptComponent) use, headless-testable:
	//! the id rides as the assetId attribute next to the path value and a
	//! resolving id wins over a stale path on load (rename survival)
	class TestAssetRefComponent : public GameObjectComponent
	{
		OOBJECT(TestAssetRefComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	private:
		String assetPath;		//!< project-relative asset path ("" = none)
		String assetId;		//!< stable asset id ("" = none/unknown)
		//--- Methods -----------------------------------------
	public:
		TestAssetRefComponent() {}
		virtual ~TestAssetRefComponent() {}
		String const & getAssetPath() const { return this->assetPath; }
		String const & getAssetId() const { return this->assetId; }
		//! set the referenced asset (id tracked via the active database)
		void setAssetReference(String const & path)
		{
			this->assetPath = path;
			this->assetId = AssetDatabase::referenceIdForValue(path, "",
				AssetDatabase::REF_PROJECT_PATH);
		}
		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar)
		{
			OParent::save(ar);
			ar->writeAttributed(this->assetPath,
				AssetDatabase::REFERENCE_ID_ATTRIBUTE,
				AssetDatabase::referenceIdForValue(this->assetPath,
					this->assetId, AssetDatabase::REF_PROJECT_PATH));
		}
		virtual void load(optr<IArchive> const & ar)
		{
			OParent::load(ar);
			ar->readAttributed(this->assetPath,
				AssetDatabase::REFERENCE_ID_ATTRIBUTE, this->assetId);
			AssetDatabase::resolveReference(this->assetPath, this->assetId,
				AssetDatabase::REF_PROJECT_PATH);
		}
	};

	//! @brief test component recording the hierarchy/active-state hooks
	//! (GameObjectComponent::onParentChanged / onSetActive) plus its update
	//! ticks - what the hierarchy unit tests probe
	class TestActivationProbeComponent : public GameObjectComponent
	{
		OOBJECT(TestActivationProbeComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	public:
		int			setActiveCalls;		//!< number of onSetActive calls
		bool		lastActiveState;	//!< last onSetActive argument
		int			parentChangedCalls;	//!< number of onParentChanged calls
		GameObject*	lastParent;			//!< last onParentChanged newParent
		bool		lastKeepWorld;		//!< last onParentChanged keepWorldTransform
		int			updateCalls;		//!< number of onUpdateComponent calls
		//--- Methods -----------------------------------------
	public:
		TestActivationProbeComponent()
			: setActiveCalls(0), lastActiveState(true)
			, parentChangedCalls(0), lastParent(NULL), lastKeepWorld(false)
			, updateCalls(0)
		{
			this->setWantsUpdates(true);
		}
		virtual ~TestActivationProbeComponent() {}
		virtual void onSetActive(bool activeInHierarchy)
		{
			++this->setActiveCalls;
			this->lastActiveState = activeInHierarchy;
		}
		virtual void onParentChanged(GameObject * newParent, bool keepWorldTransform)
		{
			++this->parentChangedCalls;
			this->lastParent = newParent;
			this->lastKeepWorld = keepWorldTransform;
		}
		virtual void onUpdateComponent(float deltaTime)
		{
			++this->updateCalls;
		}
	};

	//! register the test components once per process (component factory,
	//! TypeManager and Lua usertype - exactly what a module init does)
	void registerOrkigeTestComponents();
}

#endif //__TestComponents_h__7_7_2026__14_00_00__
