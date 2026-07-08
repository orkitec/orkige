/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	TestComponents.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "TestComponents.h"

namespace Orkige
{
	int TestHealthComponent::addCount = 0;
	int TestHealthComponent::removeCount = 0;
	//---------------------------------------------------------
	OOBJECT_IMPL(TestHealthComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getHealth)
		OFUNC(setHealth)
	OOBJECT_END
	//---------------------------------------------------------
	OOBJECT_IMPL(TestArmorComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getArmor)
		OFUNC(setArmor)
	OOBJECT_END
	//---------------------------------------------------------
	OOBJECT_IMPL(TestAssetRefComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCCR(getAssetPath)
		OFUNCCR(getAssetId)
		OFUNC(setAssetReference)
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
		}
	}
}
