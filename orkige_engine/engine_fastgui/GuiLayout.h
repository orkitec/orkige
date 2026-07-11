/********************************************************************
	created:	Saturday 2026/07/11 at 16:00
	filename: 	GuiLayout.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiLayout_h__11_7_2026__16_00_00__
#define __GuiLayout_h__11_7_2026__16_00_00__

//! @file GuiLayout.h
//! @brief the declarative UI layout (.oui) document model: an ordered list of
//! `[Type id]` sections, each an ordered list of `key = value` entries. Pure
//! text - no renderer, no Ogre, no scripting - so it parses/serialises
//! headlessly (round-trip unit-tested) and compiles in the ORKIGE_NOSCRIPT
//! build. FastGuiFactory::loadLayout turns a parsed document into widgets; an
//! agent authors the file directly via the MCP write_project_file verb.
//!
//! Grammar (canonical form is what serialize() emits):
//!   # comment            (# or ; ; ignored, dropped on reparse)
//!   [Type id]            (section header; id optional - e.g. [Layout])
//!   key = value          (entry; the parser also accepts ':' or a tab)
//! A blank line separates sections in the canonical form.

#include "engine_module/EnginePrerequisites.h"

#include <vector>

namespace Orkige
{
	//! @brief one `key = value` line of a section (order preserved)
	struct ORKIGE_ENGINE_DLL GuiLayoutEntry
	{
		String	key;
		String	value;
	};

	//! @brief one `[Type id]` section and its entries (order preserved)
	struct ORKIGE_ENGINE_DLL GuiLayoutSection
	{
		String						type;		//!< e.g. "Label", "ScrollView", "Layout"
		String						id;			//!< widget id ("" for the global [Layout])
		std::vector<GuiLayoutEntry>	entries;

		//! first value for a key (case-sensitive), or NULL if absent
		String const * find(String const & key) const;
		//! set / overwrite the first entry for a key (append if absent)
		void set(String const & key, String const & value);
	};

	//! @brief a parsed .oui document: an ordered list of sections
	class ORKIGE_ENGINE_DLL GuiLayoutDoc
	{
	public:
		std::vector<GuiLayoutSection>	sections;

		//! @brief parse .oui text. Returns false + a message on a malformed line
		//! (a key outside any section, or an empty `[]` header).
		static bool parse(String const & text, GuiLayoutDoc & out, String & error);
		//! @brief emit the canonical text (round-trips: parse then serialize is
		//! idempotent). Comments/blank input lines are not preserved.
		String serialize() const;

		//! the first section of a type (case-sensitive), or NULL
		GuiLayoutSection const * findSection(String const & type) const;
	};
}

#endif //__GuiLayout_h__11_7_2026__16_00_00__
