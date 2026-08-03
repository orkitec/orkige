/********************************************************************
	created:	Sunday 2026/08/03 at 10:00
	filename: 	EditorHelpLinks.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorHelpLinks_h__3_8_2026__10_00_00__
#define __EditorHelpLinks_h__3_8_2026__10_00_00__

#include <core_util/String.h>

//! @file EditorHelpLinks.h
//! @brief the ONE published documentation root, and the one composition that
//! turns a doc's own file name into a link to its page.
//!
//! @par Why a composition rather than a written-out URL
//! A message that says only what failed leaves a person stuck, so the refusals
//! that can be read at length point at the page that explains them. But a URL
//! spelled out inside a C++ string is invisible to the docs build: the portal
//! generator (Util/make_help_portal.py) fails on a broken link BETWEEN docs and
//! would never see one hard-coded here, so a renamed page would ship as a dead
//! link with nothing to notice it.
//!
//! Composing the link from this root plus the doc's file stem makes the C++
//! side carry a DOC NAME instead of a URL, and a doc name is checkable without
//! a network: `doc_link_lint` (Util/check_doc_links.py, a unit-labelled ctest)
//! reads every doc named in the tree's own sources - the `helpUrl` arguments
//! here and the `Docs/<name>.md` references the exporter and the engine write
//! into their messages - and fails when one of them is not a file in `Docs/`.
//! Renaming a doc therefore breaks the build rather than the link.

namespace OrkigeEditor
{
	//! the published portal root. `Help > Orkige Help` opens exactly this, and
	//! every deeper link is built from it (@ref helpUrl).
	constexpr char const * HELP_PORTAL_ROOT =
		"https://orkige.orkitec.com/help/";

	//! @brief the published page for `Docs/<page>.md`.
	//! @param page the doc's file STEM ("device-payloads"), never a URL and
	//! never a path - which is what makes the reference checkable against the
	//! corpus on disk.
	inline Orkige::String helpUrl(Orkige::String const & page)
	{
		// the generator renders one .html per doc directly under the portal
		// root, so the stem IS the page
		return Orkige::String(HELP_PORTAL_ROOT) + page + ".html";
	}
}

#endif //__EditorHelpLinks_h__3_8_2026__10_00_00__
