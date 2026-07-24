/********************************************************************
	created:	Friday 2026/07/24 at 18:00
	filename: 	OwnerComponentBadges.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __OwnerComponentBadges_h__24_7_2026__18_00_00__
#define __OwnerComponentBadges_h__24_7_2026__18_00_00__

//! @file OwnerComponentBadges.h
//! @brief the editor's registry of GLOBAL-OWNER component kinds - components
//! where one instance owns a piece of global state (the window camera, the
//! world atmosphere) while further instances stay dormant. Each entry maps a
//! component type to its Hierarchy row glyph, an owns-query answered against
//! the edit document, and the Inspector's dormancy note. The Hierarchy draws
//! the glyph ACCENT-coloured on the owning instance and DIMMED on dormant
//! ones; the Inspector shows the note under a dormant instance's header.
//! A future takeover component joins by adding ONE entry here.

#include <string>
#include <vector>

namespace Orkige
{
	class GameObject;
	class GameObjectManager;
}

namespace OrkigeEditor
{
	//! one global-owner component kind (@see the file comment)
	struct OwnerComponentBadge
	{
		//! the component's TypeInfo name (the key every editor surface uses)
		const char* componentTypeName;
		//! the inline FA glyph (UTF-8, from IconsFontAwesome6.h; the codepoint
		//! must be in EditorTheme's ICON_GLYPH_RANGES)
		const char* glyph;
		//! hover text for the Hierarchy glyph ("scene camera" / "atmosphere")
		const char* ownedNoun;
		//! the Inspector line under a DORMANT instance's header
		const char* dormantNote;
		//! does THIS object's instance own the global right now (answered
		//! against the edit document - dormant-safe, no gameplay)
		bool (*owns)(Orkige::GameObjectManager& world, Orkige::GameObject& object);
	};

	//! the registered owner-component kinds (CameraComponent, AtmosphereComponent)
	std::vector<OwnerComponentBadge> const& ownerComponentBadges();

	//! the badge for a component type name, or NULL when the type carries none
	OwnerComponentBadge const* findOwnerComponentBadge(
		std::string const& componentTypeName);
}

#endif //__OwnerComponentBadges_h__24_7_2026__18_00_00__
