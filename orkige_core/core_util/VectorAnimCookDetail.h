/********************************************************************
	created:	Thursday 2026/07/31 at 10:00
	filename: 	VectorAnimCookDetail.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorAnimCookDetail_h__31_7_2026__10_00_00__
#define __VectorAnimCookDetail_h__31_7_2026__10_00_00__

//! @file VectorAnimCookDetail.h
//! @brief the vector-animation cook's internal vocabulary, shared by its
//! translation units
//! @remarks Nothing outside the cook consumes these types - the public face is
//! @ref VectorAnimCook. They are split out only so the cook's three stages
//! (source-property sampling and path geometry, block conversion, rig assembly
//! and emission) can live in separate translation units while agreeing on one
//! set of structures.
//!
//! All arithmetic is DOUBLE precision on purpose: the emitted text is a
//! reproducibility contract (a re-cook must be byte-identical), and a float
//! intermediate would round differently than the source document's own
//! double-valued keyframes.
//!
//! The source document is read through @ref JsonValue - the one nesting-capable
//! JSON reader the core already carries. It lives under core_debugnet because
//! the editor's control endpoint was its first consumer, but the value type
//! itself is plain data with nothing network-specific about it, so the cook
//! reads documents with it rather than growing a second parser.

#include "core_debugnet/Json.h"
#include "core_util/VectorAnimCook.h"

#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Orkige
{
	//! @brief the cook's internals (@see VectorAnimCook)
	namespace VectorAnimCookDetail
	{
		//--- constants ---------------------------------------------
		//! cubic arc approximation of a quarter circle
		const double KAPPA = 0.5522847498307936;
		const int MAX_EDGE_SEGMENTS = 32;		//!< per-edge flattening cap
		const int MIN_EDGE_SEGMENTS = 1;		//!< per-edge flattening floor
		const double EPS = 1e-6;				//!< the cook's comparison epsilon
		//! @brief maximum turn per flattened segment.
		//! @remarks Per-edge angular smoothness: the absolute chord tolerance
		//! (a fraction of the whole composition) sets how many segments a curve
		//! needs for a given ABSOLUTE deviation, which grows with the curve's
		//! size - so a big belly curve gets many segments while a small but
		//! equally-round eye or claw gets only a facet or two. Roundness,
		//! though, is an ANGULAR property: a quarter-turn arc needs the same
		//! number of segments to read as round whether it is large or tiny. So
		//! a curved edge also gets a floor of
		//! ceil(turningAngle / EDGE_MAX_SEGMENT_ANGLE) segments,
		//! size-independent. This lifts ONLY genuinely curved small edges
		//! (cheap: they are short and few); the near-straight edges that
		//! dominate a heavy rig turn through almost no angle and are untouched,
		//! so the large rig barely grows. The absolute MAX_EDGE_SEGMENTS cap
		//! still bounds every edge.
		const double EDGE_MAX_SEGMENT_ANGLE = 30.0 * (3.141592653589793 / 180.0);

		//--- small value types -------------------------------------
		//! a point in whatever space its producer works in
		struct P2
		{
			double x;
			double y;
			P2() : x(0.0), y(0.0) {}
			P2(double px, double py) : x(px), y(py) {}
			bool operator==(P2 const & other) const
			{ return this->x == other.x && this->y == other.y; }
			bool operator!=(P2 const & other) const
			{ return !(*this == other); }
		};
		//! a bezier path: absolute vertices with relative in/out tangents
		struct BezPath
		{
			bool			closed;
			std::vector<P2>	v;		//!< vertices
			std::vector<P2>	i;		//!< incoming tangents, relative to v
			std::vector<P2>	o;		//!< outgoing tangents, relative to v
			BezPath() : closed(true) {}
			bool operator==(BezPath const & other) const
			{
				return this->closed == other.closed && this->v == other.v &&
					this->i == other.i && this->o == other.o;
			}
		};
		//! a 2x3 affine (a b tx / c d ty)
		struct Affine
		{
			double a, b, c, d, tx, ty;
			Affine() : a(1.0), b(0.0), c(0.0), d(1.0), tx(0.0), ty(0.0) {}
			Affine(double pa, double pb, double pc, double pd,
				double ptx, double pty)
				: a(pa), b(pb), c(pc), d(pd), tx(ptx), ty(pty) {}
		};
		//! one cubic edge of a path
		struct Edge
		{
			P2 p0, c1, c2, p3;
		};
		//! how a key interpolates toward the next one
		struct Ease
		{
			enum Mode { LIN, HOLD, BEZIER };
			Mode	mode;
			double	ox, oy, ix, iy;
			Ease() : mode(LIN), ox(0.0), oy(0.0), ix(0.0), iy(0.0) {}
		};

		//--- source-property views ---------------------------------
		//! one normalized keyframe of an animated scalar/vector property
		struct PropKey
		{
			double				t;		//!< source frame
			std::vector<double>	s;		//!< the value, padded to the dimension
			bool				h;		//!< hold key
			JsonValue const *	easeOut;	//!< the key's `o` tangent
			JsonValue const *	easeIn;		//!< the key's `i` tangent
			JsonValue const *	spatialIn;	//!< the key's `ti` tangent
			JsonValue const *	spatialOut;	//!< the key's `to` tangent
			PropKey() : t(0.0), h(false), easeOut(nullptr), easeIn(nullptr),
				spatialIn(nullptr), spatialOut(nullptr) {}
		};
		//! one normalized keyframe of an animated PATH property
		struct PathKey
		{
			double				t;			//!< source frame
			JsonValue const *	raw;		//!< the source bezier object
			BezPath				path;		//!< the parsed bezier
			bool				h;			//!< hold key
			JsonValue const *	easeOut;	//!< the key's `o` tangent
			JsonValue const *	easeIn;		//!< the key's `i` tangent
			PathKey() : t(0.0), raw(nullptr), h(false), easeOut(nullptr),
				easeIn(nullptr) {}
		};

		//--- cooked paint / stroke ---------------------------------
		//! one gradient stop: position plus straight RGBA
		struct GradStop
		{
			double at, r, g, b, a;
			GradStop() : at(0.0), r(0.0), g(0.0), b(0.0), a(1.0) {}
		};
		//! a key's paint: either a flat RGBA or a gradient
		struct Paint
		{
			bool					gradient;	//!< false = the flat RGBA below
			double					r, g, b, a;	//!< straight RGBA (flat paint)
			bool					radial;		//!< gradient kind
			P2						start;		//!< gradient start / centre
			P2						end;		//!< gradient end / radius point
			P2						focal;		//!< radial focal point
			std::vector<GradStop>	stops;		//!< ordered gradient stops
			Paint() : gradient(false), r(0.0), g(0.0), b(0.0), a(1.0),
				radial(false) {}
		};
		//! a key's stroke spec (absent on a filled region)
		struct StrokeSpec
		{
			bool	present;
			double	width;
			String	cap;
			String	join;
			double	miter;
			bool	closed;
			StrokeSpec() : present(false), width(0.0), miter(4.0),
				closed(false) {}
		};

		//--- cooked output -----------------------------------------
		//! one cooked transform-channel key
		struct ChanKey
		{
			double				frame;
			std::vector<double>	values;
			Ease				ease;
			ChanKey() : frame(0.0) {}
		};
		//! a cooked transform channel; `present` false = the channel was not
		//! kept for this layer at all (its default applies)
		struct ChannelOut
		{
			bool					present;
			bool					animated;
			std::vector<ChanKey>	keys;
			ChannelOut() : present(false), animated(false) {}
		};
		//! one cooked shape key: a full region pose at one output frame
		struct KeyOut
		{
			double							frame;
			Ease							ease;
			Paint							paint;
			std::vector<P2>					outer;
			std::vector<std::vector<P2> >	holes;
			StrokeSpec						stroke;
			bool							hasMask;
			std::vector<P2>					mask;
			KeyOut() : frame(0.0), hasMask(false) {}
		};
		//! one cooked shape block (a keyframed region)
		struct ShapeOut
		{
			std::vector<KeyOut>	keys;
			String				texture;	//!< empty = an untextured region
		};
		//! the five cooked transform channels, in emission order
		enum ChannelId
		{
			CH_POS = 0, CH_ANCHOR, CH_SCALE, CH_ROT, CH_OPACITY, CH_COUNT
		};
		//! one emitted layer of the rig
		struct EmitLayer
		{
			String					name;
			int						parent;		//!< emit index, or -1
			ChannelOut				channels[CH_COUNT];
			std::vector<ShapeOut>	shapes;
			EmitLayer() : parent(-1) {}
		};
		//! a named clip window onto the timeline
		struct ClipOut
		{
			String	name;
			double	start;
			double	end;
			bool	loop;
			ClipOut() : start(0.0), end(0.0), loop(true) {}
		};

		//--- document walk -----------------------------------------
		//! one paint block: the paths a style binds, plus its group context
		struct Block
		{
			//! one path source: its Lottie item kind and the item itself
			struct PathRef
			{
				String				kind;	//!< sh / el / rc / sr
				JsonValue const *	item;
				PathRef() : item(nullptr) {}
				PathRef(String const & k, JsonValue const * it)
					: kind(k), item(it) {}
			};
			std::vector<PathRef>			paths;
			JsonValue const *				fill;		//!< the style item
			String							kind;		//!< fill / gradient_fill
														//!< / stroke /
														//!< gradient_stroke
			JsonValue const *				trim;		//!< an inherited trim
			std::vector<JsonValue const *>	affines;	//!< group transforms
			std::vector<JsonValue const *>	opacities;	//!< group opacities
			std::vector<JsonValue const *>	modifiers;	//!< rd / pb modifiers
			std::vector<JsonValue const *>	masks;		//!< layer masks
			String							layer;		//!< owning layer name
			String							texture;	//!< image-layer texture
			Block() : fill(nullptr), kind("fill"), trim(nullptr) {}
		};
		//! one validated layer of the flattened (precomp-inlined) document
		struct FlatEntry
		{
			String				name;
			int					ty;			//!< Lottie layer type
			JsonValue const *	ks;			//!< the transform group
			double				offset;		//!< source -> output frame offset
			double				windowStart;
			double				windowEnd;
			FlatEntry *			parent;
			std::vector<Block>	blocks;
			bool				inheritOpacity;
			FlatEntry() : ty(-1), ks(nullptr), offset(0.0), windowStart(0.0),
				windowEnd(0.0), parent(nullptr), inheritOpacity(false) {}
		};

		//! @brief everything one cook run carries around: the source document,
		//! the error list, the synthesized JSON a solid/image layer needs and
		//! the resolved link-expression overrides.
		struct Context
		{
			JsonValue						document;
			std::vector<String>				errors;
			//! synthesized property objects (solid/image rect + fill) - held
			//! here so the raw pointers blocks keep stay valid
			std::deque<JsonValue>			synthetic;
			//! link expressions resolved to a copy of the referenced property,
			//! keyed by (transform group, property name)
			std::map<std::pair<JsonValue const *, String>, JsonValue>
											linkOverrides;
			std::deque<FlatEntry>			entryPool;
			std::vector<FlatEntry *>		flat;
			std::vector<VectorAnimCook::Image> images;

			void addError(String const & message)
			{ this->errors.push_back(message); }
		};

		//--- text formatting ---------------------------------------
		//! `%g` of the value rounded to four decimals, with a canonical zero
		String fmtFrame(double value);
		//! `%.5f` with values below half an emitted unit snapped to zero
		String fmtVal(double value);
		//! the optional easing suffix of a `kf` line (empty for linear)
		String fmtEase(Ease const & ease);
		//! printf-style formatting into a String
		String formatText(char const * format, ...);
		//! python's `str()` of a JSON scalar, for error messages
		String jsonStr(JsonValue const * value);

		//--- JSON access -------------------------------------------
		//! a member of an object, or nullptr when absent or JSON null
		JsonValue const * member(JsonValue const * object, char const * key);
		//! is the value present and truthy (python's truth test)
		bool truthy(JsonValue const * value);
		//! a numeric member with a default
		double numberOr(JsonValue const * object, char const * key,
			double fallback);
		//! a numeric member truncated toward zero, with a default
		int intOr(JsonValue const * object, char const * key, int fallback);
		//! the value as a list of doubles (python's `_as_list`)
		std::vector<double> asList(JsonValue const * value);

		//--- math helpers ------------------------------------------
		double bezierEase(double ox, double oy, double ix, double iy, double u);
		P2 cubicPoint(P2 const & p0, P2 const & c1, P2 const & c2,
			P2 const & p3, double t);
		double distToSegment(P2 const & p, P2 const & a, P2 const & b);
		double controlTurnAngle(P2 const & p0, P2 const & c1, P2 const & c2,
			P2 const & p3);
		bool pointInPolygon(P2 const & p, std::vector<P2> const & poly);
		double polygonArea(std::vector<P2> const & poly);
		//! python's `math.hypot(x, y)` - a correctly-rounded scaled magnitude,
		//! reproduced so the cook's lengths match the reference cook exactly
		double pyHypot(double x, double y);
		//! the exact (compensated) sum of a value run
		double pySum(std::vector<double> const & values);
		//! python's `round(value, 4)`
		double pyRound4(double value);

		//--- source properties -------------------------------------
		bool hasExpression(JsonValue const * prop);
		//! normalize an ANIMATED property's keyframes; false when malformed
		bool propKeys(JsonValue const * prop, int dim,
			std::vector<PropKey> & out);
		bool isAnimated(JsonValue const * prop, int dim);
		std::vector<double> staticValue(JsonValue const * prop, int dim,
			std::vector<double> const & fallback);
		void easeComponents(JsonValue const * tangent, int dim, double fallback,
			std::vector<P2> & out);
		//! the easing of the segment leaving `key`; false when the components
		//! disagree per dimension (a densify case)
		bool segmentEase(bool hold, JsonValue const * easeOut,
			JsonValue const * easeIn, int dim, Ease & out);
		bool hasSpatialTangents(std::vector<PropKey> const & keys);
		std::vector<double> sampleKeys(std::vector<PropKey> const & keys,
			double frame, int dim);
		std::vector<double> sampleProp(JsonValue const * prop, int dim,
			std::vector<double> const & fallback, double frame);
		std::vector<double> samplePosition(JsonValue const * prop, double frame,
			double defaultX, double defaultY);

		//--- paths -------------------------------------------------
		BezPath pathFromLottie(JsonValue const * shapeValue);
		BezPath pathEllipse(std::vector<double> const & center,
			std::vector<double> const & size);
		BezPath pathRect(std::vector<double> const & center,
			std::vector<double> const & size, double radius,
			bool roundedTopology);
		BezPath pathPolystar(JsonValue const * item, double frame);
		BezPath transformPath(BezPath const & path, Affine const & affine);
		std::vector<Edge> pathEdges(BezPath const & path);
		std::vector<int> edgeSegmentCounts(std::vector<BezPath> const & paths,
			double tol);
		std::vector<P2> flattenPath(BezPath const & path,
			std::vector<int> const & counts);
		BezPath lerpPath(BezPath const & a, BezPath const & b, double u);
		BezPath staticPathBez(JsonValue const * prop);
		std::vector<PathKey> pathPropKeys(JsonValue const * prop);
		bool isAnimatedPath(JsonValue const * prop);
		BezPath samplePathKeys(std::vector<PathKey> const & keys, double frame);
		bool edgeIsLinear(BezPath const & path, size_t index);
		BezPath roundPathCorners(BezPath const & path, double radius);
		BezPath puckerBloatPath(BezPath const & path, double rawAmount);
		BezPath blockPathAt(String const & kind, JsonValue const * item,
			double frame);
		BezPath blockPathWithModifiers(Block const & block, String const & kind,
			JsonValue const * item, double frame);
		double rectMaxRadius(JsonValue const * item);

		//--- affines -----------------------------------------------
		Affine groupAffineAt(JsonValue const * tr, double frame);
		Affine blockAffineAt(Block const & block, double frame);
		Affine composeAffines(std::vector<Affine> const & affines);

		//--- polyline utilities ------------------------------------
		void polylineChain(std::vector<P2> const & points, bool closed,
			std::vector<P2> & chain, std::vector<double> & lengths);
		P2 pointOnChain(std::vector<P2> const & chain,
			std::vector<double> const & lengths, double distance);
		std::vector<P2> sliceChain(std::vector<P2> const & chain,
			std::vector<double> const & lengths, double start, double end);
		std::vector<P2> resampleOpen(std::vector<P2> const & points, int count);
		std::vector<P2> resampleClosed(std::vector<P2> const & points,
			int count);
		std::vector<P2> clipConvex(std::vector<P2> const & subject,
			std::vector<P2> const & clip);

		//--- name / string helpers ---------------------------------
		String sanitizeName(JsonValue const * name, String const & fallback);
		String sanitizeName(String const & name, String const & fallback);
	}
}

#endif //__VectorAnimCookDetail_h__31_7_2026__10_00_00__
