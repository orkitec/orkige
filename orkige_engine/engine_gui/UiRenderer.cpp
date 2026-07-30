/********************************************************************
	created:	Wednesday 2026/07/08 at 23:30
	filename: 	UiRenderer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the gui 2D renderer (@see UiRenderer.h). The glyph
				layout math (kerning/letter spacing, top-aligned glyph
				quads, caption alignment clipping) is
				ported from the retired Gorilla library (MIT, (c) 2010
				Robin Southern); the batching around it is new: one
				retained pixel-space triangle list per screen, one
				DrawLayer2D batch per screen, dirty-tracked rebuilds.
*********************************************************************/

#include "engine_gui/UiRenderer.h"
#include "engine_gui/TextWrap.h"
#include "engine_gui/TextMarkup.h"
#include "engine_render/RenderSystem.h"
#include "core_util/StringUtil.h"
#include <core_debug/DebugMacros.h>
#include <core_debug/MemoryManager.h>


#include <algorithm>
#include <cmath>
#include <vector>

namespace Orkige
{
	namespace
	{
		inline void pushVertex(std::vector<UiVertex> & vertices,
			Real x, Real y, Vec2 const & uv, Color const & colour)
		{
			vertices.push_back(UiVertex(x, y, uv.x, uv.y, colour));
		}

		//--- inline RICH TEXT (@see TextMarkup.h). The parse is pure; these two
		//--- helpers are the atlas-facing half, shared by BOTH styled-run
		//--- elements (a markup UiCaption and a UiMarkupText) so there is exactly
		//--- one place that resolves a run's references and one that places the
		//--- broken cells.
		//! @brief parse @p text and resolve every run against the layer's atlas:
		//! a font REFERENCE (role name or `[Font.N]` index) to a baked font, a
		//! sprite NAME to an atlas sprite. Each malformed tag and each reference
		//! the atlas does not carry is ONE warn - and the surrounding text keeps
		//! drawing (an unknown font falls back to @p defaultFont, an unknown
		//! sprite is dropped).
		//! @param quiet suppress the warnings: a MEASUREMENT runs the same
		//! pipeline (and may run several times per layout pass), so only the DRAW
		//! reports - one rebuild, one set of warnings.
		void resolveMarkupRuns(UiLayer* layer, UiFont const * defaultFont,
			Color const & defaultColour, String const & text,
			std::vector<TextMarkup::ResolvedRun> & out, bool quiet = false)
		{
			out.clear();
			TextMarkup::Parse parsed;
			TextMarkup::parse(text, parsed);
			if(!quiet)
			{
				for(String const & why : parsed.diagnostics)
				{
					oDebugWarning(false, "gui markup: " << why);
				}
			}
			for(TextMarkup::Run const & run : parsed.runs)
			{
				TextMarkup::ResolvedRun resolved;
				resolved.colour = run.hasColour
					? Color(run.colour[0], run.colour[1], run.colour[2],
						run.colour[3])
					: defaultColour;
				if(run.kind == TextMarkup::Run::RK_Sprite)
				{
					resolved.sprite = layer->_getSprite(run.sprite);
					if(resolved.sprite == NULL)
					{
						if(!quiet)
						{
							oDebugWarning(false, "gui markup: no sprite '"
								<< run.sprite
								<< "' in this atlas - the inline sprite is skipped");
						}
						continue;
					}
					out.push_back(resolved);
					continue;
				}
				resolved.text = run.text;
				resolved.font = defaultFont;
				if(!run.fontRef.empty())
				{
					if(UiFont const * font = layer->_getFontByRef(run.fontRef))
					{
						resolved.font = font;
					}
					else if(!quiet)
					{
						oDebugWarning(false, "gui markup: no font '" << run.fontRef
							<< "' in this atlas - the run draws in the default font");
					}
				}
				out.push_back(resolved);
			}
		}

		//! one placed styled-run quad (pixel corners + the atlas UVs + its colour)
		struct MarkupQuad
		{
			Real	left, top, right, bottom;
			Vec2	uv[4];
			Color	colour;
		};

		//! @brief the WHOLE styled-run pipeline for one element, in one place:
		//! parse the markup, resolve its references against the atlas, build the
		//! cells through the shared metrics, break them, and - unless @p quads is
		//! NULL - place them. A NULL @p quads is the MEASURE call: it stops right
		//! after the break, so a measurement never allocates or copies the quads
		//! it would only throw away (@see UiCaption::_measureMarkup, which the
		//! layout resolver may call several times per pass).
		//! @param wrapWidth <= 0 disables width wrapping (only '\n' opens a line)
		//! @param width/height OUT: the measured extents of the laid-out block
		void buildMarkupBlock(UiLayer* layer, UiFont const * defaultFont,
			Color const & defaultColour, String const & text, Real textScale,
			Real wrapWidth, bool quiet,
			Real originX, Real originY, Real boxWidth, TextAlignment alignment,
			Real clipLeft, Real clipRight,
			std::vector<MarkupQuad> * quads, Real & lineHeightOut,
			Real & width, Real & height);

		//! @brief place already-broken cells into quads, line by line. Each line
		//! honours @p alignment inside @p boxWidth (a non-positive width, or a left
		//! alignment, starts every line at @p originX); a cell whose glyph/sprite
		//! is absent (a space, a '\n') emits nothing.
		//! @param clipRight when > 0, a quad crossing it is dropped - the
		//! single-line clipping a non-wrapping caption does inside its box
		//! @param width/height OUT: the measured extents of the laid-out block
		void layoutMarkupQuads(std::vector<WrapCell> const & cells,
			std::vector<TextMarkup::CellAttr> const & attrs,
			WrapResult const & wrapped, Real lineHeight, Real textScale,
			Real originX, Real originY,
			Real boxWidth, TextAlignment alignment, Real clipLeft, Real clipRight,
			std::vector<MarkupQuad> & out, Real & width, Real & height)
		{
			out.clear();
			width = 0;
			const int lineCount = wrapped.lineCount > 0 ? wrapped.lineCount : 1;
			height = lineHeight * Real(lineCount);
			for(size_t each = 0; each < cells.size() && each < attrs.size(); ++each)
			{
				TextMarkup::CellAttr const & attr = attrs[each];
				const int line = wrapped.lineOf[each];
				const Real lineWidth = wrapped.lineWidth[size_t(line)];
				width = std::max(width, lineWidth);
				if(attr.glyph == NULL && attr.sprite == NULL)
				{
					continue;	// a space / newline cell draws nothing
				}
				Real lineStart = originX;
				if(boxWidth > 0)
				{
					if(alignment == TextAlign_Centre)
					{
						lineStart = originX + (boxWidth - lineWidth) * 0.5f;
					}
					else if(alignment == TextAlign_Right)
					{
						lineStart = originX + boxWidth - lineWidth;
					}
				}
				MarkupQuad quad;
				// whole-pixel pen: crisp glyphs on a point-sampled atlas. A run
				// shorter than the line drops to its bottom edge, so mixed sizes
				// share one edge instead of hanging from the top (@see dropY)
				quad.left = std::floor(lineStart + wrapped.penX[each]);
				quad.top = std::floor(originY + Real(line) * lineHeight +
					Real(attr.dropY));
				quad.colour = attr.colour;
				// the SAME scaled metrics the cells were measured with, so the
				// drawn quad and the wrap can never disagree
				const Real ts = textScale > 0 ? textScale : Real(1);
				if(attr.glyph != NULL)
				{
					quad.right = quad.left + attr.glyph->getGlyphWidthScaled() * ts;
					quad.bottom = quad.top + attr.glyph->getGlyphHeightScaled() * ts;
					for(size_t corner = 0; corner < 4; ++corner)
					{
						quad.uv[corner] = attr.glyph->texCoords[corner];
					}
				}
				else
				{
					quad.right = quad.left + Real(attr.sprite->spriteWidth) * ts;
					quad.bottom = quad.top + Real(attr.sprite->spriteHeight) * ts;
					for(size_t corner = 0; corner < 4; ++corner)
					{
						quad.uv[corner] = attr.sprite->texCoords[corner];
					}
				}
				if((clipLeft > 0 && quad.left < clipLeft) ||
					(clipRight > 0 && quad.right > clipRight))
				{
					continue;	// outside the box the caption clips to
				}
				out.push_back(quad);
			}
		}

