/********************************************************************
	created:	Saturday 2026/07/12 at 10:00
	filename: 	VectorAnimAsset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorAnimAsset_h__12_7_2026__10_00_00__
#define __VectorAnimAsset_h__12_7_2026__10_00_00__

//! @file VectorAnimAsset.h
//! @brief parser for the `.oanim` text asset - a keyframed vector-shape
//! animation rig (the animated sibling of `.oshape`)
//! @remarks An `.oanim` is the NATIVE runtime form of a flat-colour vector
//! animation (imported animation sources cook to it): a layer hierarchy whose
//! transforms, opacities and path poses are keyframed over one frame timeline,
//! carved into named clips. Like `.oshape` it is plain text an agent can write
//! directly. This parser is pure and headless (orkige_core); the evaluator
//! (@see VectorAnimEval) turns the parsed document into per-frame poses that
//! feed VectorTessellator::Region streams.
//!
//! Grammar (v1), one token stream, `#` starts a line comment. Indentation is
//! cosmetic; structure comes from the keywords alone:
//!
//!   version 1
//!   fps F                        frames per second (> 0); REQUIRED, before
//!                                any clip/layer
//!   duration D                   timeline length in frames (> 0); REQUIRED,
//!                                before any clip/layer
//!   clip NAME START END loop|once
//!                                a named frame window [START, END] onto the
//!                                timeline (0 <= START < END <= duration).
//!                                Clips precede layers; names are unique. A
//!                                file with NO clip lines gets one implicit
//!                                clip `default 0 duration loop`.
//!   layer NAME parent P          opens a layer. P is the 0-based index of an
//!                                EARLIER layer, or -1 (root) - forward/self
//!                                parents are malformed, so the hierarchy is
//!                                acyclic by construction. Paint order = file
//!                                order. A layer with no `shape` blocks is a
//!                                pure transform parent (a null layer); a
//!                                solid is authored as a rectangle path.
//!   pos k N / anchor k N / scale k N / rot k N / opacity k N
//!                                keyframed transform channels of the open
//!                                layer, each at most once, N >= 1 keys follow
//!                                as `kf` lines. pos/anchor are vec2, scale is
//!                                vec2 (1 = identity), rot is scalar degrees
//!                                counter-clockwise (+y up, the `.oshape`
//!                                space), opacity is scalar 0..1. An absent
//!                                channel means its default: pos/anchor (0,0),
//!                                scale (1,1), rot 0, opacity 1.
//!   kf FRAME VALUES... [EASING]  one channel key: the frame (0..duration,
//!                                strictly increasing within a channel), the
//!                                channel's 1 or 2 values, and an optional
//!                                easing for the segment TO THE NEXT key:
//!                                  lin              linear (the default)
//!                                  hold             constant until the next key
//!                                  ease ox oy ix iy cubic value bezier through
//!                                                   (0,0) (ox,oy) (ix,iy) (1,1);
//!                                                   x = time fraction (clamped
//!                                                   to 0..1 at evaluation),
//!                                                   y = value fraction
//!   shape k N                    opens a filled path of the open layer with
//!                                N >= 1 shape keys. Each key is
//!                                  kf FRAME [EASING]
//!                                followed by one full region pose in the
//!                                `.oshape` vocabulary:
//!   fill r g b a                 straight RGBA 0..1 (colour animates by
//!                                differing per key)
//!   contour C                    C follows as C `v x y` lines (outer loop)
//!   v x y                        one contour/hole vertex (shape-local, +y up)
//!   hole H                       optional inner loop cut out of the fill
//!                                EVERY key of one shape block MUST repeat the
//!                                first key's topology exactly (same contour
//!                                count, same hole counts) - the fixed-vertex-
//!                                count law that makes path keys a pure vertex
//!                                lerp (the `.oshape` morph discipline); a
//!                                mismatch is malformed.
//!
//! Unknown keywords are reserved for later versions and ignored - but never
//! inside an open key/vertex run, which would corrupt it. On ANY malformation
//! parse() returns false and leaves the document EMPTY (the `.oshape`
//! honesty), never a half-loaded rig.

#include "core_util/VectorTessellator.h"
#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief the `.oanim` text -> animation document front end (pure, headless)
	class VectorAnimAsset
	{
	public:
		//--- Types -------------------------------------------------
		//! how a key interpolates toward the NEXT key
		enum EaseMode
		{
			EASE_LINEAR,	//!< straight lerp to the next key
			EASE_HOLD,		//!< constant until the next key
			EASE_BEZIER		//!< cubic value bezier (out/in handles)
		};
		//! per-key easing spec; the handles apply only to EASE_BEZIER
		struct Ease
		{
			EaseMode	mode;	//!< interpolation mode of the segment
			float		outX;	//!< out-handle of THIS key, time fraction 0..1
			float		outY;	//!< out-handle, value fraction
			float		inX;	//!< in-handle of the NEXT key, time fraction 0..1
			float		inY;	//!< in-handle, value fraction
			Ease() : mode(EASE_LINEAR), outX(0.0f), outY(0.0f),
				inX(1.0f), inY(1.0f) {}
		};
		//! one transform-channel key (scalar channels use value[0] only)
		struct Key
		{
			float	frame;		//!< timeline frame (0..duration)
			float	value[2];	//!< the channel value at this key
			Ease	ease;		//!< easing toward the next key
			Key() : frame(0.0f) { this->value[0] = 0.0f; this->value[1] = 0.0f; }
		};
		//! a keyframed channel; no keys = the channel default applies
		struct Channel
		{
			std::vector<Key>	keys;	//!< strictly increasing frames
		};
		//! one shape key: a full region pose at one frame
		struct ShapeKey
		{
			float						frame;	//!< timeline frame (0..duration)
			Ease						ease;	//!< easing toward the next key
			VectorTessellator::Region	region;	//!< fill + contours at this key
			ShapeKey() : frame(0.0f) {}
		};
		//! one filled path, keyframed as whole region poses. Every key shares
		//! the first key's topology (parser-enforced), so evaluation is a pure
		//! vertex/colour lerp between neighbouring keys.
		struct Shape
		{
			std::vector<ShapeKey>	keys;	//!< >= 1, strictly increasing frames
		};
		//! one layer: parent link, transform channels and its shape blocks
		struct Layer
		{
			String				name;		//!< layer name from the asset
			int					parent;		//!< index of an EARLIER layer, or -1
			Channel				pos;		//!< vec2, default (0,0)
			Channel				anchor;		//!< vec2, default (0,0)
			Channel				scale;		//!< vec2, default (1,1)
			Channel				rot;		//!< scalar degrees CCW, default 0
			Channel				opacity;	//!< scalar 0..1, default 1
			std::vector<Shape>	shapes;		//!< paint order; empty = null layer
			Layer() : parent(-1) {}
		};
		//! a named frame window onto the timeline
		struct Clip
		{
			String	name;	//!< unique clip name
			float	start;	//!< first frame (inclusive)
			float	end;	//!< last frame
			bool	loop;	//!< wrap (loop) vs clamp-and-finish (once)
			Clip() : start(0.0f), end(0.0f), loop(true) {}
		};
		//! the parsed animation document (the whole rig)
		struct Document
		{
			float				fps;		//!< frames per second
			float				duration;	//!< timeline length in frames
			std::vector<Clip>	clips;		//!< >= 1 after a successful parse
			std::vector<Layer>	layers;		//!< paint order = file order
			Document() : fps(0.0f), duration(0.0f) {}
			//! index of the named clip, or -1 when absent
			int findClip(String const & name) const;
			//! drop everything (an unloaded document)
			void clear();
		};

		//! @brief parse `.oanim` text into an animation document.
		//! @return true on a well-formed document (header present, >= 1 layer,
		//! every key run honoured, clip ranges valid, shape topology agreeing
		//! across keys). On ANY malformation it returns false and leaves out
		//! EMPTY - the honest "no animation" fallback, never a crash.
		static bool parse(String const & text, Document & out);
	};
}

#endif //__VectorAnimAsset_h__12_7_2026__10_00_00__
