/**************************************************************
	created:	2026/07/24 at 18:00
	filename: 	OwnerComponentBadges.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file OwnerComponentBadges.cpp
//! @brief the global-owner component registry (@see OwnerComponentBadges.h)

#include "OwnerComponentBadges.h"
#include "GamePreviewStage.h"	// resolveActiveSceneCamera - the camera rule
#include "IconsFontAwesome6.h"

#include <engine_gocomponent/AtmosphereComponent.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>

namespace OrkigeEditor
{
	namespace
	{
		//! the camera owns-query: the editor shell has no window camera, so
		//! "owns" is the object the Game Preview would render through with no
		//! explicit source (a "Main Camera", else the first camera object) -
		//! the ONE rule both surfaces share (@see resolveActiveSceneCamera)
		bool cameraOwns(Orkige::GameObjectManager& world,
			Orkige::GameObject& object)
		{
			return resolveActiveSceneCamera(world) == &object;
		}

		//! the atmosphere owns-query: the component's LIVE ownership registry
		//! (identical semantics in the editor and the player - the first
		//! active instance owns, @see AtmosphereComponent)
		bool atmosphereOwns(Orkige::GameObjectManager& world,
			Orkige::GameObject& object)
		{
			(void)world;
			if(!object.hasComponent<Orkige::AtmosphereComponent>())
			{
				return false;
			}
			return object.getComponentPtr<Orkige::AtmosphereComponent>()
				->isAtmosphereOwner();
		}
	}
	//---------------------------------------------------------
	std::vector<OwnerComponentBadge> const& ownerComponentBadges()
	{
		static const std::vector<OwnerComponentBadge> badges = {
			{ "CameraComponent", ICON_FA_VIDEO, "the game view",
				"inactive - another CameraComponent is the scene camera",
				&cameraOwns },
			{ "AtmosphereComponent", ICON_FA_SUN, "the sky/fog atmosphere",
				"inactive - another AtmosphereComponent owns the atmosphere",
				&atmosphereOwns },
		};
		return badges;
	}
	//---------------------------------------------------------
	OwnerComponentBadge const* findOwnerComponentBadge(
		std::string const& componentTypeName)
	{
		for(OwnerComponentBadge const& badge : ownerComponentBadges())
		{
			if(componentTypeName == badge.componentTypeName)
			{
				return &badge;
			}
		}
		return nullptr;
	}
}