		void buildMarkupBlock(UiLayer* layer, UiFont const * defaultFont,
			Color const & defaultColour, String const & text, Real textScale,
			Real wrapWidth, bool quiet,
			Real originX, Real originY, Real boxWidth, TextAlignment alignment,
			Real clipLeft, Real clipRight,
			std::vector<MarkupQuad> * quads, Real & lineHeightOut,
			Real & width, Real & height)
		{
			width = 0;
			lineHeightOut = defaultFont != NULL
				? defaultFont->getLineHeightScaled() *
					(textScale > 0 ? textScale : Real(1))
				: Real(0);
			height = lineHeightOut;
			if(quads != NULL)
			{
				quads->clear();
			}
			if(defaultFont == NULL)
			{
				return;
			}
			std::vector<TextMarkup::ResolvedRun> runs;
			resolveMarkupRuns(layer, defaultFont, defaultColour, text, runs, quiet);
			std::vector<WrapCell> cells;
			std::vector<TextMarkup::CellAttr> attrs;
			float lineHeight = float(lineHeightOut);
			TextMarkup::buildCells(runs, float(textScale), cells, attrs,
				lineHeight);
			lineHeightOut = Real(lineHeight);
			WrapResult wrapped;
			TextWrap::wrap(cells, wrapWidth, wrapped);
			const int lineCount = wrapped.lineCount > 0 ? wrapped.lineCount : 1;
			height = lineHeightOut * Real(lineCount);
			if(quads == NULL)
			{
				// MEASURE: the widest line is all the caller wants
				for(int line = 0; line < lineCount; ++line)
				{
					width = std::max(width, wrapped.lineWidth[size_t(line)]);
				}
				return;
			}
			layoutMarkupQuads(cells, attrs, wrapped, lineHeightOut, textScale,
				originX, originY, boxWidth, alignment, clipLeft, clipRight,
				*quads, width, height);
		}

		//! @brief apply an element's per-frame transform + alpha multiplier to the
		//! range it just appended to the batch. Kept OUT of the element's cached
		//! vertices (rebuilt only on content change) so an animating widget coasts
		//! without relaying out glyphs - the transform/alpha ride on the emitted
		//! copy each frame. @see UiRect::renderTransform
		inline void applyElementPost(std::vector<UiVertex> & out, size_t begin,
			Ui2DTransform const & transform, Real alphaMultiplier)
		{
			const bool hasTransform = !transform.isIdentity();
			const bool hasAlpha = alphaMultiplier != 1.0f;
			if(!hasTransform && !hasAlpha)
			{
				return;
			}
			for(size_t each = begin; each < out.size(); ++each)
			{
				if(hasTransform)
				{
					float tx = 0.0f, ty = 0.0f;
					transform.apply(out[each].x, out[each].y, tx, ty);
					out[each].x = tx;
					out[each].y = ty;
				}
				if(hasAlpha)
				{
					out[each].colour.a *= alphaMultiplier;
				}
			}
		}

