/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	TestComponents.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "TestComponents.h"
#include "core_game/SceneSerializer.h"

namespace Orkige
{
	int TestHealthComponent::addCount = 0;
	int TestHealthComponent::removeCount = 0;
	//---------------------------------------------------------
	OOBJECT_IMPL(TestHealthComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getHealth)
		OFUNC(setHealth)
		// reflected so the per-property prefab override diff sees health;
		// save/load stays hand-written positional (unchanged format)
		OPROPERTY("health", Orkige::PropertyKind::Int, getHealth, setHealth, Orkige::PROP_NONE)
	OOBJECT_END
	//---------------------------------------------------------
	OOBJECT_IMPL(TestArmorComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getArmor)
		OFUNC(setArmor)
		OPROPERTY("armor", Orkige::PropertyKind::Int, getArmor, setArmor, Orkige::PROP_NONE)
	OOBJECT_END
	//---------------------------------------------------------
	OOBJECT_IMPL(TestAssetRefComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCCR(getAssetPath)
		OFUNCCR(getAssetId)
		OFUNC(setAssetReference)
	OOBJECT_END
	//---------------------------------------------------------
	OOBJECT_IMPL(TestActivationProbeComponent)
		GAMEOBJECTCOMPONENT()
	OOBJECT_END
	//---------------------------------------------------------
	OOBJECT_IMPL(TestReflectComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getCount)
		OFUNC(setCount)
		OFUNC(getSpeed)
		OFUNC(setSpeed)
		OFUNC(getEnabled)
		OFUNC(setEnabled)
		OFUNCCR(getLabel)
		OFUNC(setLabel)
		OFUNC(getTeam)
		OFUNC(setTeam)
		OFUNCCR(getIcon)
		OFUNC(setIcon)
		// neutral enum value<->label table + one reflected property of each core
		// PropertyKind (the dual-emitting macros run in this exact block)
		OENUM_REGISTER_START("TestTeam", TestReflectComponent::Team)
			OENUM_REGISTER_VALUE(TEAM_RED)
			OENUM_REGISTER_VALUE(TEAM_BLUE)
			OENUM_REGISTER_VALUE(TEAM_GREEN)
		OENUM_REGISTER_END
		OPROPERTY("count", Orkige::PropertyKind::Int, getCount, setCount, Orkige::PROP_NONE)
		OPROPERTY("speed", Orkige::PropertyKind::Float, getSpeed, setSpeed, Orkige::PROP_NONE)
		OPROPERTY("enabled", Orkige::PropertyKind::Bool, getEnabled, setEnabled, Orkige::PROP_NONE)
		OPROPERTY("label", Orkige::PropertyKind::String, getLabel, setLabel, Orkige::PROP_NONE)
		OPROPERTY_ENUM("team", "TestTeam", getTeam, setTeam, Orkige::PROP_NONE)
		OPROPERTY_REF("icon", Orkige::PropertyKind::AssetRef, "texture", getIcon, setIcon, Orkige::PROP_NONE)
	OOBJECT_END
	//---------------------------------------------------------
	void TestTweenTargetComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void TestTweenTargetComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(TestTweenTargetComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getScalar)
		OFUNC(setScalar)
		OFUNCCR(getName)
		OFUNC(setName)
		// the numeric-interpolatable kinds (Float/Vec3/Color) + a non-numeric
		// String (tween.property must reject it)
		OPROPERTY("scalar", Orkige::PropertyKind::Float, getScalar, setScalar, Orkige::PROP_NONE)
		OPROPERTY("offset", Orkige::PropertyKind::Vec3, getOffset, setOffset, Orkige::PROP_NONE)
		OPROPERTY("color", Orkige::PropertyKind::Color, getColor, setColor, Orkige::PROP_NONE)
		OPROPERTY("name", Orkige::PropertyKind::String, getName, setName, Orkige::PROP_NONE)
	OOBJECT_END
	//---------------------------------------------------------
	void registerOrkigeTestComponents()
	{
		static bool registered = false;
		if(!registered)
		{
			registered = true;
			TestHealthComponent::OrkigeMetaExport("orkige_core_tests");
			TestArmorComponent::OrkigeMetaExport("orkige_core_tests");
			TestAssetRefComponent::OrkigeMetaExport("orkige_core_tests");
			TestActivationProbeComponent::OrkigeMetaExport("orkige_core_tests");
			TestReflectComponent::OrkigeMetaExport("orkige_core_tests");
			TestTweenTargetComponent::OrkigeMetaExport("orkige_core_tests");
		}
	}
}
