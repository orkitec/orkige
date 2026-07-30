/********************************************************************
	created:	Wednesday 2026/07/29 at 21:00
	filename: 	SvgShapeCookImpl.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The SECOND (and last) translation unit that talks to nanosvg, sibling of
	SvgRasterImpl.cpp: the rasteriser bakes a vector UI sprite into a texture
	page, this one converts a vector drawing into `.oshape` contours. Both need
	the SAME parser, so they share the one library instead of the tree carrying
	a second SVG reader - and they are the only two files that include its
	headers, so nanosvg stays out of every header, the neutral umbrella and the
	precompiled header (the engine_sound/StbVorbisImpl.cpp isolation pattern).
	The vcpkg nanosvg port precompiles the parser into a static lib, so this TU
	only includes the declaration header - no NANOSVG_IMPLEMENTATION define.

	Everything above reaches the conversion ONLY through SvgShapeCook.h, which
	depends on orkige_core alone: a cook is host-side tooling, so the shapecook
	CLI compiles this TU directly rather than linking the engine closure.
*********************************************************************/

#include "engine_gui/SvgShapeCook.h"

#include "core_util/ShapeCollider.h"
#include "core_util/VectorShapeAsset.h"
#include "core_util/VectorShapeCook.h"
#include "core_util/VectorTessellator.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wcast-qual"
#elif defined(__GNUC__)
#	pragma GCC diagnostic push
#endif

// bare names: the vcpkg NanoSVG target exports include/nanosvg AS its include
// directory, so the prefixed spelling only ever resolved by riding another
// dependency's include root - which a lean tool target does not have
#include <nanosvg.h>

#if defined(__clang__)
#	pragma clang diagnostic pop
#elif defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif

namespace Orkige
{
	namespace
	{
		typedef VectorTessellator::Point Point;
		typedef VectorTessellator::Region Region;

		//! @brief a parsed document that always deletes itself (nsvgParse
		//! mutates its input, so the caller's bytes are copied first)
		class ParsedDocument
		{
		public:
			ParsedDocument(unsigned char const * svg, int size)
			{
				// nsvgParse requires a mutable, NUL-terminated buffer
				std::vector<char> text(svg, svg + size);
				text.push_back('\0');
				this->mImage = nsvgParse(text.data(), "px", 96.0f);
			}
			~ParsedDocument()
			{
				if(this->mImage != nullptr)
				{
					nsvgDelete(this->mImage);
				}
			}
			ParsedDocument(ParsedDocument const &) = delete;
			ParsedDocument & operator=(ParsedDocument const &) = delete;
			NSVGimage * get() const { return this->mImage; }
		private:
			NSVGimage *	mImage = nullptr;
		};

		//! @brief the flat colour a filled shape paints with. A gradient has no
		//! runtime paint yet, so it flattens to its FIRST stop - the drawing's
		//! dominant colour rather than an arbitrary default.
		VectorTessellator::Colour fillColour(NSVGshape const * shape)
		{
			unsigned int packed = 0xff000000u;	// opaque black, the SVG default
			if(shape->fill.type == NSVG_PAINT_COLOR)
			{
				packed = shape->fill.color;
			}
			else if((shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT ||
				shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) &&
				shape->fill.gradient != nullptr &&
				shape->fill.gradient->nstops > 0)
			{
				packed = shape->fill.gradient->stops[0].color;
			}
			// nanosvg packs r,g,b low-to-high with the fill-opacity byte on top;
			// the element's own `opacity` multiplies on
			const float scale = 1.0f / 255.0f;
			return VectorTessellator::Colour(
				float(packed & 0xffu) * scale,
				float((packed >> 8) & 0xffu) * scale,
				float((packed >> 16) & 0xffu) * scale,
				float((packed >> 24) & 0xffu) * scale * shape->opacity);
		}

		//! @brief flatten ONE subpath's cubic run into a point loop, dropping a
		//! repeated closing vertex (the shared normalisation every contour
		//! consumer applies). An empty result means the subpath encloses nothing.
		std::vector<Point> flattenSubpath(NSVGpath const * path,
			double tolerance, bool uniform)
		{
			std::vector<Point> points;
			if(path->npts < 1)
			{
				return points;
			}
			points.push_back(Point(path->pts[0], path->pts[1]));
			// a subpath is a run of cubics sharing endpoints: 6 floats advance
			// one segment, and its last point is the next one's first
			for(int i = 0; i + 3 < path->npts; i += 3)
			{
				float const * span = &path->pts[i * 2];
				const Point p0(span[0], span[1]);
				const Point p1(span[2], span[3]);
				const Point p2(span[4], span[5]);
				const Point p3(span[6], span[7]);
				if(VectorShapeCook::isStraightCubic(p0, p1, p2, p3))
				{
					points.push_back(p3);	// a line command, written as a cubic
				}
				else if(uniform)
				{
					VectorShapeCook::flattenCubicUniform(p0, p1, p2, p3,
						VectorShapeCook::UNIFORM_CURVE_SEGMENTS, points);
				}
				else
				{
					VectorTessellator::flattenCubic(p0, p1, p2, p3,
						float(tolerance), points);
				}
			}
			std::vector<Point> loop = ShapeCollider::openLoop(points);
			if(loop.size() < 3)
			{
				loop.clear();	// nothing enclosing an area
			}
			return loop;
		}
	}

