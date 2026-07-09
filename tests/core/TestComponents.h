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
		int getHealth() const { return this->health; }
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
		int getArmor() const { return this->armor; }
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

	//! @brief reflection probe (task #94, P0/P1): a headless core component that
	//! declares one property of EACH core PropertyKind through the OPROPERTY*
	//! macros - Int/Float/Bool/String, an Enum (with a value<->label table) and
	//! an AssetRef. Exercising it in tests/core means the neutral registry +
	//! dual-emitting macros are proven in EVERY scripting config (LUA and OFF)
	//! AND on both render flavors (the core tests build in the default next tree
	//! too). All accessors are plain field get/set - no render system needed.
	class TestReflectComponent : public GameObjectComponent
	{
		OOBJECT(TestReflectComponent, GameObjectComponent)
		//--- Types -------------------------------------------
	public:
		//! the enum kind's probe (mirrors CameraComponent::ProjectionMode)
		enum Team
		{
			TEAM_RED	= 0,
			TEAM_BLUE	= 1,
			TEAM_GREEN	= 2
		};
		//--- Variables ---------------------------------------
	private:
		int		mCount;			//!< Int property backing field
		float	mSpeed;			//!< Float property backing field
		bool	mEnabled;		//!< Bool property backing field
		String	mLabel;			//!< String property backing field
		Team	mTeam;			//!< Enum property backing field
		String	mIconAssetId;	//!< AssetRef property backing field (an asset id)
		//--- Methods -----------------------------------------
	public:
		TestReflectComponent()
			: mCount(0), mSpeed(1.0f), mEnabled(false), mTeam(TEAM_RED)
		{
		}
		virtual ~TestReflectComponent() {}
		int getCount() const { return this->mCount; }
		void setCount(int value) { this->mCount = value; }
		float getSpeed() const { return this->mSpeed; }
		void setSpeed(float value) { this->mSpeed = value; }
		bool getEnabled() const { return this->mEnabled; }
		void setEnabled(bool value) { this->mEnabled = value; }
		String const & getLabel() const { return this->mLabel; }
		void setLabel(String const & value) { this->mLabel = value; }
		Team getTeam() const { return this->mTeam; }
		void setTeam(Team value) { this->mTeam = value; }
		String const & getIcon() const { return this->mIconAssetId; }
		void setIcon(String const & value) { this->mIconAssetId = value; }
	};

	//! @brief a headless reflected component carrying the NUMERIC-interpolatable
	//! property kinds plus a non-numeric one: a Float, a Vec3, a Color and a
	//! String. Drives both the property-path tween tests (tween a
	//! Float/Vec3/Color, reject the String) and the PER-PROPERTY prefab override
	//! test (a multi-property reflected component whose named fields diff
	//! individually). Uses reflection-driven save/load so its authored values
	//! ride the prefab/scene like a real reflected component.
	class TestTweenTargetComponent : public GameObjectComponent
	{
		OOBJECT(TestTweenTargetComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	private:
		float		mScalar;	//!< Float property backing field
		PropVec3	mOffset;	//!< Vec3 property backing field
		PropColor	mColor;		//!< Color property backing field
		String		mName;		//!< String property backing field (non-numeric)
		//--- Methods -----------------------------------------
	public:
		TestTweenTargetComponent() : mScalar(0.0f) {}
		virtual ~TestTweenTargetComponent() {}
		float getScalar() const { return this->mScalar; }
		void setScalar(float value) { this->mScalar = value; }
		PropVec3 getOffset() const { return this->mOffset; }
		void setOffset(PropVec3 const & value) { this->mOffset = value; }
		PropColor getColor() const { return this->mColor; }
		void setColor(PropColor const & value) { this->mColor = value; }
		String const & getName() const { return this->mName; }
		void setName(String const & value) { this->mName = value; }
		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar);
		virtual void load(optr<IArchive> const & ar);
	};

	//! register the test components once per process (component factory,
	//! TypeManager and Lua usertype - exactly what a module init does)
	void registerOrkigeTestComponents();
}

#endif //__TestComponents_h__7_7_2026__14_00_00__
