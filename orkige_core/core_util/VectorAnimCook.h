/********************************************************************
	created:	Thursday 2026/07/31 at 10:00
	filename: 	VectorAnimCook.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorAnimCook_h__31_7_2026__10_00_00__
#define __VectorAnimCook_h__31_7_2026__10_00_00__

//! @file VectorAnimCook.h
//! @brief cook a Lottie JSON animation into the native `.oanim` rig - the
//! animated sibling of the `.svg` shape cook
//! @remarks A Lottie document (the open, Linux-Foundation-standardized vector
//! animation interchange format) is translated at import time into the
//! engine's `.oanim` text asset: a layer rig with keyframed transforms,
//! opacities and path poses, carved into named clips. Cooking keeps the
//! RUNTIME dependency surface at zero (the player never parses JSON or
//! beziers) and makes unsupported features fail loudly HERE, per layer, never
//! silently at play. The source `.json` stays the artist's living document;
//! the cook is idempotent and deterministic, so re-running it after an edit
//! regenerates the same `.oanim` byte-for-byte.
//!
//! Supported subset (game-ready vector art):
//!   layers    shape (ty 4), null (ty 3), solid (ty 1), precomp (ty 0 -
//!             inlined at cook when untimed: stretch 1, no time remap; nested
//!             inlining works), parenting, in/out windows, hidden layers
//!   shapes    groups (static/animated transforms baked into shape poses),
//!             paths, ellipses, rects, polystars; flat and linear/radial
//!             gradient fills; strokes expanded to vector outlines (caps,
//!             miter/round/bevel joins, animated width/paint, dash/gap/offset);
//!             parallel trim paths, rounded-corner and pucker/bloat modifiers;
//!             additive merge
//!   clipping  one additive, non-inverted convex mask per layer (animated
//!             paths)
//!   timing    keyframes with cubic value-bezier easing and hold keys; markers
//!             become clips (a `#once` comment suffix makes a one-shot clip;
//!             zero-duration markers extend to the next marker); an explicit
//!             clip override for marker-less authoring tools
//!
//! Everything the runtime grammar cannot express directly (spatial position
//! tangents, split/per-dimension easing, misaligned multi-property shape
//! animation, keys outside the timeline, windowed animated opacity) is
//! DENSIFIED at cook: baked into per-frame keys, so the runtime interpolator
//! stays cubic value-bezier + hold. Paths flatten with a FIXED per-edge
//! segment count chosen from the worst-case curvature across ALL keyframes of
//! that path, so every key has the identical vertex count - the `.oanim`
//! topology law - and lerping the flattened vertices equals flattening the
//! lerped bezier exactly (beziers are linear in their control points).
//!
//! IMAGE layers cook to textured cutout regions: the layer becomes one shape
//! block whose region carries `texture NAME x y w h` (the image pasted into
//! its layer-local rect; the layer transform channels animate it like any
//! cutout part). The referenced image files ride along in @ref Result::images
//! - a file-referenced asset (u+p) names a path relative to the source
//! document, an embedded base64 PNG arrives decoded - so the `.oanim`'s bare
//! texture names resolve in the project once the caller materializes them.
//!
//! Out of subset - each a named, per-layer cook error (never a silent skip):
//! track mattes, layer effects, arbitrary expressions, text layers, repeaters,
//! boolean merge paths, sequential trim, non-convex/multiple/subtract masks,
//! masks on image layers, zig-zag/offset/twist modifiers, skew, auto-orient,
//! 3D layers, time stretch/remap and timed precomps. Direct layer-transform
//! link expressions are resolved before validation.
//!
//! A document where NOTHING animates cooks to a plain `.oshape` instead (the
//! static one-shape-core case) - @ref Result::kind says which.
//!
//! Coordinates are y-flipped (the source is y-down, `.oanim` is +y up;
//! rotations negate to CCW), centered on the composition's midpoint and scaled
//! so the larger composition extent spans @ref Options::extent world units.
//! Source scale 100 maps to 1.0, opacity 100 to 1.0.
//!
//! Pure and headless: the cook reads text and writes text, touching no file
//! system and no renderer.

#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the Lottie JSON -> `.oanim` cook (pure, headless)
	class VectorAnimCook
	{
	public:
		//--- Types -------------------------------------------------
		//! what the cook produced
		enum Kind
		{
			KIND_OANIM,		//!< an animated rig (`.oanim`)
			KIND_OSHAPE		//!< nothing animates: a static shape (`.oshape`)
		};
		//! what a cook is asked for
		struct Options
		{
			//! world-unit size the composition's larger side spans
			double	extent;
			//! flatten chord tolerance in composition units; <= 0 derives the
			//! default (0.25% of the larger composition extent)
			double	tolerance;
			//! clip ranges overriding the document markers, as
			//! `name:start:end[:loop|once],...` in source frames; empty = the
			//! document's own markers
			String	clips;
			Options() : extent(2.0), tolerance(0.0) {}
		};
		//! one image file an image layer referenced, for the caller to
		//! materialize beside the cooked output under @ref name
		struct Image
		{
			String						name;		//!< the bare texture name
			bool						embedded;	//!< true = @ref data holds
													//!< the decoded bytes
			std::vector<unsigned char>	data;		//!< embedded PNG bytes
			String						source;		//!< path relative to the
													//!< source document
			Image() : embedded(false) {}
		};
		//! everything one successful cook produced
		struct Result
		{
			Kind				kind;	//!< which text @ref text carries
			String				text;	//!< the cooked asset text
			std::vector<Image>	images;	//!< referenced image files, in first
										//!< use order
			Result() : kind(KIND_OANIM) {}
		};

		//--- Methods -----------------------------------------------
		//! @brief cook Lottie JSON text into an `.oanim` (or a static
		//! `.oshape`).
		//! @param lottieJson the source document's text
		//! @param options what to cook it as
		//! @param out receives the cooked text on success (untouched on
		//! failure)
		//! @param errors receives EVERY unsupported feature, per layer, one
		//! per line, on failure (untouched on success)
		//! @return true when the whole document is inside the cook subset.
		//! A refusal is total: no half-cooked rig is ever handed back.
		static bool cook(String const & lottieJson, Options const & options,
			Result & out, String & errors);
	};
}

#endif //__VectorAnimCook_h__31_7_2026__10_00_00__
