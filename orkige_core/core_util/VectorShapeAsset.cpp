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
	}
	//---------------------------------------------------------
	bool VectorShapeAsset::parse(String const & text,
		std::vector<VectorTessellator::Region> & outRegions)
	{
		outRegions.clear();
		std::vector<VectorTessellator::Region> regions;
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
				VectorTessellator::Region & region = regions.back();
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
				regions.push_back(region);
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
				regions.back().outer.clear();
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
				regions.back().holes.push_back(std::vector<VectorTessellator::Point>());
				target = TARGET_HOLE;
				remaining = count;
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

		// no dangling loop, at least one region, and every filled region must
		// actually be fillable (outer >= 3, any hole >= 3)
		if(remaining != 0 || regions.empty())
		{
			return false;
		}
		for(VectorTessellator::Region const & region : regions)
		{
			if(region.outer.size() < 3)
			{
				return false;
			}
			for(std::vector<VectorTessellator::Point> const & hole : region.holes)
			{
				if(hole.size() < 3)
				{
					return false;
				}
			}
		}
		outRegions.swap(regions);
		return true;
	}
}
