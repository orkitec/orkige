/********************************************************************
	created:	Wednesday 2026/07/08 at 23:30
	filename: 	UiRenderer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __UiRenderer_h__8_7_2026__23_30_00__
#define __UiRenderer_h__8_7_2026__23_30_00__

//! @file UiRenderer.h
//! @brief the gui 2D renderer: a retained scene of rects, captions
//! and markup texts over ONE atlas, batched into ONE DrawLayer2D batch
//! @remarks Gorilla's cross-backend successor (the glyph layout math -
//! kerning, letter spacing, top-aligned glyphs, markup colour codes -
//! is ported from it; Gorilla was MIT, (c) 2010 Robin Southern), built
//! lean for mobile:
//! - a UiScreen is ONE draw call: all layers and all elements of one
//!   atlas concatenate into a single pixel-space triangle batch on the
//!   engine_render DrawLayer2D facade - batch count scales with screens
//!   (atlases), never with widgets,
//! - retained + dirty-tracked: elements keep their built vertices and
//!   relayout only when THEIR properties change; the screen resubmits
//!   only when something changed (steady-state frames allocate nothing
//!   and upload nothing),
//! - value-owned vertex storage with retained capacity (std::vector,
//!   cleared not freed) - no per-frame heap traffic,
//! - renderer-blind: the same code runs on the classic and the
//!   Ogre-Next backend; everything GPU-facing lives behind DrawLayer2D.
//! Coordinates are window pixels, origin top-left (the DrawLayer2D
//! contract); glyphs render TOP-aligned at the cursor, space advances
//! by the font's spacelength and renders nothing.

#include "engine_gui/UiAtlas.h"
#include "engine_render/DrawLayer2D.h"
#include <core_util/optr.h>
#include <core_util/Ui2DTransform.h>

#include <vector>

namespace Orkige
{
	class UiScreen;
	class UiLayer;
	class UiRect;
	class UiCaption;
	class UiMarkupText;

	//! the shared vertex vocabulary of the element emitters
	typedef DrawLayer2D::Vertex2D UiVertex;

	//! @brief pure quad emitters for the nine-slice and tiled UiRect fill
	//! modes. No render system, no vertex assembly - they produce plain quads
	//! (pixel corners + normalized UV sub-rect) a UiRect turns into triangles
	//! and a unit test asserts on directly.
	namespace UiNineSlice
	{
		//! one output cell: pixel corners (x0,y0 top-left .. x1,y1 bottom-right)
		//! and the normalized UV sub-rect sampled for it
		struct Quad
		{
			Real x0, y0, x1, y1;
			Real u0, v0, u1, v1;
		};

		//! @brief emit the (up to) 9 quads of a nine-slice fill. Corner bands
		//! keep their device-pixel size; edges stretch along one axis and the
		//! centre stretches both. If the target is too small for the corner
		//! bands they shrink proportionally so no quad overlaps. Zero-area
		//! quads are dropped, so a fully collapsed axis yields fewer cells.
		//! @param cornerL,cornerR,cornerT,cornerB band sizes in DEVICE pixels
		//! @param uL,uT,uR,uB the sprite's normalized UV rect
		//! @param fracL,fracR,fracT,fracB the corner inset as a fraction 0..1 of
		//! the sprite span (design inset / sprite design size = the UV split)
		ORKIGE_ENGINE_DLL void buildNineSlice(Real left, Real top,
			Real width, Real height,
			Real cornerL, Real cornerR, Real cornerT, Real cornerB,
			Real uL, Real uT, Real uR, Real uB,
			Real fracL, Real fracR, Real fracT, Real fracB,
			std::vector<Quad> & out);

		//! @brief emit ceil(width/tileW) x ceil(height/tileH) quads tiling the
		//! sprite across the target; the last partial row/column clamps its UV
		//! so only the covered fraction of the sprite is sampled.
		ORKIGE_ENGINE_DLL void buildTiled(Real left, Real top,
			Real width, Real height, Real tileW, Real tileH,
			Real uL, Real uT, Real uR, Real uB,
			std::vector<Quad> & out);
	}

	//! @brief a full-window surface over one UiAtlas: owns z-indexed
	//! UiLayers and submits their triangles as ONE batch per frame
	//! @remarks replaces Gorilla's Silverback/Screen pair; owned by
	//! GuiManager (one per GuiView/atlas). update() is the once-
	//! per-frame poke: it rebuilds dirty content and resubmits - clean
	//! frames return immediately.
	class ORKIGE_ENGINE_DLL UiScreen
	{
		friend class UiLayer;
	public:
		//! @param atlas outlives the screen (GuiManager owns both)
		//! @param drawLayer the facade layer this screen composites on
		UiScreen(UiAtlas const * atlas, optr<DrawLayer2D> const & drawLayer);
		//! destroys the remaining layers; the draw layer dies with the
		//! handle (facade RAII)
		~UiScreen();

		//! create a layer at z index (0 draws first, 15 last)
		UiLayer* createLayer(uint index = 0);
		//! destroy a layer and its contents
		void destroy(UiLayer* layer);

		inline UiAtlas const * getAtlas() const { return this->mAtlas; }
		//! current window size in pixels (widgets lay out against it)
		inline Real getWidth() const { return this->mWidth; }
		inline Real getHeight() const { return this->mHeight; }

		//! @brief pin the layout surface to an explicit pixel size instead of
		//! the live window - the editor GUI Preview stage lays a gui out at a
		//! SIMULATED device resolution and composites it into an offscreen
		//! target. Pass 0,0 to follow the window again (the default). Forces a
		//! relayout on the next update.
		void setSurfaceSize(Real width, Real height);

		inline bool isVisible() const { return this->mIsVisible; }
		//! show/hide the whole screen (the submitted batch stays; batch
		//! count drops by one while hidden)
		void setVisible(bool visible);
		inline void show() { this->setVisible(true); }
		inline void hide() { this->setVisible(false); }

		//! compositing order among ALL 2D layers (GuiManager's
		//! reorderViews assigns it; ascending composites later)
		void setZOrder(int zOrder);

		//! force a full relayout + resubmission on the next update
		//! (atlas texture replacement)
		void requestFullRedraw();

		//! per-frame poke (GuiManager's FrameStarted): follow window
		//! resizes, rebuild dirty layers, resubmit the batch
		void update();

		//! total vertices submitted on the last update (across every batch) -
		//! a render probe for selfchecks ("did the HUD actually draw?")
		inline size_t getLastVertexCount() const { return this->mLastVertexCount; }

		//--- performance-contract probes (the "1 draw per screen per atlas,
		//--- dirty-tracked, zero steady-state allocation" promise made
		//--- enforceable; @see the demo_gui_matrix perf assertions) ---
		//! @brief draw submissions (DrawLayer2D batches) the screen holds after
		//! its last rebuild: 1 for the common case (every layer of one atlas
		//! concatenated), +1 per scissored (scroll) layer. The batch-count
		//! contract a selfcheck pins.
		inline size_t getLastBatchCount() const { return this->mLastBatchCount; }
		//! @brief a monotonic count of batch RESUBMITS (the dirty path: clear +
		//! re-emit + resubmit). A steady screen never advances it; one content
		//! change advances it once; an active animation advances it each frame
		//! and stops at completion. Read deltas to assert dirty-tracking.
		inline size_t getRebuildCount() const { return this->mRebuildCount; }
		//! @brief a monotonic count of element GEOMETRY rebuilds (a UiRect/
		//! UiCaption/UiMarkupText actually re-tessellated its vertices). Distinct
		//! from getRebuildCount: a post-pass scale/rotation/alpha animation
		//! RESUBMITS the batch each frame but rebuilds NO geometry, so this stays
		//! flat while it animates - the proof of the transform-only design claim.
		inline size_t getGeometryRebuildCount() const { return this->mGeometryRebuildCount; }
		//! @brief the retained scratch buffer's current capacity (elements). Stable
		//! across identical rebuilt frames after warmup = no per-frame reallocation
		//! (the steady-state-allocation contract, approximated by capacity).
		inline size_t getScratchCapacity() const { return this->mScratch.capacity(); }

		//! layer callback: content changed, resubmit on the next update
		inline void _markDirty() { this->mDirty = true; }
	protected:
		//! submit the accumulated scratch buffer as one (optionally scissored)
		//! batch and clear it; a no-op on an empty buffer
		void _flushBatch(DrawLayer2D::ScissorRect const * scissor);

		UiAtlas const *			mAtlas;
		optr<DrawLayer2D>		mDrawLayer;
		std::vector<UiLayer*>	mLayers;		//!< creation order
		std::vector<UiVertex>	mScratch;		//!< retained batch buffer
		Real					mWidth, mHeight;
		//! when true, mWidth/mHeight are pinned (setSurfaceSize) and update()
		//! does NOT follow the live window - the GUI Preview surface
		bool					mHasSurfaceSize;
		bool					mIsVisible;
		bool					mDirty;
		bool					mForceRedraw;
		size_t					mLastVertexCount;	//!< sum over the last update's batches
		size_t					mLastBatchCount;	//!< draw submissions in the last rebuild
		size_t					mRebuildCount;		//!< monotonic batch-resubmit counter
		size_t					mGeometryRebuildCount;	//!< monotonic element vertex-rebuild counter
	private:
		UiScreen(UiScreen const &);					// non-copyable
		UiScreen & operator=(UiScreen const &);		// non-copyable
	};

	//! @brief one z index of a UiScreen holding the drawable elements
	//! @remarks visibility rides here for whole widget groups (the Lua
	//! GuiLayer usertype wraps show/hide/isVisible/setVisible)
	class ORKIGE_ENGINE_DLL UiLayer
	{
		friend class UiScreen;
	public:
		inline bool isVisible() const { return this->mVisible; }
		inline void setVisible(bool visible)
		{
			if(this->mVisible == visible)
			{
				return;
			}
			this->mVisible = visible;
			this->_markDirty();
		}
		inline void show() { this->setVisible(true); }
		inline void hide() { this->setVisible(false); }

		//! z index inside the screen (0 draws first)
		inline uint getIndex() const { return this->mIndex; }

		//! @brief multiply the alpha of every vertex of this layer
		//! (0..1; fade whole widget groups)
		void setAlphaModifier(Real alphaModifier);
		inline Real getAlphaModifier() const { return this->mAlphaModifier; }

		//! @brief clip this layer to a pixel rect - the whole layer submits as
		//! its own scissored batch (a scroll viewport's content layer). Clears
		//! it with clearScissor. The clip is analytic + backend-identical
		//! (@see DrawLayer2D). Content on the clipped layer beyond the rect is
		//! trimmed at submission.
		void setScissor(DrawLayer2D::ScissorRect const & scissor);
		void clearScissor();
		inline bool hasScissor() const { return this->mHasScissor; }
		inline DrawLayer2D::ScissorRect const & getScissor() const { return this->mScissor; }

		//--- elements (create here, destroy here - the layer deletes) ---
		UiRect* createRectangle(Real left, Real top,
			Real width = 100, Real height = 100);
		UiRect* createRectangle(Vec2 const & position, Vec2 const & size)
		{
			return this->createRectangle(position.x, position.y,
				size.x, size.y);
		}
		void destroyRectangle(UiRect* rect);
		void destroyAllRectangles();

		UiCaption* createCaption(uint fontIndex, Real left, Real top,
			String const & text);
		void destroyCaption(UiCaption* caption);
		void destroyAllCaptions();

		UiMarkupText* createMarkupText(uint defaultFontIndex,
			Real left, Real top, String const & text);
		void destroyMarkupText(UiMarkupText* markupText);
		void destroyAllMarkupTexts();

		//--- atlas shortcuts for the elements ---
		inline Vec2 _getSolidUV() const
		{
			return this->mParent->getAtlas()->getWhitePixel();
		}
		inline UiSprite const * _getSprite(String const & name) const
		{
			return this->mParent->getAtlas()->getSprite(name);
		}
		inline UiFont const * _getFont(uint index) const
		{
			return this->mParent->getAtlas()->getFont(index);
		}
		//! @brief a font by REFERENCE - a role name (`heading`) or a `[Font.N]`
		//! index as text (`24`), the vocabulary a markup `[f=..]` span and the
		//! `.oui` `font` key share. NULL when the atlas carries neither.
		inline UiFont const * _getFontByRef(String const & ref) const
		{
			uint index = 0;
			if(!this->mParent->getAtlas()->resolveFontRef(ref, index))
			{
				return NULL;
			}
			return this->mParent->getAtlas()->getFont(index);
		}
		inline Color _getMarkupColour(uint index) const
		{
			return this->mParent->getAtlas()->getMarkupColour(index);
		}

		//! element callback: relayout/resubmit on the next update
		inline void _markDirty() { this->mParent->_markDirty(); }
	protected:
		UiLayer(uint index, UiScreen* parent);
		~UiLayer();

		//! append the (relaid-out where dirty) element vertices
		void _render(std::vector<UiVertex> & out, bool force);

		uint						mIndex;
		UiScreen*					mParent;
		std::vector<UiRect*>		mRects;
		std::vector<UiCaption*>		mCaptions;
		std::vector<UiMarkupText*>	mMarkupTexts;
		bool						mVisible;
		Real						mAlphaModifier;
		bool						mHasScissor;	//!< clip this layer to mScissor
		DrawLayer2D::ScissorRect	mScissor;		//!< the clip rect (pixels)
	private:
		UiLayer(UiLayer const &);					// non-copyable
		UiLayer & operator=(UiLayer const &);		// non-copyable
	};

	//! a single textured or solid rectangle (widget bodies, sprites,
	//! the solid whitepixel fill)
	class ORKIGE_ENGINE_DLL UiRect
	{
		friend class UiLayer;
	public:
		//! how the sprite fills the rect. Stretch (default) is one quad; the
		//! others need a sprite (a solid fill is always stretched).
		enum DrawMode
		{
			DM_Stretch = 0,	//!< one quad, the sprite stretched over the rect
			DM_NineSlice,	//!< fixed corner bands + stretched edges/centre
			DM_Tiled		//!< the sprite repeated across the rect
		};
		//! does a point lie within this rectangle?
		inline bool intersects(Vec2 const & coordinates) const
		{
			return coordinates.x >= this->mLeft &&
				coordinates.x <= this->mRight &&
				coordinates.y >= this->mTop &&
				coordinates.y <= this->mBottom;
		}

		inline Vec2 position() const { return Vec2(this->mLeft, this->mTop); }
		inline void position(Real left, Real top)
		{
			this->left(left);
			this->top(top);
		}
		inline void position(Vec2 const & position)
		{
			this->left(position.x);
			this->top(position.y);
		}

		inline Real left() const { return this->mLeft; }
		inline void left(Real left)
		{
			const Real w = this->width();
			this->mLeft = left;
			this->mRight = left + w;
			this->mDirty = true;
			this->mLayer->_markDirty();
		}

		inline Real top() const { return this->mTop; }
		inline void top(Real top)
		{
			const Real h = this->height();
			this->mTop = top;
			this->mBottom = top + h;
			this->mDirty = true;
			this->mLayer->_markDirty();
		}

		inline Real width() const { return this->mRight - this->mLeft; }
		inline void width(Real width)
		{
			this->mRight = this->mLeft + width;
			this->mDirty = true;
			this->mLayer->_markDirty();
		}

		inline Real height() const { return this->mBottom - this->mTop; }
		inline void height(Real height)
		{
			this->mBottom = this->mTop + height;
			this->mDirty = true;
			this->mLayer->_markDirty();
		}

		inline Vec2 size() const
		{
			return Vec2(this->width(), this->height());
		}

		//! tint colour (multiplies a sprite; fills solid otherwise)
		void background_colour(Color const & colour);
		//! current tint colour (the animation read-back for a colour tween)
		inline Color const & colour() const { return this->mColour; }
		//! skip drawing (alpha 0)
		void no_background();
		//! transparency of the whole rect
		void setAlpha(Real alpha);
		//! sprite from the atlas; NULL = solid whitepixel fill
		void background_image(UiSprite const * sprite);
		//! sprite by name; "" or "none" = solid whitepixel fill
		void background_image(String const & spriteNameOrNone);

		//! @brief fill mode (Stretch / NineSlice / Tiled). NineSlice needs a
		//! sprite carrying slice insets (else it falls back to Stretch); Tiled
		//! needs any sprite. A solid fill always draws stretched.
		void setDrawMode(DrawMode mode);
		inline DrawMode getDrawMode() const { return this->mDrawMode; }

		//! @brief a per-frame scale/rotation about a pivot (window pixels) applied
		//! to the emitted vertices - the widget-animation transform. Identity by
		//! default; changing it resubmits the batch but does NOT rebuild the local
		//! geometry (a cheap coast for an animating widget). @see UiLayer::_render
		void renderTransform(Ui2DTransform const & transform);
		inline Ui2DTransform const & renderTransform() const { return this->mRenderTransform; }
		//! @brief a per-frame alpha multiplier folded into the emitted vertex alpha
		//! (0..1) - the cascading group-alpha channel, independent of the base
		//! colour. 1 = opaque.
		void renderAlpha(Real alphaMultiplier);
		inline Real renderAlpha() const { return this->mRenderAlpha; }

		//! rebuild the vertices (dirty path; not for users)
		void _redraw();
	protected:
		UiRect(Real left, Real top, Real width, Real height, UiLayer* parent);
		~UiRect() {}

		UiLayer*				mLayer;
		Real					mLeft, mTop, mRight, mBottom;
		Color					mColour;
		Vec2					mUV[4];
		bool					mDirty;
		DrawMode				mDrawMode;
		//! the sprite backing a nine-slice/tiled fill, or NULL for a solid /
		//! plain-stretch fill. Points into the atlas (owned by GuiManager,
		//! outlives the rect, addresses stable in the atlas' sprite map).
		UiSprite const *		mSprite;
		Ui2DTransform			mRenderTransform;	//!< animation transform (post-pass)
		Real					mRenderAlpha;		//!< cascade alpha multiplier
		std::vector<UiVertex>	mVertices;
	};

	//! a single line of font text with alignment inside an optional box
	class ORKIGE_ENGINE_DLL UiCaption
	{
		friend class UiLayer;
	public:
		//! does a point lie within the caption's box?
		inline bool intersects(Vec2 const & coordinates) const
		{
			return coordinates.x >= this->mLeft &&
				coordinates.x <= this->mLeft + this->mWidth &&
				coordinates.y >= this->mTop &&
				coordinates.y <= this->mTop + this->mHeight;
		}

		inline Real left() const { return this->mLeft; }
		void left(Real left);
		inline Real top() const { return this->mTop; }
		void top(Real top);

		inline Real width() const { return this->mWidth; }
		void width(Real width);
		inline Real height() const { return this->mHeight; }
		void height(Real height);
		void size(Real width, Real height);

		inline String const & text() const { return this->mText; }
		void text(String const & text);

		inline TextAlignment align() const { return this->mAlignment; }
		void align(TextAlignment alignment);
		inline VerticalAlignment vertical_align() const
		{
			return this->mVerticalAlign;
		}
		void vertical_align(VerticalAlignment alignment);

		inline Color colour() const { return this->mColour; }
		void colour(Color const & colour);
		inline void colour(Colours::Colour colour)
		{
			this->colour(webcolour(colour));
		}

		//! historical flag (kept for API compatibility; layout always
		//! follows the global UiGlyph::scale, exactly like Gorilla did)
		inline void scaled(bool scaled) { this->mScaled = scaled; }

		//! @brief re-point this caption at another `[Font.N]` of the atlas
		//! (the `.oui` `font` key / a widget's setFontIndex). An index the
		//! atlas never loaded is ignored (the caption keeps its font), so a
		//! bad reference degrades to the current look, never to no text.
		void setFont(uint fontIndex);

		//! @brief a per-caption glyph SIZE multiplier on top of the global
		//! UiGlyph::scale (1 = the font's baked size). One baked font then
		//! serves several display sizes: every metric this caption reads -
		//! advance, glyph box, line height, space, kerning, letter spacing -
		//! is multiplied, so measurement, wrapping and the emitted quads all
		//! agree. A non-integer factor resamples the baked glyph texels, so
		//! text away from 1 (or an integer multiple) is softer than a font
		//! baked at that size - the documented trade.
		void setTextScale(Real scale);
		inline Real textScale() const { return this->mTextScale; }

		//! @brief read the text as inline RICH TEXT: `[c=RRGGBB]`/`[f=heading]`
		//! spans and `[sprite=name]` icons inside the one caption (@see
		//! TextMarkup.h for the grammar and its warn-and-stay-readable verdicts).
		//! Default OFF, and a plain string in markup mode measures and draws
		//! exactly as it does with markup off - the tags are the only difference.
		//! Composes with setWrap (runs and sprites flow across the breaks), with
		//! the alignment and with setTextScale. A styled run's own font may be
		//! TALLER than the caption's, which raises the line height for the whole
		//! block.
		void setMarkup(bool markup);
		inline bool getMarkup() const { return this->mMarkup; }

		//! @brief wrap the text to the caption's width() instead of a single
		//! clipped line. Break rules (@see TextWrap): break at spaces (latin),
		//! between CJK glyphs, hard-break a single over-wide word at the glyph
		//! limit; explicit '\n' forces a break. Each wrapped line honours the
		//! horizontal alignment inside width(); the block honours the vertical
		//! alignment inside height(). Default off (a single clipped line).
		void setWrap(bool wrap);
		inline bool getWrap() const { return this->mWrap; }
		//! @brief the wrapped height for a given box width (px): line count times
		//! the line height. width() is not touched. Feeds the layout resolver's
		//! height-for-width. A non-positive width or empty text measures one line.
		Real measureWrappedHeight(Real width) const;
		//! @brief the baked font backing this caption (NULL when the glyph index
		//! named no font). A widget that has to reason about the SAME wrap the
		//! caption draws - a multi-line field placing its caret - runs
		//! TextWrap::buildRun on it rather than duplicating the metrics.
		inline UiFont const * font() const { return this->mFont; }

		//! @brief per-frame scale/rotation about a pivot (@see UiRect::renderTransform)
		void renderTransform(Ui2DTransform const & transform);
		inline Ui2DTransform const & renderTransform() const { return this->mRenderTransform; }
		//! @brief per-frame cascade alpha multiplier (@see UiRect::renderAlpha)
		void renderAlpha(Real alphaMultiplier);
		inline Real renderAlpha() const { return this->mRenderAlpha; }

		//! measure the text without drawing (single line, kerning +
		//! letter spacing applied; x excludes the trailing kerning)
		void _calculateDrawSize(Vec2 & size);
		//! rebuild the vertices (dirty path; not for users)
		void _redraw();
	protected:
		UiCaption(uint fontIndex, Real left, Real top,
			String const & caption, UiLayer* parent);
		~UiCaption() {}

		UiLayer*				mLayer;
		UiFont const *			mFont;
		Real					mLeft, mTop, mWidth, mHeight;
		TextAlignment			mAlignment;
		VerticalAlignment		mVerticalAlign;
		String					mText;
		Color					mColour;
		bool					mDirty;
		bool					mScaled;
		bool					mWrap;				//!< wrap to width() (@see setWrap)
		bool					mMarkup;			//!< read the text as rich text (@see setMarkup)
		Real					mTextScale;			//!< per-caption size factor
		Ui2DTransform			mRenderTransform;	//!< animation transform (post-pass)
		Real					mRenderAlpha;		//!< cascade alpha multiplier
		std::vector<UiVertex>	mVertices;
		//! emit the vertices for the wrapped (multi-line) layout (@see _redraw)
		void _redrawWrapped();
		//! @brief the top of a block @p blockHeight tall inside height(), by the
		//! vertical alignment - the ONE rule every multi-line path places with
		Real _blockTop(Real blockHeight) const;
		//! emit the vertices for the styled-run (markup) layout (@see setMarkup)
		void _redrawMarkup();
		//! @brief measure the rich-text block at box width @p width (<= 0 = no
		//! width wrapping): the widest line into @p size.x, the line count times
		//! the block's line height into @p size.y. The ONE markup measurement -
		//! _calculateDrawSize, measureWrappedHeight and _redrawMarkup all read it.
		void _measureMarkup(Real width, Vec2 & size) const;

		//--- the metrics this caption lays out with: the font's *Scaled values
		//--- (global UiGlyph::scale) times this caption's own mTextScale. EVERY
		//--- metric read goes through these, so a scaled caption can never
		//--- half-apply its factor (measure one way, draw another).
		inline Real lineHeightPx() const
		{ return this->mFont->getLineHeightScaled() * this->mTextScale; }
		inline Real spaceLengthPx() const
		{ return this->mFont->getSpaceLengthScaled() * this->mTextScale; }
		inline Real letterSpacingPx() const
		{ return this->mFont->getLetterSpacingScaled() * this->mTextScale; }
		inline Real kerningPx(UiGlyph const & glyph, uint leftOf) const
		{ return glyph.getKerningScaled(leftOf) * this->mTextScale; }
		inline Real glyphWidthPx(UiGlyph const & glyph) const
		{ return glyph.getGlyphWidthScaled() * this->mTextScale; }
		inline Real glyphHeightPx(UiGlyph const & glyph) const
		{ return glyph.getGlyphHeightScaled() * this->mTextScale; }
		inline Real glyphAdvancePx(UiGlyph const & glyph) const
		{ return glyph.getGlyphAdvanceScaled() * this->mTextScale; }
	};

	//! @brief multi-line RICH text: the inline markup grammar
	//! (`[c=RRGGBB]`/`[f=heading]` spans, `[sprite=name]` icons, `[[` for a
	//! literal '['; @see TextMarkup.h) laid out over the shared line-breaker.
	//! Always reads its text as markup - it IS the styled-run element, so a
	//! literal '[' in its text needs the escape. A plain caption opts in per
	//! element instead (@see UiCaption::setMarkup).
	class ORKIGE_ENGINE_DLL UiMarkupText
	{
		friend class UiLayer;
	public:
		inline Real left() const { return this->mLeft; }
		void left(Real left);
		inline Real top() const { return this->mTop; }
		void top(Real top);

		//! measured size of the laid-out text (valid after
		//! _calculateCharacters, which overwrites any set value -
		//! historical Gorilla behavior the widgets rely on)
		inline Real width() const { return this->mWidth; }
		void width(Real width);
		inline Real height() const { return this->mHeight; }
		void height(Real height);
		void size(Real width, Real height);

		inline String const & text() const { return this->mText; }
		void text(String const & text);

		//! historical flag (@see UiCaption::scaled)
		inline void scaled(bool scaled) { this->mScaled = scaled; }

		//! @brief re-point the DEFAULT font (an `[f=..]` run still switches per
		//! run); an index the atlas never loaded is ignored (@see UiCaption::setFont)
		void setDefaultFont(uint fontIndex);
		//! @brief the colour a run starts in, outside any `[c=..]` span (white by
		//! default); closing a span returns to it
		void setDefaultColour(Color const & colour);
		inline Color const & defaultColour() const { return this->mDefaultColour; }
		//! @brief a per-element glyph SIZE multiplier (@see UiCaption::setTextScale)
		void setTextScale(Real scale);
		inline Real textScale() const { return this->mTextScale; }

		//! @brief wrap the markup to @c wrapWidth instead of laying out one long
		//! line per paragraph. Markup runs (colour/font) and inline sprites
		//! flow across the breaks - a run split by a wrap keeps its style, a
		//! sprite that does not fit moves whole to the next line; explicit '\n'
		//! still forces breaks (@see TextWrap). Default off (byte-identical to
		//! the historical layout).
		void setWrap(bool wrap);
		inline bool getWrap() const { return this->mWrap; }
		//! @brief the box width wrapping fits into (px); 0 or a non-positive value
		//! disables width wrapping even when wrap is on. Set from the widget size.
		void wrapWidth(Real width);
		inline Real wrapWidth() const { return this->mWrapWidth; }
		//! @brief the laid-out height for a given box width (px), the resolver's
		//! height-for-width hook. Does not mutate the live layout.
		Real measureWrappedHeight(Real width) const;

		//! @brief per-frame scale/rotation about a pivot (@see UiRect::renderTransform)
		void renderTransform(Ui2DTransform const & transform);
		inline Ui2DTransform const & renderTransform() const { return this->mRenderTransform; }
		//! @brief per-frame cascade alpha multiplier (@see UiRect::renderAlpha)
		void renderAlpha(Real alphaMultiplier);
		inline Real renderAlpha() const { return this->mRenderAlpha; }

		//! lay the characters out (also refreshes width()/height());
		//! runs automatically on dirty text before drawing
		void _calculateCharacters();
		//! rebuild the vertices (dirty path; not for users)
		void _redraw();
	protected:
		UiMarkupText(uint defaultFontIndex, Real left, Real top,
			String const & text, UiLayer* parent);
		~UiMarkupText() {}

		//! one laid-out character/sprite quad
		struct Character
		{
			Vec2	position[4];
			Vec2	uv[4];
			Color	colour;
		};

		UiLayer*				mLayer;
		UiFont const *			mDefaultFont;
		Real					mLeft, mTop, mWidth, mHeight;
		String					mText;
		bool					mDirty, mTextDirty;
		bool					mScaled;
		bool					mWrap;				//!< wrap to mWrapWidth (@see setWrap)
		Real					mWrapWidth;			//!< box width for wrapping (px)
		Color					mDefaultColour;		//!< the pre-markup run colour
		Real					mTextScale;			//!< per-element size factor
		Ui2DTransform			mRenderTransform;	//!< animation transform (post-pass)
		Real					mRenderAlpha;		//!< cascade alpha multiplier
		std::vector<Character>	mCharacters;
		std::vector<UiVertex>	mVertices;

		//! @brief the shared markup walk: parse @p text into positioned
		//! Characters, wrapping to @p wrapWidth px when > 0 (0 = the historical
		//! single-line-per-paragraph layout). Fills @p out + @p width + @p height.
		//! Static so @c measureWrappedHeight can run it into a throwaway buffer
		//! without disturbing the live layout.
		//! @param textScale the per-element glyph size factor (@see setTextScale)
		//! @param defaultColour the colour a run starts in (@see setDefaultColour)
		//! @param quiet suppress the markup parse/resolve warnings - a pure
		//! MEASUREMENT re-runs this pipeline, and only the draw should report
		static void _layoutMarkup(UiLayer* layer, UiFont const * defaultFont,
			String const & text, Real originX, Real originY, Real wrapWidth,
			Real textScale, Color const & defaultColour,
			std::vector<Character> & out, Real & width, Real & height,
			bool quiet = false);
	};
}

#endif //__UiRenderer_h__8_7_2026__23_30_00__
