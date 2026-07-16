/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	VectorAnimAsset.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file VectorAnimAsset.cpp
//! @brief the `.oanim` grammar parser (@see VectorAnimAsset.h)

#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorShapeAsset.h"

#include <sstream>

namespace Orkige
{
	namespace
	{
		//! which loop the current `contour`/`hole`/`mask` vertex run is filling
		enum VertexTarget
		{
			TARGET_NONE,
			TARGET_OUTER,
			TARGET_HOLE,
			TARGET_MASK
		};

		//! @brief the optional trailing easing spec of a `kf` line: absent =
		//! linear, else `lin` / `hold` / `ease ox oy ix iy`. Anything else
		//! (including trailing garbage) is malformed.
		bool parseEase(std::istringstream & tokens, VectorAnimAsset::Ease & ease)
		{
			String mode;
			if(!(tokens >> mode))
			{
				ease = VectorAnimAsset::Ease();	// no spec: linear
				return true;
			}
			if(mode == "lin")
			{
				ease.mode = VectorAnimAsset::EASE_LINEAR;
				return true;
			}
			if(mode == "hold")
			{
				ease.mode = VectorAnimAsset::EASE_HOLD;
				return true;
			}
			if(mode == "ease")
			{
				ease.mode = VectorAnimAsset::EASE_BEZIER;
				return static_cast<bool>(tokens >> ease.outX >> ease.outY >>
					ease.inX >> ease.inY);
			}
			return false;
		}

