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
		bool					mIsVisible;
		bool					mDirty;
		bool					mForceRedraw;
		size_t					mLastVertexCount;	//!< sum over the last update's batches
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
		std::vector<UiVertex>	mVertices;
	};

	//! @brief multi-line text with the light markup language: %0-%9
	//! switch to a markup colour, %R resets it, %M toggles monospace,
	//! %@N% switches the font, %:name% inserts a sprite, %% escapes
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
		std::vector<Character>	mCharacters;
		std::vector<UiVertex>	mVertices;
	};
}

#endif //__UiRenderer_h__8_7_2026__23_30_00__