		//! two triangles for one quad, in the historical fill winding
		//! (a=TL b=TR c=BL d=BR; triangles (c,b,a),(c,d,b))
		inline void pushQuad(std::vector<UiVertex> & vertices,
			Real x0, Real y0, Real x1, Real y1,
			Real u0, Real v0, Real u1, Real v1, Color const & colour)
		{
			vertices.push_back(UiVertex(x0, y1, u0, v1, colour));	// BL
			vertices.push_back(UiVertex(x1, y0, u1, v0, colour));	// TR
			vertices.push_back(UiVertex(x0, y0, u0, v0, colour));	// TL
			vertices.push_back(UiVertex(x0, y1, u0, v1, colour));	// BL
			vertices.push_back(UiVertex(x1, y1, u1, v1, colour));	// BR
			vertices.push_back(UiVertex(x1, y0, u1, v0, colour));	// TR
		}
	}
	//---------------------------------------------------------
	//--- UiScreen --------------------------------------------
	//---------------------------------------------------------
	UiScreen::UiScreen(UiAtlas const * atlas,
		optr<DrawLayer2D> const & drawLayer)
		: mAtlas(atlas), mDrawLayer(drawLayer), mWidth(0), mHeight(0),
		mHasSurfaceSize(false),
		mIsVisible(true), mDirty(false), mForceRedraw(false), mLastVertexCount(0),
		mLastBatchCount(0), mRebuildCount(0), mGeometryRebuildCount(0)
	{
		oAssert(this->mAtlas);
		oAssert(this->mDrawLayer);
		unsigned int windowWidth = 0, windowHeight = 0;
		RenderSystem::get()->getWindowSize(windowWidth, windowHeight);
		this->mWidth = Real(windowWidth);
		this->mHeight = Real(windowHeight);
	}
	//---------------------------------------------------------
	void UiScreen::setSurfaceSize(Real width, Real height)
	{
		if(width > 0 && height > 0)
		{
			this->mHasSurfaceSize = true;
			this->mWidth = width;
			this->mHeight = height;
		}
		else
		{
			this->mHasSurfaceSize = false;
		}
		this->requestFullRedraw();
	}
	//---------------------------------------------------------
	UiScreen::~UiScreen()
	{
		for(UiLayer* layer : this->mLayers)
		{
			delete layer;
		}
		// dropping the draw-layer handle removes the screen from the
		// window compositing (facade RAII)
	}
	//---------------------------------------------------------
	UiLayer* UiScreen::createLayer(uint index)
	{
		UiLayer* layer = new UiLayer(index, this);
		// keep mLayers sorted by index, creation order within one index
		// (= Gorilla's index-bucket draw order)
		std::vector<UiLayer*>::iterator at = this->mLayers.begin();
		while(at != this->mLayers.end() && (*at)->getIndex() <= index)
		{
			++at;
		}
		this->mLayers.insert(at, layer);
		this->_markDirty();
		return layer;
	}
	//---------------------------------------------------------
	void UiScreen::destroy(UiLayer* layer)
	{
		if(layer == NULL)
		{
			return;
		}
		this->mLayers.erase(std::find(this->mLayers.begin(),
			this->mLayers.end(), layer));
		delete layer;
		this->_markDirty();
	}
	//---------------------------------------------------------
	void UiScreen::setVisible(bool visible)
	{
		this->mIsVisible = visible;
		this->mDrawLayer->setVisible(visible);
	}
	//---------------------------------------------------------
	void UiScreen::setZOrder(int zOrder)
	{
		this->mDrawLayer->setZOrder(zOrder);
	}
	//---------------------------------------------------------
	void UiScreen::requestFullRedraw()
	{
		this->mForceRedraw = true;
		this->mDirty = true;
	}
	//---------------------------------------------------------
	void UiScreen::update()
	{
		// window resizes force a full relayout: widget positions are
		// pixels, and the atlas metrics stay pixel-exact on any size
		bool force = this->mForceRedraw;
		// a pinned surface (the GUI Preview stage) lays out at its own fixed
		// size; otherwise the live window size drives it, and a window resize
		// forces a full relayout (widget positions are pixels)
		if(!this->mHasSurfaceSize)
		{
			unsigned int windowWidth = 0, windowHeight = 0;
			RenderSystem::get()->getWindowSize(windowWidth, windowHeight);
			if(windowWidth > 0 && windowHeight > 0 &&
				(this->mWidth != Real(windowWidth) ||
				 this->mHeight != Real(windowHeight)))
			{
				this->mWidth = Real(windowWidth);
				this->mHeight = Real(windowHeight);
				force = true;
			}
		}
		if(!this->mDirty && !force)
		{
			return;	// steady state: nothing rebuilt, nothing resubmitted
		}
		this->mForceRedraw = false;
		this->mDirty = false;

		// a full rebuild this frame (the dirty path); the batch tally restarts
		++this->mRebuildCount;
		this->mLastBatchCount = 0;
		this->mDrawLayer->clear();
		this->mLastVertexCount = 0;
		this->mScratch.clear();
		// growth probe over the whole rebuild: the scratch is a reused member,
		// so it only reallocates when this screen out-grows its previous peak
		const std::size_t scratchCapacityBefore = this->mScratch.capacity();

		// The common case has no scissored layer: every visible layer
		// concatenates into ONE batch = one draw call per screen (unchanged).
		// A scroll viewport tags its content layer with a clip rect; such a
		// layer flushes the pending unclipped batch, then submits its own
		// scissored batch - batch count grows by one per scroll region only.
		bool anyScissor = false;
		for(UiLayer* layer : this->mLayers)
		{
			if(layer->mVisible && layer->hasScissor())
			{
				anyScissor = true;
				break;
			}
		}
		if(!anyScissor)
		{
			for(UiLayer* layer : this->mLayers)
			{
				if(layer->mVisible)
				{
					layer->_render(this->mScratch, force);
				}
			}
			this->_flushBatch(NULL);
			MemoryManager::countGrowth(MemoryManager::TAG_GUI,
				scratchCapacityBefore, this->mScratch.capacity());
			return;
		}
		for(UiLayer* layer : this->mLayers)
		{
			if(!layer->mVisible)
			{
				continue;
			}
			if(layer->hasScissor())
			{
				// close the run of unclipped layers, then emit this layer clipped
				this->_flushBatch(NULL);
				layer->_render(this->mScratch, force);
				const DrawLayer2D::ScissorRect scissor = layer->getScissor();
				this->_flushBatch(&scissor);
			}
			else
			{
				layer->_render(this->mScratch, force);
			}
		}
		this->_flushBatch(NULL);
		MemoryManager::countGrowth(MemoryManager::TAG_GUI,
			scratchCapacityBefore, this->mScratch.capacity());
	}
	//---------------------------------------------------------
	void UiScreen::_flushBatch(DrawLayer2D::ScissorRect const * scissor)
	{
		if(this->mScratch.empty())
		{
			return;
		}
		this->mDrawLayer->addTriangles(this->mAtlas->getTextureName(),
			this->mScratch.data(), this->mScratch.size(),
			NULL, 0, scissor);
		this->mLastVertexCount += this->mScratch.size();
		++this->mLastBatchCount;	// one real draw submission (non-empty batch)
		this->mScratch.clear();
	}
	//---------------------------------------------------------
	//--- UiLayer ---------------------------------------------
	//---------------------------------------------------------
	UiLayer::UiLayer(uint index, UiScreen* parent)
		: mIndex(index), mParent(parent), mVisible(true), mAlphaModifier(1.0f),
		mHasScissor(false)
	{
	}
	//---------------------------------------------------------
	void UiLayer::setScissor(DrawLayer2D::ScissorRect const & scissor)
	{
		this->mHasScissor = true;
		this->mScissor = scissor;
		this->_markDirty();
	}
	//---------------------------------------------------------
	void UiLayer::clearScissor()
	{
		if(!this->mHasScissor)
		{
			return;
		}
		this->mHasScissor = false;
		this->_markDirty();
	}
	//---------------------------------------------------------
	UiLayer::~UiLayer()
	{
		this->destroyAllRectangles();
		this->destroyAllCaptions();
		this->destroyAllMarkupTexts();
	}
	//---------------------------------------------------------
	void UiLayer::setAlphaModifier(Real alphaModifier)
	{
		this->mAlphaModifier = alphaModifier;
		this->_markDirty();
	}
	//---------------------------------------------------------
	UiRect* UiLayer::createRectangle(Real left, Real top,
		Real width, Real height)
	{
		UiRect* rect = new UiRect(left, top, width, height, this);
		this->mRects.push_back(rect);
		return rect;
	}
	//---------------------------------------------------------
	void UiLayer::destroyRectangle(UiRect* rect)
	{
		if(rect == NULL)
		{
			return;
		}
		this->mRects.erase(std::find(this->mRects.begin(),
			this->mRects.end(), rect));
		delete rect;
		this->_markDirty();
	}
	//---------------------------------------------------------
	void UiLayer::destroyAllRectangles()
	{
		for(UiRect* rect : this->mRects)
		{
			delete rect;
		}
		this->mRects.clear();
		this->_markDirty();
	}
	//---------------------------------------------------------
	UiCaption* UiLayer::createCaption(uint fontIndex, Real left, Real top,
		String const & text)
	{
		UiCaption* caption = new UiCaption(fontIndex, left, top, text, this);
		this->mCaptions.push_back(caption);
		return caption;
	}
	//---------------------------------------------------------
	void UiLayer::destroyCaption(UiCaption* caption)
	{
		if(caption == NULL)
		{
			return;
		}
		this->mCaptions.erase(std::find(this->mCaptions.begin(),
			this->mCaptions.end(), caption));
		delete caption;
		this->_markDirty();
	}
	//---------------------------------------------------------
	void UiLayer::destroyAllCaptions()
	{
		for(UiCaption* caption : this->mCaptions)
		{
			delete caption;
		}
		this->mCaptions.clear();
		this->_markDirty();
	}
	//---------------------------------------------------------
	UiMarkupText* UiLayer::createMarkupText(uint defaultFontIndex,
		Real left, Real top, String const & text)
	{
		UiMarkupText* markupText =
			new UiMarkupText(defaultFontIndex, left, top, text, this);
		this->mMarkupTexts.push_back(markupText);
		return markupText;
	}
	//---------------------------------------------------------
	void UiLayer::destroyMarkupText(UiMarkupText* markupText)
	{
		if(markupText == NULL)
		{
			return;
		}
		this->mMarkupTexts.erase(std::find(this->mMarkupTexts.begin(),
			this->mMarkupTexts.end(), markupText));
		delete markupText;
		this->_markDirty();
	}
	//---------------------------------------------------------
	void UiLayer::destroyAllMarkupTexts()
	{
		for(UiMarkupText* markupText : this->mMarkupTexts)
		{
			delete markupText;
		}
		this->mMarkupTexts.clear();
		this->_markDirty();
	}
	//---------------------------------------------------------
	void UiLayer::_render(std::vector<UiVertex> & out, bool force)
	{
		if(this->mAlphaModifier == 0.0f)
		{
			return;
		}
		const size_t begin = out.size();

		for(UiRect* rect : this->mRects)
		{
			if(rect->mDirty || force)
			{
				// a real GEOMETRY rebuild (vertices rebuilt), distinct from the
				// batch resubmit below: a post-pass transform/alpha never sets
				// mDirty, so animating one leaves this counter untouched
				++this->mParent->mGeometryRebuildCount;
				rect->_redraw();
			}
			const size_t elementBegin = out.size();
			out.insert(out.end(), rect->mVertices.begin(),
				rect->mVertices.end());
			applyElementPost(out, elementBegin, rect->mRenderTransform,
				rect->mRenderAlpha);
		}
		for(UiCaption* caption : this->mCaptions)
		{
			if(caption->mDirty || force)
			{
				++this->mParent->mGeometryRebuildCount;
				caption->_redraw();
			}
			const size_t elementBegin = out.size();
			out.insert(out.end(), caption->mVertices.begin(),
				caption->mVertices.end());
			applyElementPost(out, elementBegin, caption->mRenderTransform,
				caption->mRenderAlpha);
		}
		for(UiMarkupText* markupText : this->mMarkupTexts)
		{
			if(markupText->mTextDirty || force)
			{
				markupText->_calculateCharacters();
			}
			if(markupText->mDirty || force)
			{
				++this->mParent->mGeometryRebuildCount;
				markupText->_redraw();
			}
			const size_t elementBegin = out.size();
			out.insert(out.end(), markupText->mVertices.begin(),
				markupText->mVertices.end());
			applyElementPost(out, elementBegin, markupText->mRenderTransform,
				markupText->mRenderAlpha);
		}

		if(this->mAlphaModifier != 1.0f)
		{
			for(size_t each = begin; each < out.size(); ++each)
			{
				out[each].colour.a *= this->mAlphaModifier;
			}
		}
	}
	//---------------------------------------------------------
	//--- UiRect ----------------------------------------------
	//---------------------------------------------------------
	UiRect::UiRect(Real left, Real top, Real width, Real height,
		UiLayer* parent)
		: mLayer(parent), mLeft(left), mTop(top), mRight(left + width),
		mBottom(top + height), mColour(1, 1, 1, 1), mDirty(true),
		mDrawMode(DM_Stretch), mSprite(NULL), mRenderAlpha(1.0f)
	{
		this->mUV[0] = this->mUV[1] = this->mUV[2] = this->mUV[3] =
			this->mLayer->_getSolidUV();
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::background_colour(Color const & colour)
	{
		this->mColour = colour;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::no_background()
	{
		this->mColour.a = 0;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::setAlpha(Real alpha)
	{
		this->mColour.a = alpha;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::background_image(UiSprite const * sprite)
	{
		// keep the sprite for the nine-slice/tiled fill modes (NULL = solid);
		// the sprite outlives the rect (owned by the atlas / GuiManager)
		this->mSprite = sprite;
		if(sprite == NULL)
		{
			this->mUV[0] = this->mUV[1] = this->mUV[2] = this->mUV[3] =
				this->mLayer->_getSolidUV();
		}
		else
		{
			this->mUV[TopLeft].x = this->mUV[BottomLeft].x = sprite->uvLeft;
			this->mUV[TopLeft].y = this->mUV[TopRight].y = sprite->uvTop;
			this->mUV[TopRight].x = this->mUV[BottomRight].x = sprite->uvRight;
			this->mUV[BottomRight].y = this->mUV[BottomLeft].y =
				sprite->uvBottom;
		}
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::setDrawMode(DrawMode mode)
	{
		if(this->mDrawMode == mode)
		{
			return;
		}
		this->mDrawMode = mode;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::renderTransform(Ui2DTransform const & transform)
	{
		// the transform rides on the emitted copy each frame (@see _render), so
		// only the batch needs resubmitting - the local geometry stays cached
		this->mRenderTransform = transform;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::renderAlpha(Real alphaMultiplier)
	{
		this->mRenderAlpha = alphaMultiplier;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiRect::background_image(String const & spriteNameOrNone)
	{
		if(spriteNameOrNone.empty() || spriteNameOrNone == "none")
		{
			this->background_image(static_cast<UiSprite const *>(NULL));
			return;
		}
		UiSprite const * sprite = this->mLayer->_getSprite(spriteNameOrNone);
		oAssertDesc(sprite, "UiRect: sprite not found: " << spriteNameOrNone);
		if(sprite == NULL)
		{
			return;
		}
		this->background_image(sprite);
	}
	//---------------------------------------------------------
	void UiRect::_redraw()
	{
		if(this->mDirty == false)
		{
			return;
		}
		this->mVertices.clear();
		if(this->mColour.a == 0)
		{
			this->mDirty = false;
			return;
		}

		const Real width = this->mRight - this->mLeft;
		const Real height = this->mBottom - this->mTop;

		// a sprite-backed nine-slice / tiled fill emits several quads; a solid
		// fill or a plain sprite is one stretched quad (mUV holds its corners).
		// UiGlyph::scale gives the corner bands a stable DEVICE size, matching
		// how glyphs and authored sizes scale with the display density.
		if(this->mSprite != NULL && this->mDrawMode == DM_NineSlice &&
			this->mSprite->hasSlices() && width > 0 && height > 0)
		{
			const Real scaleX = UiGlyph::scale.x;
			const Real scaleY = UiGlyph::scale.y;
			std::vector<UiNineSlice::Quad> quads;
			UiNineSlice::buildNineSlice(this->mLeft, this->mTop, width, height,
				this->mSprite->sliceLeft * scaleX,
				this->mSprite->sliceRight * scaleX,
				this->mSprite->sliceTop * scaleY,
				this->mSprite->sliceBottom * scaleY,
				this->mSprite->uvLeft, this->mSprite->uvTop,
				this->mSprite->uvRight, this->mSprite->uvBottom,
				this->mSprite->spriteWidth > 0
					? this->mSprite->sliceLeft / this->mSprite->spriteWidth : 0,
				this->mSprite->spriteWidth > 0
					? this->mSprite->sliceRight / this->mSprite->spriteWidth : 0,
				this->mSprite->spriteHeight > 0
					? this->mSprite->sliceTop / this->mSprite->spriteHeight : 0,
				this->mSprite->spriteHeight > 0
					? this->mSprite->sliceBottom / this->mSprite->spriteHeight : 0,
				quads);
			for(UiNineSlice::Quad const & q : quads)
			{
				pushQuad(this->mVertices, q.x0, q.y0, q.x1, q.y1,
					q.u0, q.v0, q.u1, q.v1, this->mColour);
			}
		}
		else if(this->mSprite != NULL && this->mDrawMode == DM_Tiled &&
			width > 0 && height > 0)
		{
			std::vector<UiNineSlice::Quad> quads;
			UiNineSlice::buildTiled(this->mLeft, this->mTop, width, height,
				this->mSprite->spriteWidth * UiGlyph::scale.x,
				this->mSprite->spriteHeight * UiGlyph::scale.y,
				this->mSprite->uvLeft, this->mSprite->uvTop,
				this->mSprite->uvRight, this->mSprite->uvBottom, quads);
			for(UiNineSlice::Quad const & q : quads)
			{
				pushQuad(this->mVertices, q.x0, q.y0, q.x1, q.y1,
					q.u0, q.v0, q.u1, q.v1, this->mColour);
			}
		}
		else
		{
			// corner walk: a=TL b=TR c=BL d=BR; triangles (c,b,a),(c,d,b)
			// - Gorilla's fill winding
			pushVertex(this->mVertices, this->mLeft, this->mBottom,
				this->mUV[BottomLeft], this->mColour);
			pushVertex(this->mVertices, this->mRight, this->mTop,
				this->mUV[TopRight], this->mColour);
			pushVertex(this->mVertices, this->mLeft, this->mTop,
				this->mUV[TopLeft], this->mColour);

			pushVertex(this->mVertices, this->mLeft, this->mBottom,
				this->mUV[BottomLeft], this->mColour);
			pushVertex(this->mVertices, this->mRight, this->mBottom,
				this->mUV[BottomRight], this->mColour);
			pushVertex(this->mVertices, this->mRight, this->mTop,
				this->mUV[TopRight], this->mColour);
		}
		this->mDirty = false;
	}
	//---------------------------------------------------------
	//--- UiNineSlice (pure quad emitters) --------------------
	//---------------------------------------------------------
	namespace UiNineSlice
	{
		namespace
		{
			//! append a quad, skipping a zero/negative-area cell
			inline void emit(std::vector<Quad> & out, Real x0, Real y0,
				Real x1, Real y1, Real u0, Real v0, Real u1, Real v1)
			{
				if(x1 <= x0 || y1 <= y0)
				{
					return;
				}
				Quad q;
				q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
				q.u0 = u0; q.v0 = v0; q.u1 = u1; q.v1 = v1;
				out.push_back(q);
			}
		}
		//-----------------------------------------------------
		void buildNineSlice(Real left, Real top, Real width, Real height,
			Real cornerL, Real cornerR, Real cornerT, Real cornerB,
			Real uL, Real uT, Real uR, Real uB,
			Real fracL, Real fracR, Real fracT, Real fracB,
			std::vector<Quad> & out)
		{
			// shrink the corner bands proportionally when the target is too
			// small to hold both of them, so the middle column/row never
			// inverts and no quads overlap
			Real bandL = cornerL, bandR = cornerR;
			if(bandL + bandR > width && bandL + bandR > 0)
			{
				const Real k = width / (bandL + bandR);
				bandL *= k;
				bandR *= k;
			}
			Real bandT = cornerT, bandB = cornerB;
			if(bandT + bandB > height && bandT + bandB > 0)
			{
				const Real k = height / (bandT + bandB);
				bandT *= k;
				bandB *= k;
			}

			// pixel column/row split lines
			const Real x0 = left;
			const Real x1 = left + bandL;
			const Real x2 = left + width - bandR;
			const Real x3 = left + width;
			const Real y0 = top;
			const Real y1 = top + bandT;
			const Real y2 = top + height - bandB;
			const Real y3 = top + height;

			// UV split lines from the corner fractions of the sprite span
			const Real uSpan = uR - uL;
			const Real vSpan = uB - uT;
			const Real ux0 = uL;
			const Real ux1 = uL + fracL * uSpan;
			const Real ux2 = uR - fracR * uSpan;
			const Real ux3 = uR;
			const Real vy0 = uT;
			const Real vy1 = uT + fracT * vSpan;
			const Real vy2 = uB - fracB * vSpan;
			const Real vy3 = uB;

			// row 0 (top): TL corner, top edge, TR corner
			emit(out, x0, y0, x1, y1, ux0, vy0, ux1, vy1);
			emit(out, x1, y0, x2, y1, ux1, vy0, ux2, vy1);
			emit(out, x2, y0, x3, y1, ux2, vy0, ux3, vy1);
			// row 1 (middle): left edge, centre, right edge
			emit(out, x0, y1, x1, y2, ux0, vy1, ux1, vy2);
			emit(out, x1, y1, x2, y2, ux1, vy1, ux2, vy2);
			emit(out, x2, y1, x3, y2, ux2, vy1, ux3, vy2);
			// row 2 (bottom): BL corner, bottom edge, BR corner
			emit(out, x0, y2, x1, y3, ux0, vy2, ux1, vy3);
			emit(out, x1, y2, x2, y3, ux1, vy2, ux2, vy3);
			emit(out, x2, y2, x3, y3, ux2, vy2, ux3, vy3);
		}
		//-----------------------------------------------------
		void buildTiled(Real left, Real top, Real width, Real height,
			Real tileW, Real tileH, Real uL, Real uT, Real uR, Real uB,
			std::vector<Quad> & out)
		{
			if(tileW <= 0 || tileH <= 0)
			{
				emit(out, left, top, left + width, top + height, uL, uT, uR, uB);
				return;
			}
			const Real uSpan = uR - uL;
			const Real vSpan = uB - uT;
			for(Real y = 0; y < height; y += tileH)
			{
				const Real cellH = std::min(tileH, height - y);
				const Real vFrac = cellH / tileH;
				for(Real x = 0; x < width; x += tileW)
				{
					const Real cellW = std::min(tileW, width - x);
					const Real uFrac = cellW / tileW;
					emit(out, left + x, top + y,
						left + x + cellW, top + y + cellH,
						uL, uT, uL + uFrac * uSpan, uT + vFrac * vSpan);
				}
			}
		}
	}
	//---------------------------------------------------------
	//--- UiCaption -------------------------------------------
	//---------------------------------------------------------
	UiCaption::UiCaption(uint fontIndex, Real left, Real top,
		String const & caption, UiLayer* parent)
		: mLayer(parent), mFont(parent->_getFont(fontIndex)),
		mLeft(left), mTop(top), mWidth(0), mHeight(0),
		mAlignment(TextAlign_Left), mVerticalAlign(VerticalAlign_Top),
		mText(caption), mColour(1, 1, 1, 1), mDirty(true), mScaled(true),
		mWrap(false), mMarkup(false), mTextScale(1.0f), mRenderAlpha(1.0f)
	{
		oAssertDesc(this->mFont, "UiCaption: font (glyph index) not in atlas");
		if(this->mFont == NULL)
		{
			this->mDirty = false;
			return;
		}
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::left(Real left)
	{
		oAssertDesc(std::floor(left) == std::ceil(left),
			"Gui: text label positioned on subpixel. expect graphical artefacts.");
		this->mLeft = left;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::top(Real top)
	{
		oAssertDesc(std::floor(top) == std::ceil(top),
			"Gui: text label positioned on subpixel. expect graphical artefacts.");
		this->mTop = top;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::width(Real width)
	{
		this->mWidth = width;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::height(Real height)
	{
		this->mHeight = height;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::size(Real width, Real height)
	{
		this->mWidth = width;
		this->mHeight = height;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::text(String const & text)
	{
		this->mText = text;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::align(TextAlignment alignment)
	{
		this->mAlignment = alignment;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::vertical_align(VerticalAlignment alignment)
	{
		this->mVerticalAlign = alignment;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::colour(Color const & colour)
	{
		this->mColour = colour;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::renderTransform(Ui2DTransform const & transform)
	{
		this->mRenderTransform = transform;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::renderAlpha(Real alphaMultiplier)
	{
		this->mRenderAlpha = alphaMultiplier;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::setWrap(bool wrap)
	{
		if(this->mWrap == wrap)
		{
			return;
		}
		this->mWrap = wrap;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::setMarkup(bool markup)
	{
		if(this->mMarkup == markup)
		{
			return;
		}
		this->mMarkup = markup;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::setFont(uint fontIndex)
	{
		UiFont const * font = this->mLayer->_getFont(fontIndex);
		if(font == NULL || font == this->mFont)
		{
			// an index the atlas never loaded keeps the current font (the
			// caller warns): a bad reference must never blank the text
			return;
		}
		this->mFont = font;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::setTextScale(Real scale)
	{
		if(scale <= 0.0f || this->mTextScale == scale)
		{
			return;
		}
		this->mTextScale = scale;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::_measureMarkup(Real width, Vec2 & size) const
	{
		// the MEASURE call of the one styled-run pipeline: it stops after the line
		// break, so no quad is built for a number (@see buildMarkupBlock), and it
		// stays quiet - only the draw reports a malformed tag
		Real lineHeight = 0;
		buildMarkupBlock(this->mLayer, this->mFont, this->mColour, this->mText,
			this->mTextScale, width, true, 0, 0, 0, TextAlign_Left, 0, 0, NULL,
			lineHeight, size.x, size.y);
	}
	//---------------------------------------------------------
	Real UiCaption::measureWrappedHeight(Real width) const
	{
		if(this->mFont == NULL)
		{
			return 0;
		}
		const Real lineHeight = this->lineHeightPx();
		if(this->mText.empty())
		{
			return lineHeight;
		}
		if(this->mMarkup)
		{
			Vec2 size;
			this->_measureMarkup(width, size);
			return size.y;
		}
		std::vector<WrapCell> cells;
		std::vector<UiGlyph const *> glyphs;
		TextWrap::buildRun(*this->mFont, this->mText, cells, glyphs,
			float(this->mTextScale));
		WrapResult wrapped;
		TextWrap::wrap(cells, width, wrapped);
		const int lines = wrapped.lineCount > 0 ? wrapped.lineCount : 1;
		return lineHeight * Real(lines);
	}
	//---------------------------------------------------------
	void UiCaption::_calculateDrawSize(Vec2 & size)
	{
		oAssertDesc(this->mFont != NULL,
			"Font rendering can't find glyph data. Font size correctly specified?");
		if(this->mMarkup)
		{
			// rich text measures over its RUNS (the tags are not glyphs), at the
			// caption's own width when it wraps and unconstrained otherwise
			this->_measureMarkup(this->mWrap ? this->mWidth : Real(0), size);
			return;
		}
		Real cursor = 0, kerning = 0;
		uint thisChar = 0, lastChar = 0;
		size.x = 0;
		size.y = this->lineHeightPx();

		for(size_t i = 0; i < this->mText.length(); ++i)
		{
			wchar_t unicodeChar;
			const std::size_t multiByteLength =
				StringUtil::multibyteCharStringToWideCharString(
					&unicodeChar, &this->mText[i], 5);
			thisChar = uint(unicodeChar);

			if(thisChar == ' ')
			{
				lastChar = thisChar;
				cursor += this->spaceLengthPx();
				continue;
			}
			UiGlyph const * glyph = this->mFont->getGlyph(thisChar);
			if(glyph == NULL)
			{
				lastChar = 0;
				continue;
			}
			kerning = this->kerningPx(*glyph, lastChar);
			if(kerning == 0)
			{
				kerning = this->letterSpacingPx();
			}
			cursor += this->glyphAdvancePx(*glyph) + kerning;
			lastChar = thisChar;
			if(multiByteLength > 0)
			{
				i += multiByteLength - 1;
			}
		}
		size.x = cursor - kerning;
	}
	//---------------------------------------------------------
	void UiCaption::_redraw()
	{
		if(this->mDirty == false)
		{
			return;
		}
		this->mVertices.clear();
		if(this->mText.empty())
		{
			this->mDirty = false;
			return;
		}
		oAssertDesc(this->mFont != NULL,
			"Font rendering can't find glyph data. Font size correctly specified?");

		// rich text takes the styled-run path (which handles wrapped AND single
		// line, since a run stream needs one placement either way)
		if(this->mMarkup)
		{
			this->_redrawMarkup();
			this->mDirty = false;
			return;
		}

		// wrap-to-width labels break across lines (@see _redrawWrapped); the
		// single-line clipped path below is byte-identical when wrap is off
		if(this->mWrap && this->mWidth > 0)
		{
			this->_redrawWrapped();
			this->mDirty = false;
			return;
		}

		Real cursorX = 0, cursorY = 0, kerning = 0;
		Vec2 knownSize;
		bool clipLeft = false, clipRight = false;
		Real clipLeftPos = 0, clipRightPos = 0;

		if(this->mAlignment == TextAlign_Left)
		{
			cursorX = this->mLeft;
			if(this->mWidth)
			{
				clipRight = true;
				clipRightPos = this->mLeft + this->mWidth;
			}
		}
		else if(this->mAlignment == TextAlign_Centre)
		{
			this->_calculateDrawSize(knownSize);
			cursorX = this->mLeft + (this->mWidth * 0.5f)
				- (knownSize.x * 0.5f);
			if(this->mWidth)
			{
				clipLeft = true;
				clipLeftPos = this->mLeft;
				clipRight = true;
				clipRightPos = this->mLeft + this->mWidth;
			}
		}
		else if(this->mAlignment == TextAlign_Right)
		{
			this->_calculateDrawSize(knownSize);
			cursorX = this->mLeft + this->mWidth - knownSize.x;
			if(this->mWidth)
			{
				clipLeft = true;
				clipLeftPos = this->mLeft;
			}
		}

		if(this->mVerticalAlign == VerticalAlign_Top)
		{
			cursorY = this->mTop;
		}
		else if(this->mVerticalAlign == VerticalAlign_Middle)
		{
			cursorY = this->mTop + (this->mHeight * 0.5f)
				- (this->lineHeightPx() * 0.5f);
		}
		else if(this->mVerticalAlign == VerticalAlign_Bottom)
		{
			cursorY = this->mTop + this->mHeight - this->lineHeightPx();
		}

		cursorX = std::floor(cursorX);
		cursorY = std::floor(cursorY);
		uint thisChar = 0, lastChar = 0;

		for(size_t i = 0; i < this->mText.size(); ++i)
		{
			wchar_t unicodeChar;
			const std::size_t multiByteLength =
				StringUtil::multibyteCharStringToWideCharString(
					&unicodeChar, &this->mText[i], 5);
			thisChar = uint(unicodeChar);

			if(thisChar == ' ')
			{
				lastChar = thisChar;
				cursorX += this->spaceLengthPx();
				continue;
			}
			UiGlyph const * glyph = this->mFont->getGlyph(thisChar);
			if(glyph == NULL)
			{
				lastChar = 0;
				continue;
			}
			kerning = this->kerningPx(*glyph, lastChar);
			if(kerning == 0)
			{
				kerning = this->letterSpacingPx();
			}

			// glyphs render TOP-aligned at the cursor (the atlas bakes the
			// baseline into each glyph rect)
			const Real left = cursorX;
			const Real top = cursorY;
			const Real right = left + this->glyphWidthPx(*glyph);
			const Real bottom = top + this->glyphHeightPx(*glyph);

			if((clipLeft && left < clipLeftPos) ||
				(clipRight && right > clipRightPos))
			{
				cursorX += this->glyphAdvancePx(*glyph) + kerning;
				lastChar = thisChar;
				continue;
			}

			pushVertex(this->mVertices, left, bottom,
				glyph->texCoords[BottomLeft], this->mColour);
			pushVertex(this->mVertices, right, top,
				glyph->texCoords[TopRight], this->mColour);
			pushVertex(this->mVertices, left, top,
				glyph->texCoords[TopLeft], this->mColour);

			pushVertex(this->mVertices, left, bottom,
				glyph->texCoords[BottomLeft], this->mColour);
			pushVertex(this->mVertices, right, bottom,
				glyph->texCoords[BottomRight], this->mColour);
			pushVertex(this->mVertices, right, top,
				glyph->texCoords[TopRight], this->mColour);

			cursorX += this->glyphAdvancePx(*glyph) + kerning;
			lastChar = thisChar;
			if(multiByteLength > 0)
			{
				i += multiByteLength - 1;
			}
		}
		this->mDirty = false;
	}
	//---------------------------------------------------------
	void UiCaption::_redrawMarkup()
	{
		// caller guarantees mFont and non-empty text
		const bool doWrap = this->mWrap && this->mWidth > 0;
		// the block sits inside height() by the vertical alignment, which needs its
		// height first - one measure, then the placement at the resolved origin
		Vec2 block;
		this->_measureMarkup(doWrap ? this->mWidth : Real(0), block);
		const Real baseY = this->_blockTop(block.y);
		// a NON-wrapping caption clips to its box like the single-line path does
		// (a wrapping one has no overflow to clip)
		Real clipLeft = 0, clipRight = 0;
		if(!doWrap && this->mWidth > 0)
		{
			clipLeft = this->mAlignment == TextAlign_Left ? Real(0) : this->mLeft;
			clipRight = this->mLeft + this->mWidth;
		}

		std::vector<MarkupQuad> quads;
		Real lineHeight = 0, measuredWidth = 0, measuredHeight = 0;
		buildMarkupBlock(this->mLayer, this->mFont, this->mColour, this->mText,
			this->mTextScale, doWrap ? this->mWidth : Real(0), false,
			this->mLeft, baseY, this->mWidth, this->mAlignment,
			clipLeft, clipRight, &quads, lineHeight, measuredWidth,
			measuredHeight);
		for(MarkupQuad const & quad : quads)
		{
			pushQuad(this->mVertices, quad.left, quad.top, quad.right, quad.bottom,
				quad.uv[TopLeft].x, quad.uv[TopLeft].y,
				quad.uv[BottomRight].x, quad.uv[BottomRight].y, quad.colour);
		}
	}
	//---------------------------------------------------------
	Real UiCaption::_blockTop(Real blockHeight) const
	{
		if(this->mVerticalAlign == VerticalAlign_Middle)
		{
			return this->mTop + (this->mHeight - blockHeight) * 0.5f;
		}
		if(this->mVerticalAlign == VerticalAlign_Bottom)
		{
			return this->mTop + this->mHeight - blockHeight;
		}
		return this->mTop;
	}
	//---------------------------------------------------------
	void UiCaption::_redrawWrapped()
	{
		// caller guarantees mFont, non-empty text and mWidth > 0
		const Real lineHeight = this->lineHeightPx();
		std::vector<WrapCell> cells;
		std::vector<UiGlyph const *> glyphs;
		TextWrap::buildRun(*this->mFont, this->mText, cells, glyphs,
			float(this->mTextScale));
		WrapResult wrapped;
		TextWrap::wrap(cells, this->mWidth, wrapped);
		const int lineCount = wrapped.lineCount > 0 ? wrapped.lineCount : 1;
		const Real blockHeight = lineHeight * Real(lineCount);

		// the whole block sits inside height() by the vertical alignment
		const Real baseY = this->_blockTop(blockHeight);

		for(size_t each = 0; each < cells.size(); ++each)
		{
			UiGlyph const * glyph = glyphs[each];
			if(glyph == NULL)
			{
				continue;	// a space / newline emits nothing
			}
			const int line = wrapped.lineOf[each];
			// each line honours the horizontal alignment inside width()
			Real lineStart = this->mLeft;
			const Real lineWidth = wrapped.lineWidth[size_t(line)];
			if(this->mAlignment == TextAlign_Centre)
			{
				lineStart = this->mLeft + (this->mWidth - lineWidth) * 0.5f;
			}
			else if(this->mAlignment == TextAlign_Right)
			{
				lineStart = this->mLeft + this->mWidth - lineWidth;
			}
			const Real left = std::floor(lineStart + wrapped.penX[each]);
			const Real top = std::floor(baseY + Real(line) * lineHeight);
			const Real right = left + this->glyphWidthPx(*glyph);
			const Real bottom = top + this->glyphHeightPx(*glyph);

			pushVertex(this->mVertices, left, bottom,
				glyph->texCoords[BottomLeft], this->mColour);
			pushVertex(this->mVertices, right, top,
				glyph->texCoords[TopRight], this->mColour);
			pushVertex(this->mVertices, left, top,
				glyph->texCoords[TopLeft], this->mColour);

			pushVertex(this->mVertices, left, bottom,
				glyph->texCoords[BottomLeft], this->mColour);
			pushVertex(this->mVertices, right, bottom,
				glyph->texCoords[BottomRight], this->mColour);
			pushVertex(this->mVertices, right, top,
				glyph->texCoords[TopRight], this->mColour);
		}
	}
	//---------------------------------------------------------
	//--- UiMarkupText ----------------------------------------
	//---------------------------------------------------------
	UiMarkupText::UiMarkupText(uint defaultFontIndex, Real left, Real top,
		String const & text, UiLayer* parent)
		: mLayer(parent), mDefaultFont(parent->_getFont(defaultFontIndex)),
		mLeft(left), mTop(top), mWidth(0), mHeight(0), mText(text),
		mDirty(true), mTextDirty(true), mScaled(true), mWrap(false),
		mWrapWidth(0), mDefaultColour(parent->_getMarkupColour(0)),
		mTextScale(1.0f), mRenderAlpha(1.0f)
	{
		oAssertDesc(this->mDefaultFont,
			"UiMarkupText: font (glyph index) not in atlas");
		if(this->mDefaultFont == NULL)
		{
			this->mDirty = false;
			this->mTextDirty = false;
			return;
		}
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::left(Real left)
	{
		this->mLeft = left;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::top(Real top)
	{
		this->mTop = top;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::width(Real width)
	{
		this->mWidth = width;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::height(Real height)
	{
		this->mHeight = height;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::size(Real width, Real height)
	{
		this->mWidth = width;
		this->mHeight = height;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::text(String const & text)
	{
		this->mText = text;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::renderTransform(Ui2DTransform const & transform)
	{
		this->mRenderTransform = transform;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::renderAlpha(Real alphaMultiplier)
	{
		this->mRenderAlpha = alphaMultiplier;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::setWrap(bool wrap)
	{
		if(this->mWrap == wrap)
		{
			return;
		}
		this->mWrap = wrap;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::wrapWidth(Real width)
	{
		if(this->mWrapWidth == width)
		{
			return;
		}
		this->mWrapWidth = width;
		if(this->mWrap)
		{
			this->mDirty = true;
			this->mTextDirty = true;
			this->mLayer->_markDirty();
		}
	}
	//---------------------------------------------------------
	void UiMarkupText::setDefaultFont(uint fontIndex)
	{
		UiFont const * font = this->mLayer->_getFont(fontIndex);
		if(font == NULL || font == this->mDefaultFont)
		{
			return;		// an unloaded index keeps the current font
		}
		this->mDefaultFont = font;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::setDefaultColour(Color const & colour)
	{
		if(this->mDefaultColour == colour)
		{
			return;
		}
		this->mDefaultColour = colour;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiMarkupText::setTextScale(Real scale)
	{
		if(scale <= 0.0f || this->mTextScale == scale)
		{
			return;
		}
		this->mTextScale = scale;
		this->mDirty = true;
		this->mTextDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	Real UiMarkupText::measureWrappedHeight(Real width) const
	{
		if(this->mDefaultFont == NULL)
		{
			return 0;
		}
		// the MEASURE call: no quads, no Characters - just the broken line count
		// times the block's line height (@see buildMarkupBlock)
		Real lineHeight = 0, measuredWidth = 0, measuredHeight = 0;
		buildMarkupBlock(this->mLayer, this->mDefaultFont, this->mDefaultColour,
			this->mText, this->mTextScale, width, true, this->mLeft, this->mTop, 0,
			TextAlign_Left, 0, 0, NULL, lineHeight, measuredWidth, measuredHeight);
		return measuredHeight;
	}
	//---------------------------------------------------------
	void UiMarkupText::_calculateCharacters()
	{
		if(this->mTextDirty == false)
		{
			return;
		}
		oAssertDesc(this->mDefaultFont,
			"Font rendering can't find glyph data. Font size correctly specified?");
		if(this->mDefaultFont == NULL)
		{
			this->mTextDirty = false;
			return;
		}
		// wrap to the box width when armed; 0 = the historical single-line-per-
		// paragraph layout (byte-identical when wrap is off)
		const Real wrapTo = this->mWrap ? this->mWrapWidth : Real(0);
		UiMarkupText::_layoutMarkup(this->mLayer, this->mDefaultFont, this->mText,
			this->mLeft, this->mTop, wrapTo, this->mTextScale,
			this->mDefaultColour, this->mCharacters, this->mWidth, this->mHeight);
		this->mTextDirty = false;
	}
	//---------------------------------------------------------
	void UiMarkupText::_layoutMarkup(UiLayer* layer, UiFont const * defaultFont,
		String const & text, Real originX, Real originY, Real wrapWidth,
		Real textScale, Color const & defaultColour,
		std::vector<Character> & out, Real & width, Real & height, bool quiet)
	{
		out.clear();
		width = 0;
		height = 0;
		if(defaultFont == NULL)
		{
			return;
		}
		// the ONE styled-run pipeline both elements share (@see buildMarkupBlock).
		// wrapWidth <= 0 disables WIDTH wrapping, so only an explicit '\n' opens a
		// line (the one-line-per-paragraph layout).
		std::vector<MarkupQuad> quads;
		Real lineHeight = 0;
		buildMarkupBlock(layer, defaultFont, defaultColour, text, textScale,
			wrapWidth, quiet, originX, originY, 0, TextAlign_Left, 0, 0, &quads,
			lineHeight, width, height);
		out.reserve(quads.size());
		for(MarkupQuad const & quad : quads)
		{
			Character character;
			character.position[TopLeft] = Vec2(quad.left, quad.top);
			character.position[TopRight] = Vec2(quad.right, quad.top);
			character.position[BottomLeft] = Vec2(quad.left, quad.bottom);
			character.position[BottomRight] = Vec2(quad.right, quad.bottom);
			character.uv[0] = quad.uv[0];
			character.uv[1] = quad.uv[1];
			character.uv[2] = quad.uv[2];
			character.uv[3] = quad.uv[3];
			character.colour = quad.colour;
			out.push_back(character);
		}
	}
	//---------------------------------------------------------
	void UiMarkupText::_redraw()
	{
		if(this->mDirty == false)
		{
			return;
		}
		this->mVertices.clear();
		for(Character const & character : this->mCharacters)
		{
			// quad -> two triangles: [3],[1],[0] and [3],[2],[1]
			pushVertex(this->mVertices, character.position[3].x,
				character.position[3].y, character.uv[3], character.colour);
			pushVertex(this->mVertices, character.position[1].x,
				character.position[1].y, character.uv[1], character.colour);
			pushVertex(this->mVertices, character.position[0].x,
				character.position[0].y, character.uv[0], character.colour);

			pushVertex(this->mVertices, character.position[3].x,
				character.position[3].y, character.uv[3], character.colour);
			pushVertex(this->mVertices, character.position[2].x,
				character.position[2].y, character.uv[2], character.colour);
			pushVertex(this->mVertices, character.position[1].x,
				character.position[1].y, character.uv[1], character.colour);
		}
		this->mDirty = false;
	}
}