		//! @brief do two regions share topology (same contour + hole counts) -
		//! the fixed-vertex-count law across a shape block's keys
		bool sameRegionTopology(VectorTessellator::Region const & a,
			VectorTessellator::Region const & b)
		{
			if(a.outer.size() != b.outer.size() ||
				a.holes.size() != b.holes.size() ||
				a.paintType != b.paintType ||
				a.gradientStops.size() != b.gradientStops.size())
			{
				return false;
			}
			// a stroke keeps its kind, style and mask structure across keys -
			// only its centreline, width and paint animate
			if(a.kind != b.kind || a.strokeCap != b.strokeCap ||
				a.strokeJoin != b.strokeJoin ||
				a.strokeClosed != b.strokeClosed ||
				a.mask.size() != b.mask.size())
			{
				return false;
			}
			for(std::size_t h = 0; h < a.holes.size(); ++h)
			{
				if(a.holes[h].size() != b.holes[h].size())
				{
					return false;
				}
			}
			return true;
		}
	}
	//---------------------------------------------------------
	int VectorAnimAsset::Document::findClip(String const & name) const
	{
		for(std::size_t c = 0; c < this->clips.size(); ++c)
		{
			if(this->clips[c].name == name)
			{
				return static_cast<int>(c);
			}
		}
		return -1;
	}
	//---------------------------------------------------------
	void VectorAnimAsset::Document::clear()
	{
		this->fps = 0.0f;
		this->duration = 0.0f;
		this->clips.clear();
		this->layers.clear();
	}
	//---------------------------------------------------------
	bool VectorAnimAsset::parse(String const & text, Document & out,
		ParseError * error)
	{
		out.clear();
		Document doc;
		bool haveFps = false;
		bool haveDuration = false;

		// parse state: at most ONE of channel/shape is open at a time (every
		// opener closes the previous element first)
		int layerIdx = -1;			//!< the open layer (-1 = none yet)
		Channel * openChannel = nullptr;	//!< channel taking `kf` lines
		int channelDim = 0;			//!< values per key (1 or 2)
		int channelRemaining = 0;	//!< channel keys still expected
		Shape * openShape = nullptr;	//!< shape block taking `kf` keys
		int shapeRemaining = 0;		//!< shape keys still expected
		bool inShapeKey = false;	//!< a shape key's region is being built
		bool shapeKeyHasFill = false;	//!< the open shape key saw its `fill`
		bool shapeKeyHasFocal = false;	//!< radial key saw its optional focal
		VertexTarget target = TARGET_NONE;	//!< where `v` lines go
		int vertsRemaining = 0;		//!< vertices still expected for the loop
		int stopsRemaining = 0;		//!< gradient stops still expected
		int lineNumber = 0;			//!< 1-based, for the failure readback

		// record where and why the parse stopped; every malformation exit runs
		// through here so a caller's ParseError always names the offending line
		auto fail = [&](char const * message) -> bool
		{
			if(error != nullptr)
			{
				error->line = lineNumber;
				error->message = message;
			}
			return false;
		};

		// close the open shape key (if any): its vertex runs must be complete,
		// the region fillable, and its topology must match the block's first key
		auto closeShapeKey = [&]() -> bool
		{
			if(!inShapeKey)
			{
				return true;
			}
			if(vertsRemaining != 0)
			{
				return fail("shape key ends inside a vertex run (fewer v lines "
					"than the declared count)");
			}
			if(stopsRemaining != 0)
			{
				return fail("shape key ends inside a gradient (fewer stop lines "
					"than the declared count)");
			}
			if(!shapeKeyHasFill)
			{
				return fail("shape key has no fill/linear/radial paint");
			}
			VectorTessellator::Region const & region =
				openShape->keys.back().region;
			const bool isStroke =
				region.kind == VectorTessellator::REGION_STROKE;
			// a stroke's contour is a CENTRELINE (two points is a straight
			// ribbon); a fill's is a boundary and needs an area
			if(region.outer.size() < (isStroke ? 2u : 3u))
			{
				return fail(isStroke
					? "stroke centreline needs at least 2 vertices"
					: "fill contour needs at least 3 vertices");
			}
			if(isStroke && !region.holes.empty())
			{
				return fail("a stroke region cannot carry holes");
			}
			if(!region.mask.empty() && region.mask.size() < 3)
			{
				return fail("mask polygon needs at least 3 vertices");
			}
			for(std::vector<VectorTessellator::Point> const & hole :
				region.holes)
			{
				if(hole.size() < 3)
				{
					return fail("hole contour needs at least 3 vertices");
				}
			}
			if(openShape->keys.size() > 1 && !sameRegionTopology(
				openShape->keys.front().region, region))
			{
				// the fixed-topology law
				return fail("shape key topology differs from the block's first "
					"key (contour/hole/mask/stop counts and stroke style must "
					"repeat exactly)");
			}
			inShapeKey = false;
			target = TARGET_NONE;
			return true;
		};
		// close whatever element is open - every structural keyword (and EOF)
		// runs through here, so a truncated run can never be silently dropped
		auto closeElements = [&]() -> bool
		{
			if(openChannel != nullptr && channelRemaining > 0)
			{
				return fail("channel ends early (fewer kf lines than the "
					"declared count)");
			}
			if(!closeShapeKey())
			{
				return false;
			}
			if(openShape != nullptr && shapeRemaining > 0)
			{
				return fail("shape block ends early (fewer kf keys than the "
					"declared count)");
			}
			openChannel = nullptr;
			channelRemaining = 0;
			openShape = nullptr;
			shapeRemaining = 0;
			return true;
		};

		std::istringstream lines(text);
		String rawLine;
		while(std::getline(lines, rawLine))
		{
			++lineNumber;
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
				if(target == TARGET_NONE || vertsRemaining <= 0)
				{
					return fail("vertex outside an open contour/hole/mask run");
				}
				float x = 0.0f, y = 0.0f;
				if(!(tokens >> x >> y))
				{
					return fail("malformed vertex (want: v x y)");
				}
				VectorTessellator::Region & region =
					openShape->keys.back().region;
				if(target == TARGET_OUTER)
				{
					region.outer.push_back(VectorTessellator::Point(x, y));
				}
				else if(target == TARGET_MASK)
				{
					region.mask.push_back(VectorTessellator::Point(x, y));
				}
				else
				{
					region.holes.back().push_back(
						VectorTessellator::Point(x, y));
				}
				--vertsRemaining;
			}
			else if(keyword == "kf")
			{
				if(openChannel != nullptr)
				{
					// a channel key: frame, the channel's values, easing
					Key key;
					if(!(tokens >> key.frame))
					{
						return fail("malformed channel key (want: kf frame "
							"value(s) [easing])");
					}
					for(int d = 0; d < channelDim; ++d)
					{
						if(!(tokens >> key.value[d]))
						{
							return fail("channel key is missing a value (this "
								"channel carries per-key values)");
						}
					}
					if(!parseEase(tokens, key.ease))
					{
						return fail("malformed easing (want: lin, hold, or "
							"ease ox oy ix iy)");
					}
					if(key.frame < 0.0f || key.frame > doc.duration)
					{
						return fail("key frame outside the 0..duration timeline");
					}
					if(!openChannel->keys.empty() &&
						key.frame <= openChannel->keys.back().frame)
					{
						// frames strictly increasing
						return fail("key frames must strictly increase");
					}
					openChannel->keys.push_back(key);
					if(--channelRemaining == 0)
					{
						openChannel = nullptr;	// channel complete
					}
				}
				else if(openShape != nullptr)
				{
					// a shape key: close the previous key's region first
					if(!closeShapeKey())
					{
						return false;
					}
					if(shapeRemaining <= 0)
					{
						return fail("more shape keys than the block declared");
					}
					ShapeKey key;
					if(!(tokens >> key.frame) || !parseEase(tokens, key.ease))
					{
						return fail("malformed shape key (want: kf frame "
							"[easing])");
					}
					if(key.frame < 0.0f || key.frame > doc.duration)
					{
						return fail("key frame outside the 0..duration timeline");
					}
					if(!openShape->keys.empty() &&
						key.frame <= openShape->keys.back().frame)
					{
						// frames strictly increasing
						return fail("key frames must strictly increase");
					}
					openShape->keys.push_back(key);
					--shapeRemaining;
					inShapeKey = true;
					shapeKeyHasFill = false;
					shapeKeyHasFocal = false;
					target = TARGET_NONE;
					vertsRemaining = 0;
					stopsRemaining = 0;
				}
				else
				{
					return fail("key with no open channel or shape block to "
						"receive it");
				}
			}
			else if(keyword == "fill")
			{
				// exactly one fill per shape key, before its contour
				if(!inShapeKey || shapeKeyHasFill || vertsRemaining != 0)
				{
					return fail("fill outside an open shape key, repeated, or "
						"inside a vertex run");
				}
				float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
				if(!(tokens >> r >> g >> b >> a))
				{
					return fail("malformed fill (want: fill r g b a)");
				}
				openShape->keys.back().region.fill =
					VectorTessellator::Colour(r, g, b, a);
				shapeKeyHasFill = true;
			}
			else if(keyword == "linear" || keyword == "radial")
			{
				if(!inShapeKey || shapeKeyHasFill || vertsRemaining != 0)
				{
					return fail("gradient outside an open shape key, repeated, "
						"or inside a vertex run");
				}
				float sx = 0.0f, sy = 0.0f, ex = 0.0f, ey = 0.0f;
				int count = 0;
				if(!(tokens >> sx >> sy >> ex >> ey >> count) || count < 2)
				{
					return fail("malformed gradient (want: linear|radial sx sy "
						"ex ey stopCount>=2)");
				}
				VectorTessellator::Region & region =
					openShape->keys.back().region;
				region.paintType = keyword == "linear"
					? VectorTessellator::PAINT_LINEAR_GRADIENT
					: VectorTessellator::PAINT_RADIAL_GRADIENT;
				region.gradientStart = VectorTessellator::Point(sx, sy);
				region.gradientEnd = VectorTessellator::Point(ex, ey);
				region.gradientFocal = region.gradientStart;
				region.gradientStops.reserve(count);
				stopsRemaining = count;
				shapeKeyHasFill = true;
			}
			else if(keyword == "focal")
			{
				if(!inShapeKey || !shapeKeyHasFill || shapeKeyHasFocal ||
					vertsRemaining != 0 || stopsRemaining <= 0)
				{
					return fail("focal outside an open radial gradient (or "
						"repeated)");
				}
				VectorTessellator::Region & region =
					openShape->keys.back().region;
				float fx = 0.0f, fy = 0.0f;
				if(region.paintType != VectorTessellator::PAINT_RADIAL_GRADIENT ||
					!(tokens >> fx >> fy))
				{
					return fail("malformed focal (want: focal fx fy, radial "
						"gradients only)");
				}
				region.gradientFocal = VectorTessellator::Point(fx, fy);
				shapeKeyHasFocal = true;
			}
			else if(keyword == "stop")
			{
				if(!inShapeKey || !shapeKeyHasFill || stopsRemaining <= 0)
				{
					return fail("stop outside an open gradient (or more stops "
						"than declared)");
				}
				float at = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
				if(!(tokens >> at >> r >> g >> b >> a) || at < 0.0f || at > 1.0f)
				{
					return fail("malformed stop (want: stop at r g b a, "
						"at in 0..1)");
				}
				std::vector<VectorTessellator::GradientStop> & stops =
					openShape->keys.back().region.gradientStops;
				if(!stops.empty() && at < stops.back().offset)
				{
					return fail("gradient stop offsets must not decrease");
				}
				stops.push_back(VectorTessellator::GradientStop(at,
					VectorTessellator::Colour(r, g, b, a)));
				--stopsRemaining;
			}
			else if(keyword == "stroke")
			{
				// the open key's region becomes a stroke: its contour is the
				// centreline, swept at render time (@see VectorTessellator)
				if(!inShapeKey || !shapeKeyHasFill || vertsRemaining != 0 ||
					stopsRemaining != 0 ||
					!openShape->keys.back().region.outer.empty() ||
					!VectorShapeAsset::parseStrokeSpec(tokens,
						openShape->keys.back().region))
				{
					return fail("malformed stroke spec, or stroke after the "
						"key's contour");
				}
			}
			else if(keyword == "mask")
			{
				// an optional convex clip polygon for this key's stroke
				int count = 0;
				if(!inShapeKey || vertsRemaining != 0 || stopsRemaining != 0 ||
					openShape->keys.back().region.outer.empty() ||
					!openShape->keys.back().region.mask.empty() ||
					!(tokens >> count) || count < 3)
				{
					return fail("malformed mask (want: mask count>=3, once, "
						"after the key's contour)");
				}
				openShape->keys.back().region.mask.reserve(count);
				target = TARGET_MASK;
				vertsRemaining = count;
			}
			else if(keyword == "contour")
			{
				int count = 0;
				const bool stroke = inShapeKey &&
					openShape->keys.back().region.kind ==
						VectorTessellator::REGION_STROKE;
				if(!inShapeKey || !shapeKeyHasFill || vertsRemaining != 0 ||
					stopsRemaining != 0 ||
					!openShape->keys.back().region.outer.empty() ||
					!(tokens >> count) || count < (stroke ? 2 : 1))
				{
					return fail("malformed contour (want: contour count, once "
						"per shape key, after its paint)");
				}
				openShape->keys.back().region.outer.reserve(count);
				target = TARGET_OUTER;
				vertsRemaining = count;
			}
			else if(keyword == "hole")
			{
				int count = 0;
				if(!inShapeKey || vertsRemaining != 0 ||
					openShape->keys.back().region.outer.empty() ||
					openShape->keys.back().region.kind !=
						VectorTessellator::REGION_FILL ||
					!(tokens >> count) || count <= 0)
				{
					return fail("malformed hole (want: hole count, fill "
						"regions only, after the key's contour)");
				}
				VectorTessellator::Region & region =
					openShape->keys.back().region;
				region.holes.push_back(std::vector<VectorTessellator::Point>());
				region.holes.back().reserve(count);
				target = TARGET_HOLE;
				vertsRemaining = count;
			}
			else if(keyword == "pos" || keyword == "anchor" ||
				keyword == "scale" || keyword == "rot" || keyword == "opacity")
			{
				if(!closeElements())
				{
					return false;
				}
				if(layerIdx < 0)
				{
					return fail("transform channel outside a layer");
				}
				String k;
				int count = 0;
				if(!(tokens >> k) || k != "k" || !(tokens >> count) ||
					count <= 0)
				{
					return fail("malformed channel header (want: "
						"pos|anchor|scale|rot|opacity k count)");
				}
				Layer & layer = doc.layers[layerIdx];
				Channel * channel = nullptr;
				if(keyword == "pos") { channel = &layer.pos; }
				else if(keyword == "anchor") { channel = &layer.anchor; }
				else if(keyword == "scale") { channel = &layer.scale; }
				else if(keyword == "rot") { channel = &layer.rot; }
				else { channel = &layer.opacity; }
				if(!channel->keys.empty())
				{
					return fail("channel redefined on this layer");
				}
				channel->keys.reserve(count);
				openChannel = channel;
				channelDim = (keyword == "rot" || keyword == "opacity") ? 1 : 2;
				channelRemaining = count;
			}
			else if(keyword == "shape")
			{
				if(!closeElements())
				{
					return false;
				}
				if(layerIdx < 0)
				{
					return fail("shape block outside a layer");
				}
				String k;
				int count = 0;
				if(!(tokens >> k) || k != "k" || !(tokens >> count) ||
					count <= 0)
				{
					return fail("malformed shape header (want: shape k count)");
				}
				doc.layers[layerIdx].shapes.push_back(Shape());
				openShape = &doc.layers[layerIdx].shapes.back();
				openShape->keys.reserve(count);
				shapeRemaining = count;
			}
			else if(keyword == "layer")
			{
				if(!closeElements())
				{
					return false;
				}
				if(!haveFps || !haveDuration)
				{
					return fail("layer before the fps/duration header");
				}
				String name, parentWord;
				int parent = 0;
				if(!(tokens >> name >> parentWord) || parentWord != "parent" ||
					!(tokens >> parent))
				{
					return fail("malformed layer (want: layer name parent "
						"index)");
				}
				if(parent < -1 ||
					parent >= static_cast<int>(doc.layers.size()))
				{
					// only earlier layers (or -1) may parent
					return fail("layer parent must be an earlier layer index "
						"or -1");
				}
				Layer layer;
				layer.name = name;
				layer.parent = parent;
				doc.layers.push_back(layer);
				layerIdx = static_cast<int>(doc.layers.size()) - 1;
			}
			else if(keyword == "clip")
			{
				if(!closeElements())
				{
					return false;
				}
				if(!haveFps || !haveDuration || layerIdx != -1)
				{
					// clips live in the header, before layers
					return fail("clips belong in the header, after "
						"fps/duration and before the first layer");
				}
				String name, mode;
				float start = 0.0f, end = 0.0f;
				if(!(tokens >> name >> start >> end >> mode))
				{
					return fail("malformed clip (want: clip name start end "
						"loop|once)");
				}
				if(start < 0.0f || end <= start || end > doc.duration)
				{
					return fail("clip frame range outside the timeline (want: "
						"0 <= start < end <= duration)");
				}
				if(doc.findClip(name) != -1)
				{
					return fail("duplicate clip name");
				}
				Clip clip;
				clip.name = name;
				clip.start = start;
				clip.end = end;
				if(mode == "loop") { clip.loop = true; }
				else if(mode == "once") { clip.loop = false; }
				else
				{
					return fail("clip mode must be loop or once");
				}
				doc.clips.push_back(clip);
			}
			else if(keyword == "fps")
			{
				float fps = 0.0f;
				if(!closeElements())
				{
					return false;
				}
				if(layerIdx != -1 || !doc.clips.empty() ||
					!(tokens >> fps) || fps <= 0.0f)
				{
					return fail("malformed fps (want: fps value>0, in the "
						"header before clips and layers)");
				}
				doc.fps = fps;
				haveFps = true;
			}
			else if(keyword == "duration")
			{
				float duration = 0.0f;
				if(!closeElements())
				{
					return false;
				}
				if(layerIdx != -1 || !doc.clips.empty() ||
					!(tokens >> duration) || duration <= 0.0f)
				{
					return fail("malformed duration (want: duration frames>0, "
						"in the header before clips and layers)");
				}
				doc.duration = duration;
				haveDuration = true;
			}
			else if(keyword == "version")
			{
				int version = 0;
				if(!closeElements())
				{
					return false;
				}
				if(!(tokens >> version) || version <= 0)
				{
					return fail("malformed version (want: version number>0)");
				}
			}
			else
			{
				// unknown keywords are reserved for later versions and ignored
				// - but only between complete elements, never mid-run
				if(!closeElements())
				{
					return false;
				}
			}
		}

		// nothing may be left dangling; header and at least one layer required
		if(!closeElements())
		{
			return false;
		}
		if(!haveFps || !haveDuration)
		{
			return fail("missing fps/duration header");
		}
		if(doc.layers.empty())
		{
			return fail("document has no layers");
		}
		// a file with no clip lines is one looping clip over the whole timeline
		if(doc.clips.empty())
		{
			Clip clip;
			clip.name = "default";
			clip.start = 0.0f;
			clip.end = doc.duration;
			clip.loop = true;
			doc.clips.push_back(clip);
		}
		out.fps = doc.fps;
		out.duration = doc.duration;
		out.clips.swap(doc.clips);
		out.layers.swap(doc.layers);
		return true;
	}
}
