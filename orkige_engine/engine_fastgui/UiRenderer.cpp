/********************************************************************
	created:	Wednesday 2026/07/08 at 23:30
	filename: 	UiRenderer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the fastgui 2D renderer (@see UiRenderer.h). The glyph
				layout math (kerning/letter spacing, top-aligned glyph
				quads, caption alignment clipping, markup codes) is
				ported from the retired Gorilla library (MIT, (c) 2010
				Robin Southern); the batching around it is new: one
				retained pixel-space triangle list per screen, one
				DrawLayer2D batch per screen, dirty-tracked rebuilds.
*********************************************************************/

#include "engine_fastgui/UiRenderer.h"
#include "engine_render/RenderSystem.h"
#include "core_util/StringUtil.h"
#include <core_debug/DebugMacros.h>

#include <OgreStringConverter.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		inline void pushVertex(std::vector<UiVertex> & vertices,
			Real x, Real y, Vec2 const & uv, Color const & colour)
		{
			vertices.push_back(UiVertex(x, y, uv.x, uv.y, colour));
		}
	}
	//---------------------------------------------------------
	//--- UiScreen --------------------------------------------
	//---------------------------------------------------------
	UiScreen::UiScreen(UiAtlas const * atlas,
		optr<DrawLayer2D> const & drawLayer)
		: mAtlas(atlas), mDrawLayer(drawLayer), mWidth(0), mHeight(0),
		mIsVisible(true), mDirty(false), mForceRedraw(false)
	{
		oAssert(this->mAtlas);
		oAssert(this->mDrawLayer);
		unsigned int windowWidth = 0, windowHeight = 0;
		RenderSystem::get()->getWindowSize(windowWidth, windowHeight);
		this->mWidth = Real(windowWidth);
		this->mHeight = Real(windowHeight);
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
		if(!this->mDirty && !force)
		{
			return;	// steady state: nothing rebuilt, nothing resubmitted
		}
		this->mForceRedraw = false;
		this->mDirty = false;

		// concatenate every visible layer (ascending index) into the
		// retained scratch buffer - capacity survives clear()
		this->mScratch.clear();
		for(UiLayer* layer : this->mLayers)
		{
			if(layer->mVisible)
			{
				layer->_render(this->mScratch, force);
			}
		}

		// ONE batch on the atlas texture = one draw call per screen
		this->mDrawLayer->clear();
		if(!this->mScratch.empty())
		{
			this->mDrawLayer->addTriangles(this->mAtlas->getTextureName(),
				this->mScratch.data(), this->mScratch.size());
		}
	}
	//---------------------------------------------------------
	//--- UiLayer ---------------------------------------------
	//---------------------------------------------------------
	UiLayer::UiLayer(uint index, UiScreen* parent)
		: mIndex(index), mParent(parent), mVisible(true), mAlphaModifier(1.0f)
	{
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
				rect->_redraw();
			}
			out.insert(out.end(), rect->mVertices.begin(),
				rect->mVertices.end());
		}
		for(UiCaption* caption : this->mCaptions)
		{
			if(caption->mDirty || force)
			{
				caption->_redraw();
			}
			out.insert(out.end(), caption->mVertices.begin(),
				caption->mVertices.end());
		}
		for(UiMarkupText* markupText : this->mMarkupTexts)
		{
			if(markupText->mTextDirty || force)
			{
				markupText->_calculateCharacters();
			}
			if(markupText->mDirty || force)
			{
				markupText->_redraw();
			}
			out.insert(out.end(), markupText->mVertices.begin(),
				markupText->mVertices.end());
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
		mBottom(top + height), mColour(1, 1, 1, 1), mDirty(true)
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
		if(this->mColour.a != 0)
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
	//--- UiCaption -------------------------------------------
	//---------------------------------------------------------
	UiCaption::UiCaption(uint fontIndex, Real left, Real top,
		String const & caption, UiLayer* parent)
		: mLayer(parent), mFont(parent->_getFont(fontIndex)),
		mLeft(left), mTop(top), mWidth(0), mHeight(0),
		mAlignment(TextAlign_Left), mVerticalAlign(VerticalAlign_Top),
		mText(caption), mColour(1, 1, 1, 1), mDirty(true), mScaled(true)
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
			"FastGui: text label positioned on subpixel. expect graphical artefacts.");
		this->mLeft = left;
		this->mDirty = true;
		this->mLayer->_markDirty();
	}
	//---------------------------------------------------------
	void UiCaption::top(Real top)
	{
		oAssertDesc(std::floor(top) == std::ceil(top),
			"FastGui: text label positioned on subpixel. expect graphical artefacts.");
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
	void UiCaption::_calculateDrawSize(Vec2 & size)
	{
		oAssertDesc(this->mFont != NULL,
			"Font rendering can't find glyph data. Font size correctly specified?");
		Real cursor = 0, kerning = 0;
		uint thisChar = 0, lastChar = 0;
		size.x = 0;
		size.y = this->mFont->getLineHeightScaled();

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
				cursor += this->mFont->getSpaceLengthScaled();
				continue;
			}
			UiGlyph const * glyph = this->mFont->getGlyph(thisChar);
			if(glyph == NULL)
			{
				lastChar = 0;
				continue;
			}
			kerning = glyph->getKerningScaled(lastChar);
			if(kerning == 0)
			{
				kerning = this->mFont->getLetterSpacingScaled();
			}
			cursor += glyph->getGlyphAdvanceScaled() + kerning;
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
				- (this->mFont->getLineHeightScaled() * 0.5f);
		}
		else if(this->mVerticalAlign == VerticalAlign_Bottom)
		{
			cursorY = this->mTop + this->mHeight
				- this->mFont->getLineHeightScaled();
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
				cursorX += this->mFont->getSpaceLengthScaled();
				continue;
			}
			UiGlyph const * glyph = this->mFont->getGlyph(thisChar);
			if(glyph == NULL)
			{
				lastChar = 0;
				continue;
			}
			kerning = glyph->getKerningScaled(lastChar);
			if(kerning == 0)
			{
				kerning = this->mFont->getLetterSpacingScaled();
			}

			// glyphs render TOP-aligned at the cursor (the atlas bakes the
			// baseline into each glyph rect)
			const Real left = cursorX;
			const Real top = cursorY;
			const Real right = left + glyph->getGlyphWidthScaled();
			const Real bottom = top + glyph->getGlyphHeightScaled();

			if((clipLeft && left < clipLeftPos) ||
				(clipRight && right > clipRightPos))
			{
				cursorX += glyph->getGlyphAdvanceScaled() + kerning;
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

			cursorX += glyph->getGlyphAdvanceScaled() + kerning;
			lastChar = thisChar;
			if(multiByteLength > 0)
			{
				i += multiByteLength - 1;
			}
		}
		this->mDirty = false;
	}
	//---------------------------------------------------------
	//--- UiMarkupText ----------------------------------------
	//---------------------------------------------------------
	UiMarkupText::UiMarkupText(uint defaultFontIndex, Real left, Real top,
		String const & text, UiLayer* parent)
		: mLayer(parent), mDefaultFont(parent->_getFont(defaultFontIndex)),
		mLeft(left), mTop(top), mWidth(0), mHeight(0), mText(text),
		mDirty(true), mTextDirty(true), mScaled(true)
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
	void UiMarkupText::_calculateCharacters()
	{
		if(this->mTextDirty == false)
		{
			return;
		}
		oAssertDesc(this->mDefaultFont,
			"Font rendering can't find glyph data. Font size correctly specified?");

		Real cursorX = this->mLeft, cursorY = this->mTop, kerning = 0;
		uint thisChar = 0, lastChar = 0;
		this->mWidth = 0;
		this->mHeight = 0;
		this->mCharacters.clear();

		bool markupMode = false;
		bool fixedWidth = false;
		Color colour = this->mLayer->_getMarkupColour(0);
		UiFont const * font = this->mDefaultFont;
		Real lineHeight = font->getLineHeightScaled();

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
				cursorX += font->getSpaceLengthScaled();
				continue;
			}
			if(thisChar == '\n')
			{
				lastChar = thisChar;
				cursorX = this->mLeft;
				cursorY += lineHeight;
				lineHeight = font->getLineHeightScaled();
				continue;
			}
			if(thisChar < font->getRangeBegin() ||
				thisChar > font->getRangeEnd())
			{
				lastChar = 0;
				continue;
			}
			if(thisChar == '%' && markupMode == false)
			{
				markupMode = true;
				continue;
			}
			if(markupMode == true)
			{
				if(thisChar == '%')
				{
					// escape character - falls through and draws '%'
				}
				else
				{
					markupMode = false;
					if(thisChar >= '0' && thisChar <= '9')
					{
						colour = this->mLayer->_getMarkupColour(
							uint(thisChar - '0'));
					}
					else if(thisChar == 'R' || thisChar == 'r')
					{
						colour = this->mLayer->_getMarkupColour(0);
					}
					else if(thisChar == 'M' || thisChar == 'm')
					{
						fixedWidth = !fixedWidth;
					}
					else if(thisChar == '@')
					{
						// %@<fontIndex>% switches the font
						bool foundIt = false;
						const size_t begin = i;
						while(i < this->mText.size())
						{
							if(this->mText[i] == '%')
							{
								foundIt = true;
								break;
							}
							++i;
						}
						if(foundIt == false)
						{
							return;
						}
						const uint index =
							Ogre::StringConverter::parseUnsignedInt(
								this->mText.substr(begin + 1, i - begin - 1));
						font = this->mLayer->_getFont(index);
						if(font == NULL)
						{
							return;
						}
						lineHeight = std::max(lineHeight,
							font->getLineHeightScaled());
						continue;
					}
					else if(thisChar == ':')
					{
						// %:<spriteName>% inserts a sprite inline
						bool foundIt = false;
						const size_t begin = i;
						while(i < this->mText.size())
						{
							if(this->mText[i] == '%')
							{
								foundIt = true;
								break;
							}
							++i;
						}
						if(foundIt == false)
						{
							return;
						}
						const String spriteName =
							this->mText.substr(begin + 1, i - begin - 1);
						UiSprite const * sprite =
							this->mLayer->_getSprite(spriteName);
						if(sprite == NULL)
						{
							continue;
						}
						const Real left = cursorX;
						const Real top = cursorY;
						const Real right = left + sprite->spriteWidth;
						const Real bottom = top + sprite->spriteHeight;

						Character character;
						character.position[TopLeft] = Vec2(left, top);
						character.position[TopRight] = Vec2(right, top);
						character.position[BottomLeft] = Vec2(left, bottom);
						character.position[BottomRight] = Vec2(right, bottom);
						character.uv[0] = sprite->texCoords[0];
						character.uv[1] = sprite->texCoords[1];
						character.uv[2] = sprite->texCoords[2];
						character.uv[3] = sprite->texCoords[3];
						character.colour = colour;
						this->mCharacters.push_back(character);

						cursorX += sprite->spriteWidth;
						lineHeight = std::max(lineHeight,
							sprite->spriteHeight);
						continue;
					}
					continue;
				}
				markupMode = false;
			}

			UiGlyph const * glyph = font->getGlyph(thisChar);
			if(glyph == NULL)
			{
				continue;
			}
			if(!fixedWidth)
			{
				kerning = glyph->getKerningScaled(lastChar);
				if(kerning == 0)
				{
					kerning = font->getLetterSpacingScaled();
				}
			}
			// whole-pixel cursor: crisp glyphs (point-sampled atlas)
			cursorX = std::floor(cursorX);
			cursorY = std::floor(cursorY);

			const Real right = cursorX + glyph->getGlyphWidthScaled();
			const Real bottom = cursorY + glyph->getGlyphHeightScaled();

			Character character;
			character.position[TopLeft] = Vec2(cursorX, cursorY);
			character.position[TopRight] = Vec2(right, cursorY);
			character.position[BottomLeft] = Vec2(cursorX, bottom);
			character.position[BottomRight] = Vec2(right, bottom);
			character.uv[0] = glyph->texCoords[0];
			character.uv[1] = glyph->texCoords[1];
			character.uv[2] = glyph->texCoords[2];
			character.uv[3] = glyph->texCoords[3];
			character.colour = colour;
			this->mCharacters.push_back(character);

			if(fixedWidth)
			{
				cursorX += font->getMonoWidthScaled();
			}
			else
			{
				cursorX += glyph->getGlyphAdvanceScaled() + kerning;
			}
			lastChar = thisChar;

			// track the laid-out extents
			if(this->mWidth < cursorX)
			{
				this->mWidth = cursorX;
			}
			if(this->mHeight < cursorY)
			{
				this->mHeight = cursorY;
			}
			if(multiByteLength > 0)
			{
				i += multiByteLength - 1;
			}
		}
		this->mHeight -= this->mTop;
		this->mHeight += lineHeight;
		this->mWidth -= this->mLeft;
		this->mTextDirty = false;
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
