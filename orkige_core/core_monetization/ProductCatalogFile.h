/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	ProductCatalogFile.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ProductCatalogFile_h__4_8_2026__10_00_00__
#define __ProductCatalogFile_h__4_8_2026__10_00_00__

//! @file ProductCatalogFile.h
//! @brief parser for the `.ocatalog` text asset - what a game sells, in one
//! diffable file.
//!
//! WHY THIS IS A FILE AND NOT CODE: the identifiers a game is sold under are
//! decided in each store's console, per platform, by whoever set the products
//! up - and they change (a product re-created after a mistake gets a new one).
//! A catalog in C++ makes that a rebuild; a catalog in the project makes it an
//! edit an agent can perform over write_project_file and the editor can show.
//! It is a CONFIG ASSET (@see the manifest Setting `store.catalog`), so it
//! lives outside `assets/`, is not id-tracked, and ships with every export.
//!
//! NOTHING SECRET LIVES HERE. Store product identifiers are public - they
//! travel to the storefront from the player's own device and appear in every
//! receipt. Signing credentials and API keys do NOT (@see Docs/security.md);
//! the catalog has no field for one and must never grow one.
//!
//! GRAMMAR - one directive per LINE, `#` starts a line comment, tokens are
//! whitespace-separated and keywords are case-insensitive:
//!
//!   version 1                 optional; must be the FIRST directive, and only
//!                             version 1 is accepted
//!   product LOGICAL_ID        begins a product. LOGICAL_ID is what game code,
//!                             scenes, scripts and analytics say forever.
//!
//! and inside a product, in any order:
//!
//!   kind consumable | non_consumable | subscription
//!                             what owning it means (default: consumable)
//!   noads true | false        owning it suppresses ad serving - THE link
//!                             between the store and ad sides (default: false)
//!   ios STORE_ID              the identifier it is sold under on that
//!   android STORE_ID          storefront. One line per store it ships to; a
//!   macos STORE_ID            product with no line for the running platform's
//!   windows STORE_ID          storefront is refused BY NAME when someone tries
//!   web STORE_ID              to buy it, rather than sent as an empty string.
//!   simulated STORE_ID
//!
//! EVERYTHING IS AN ERROR THAT IS NOT UNDERSTOOD - an unknown directive, an
//! unknown product kind, a duplicate logical id, a storefront line before any
//! `product`. A typo silently ignored here is an unbuyable product discovered
//! by a player, so the parse refuses with the LINE NUMBER and the editor's live
//! diagnostics turn it into a clickable marker.

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/ProductCatalog.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief the `.ocatalog` reader - pure text in, ProductCatalog out.
	class ORKIGE_CORE_DLL ProductCatalogFile
	{
	public:
		//! the extension a catalog asset carries
		static char const * const EXTENSION;

		//! @brief the manifest Settings key naming the project's catalog file
		//! ("store.catalog"), a project-relative path. It is a CONFIG ASSET, so
		//! it also has to appear in the exporter's config-setting vocabulary or
		//! it would not ship.
		static char const * const CATALOG_SETTING_KEY;

		//! @brief parse `.ocatalog` text into @p out (which is CLEARED first, so
		//! a failed parse never leaves half a catalog behind).
		//! @param outError optional; filled with "line N: ..." on failure
		//! @return false on the first thing it does not understand
		static bool parse(String const & text, ProductCatalog & out,
			String * outError);

		//! @brief clean-format inverse of parse(): parse(serialize(c)) rebuilds
		//! c. Products come out in the catalog's own stable (sorted) order, so
		//! a rewrite is diffable.
		static String serialize(ProductCatalog const & catalog);
	};

	/** @} */
}

#endif //__ProductCatalogFile_h__4_8_2026__10_00_00__
