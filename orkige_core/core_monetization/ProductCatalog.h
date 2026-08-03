/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	ProductCatalog.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ProductCatalog_h__3_8_2026__10_00_00__
#define __ProductCatalog_h__3_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_util/String.h"

#include <cstddef>
#include <map>

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief what a game sells, as a LOGICAL catalog with a per-storefront
	//! identifier column - the data model the whole store seam is built on.
	//!
	//! THE SHAPE THAT MATTERS: one logical product carries a DIFFERENT
	//! identifier on every store it is sold through. Store consoles are
	//! independent registries; an identifier taken on one platform may be
	//! unavailable on another, a product re-created after a mistake gets a new
	//! one, and a game ported later inherits whatever was already registered.
	//! So the mapping is one-to-many by construction:
	//!
	//!     "remove_ads"  ->  ios      : "com.example.game.removeads"
	//!                       android  : "remove_ads_v2"
	//!                       simulated: "remove_ads"
	//!
	//! Game code, scenes, scripts and analytics only ever speak the LOGICAL id.
	//! The identifier that travels to a storefront is resolved at the seam, and
	//! the answers that come BACK carry the storefront's identifier, so the
	//! reverse lookup is load-bearing too: a restore reports a list of store
	//! identifiers with no request to correlate them against, and they have to
	//! become logical ids before an entitlement can be recorded.
	//!
	//! @remarks Pure data with no I/O. Loading a catalog from a manifest or a
	//! project file is a separate concern layered on top; the seam and its
	//! tests need only the mapping.
	class ORKIGE_CORE_DLL ProductCatalog
	{
		//--- Variables ---------------------------------------
	private:
		//! the products, keyed by LOGICAL id
		std::map<String, Product>	mProducts;
		//! per-storefront store id -> logical id (the reverse index)
		std::map<StorefrontId, std::map<String, String> >	mReverse;
		//--- Methods -----------------------------------------
	public:
		ProductCatalog();

		//! @brief add or replace a product (its per-storefront identifiers are
		//! bound separately, @see addStoreId). Replacing keeps any identifiers
		//! already bound, so refreshing a product's metadata after a store
		//! query cannot silently unsell it.
		void add(Product const & product);

		//! @brief bind a storefront identifier to a logical product.
		//! @return false when @p logicalId is not in the catalog, or when
		//! either id is empty - a silently dropped binding would surface much
		//! later as an unbuyable product
		bool addStoreId(String const & logicalId, StorefrontId storefront,
			String const & storeId);

		//! the product for a logical id, or NULL when absent
		Product const * find(String const & logicalId) const;
		//! mutable access for the metadata a completed product query fills in
		Product * findMutable(String const & logicalId);

		//! @brief the identifier @p logicalId is sold under on @p storefront,
		//! or "" when it is not sold there
		String storeIdFor(String const & logicalId, StorefrontId storefront) const;

		//! @brief THE REVERSE LOOKUP: the logical id behind a storefront's own
		//! identifier, or "" when this catalog does not sell it. Needed for
		//! every answer a store volunteers - restored entitlements and
		//! transactions that settle in a later session arrive with no request
		//! to correlate against.
		String logicalIdFor(StorefrontId storefront, String const & storeId) const;

		//! every store identifier this catalog sells on @p storefront
		StringVector storeIdsFor(StorefrontId storefront) const;
		//! every logical id, in a stable (sorted) order
		StringVector logicalIds() const;

		//! @brief does @p logicalId grant the no-ads entitlement (@see
		//! Product::grantsNoAds) - false for an unknown product
		bool grantsNoAds(String const & logicalId) const;

		//! how many products the catalog carries
		std::size_t count() const { return this->mProducts.size(); }
		//! drop everything
		void clear();
	};

	/** @} */
}

#endif //__ProductCatalog_h__3_8_2026__10_00_00__
