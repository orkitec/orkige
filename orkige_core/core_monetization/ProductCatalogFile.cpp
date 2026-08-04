/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	ProductCatalogFile.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/ProductCatalogFile.h"

#include "core_monetization/MonetizationTypes.h"
#include "core_util/StringUtil.h"

#include <cstddef>
#include <sstream>
#include <string>

namespace Orkige
{
	char const * const ProductCatalogFile::EXTENSION = ".ocatalog";
	char const * const ProductCatalogFile::CATALOG_SETTING_KEY = "store.catalog";

	namespace
	{
		//! report "line N: what" into the optional error string
		bool fail(String * outError, int line, String const & what)
		{
			if(outError)
			{
				*outError = "line " + std::to_string(line) + ": " + what;
			}
			return false;
		}

		//! strip a `#` line comment and the surrounding whitespace
		String strippedLine(String const & raw)
		{
			String line = raw;
			const String::size_type comment = line.find('#');
			if(comment != String::npos) { line.erase(comment); }

			const String::size_type first = line.find_first_not_of(" \t\r\n");
			if(first == String::npos) { return ""; }
			const String::size_type last = line.find_last_not_of(" \t\r\n");
			return line.substr(first, last - first + 1);
		}

		//! read a `true`/`false` token (also accepting 1/0)
		bool readBool(String const & token, bool & out)
		{
			const String value = StringUtil::to_lower_copy(token);
			if(value == "true" || value == "1") { out = true; return true; }
			if(value == "false" || value == "0") { out = false; return true; }
			return false;
		}

		//! every storefront column a catalog line may name, in enum order
		StorefrontId const * storefrontColumns(std::size_t & outCount)
		{
			static const StorefrontId COLUMNS[] =
			{
				SF_IOS, SF_ANDROID, SF_MACOS, SF_WINDOWS, SF_WEB, SF_SIMULATED
			};
			outCount = sizeof(COLUMNS) / sizeof(COLUMNS[0]);
			return COLUMNS;
		}
	}
	//---------------------------------------------------------
	bool ProductCatalogFile::parse(String const & text, ProductCatalog & out,
		String * outError)
	{
		// A FAILED PARSE MUST NEVER LEAVE HALF A CATALOG BEHIND: a game running
		// on a partially-read catalog would sell some products and silently
		// refuse others, which is the same fault the line-numbered refusals
		// exist to prevent. So the file is built into a catalog of our own and
		// only handed over once the whole of it was understood.
		out.clear();
		if(outError) { outError->clear(); }

		ProductCatalog working;
		std::istringstream lines(text);
		String rawLine;
		int lineNumber = 0;
		bool sawDirective = false;
		// the product later directives attach to ("" = none opened yet)
		String currentId;

		while(std::getline(lines, rawLine))
		{
			++lineNumber;
			const String line = strippedLine(rawLine);
			if(line.empty()) { continue; }

			std::istringstream tokens(line);
			String directive;
			tokens >> directive;
			const String key = StringUtil::to_lower_copy(directive);

			if(key == "version")
			{
				if(sawDirective)
				{
					return fail(outError, lineNumber,
						"'version' must be the first directive");
				}
				int version = 0;
				if(!(tokens >> version) || version != 1)
				{
					return fail(outError, lineNumber,
						"only version 1 catalogs are understood");
				}
				sawDirective = true;
				continue;
			}
			sawDirective = true;

			if(key == "product")
			{
				String logicalId;
				if(!(tokens >> logicalId) || logicalId.empty())
				{
					return fail(outError, lineNumber,
						"'product' needs a logical id");
				}
				if(working.find(logicalId) != NULL)
				{
					return fail(outError, lineNumber, "the product '"
						+ logicalId + "' is already in this catalog");
				}
				Product product;
				product.id = logicalId;
				working.add(product);
				currentId = logicalId;
				continue;
			}

			if(currentId.empty())
			{
				return fail(outError, lineNumber, "'" + directive + "' has no "
					"product to belong to - open one with 'product <id>' first");
			}

			if(key == "kind")
			{
				String kindToken;
				if(!(tokens >> kindToken))
				{
					return fail(outError, lineNumber, "'kind' needs a value "
						"(consumable, non_consumable or subscription)");
				}
				ProductKind kind = PK_CONSUMABLE;
				if(!productKindFromName(StringUtil::to_lower_copy(kindToken),
					kind))
				{
					return fail(outError, lineNumber, "'" + kindToken + "' is "
						"not a product kind (consumable, non_consumable or "
						"subscription)");
				}
				Product * product = working.findMutable(currentId);
				product->kind = kind;
				continue;
			}

			if(key == "noads")
			{
				String boolToken;
				bool grantsNoAds = false;
				if(!(tokens >> boolToken) || !readBool(boolToken, grantsNoAds))
				{
					return fail(outError, lineNumber,
						"'noads' needs true or false");
				}
				Product * product = working.findMutable(currentId);
				product->grantsNoAds = grantsNoAds;
				continue;
			}

			// anything left has to be a storefront column
			const StorefrontId storefront = storefrontFromName(key);
			if(storefront == SF_UNKNOWN)
			{
				return fail(outError, lineNumber, "'" + directive + "' is not "
					"a catalog directive (kind, noads, or a storefront: ios, "
					"android, macos, windows, web, simulated)");
			}
			String storeId;
			if(!(tokens >> storeId) || storeId.empty())
			{
				return fail(outError, lineNumber, "'" + directive + "' needs "
					"the identifier this product is sold under there");
			}
			if(!working.addStoreId(currentId, storefront, storeId))
			{
				return fail(outError, lineNumber, "'" + storeId + "' could not "
					"be bound to '" + currentId + "'");
			}
		}

		out = working;
		return true;
	}
	//---------------------------------------------------------
	String ProductCatalogFile::serialize(ProductCatalog const & catalog)
	{
		std::ostringstream text;
		text << "version 1\n";

		std::size_t columnCount = 0;
		StorefrontId const * columns = storefrontColumns(columnCount);

		const StringVector ids = catalog.logicalIds();
		for(std::size_t i = 0; i < ids.size(); ++i)
		{
			Product const * product = catalog.find(ids[i]);
			if(product == NULL) { continue; }

			text << "\nproduct " << product->id << "\n";
			text << "\tkind " << productKindName(product->kind) << "\n";
			if(product->grantsNoAds)
			{
				text << "\tnoads true\n";
			}
			for(std::size_t c = 0; c < columnCount; ++c)
			{
				const String storeId = catalog.storeIdFor(product->id,
					columns[c]);
				if(storeId.empty()) { continue; }
				text << "\t" << storefrontName(columns[c]) << " " << storeId
					<< "\n";
			}
		}
		return text.str();
	}
}
