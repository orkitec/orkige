/**************************************************************
	created:	2026/08/03 at 14:00
	filename: 	HelpLinkTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//! @file HelpLinkTests.cpp
//! @brief the composition a refusal's doc link is built from.
//!
//! WHETHER the target exists is not this test's job and must not become it:
//! `doc_link_lint` (Util/check_doc_links.py) resolves every doc named anywhere
//! in the tree's sources against `Docs/` and fails when one is missing, which
//! is a tree-wide gate rather than a per-caller one. What is left here is the
//! pure part that gate cannot see - that a stem becomes the address of its
//! published page.

#include <core_util/HelpLink.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a doc name becomes its published page address", "[help]")
{
	// the portal renders one page per doc directly under the root, so the
	// stem IS the page - and the root ends in a separator, so the two
	// concatenate without a second opinion about slashes
	const Orkige::String root(Orkige::HELP_PORTAL_URL);
	REQUIRE_FALSE(root.empty());
	REQUIRE(root.back() == '/');
	REQUIRE(Orkige::helpUrl("sdk-pack") == root + "sdk-pack.html");
}