	namespace SvgShapeCook
	{
		//---------------------------------------------------------
		bool extractRegions(unsigned char const * svg, int size,
			VectorShapeCook::Options const & options,
			std::vector<Region> & outRegions, int * outFilledShapes,
			String * error)
		{
			outRegions.clear();
			if(outFilledShapes != nullptr)
			{
				*outFilledShapes = 0;
			}
			auto fail = [error](char const * message) -> bool
			{
				if(error != nullptr)
				{
					*error = message;
				}
				return false;
			};
			if(svg == nullptr || size <= 0)
			{
				return fail("the SVG source is empty");
			}
			ParsedDocument document(svg, size);
			if(document.get() == nullptr)
			{
				return fail("the SVG could not be parsed");
			}
			const double tolerance = VectorShapeCook::resolveTolerance(options,
				double(document.get()->width), double(document.get()->height));

			int filled = 0;
			for(NSVGshape const * shape = document.get()->shapes;
				shape != nullptr; shape = shape->next)
			{
				// an unfilled outline and a hidden element paint nothing
				if(shape->fill.type == NSVG_PAINT_NONE ||
					(shape->flags & NSVG_FLAGS_VISIBLE) == 0)
				{
					continue;
				}
				++filled;
				const VectorTessellator::Colour colour = fillColour(shape);
				// the regions THIS shape contributes; a later subpath contained
				// by an earlier one is that region's hole, not a second fill
				const std::size_t firstRegion = outRegions.size();
				// the parser PREPENDS each subpath, so its list runs backwards;
				// paint order (and hole ownership, which needs the enclosing
				// subpath first) follows the document
				std::vector<NSVGpath const *> subpaths;
				for(NSVGpath const * path = shape->paths; path != nullptr;
					path = path->next)
				{
					subpaths.push_back(path);
				}
				std::reverse(subpaths.begin(), subpaths.end());
				for(NSVGpath const * path : subpaths)
				{
					std::vector<Point> loop =
						flattenSubpath(path, tolerance, options.uniform);
					if(loop.empty())
					{
						continue;
					}
					std::size_t owner = outRegions.size();
					for(std::size_t i = outRegions.size(); i > firstRegion; --i)
					{
						if(VectorShapeCook::containsPoint(
							outRegions[i - 1].outer, loop.front()))
						{
							owner = i - 1;
							break;
						}
					}
					if(owner < outRegions.size())
					{
						outRegions[owner].holes.push_back(loop);
						continue;
					}
					Region region;
					region.fill = colour;
					region.outer.swap(loop);
					outRegions.push_back(region);
				}
			}
			if(outFilledShapes != nullptr)
			{
				*outFilledShapes = filled;
			}
			if(filled == 0)
			{
				return fail("no fillable shapes in the SVG");
			}
			if(outRegions.empty())
			{
				return fail("no closed contours with >= 3 vertices");
			}
			return true;
		}
		//---------------------------------------------------------
		bool cook(unsigned char const * svg, int size,
			VectorShapeCook::Options const & options, String & outText,
			String * error)
		{
			VectorShapeAsset::ParsedShape shape;
			if(!extractRegions(svg, size, options, shape.base, nullptr, error))
			{
				return false;
			}
			if(!VectorShapeCook::emit(shape, options.extent,
				"orkige vector shape - cooked from SVG\n"
				"units are world units, +x right, +y UP", outText))
			{
				if(error != nullptr)
				{
					*error = "no closed contours with >= 3 vertices";
				}
				return false;
			}
			return true;
		}
		//---------------------------------------------------------
		bool cookMorphSet(std::vector<Source> const & poses,
			VectorShapeCook::Options const & options, String & outText,
			String * error)
		{
			if(poses.empty())
			{
				if(error != nullptr)
				{
					*error = "a morph set needs a base SVG";
				}
				return false;
			}
			// every pose flattens at the FIXED resolution so matching path
			// structures produce corresponding vertices
			VectorShapeCook::Options uniform = options;
			uniform.uniform = true;

			VectorShapeAsset::ParsedShape shape;
			String reason;
			if(!extractRegions(poses[0].data, poses[0].size, uniform,
				shape.base, nullptr, &reason))
			{
				if(error != nullptr)
				{
					*error = "the base SVG has no fillable contours - " + reason;
				}
				return false;
			}
			for(std::size_t i = 1; i < poses.size(); ++i)
			{
				VectorShapeAsset::MorphTarget target;
				target.name = poses[i].name;
				if(!extractRegions(poses[i].data, poses[i].size, uniform,
					target.regions, nullptr, &reason))
				{
					if(error != nullptr)
					{
						*error = "morph target '" + poses[i].name + "' - " +
							reason;
					}
					return false;
				}
				if(!VectorShapeCook::checkTopology(shape.base, target.regions,
					target.name, error))
				{
					return false;
				}
				shape.morphs.push_back(target);
			}
			char count[64];
			std::snprintf(count, sizeof(count),
				"base pose + %d morph target(s)",
				int(shape.morphs.size()));
			if(!VectorShapeCook::emit(shape, options.extent,
				String("orkige vector shape - morph set cooked from SVG\n") +
					count, outText))
			{
				if(error != nullptr)
				{
					*error = "no closed contours with >= 3 vertices";
				}
				return false;
			}
			return true;
		}
	}
}
