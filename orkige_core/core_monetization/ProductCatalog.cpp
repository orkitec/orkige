/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	ProductCatalog.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/ProductCatalog.h"

#include "core_debug/DebugMacros.h"

namespace Orkige
{
	//---------------------------------------------------------
	ProductCatalog::ProductCatalog()
	{
	}
	//---------------------------------------------------------
	void ProductCatalog::add(Product const & product)
	{
		if(product.id.empty())
		{
			oDebugWarn("monetization", 0,
				"a product with an empty logical id was ignored");
			return;
		}
		// replacing keeps the storefront bindings: a product query refreshes
		// title/price and must not unsell the product on any storefront
		this->mProducts[product.id] = product;
	}
	//---------------------------------------------------------
	bool ProductCatalog::addStoreId(String const & logicalId,
		StorefrontId storefront, String const & storeId)
	{
		if(logicalId.empty() || storeId.empty())
		{
			oDebugWarn("monetization", 0,
				"a storefront binding with an empty id was refused");
			return false;
		}
		if(this->mProducts.find(logicalId) == this->mProducts.end())
		{
			// binding an identifier to a product nobody declared would look
			// fine until the purchase, so it is refused here and now
			oDebugWarn("monetization", 0, "storefront binding for the unknown "
				"product '" << logicalId << "' was refused");
			return false;
		}
		this->mReverse[storefront][storeId] = logicalId;
		return true;
	}
	//---------------------------------------------------------
	Product const * ProductCatalog::find(String const & logicalId) const
	{
		std::map<String, Product>::const_iterator it =
			this->mProducts.find(logicalId);
		return (it == this->mProducts.end()) ? NULL : &it->second;
	}
	//---------------------------------------------------------
	Product * ProductCatalog::findMutable(String const & logicalId)
	{
		std::map<String, Product>::iterator it = this->mProducts.find(logicalId);
		return (it == this->mProducts.end()) ? NULL : &it->second;
	}
	//---------------------------------------------------------
	String ProductCatalog::storeIdFor(String const & logicalId,
		StorefrontId storefront) const
	{
		std::map<StorefrontId, std::map<String, String> >::const_iterator front =
			this->mReverse.find(storefront);
		if(front == this->mReverse.end()) { return String(); }

		// the forward direction is a scan of the (small) per-storefront column;
		// a catalog is tens of products, and keeping ONE index means the two
		// directions can never disagree
		for(std::map<String, String>::const_iterator it = front->second.begin();
			it != front->second.end(); ++it)
		{
			if(it->second == logicalId) { return it->first; }
		}
		return String();
	}
	//---------------------------------------------------------
	String ProductCatalog::logicalIdFor(StorefrontId storefront,
		String const & storeId) const
	{
		std::map<StorefrontId, std::map<String, String> >::const_iterator front =
			this->mReverse.find(storefront);
		if(front == this->mReverse.end()) { return String(); }

		std::map<String, String>::const_iterator it = front->second.find(storeId);
		return (it == front->second.end()) ? String() : it->second;
	}
	//---------------------------------------------------------
	StringVector ProductCatalog::storeIdsFor(StorefrontId storefront) const
	{
		StringVector ids;
		std::map<StorefrontId, std::map<String, String> >::const_iterator front =
			this->mReverse.find(storefront);
		if(front == this->mReverse.end()) { return ids; }

		ids.reserve(front->second.size());
		for(std::map<String, String>::const_iterator it = front->second.begin();
			it != front->second.end(); ++it)
		{
			ids.push_back(it->first);
		}
		return ids;
	}
	//---------------------------------------------------------
	StringVector ProductCatalog::logicalIds() const
	{
		StringVector ids;
		ids.reserve(this->mProducts.size());
		for(std::map<String, Product>::const_iterator it = this->mProducts.begin();
			it != this->mProducts.end(); ++it)
		{
			ids.push_back(it->first);
		}
		return ids;
	}
	//---------------------------------------------------------
	bool ProductCatalog::grantsNoAds(String const & logicalId) const
	{
		Product const * product = this->find(logicalId);
		return product != NULL && product->grantsNoAds;
	}
	//---------------------------------------------------------
	void ProductCatalog::clear()
	{
		this->mProducts.clear();
		this->mReverse.clear();
	}
}
