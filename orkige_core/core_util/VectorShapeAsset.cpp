/**************************************************************
	created:	2026/07/10 at 10:00
	filename: 	VectorShapeAsset.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file VectorShapeAsset.cpp
//! @brief the `.oshape` grammar parser (@see VectorShapeAsset.h)

#include "core_util/VectorShapeAsset.h"

#include <sstream>

namespace Orkige
{
	namespace
	{
		//! which loop the current `contour`/`hole` run is filling
		enum FillTarget
		{
			TARGET_NONE,
			TARGET_OUTER,
			TARGET_HOLE
		};

		//! @brief every filled region must be fillable: outer >= 3, holes >= 3
		bool validateRegions(
			std::vector<VectorTessellator::Region> const & regions)
		{
			if(regions.empty())
			{
				return false;
			}
			for(VectorTessellator::Region const & region : regions)
			{
				if(region.outer.size() < 3)
				{
					return false;
				}
				for(std::vector<VectorTessellator::Point> const & hole :
					region.holes)
				{
					if(hole.size() < 3)
					{
						return false;
					}
				}
			}
			return true;
		}
	}
	//---------------------------------------------------------
	bool VectorShapeAsset::parse(String const & text,
		std::vector<VectorTessellator::Region> & outRegions)
	{
		outRegions.clear();
		ParsedShape parsed;
		if(!VectorShapeAsset::parse(text, parsed))
		{
			return false;
		}
		outRegions.swap(parsed.base);
		return true;
	}
	//---------------------------------------------------------
	bool VectorShapeAsset::parse(String const & text, ParsedShape & out)
	{
		out.base.clear();
		out.morphs.clear();

		// the region list currently being filled: the base first, then each
		// `morph` block switches to a fresh target's list
		std::vector<VectorTessellator::Region> base;
		std::vector<MorphTarget> morphs;
		std::vector<VectorTessellator::Region>* current = &base;
		bool haveRegion = false;
		FillTarget target = TARGET_NONE;
		int remaining = 0;	//!< vertices still expected for the open loop

		std::istringstream lines(text);
		String rawLine;
		while(std::getline(lines, rawLine))
		{
			// strip a trailing comment, then tokenize on whitespace
			const std::size_t hash = rawLine.find('#');
			if(hash != String::npos)
			{
				rawLine.erase(hash);
			}
			std::istringstream tokens(rawLine);
			String keyword;
			if(!(tokens >> keyword) || keyword.empty())
			{
				continue;	// blank / comment-only line
			}

			if(keyword == "v")
			{
				// a vertex only makes sense while a loop is open and unfilled
				if(target == TARGET_NONE || remaining <= 0)
				{
					return false;
				}
				float x = 0.0f, y = 0.0f;
				if(!(tokens >> x >> y))
				{
					return false;
				}
				VectorTessellator::Region & region = current->back();
				if(target == TARGET_OUTER)
				{
					region.outer.push_back(VectorTessellator::Point(x, y));
				}
				else
				{
					region.holes.back().push_back(
						VectorTessellator::Point(x, y));
				}
				--remaining;
			}
			else if(keyword == "fill")
			{
				// a fill can only open after the previous loop finished
				if(remaining != 0)
				{
					return false;
				}
				float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
				if(!(tokens >> r >> g >> b >> a))
				{
					return false;
				}
				VectorTessellator::Region region;
				region.fill = VectorTessellator::Colour(r, g, b, a);
				current->push_back(region);
				haveRegion = true;
				target = TARGET_NONE;
			}
			else if(keyword == "contour")
			{
				int count = 0;
				if(remaining != 0 || !haveRegion || !(tokens >> count) ||
					count <= 0)
				{
					return false;
				}
				current->back().outer.clear();
				target = TARGET_OUTER;
				remaining = count;
			}
			else if(keyword == "hole")
			{
				int count = 0;
				if(remaining != 0 || !haveRegion || !(tokens >> count) ||
					count <= 0)
				{
					return false;
				}
				current->back().holes.push_back(
					std::vector<VectorTessellator::Point>());
				target = TARGET_HOLE;
				remaining = count;
			}
			else if(keyword == "morph")
			{
				// a morph target opens only between complete loops, and only
				// after a base pose exists to blend from
				String name;
				tokens >> name;	// an unnamed target is allowed (empty name)
				if(remaining != 0 || base.empty())
				{
					return false;
				}
				MorphTarget morph;
				morph.name = name;
				morphs.push_back(morph);
				current = &morphs.back().regions;
				haveRegion = false;
				target = TARGET_NONE;
			}
			else if(keyword == "version")
			{
				int version = 0;
				if(!(tokens >> version) || version <= 0)
				{
					return false;
				}
			}
			// any other keyword (reserved words like stroke/gradient) is
			// ignored - but never mid-loop, which would corrupt the run
			else if(remaining != 0)
			{
				return false;
			}
		}

		// no dangling loop; the base and every morph target must be well-formed
		if(remaining != 0 || !validateRegions(base))
		{
			return false;
		}
		for(MorphTarget const & morph : morphs)
		{
			if(!validateRegions(morph.regions))
			{
				return false;
			}
		}
		out.base.swap(base);
		out.morphs.swap(morphs);
		return true;
	}
}
