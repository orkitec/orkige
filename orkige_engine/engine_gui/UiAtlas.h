/********************************************************************
	created:	Wednesday 2026/07/08 at 23:30
	filename: 	UiAtlas.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __UiAtlas_h__8_7_2026__23_30_00__
#define __UiAtlas_h__8_7_2026__23_30_00__

//! @file UiAtlas.h
//! @brief the .ogui texture-atlas model of the gui system: sprites,
//! fonts (glyph metrics + kerning), the whitepixel and markup colours
//! @remarks Backend-neutral by construction: parsing goes through
//! Ogre::ConfigFile (present identically in classic OGRE and Ogre-Next,
//! iterated through the engine_util/ConfigFileUtil.h seam) and the only
//! renderer contact is RenderSystem::getTextureSize. The .ogui format
//! and the glyph/kerning semantics are inherited from the retired
//! Gorilla library (MIT, (c) 2010 Robin Southern) - the format contract
//! (Util/make_gui_atlas.py generates it, GuiAtlasTests verifies
//! it) is unchanged: [Texture] file+whitepixel, [Font.N] offset/
//! lineheight/baseline/spacelength/monowidth/letterspacing/range/
//! glyph_<code> "x y w h [advance]"/kerning_<code>, [Sprites]
//! name "x y w h".

#include "engine_module/EnginePrerequisites.h"

#include <map>
#include <unordered_map>
#include <vector>

namespace Ogre
{
	class ConfigFile;
}

namespace Orkige
{
	//! the classic web colour vocabulary of the gui widgets
	namespace Colours
	{
		enum Colour
		{
			None = 0, // No Colour.
			AliceBlue=0xf0f8ff,      Gainsboro=0xdcdcdc,            MistyRose=0xffe4e1,
			AntiqueWhite=0xfaebd7,   GhostWhite=0xf8f8ff,           Moccasin=0xffe4b5,
			Aqua=0x00ffff,           Gold=0xffd700,                 NavajoWhite=0xffdead,
			Aquamarine=0x7fffd4,     Goldenrod=0xdaa520,            Navy=0x000080,
			Azure=0xf0ffff,          Gray=0x808080,                 OldLace=0xfdf5e6,
			Beige=0xf5f5dc,          Green=0x008000,                Olive=0x808000,
			Bisque=0xffe4c4,         GreenYellow=0xadff2f,          OliveDrab=0x6b8e23,
			Black=0x000000,          Grey=0x808080,                 Orange=0xffa500,
			BlanchedAlmond=0xffebcd, Honeydew=0xf0fff0,             OrangeRed=0xff4500,
			Blue=0x0000ff,           HotPink=0xff69b4,              Orchid=0xda70d6,
			BlueViolet=0x8a2be2,     IndianRed=0xcd5c5c,            PaleGoldenrod=0xeee8aa,
			Brown=0xa52a2a,          Indigo=0x4b0082,               PaleGreen=0x98fb98,
			Burlywood=0xdeb887,      Ivory=0xfffff0,                PaleTurquoise=0xafeeee,
			CadetBlue=0x5f9ea0,      Khaki=0xf0e68c,                PaleVioletRed=0xdb7093,
			Chartreuse=0x7fff00,     Lavender=0xe6e6fa,             PapayaWhip=0xffefd5,
			Chocolate=0xd2691e,      LavenderBlush=0xfff0f5,        PeachPuff=0xffdab9,
			Coral=0xff7f50,          LawnGreen=0x7cfc00,            Peru=0xcd853f,
			CornflowerBlue=0x6495ed, LemonChiffon=0xfffacd,         Pink=0xffc0cb,
			Cornsilk=0xfff8dc,       LightBlue=0xadd8e6,            Plum=0xdda0dd,
			Crimson=0xdc143c,        LightCoral=0xf08080,           PowderBlue=0xb0e0e6,
			Cyan=0x00ffff,           LightCyan=0xe0ffff,            Purple=0x800080,
			DarkBlue=0x00008b,       LightGoldenrodyellow=0xfafad2, Red=0xff0000,
			DarkCyan=0x008b8b,       LightGray=0xd3d3d3,            RosyBrown=0xbc8f8f,
			DarkGoldenrod=0xb8860b,  LightGreen=0x90ee90,           RoyalBlue=0x4169e1,
			DarkGray=0xa9a9a9,       LightGrey=0xd3d3d3,            SaddleBrown=0x8b4513,
			DarkGreen=0x006400,      LightPink=0xffb6c1,            Salmon=0xfa8072,
			DarkGrey=0xa9a9a9,       LightSalmon=0xffa07a,          SandyBrown=0xf4a460,
			DarkKhaki=0xbdb76b,      LightSeagreen=0x20b2aa,        SeaGreen=0x2e8b57,
			DarkMagenta=0x8b008b,    LightSkyblue=0x87cefa,         SeaShell=0xfff5ee,
			DarkOlivegreen=0x556b2f, LightSlategray=0x778899,       Sienna=0xa0522d,
			DarkOrange=0xff8c00,     LightSlategrey=0x778899,       Silver=0xc0c0c0,
			DarkOrchid=0x9932cc,     LightSteelblue=0xb0c4de,       SkyBlue=0x87ceeb,
			DarkRed=0x8b0000,        LightYellow=0xffffe0,          SlateBlue=0x6a5acd,
			DarkSalmon=0xe9967a,     Lime=0x00ff00,                 SlateGray=0x708090,
			DarkSeagreen=0x8fbc8f,   LimeGreen=0x32cd32,            SlateGrey=0x708090,
			DarkSlateblue=0x483d8b,  Linen=0xfaf0e6,                Snow=0xfffafa,
			DarkSlategray=0x2f4f4f,  Magenta=0xff00ff,              SpringGreen=0x00ff7f,
			DarkSlategrey=0x2f4f4f,  Maroon=0x800000,               SteelBlue=0x4682b4,
			DarkTurquoise=0x00ced1,  MediumAquamarine=0x66cdaa,     Tan=0xd2b48c,
			DarkViolet=0x9400d3,     MediumBlue=0x0000cd,           Teal=0x008080,
			DeepPink=0xff1493,       MediumOrchid=0xba55d3,         Thistle=0xd8bfd8,
			DeepSkyblue=0x00bfff,    MediumPurple=0x9370db,         Tomato=0xff6347,
			DimGray=0x696969,        MediumSeaGreen=0x3cb371,       Turquoise=0x40e0d0,
			DimGrey=0x696969,        MediumSlateBlue=0x7b68ee,      Violet=0xee82ee,
			DodgerBlue=0x1e90ff,     MediumSpringGreen=0x00fa9a,    Wheat=0xf5deb3,
			FireBrick=0xb22222,      MediumTurquoise=0x48d1cc,      White=0xffffff,
			FloralWhite=0xfffaf0,    MediumBioletRed=0xc71585,      WhiteSmoke=0xf5f5f5,
			ForestGreen=0x228b22,    MidnightBlue=0x191970,         Yellow=0xffff00,
			Fuchsia=0xff00ff,        MintCream=0xf5fffa,            YellowGreen=0x9acd32
		}; // Colour
	} // namespace Colours

	//! convert three/four 0..255 RGBA values into a Color
	ORKIGE_ENGINE_DLL Color rgb(unsigned char r, unsigned char g,
		unsigned char b, unsigned char a = 255);

	//! turn a Colours::Colour web colour into a Color
	ORKIGE_ENGINE_DLL Color webcolour(Colours::Colour colour, Real alpha = 1.0f);

	//! the historical spellings Colours::rgb / Colours::webcolour
	namespace Colours
	{
		using Orkige::rgb;
		using Orkige::webcolour;
	}

	//! names of each corner/vertex of a quad
	enum QuadCorner
	{
		TopLeft     = 0,
		TopRight    = 1,
		BottomRight = 2,
		BottomLeft  = 3
	};

	//! horizontal text alignment for captions
	enum TextAlignment
	{
		TextAlign_Left,   // Place the text to where left is (X = left)
		TextAlign_Right,  // Place the text to the right of left (X = left - text_width)
		TextAlign_Centre, // Place the text centered at left (X = left - (text_width / 2 ) )
	};

	//! vertical text alignment for captions
	enum VerticalAlignment
	{
		VerticalAlign_Top,
		VerticalAlign_Middle,
		VerticalAlign_Bottom
	};

	//! kerning entry: extra advance when THIS glyph follows `character`
	struct ORKIGE_ENGINE_DLL UiKerning
	{
		UiKerning(uint _character, Real _kerning)
			: character(_character), kerning(_kerning) {}
		uint	character;
		Real	kerning;
	};

	//! texture and size information about a single font character
	class ORKIGE_ENGINE_DLL UiGlyph
	{
	public:
		UiGlyph() : uvTop(0), uvBottom(0), uvWidth(0), uvHeight(0),
			uvLeft(0), uvRight(0), glyphWidth(0), glyphHeight(0),
			glyphAdvance(0) {}

		Vec2	texCoords[4];	//!< normalized quad UVs (@see QuadCorner)
		Real	uvTop, uvBottom, uvWidth, uvHeight, uvLeft, uvRight;
		std::vector<UiKerning> kerning;

		//! the historical global glyph scale hook (games scale fonts by
		//! resolution through it; every *Scaled metric multiplies with it)
		static Vec2 scale;

		//! kerning of this glyph when drawn right of `left_of` (0 = none)
		inline Real getKerningScaled(uint left_of) const
		{
			for(size_t each = 0; each < this->kerning.size(); ++each)
			{
				if(this->kerning[each].character == left_of)
				{
					return this->kerning[each].kerning * UiGlyph::scale.x;
				}
			}
			return 0;
		}

		void setGlyphWidth(Real width)		{ this->glyphWidth = width; }
		void setGlyphHeight(Real height)	{ this->glyphHeight = height; }
		void setGlyphAdvance(Real advance)	{ this->glyphAdvance = advance; }

		Real getGlyphWidthScaled() const	{ return UiGlyph::scale.x * this->glyphWidth; }
		Real getGlyphHeightScaled() const	{ return UiGlyph::scale.y * this->glyphHeight; }
		Real getGlyphAdvanceScaled() const	{ return UiGlyph::scale.x * this->glyphAdvance; }
	protected:
		Real	glyphWidth, glyphHeight, glyphAdvance;
	};

	//! named portion of the atlas texture
	class ORKIGE_ENGINE_DLL UiSprite
	{
	public:
		UiSprite() : uvTop(0), uvLeft(0), uvRight(0), uvBottom(0),
			spriteWidth(0), spriteHeight(0),
			sliceLeft(0), sliceRight(0), sliceTop(0), sliceBottom(0) {}

		Real	uvTop, uvLeft, uvRight, uvBottom;	//!< normalized after load
		Real	spriteWidth, spriteHeight;			//!< pixel size
		Vec2	texCoords[4];						//!< @see QuadCorner
		//! @brief nine-slice border insets in SPRITE pixels (all 0 = a plain
		//! stretched quad). A UiRect drawn nine-sliced keeps these corner bands
		//! at a fixed size while the edges/centre stretch, so a panel or button
		//! resizes without distorting its rounded corners (@see UiRect draw mode).
		Real	sliceLeft, sliceRight, sliceTop, sliceBottom;
		//! any non-zero inset makes this a valid nine-slice source
		inline bool hasSlices() const
		{
			return this->sliceLeft > 0 || this->sliceRight > 0 ||
				this->sliceTop > 0 || this->sliceBottom > 0;
		}
	};

	class UiFont;

	//! @brief the lazy glyph baker a runtime (TTF) font consults for a
	//! codepoint outside its eager base range: bakes the glyph into free
	//! atlas space, records it in the font's sparse page and returns it.
	//! @remarks FontAtlas is the only implementer; bitmap fonts have no
	//! provider (getGlyph answers NULL outside the range as before). Kept
	//! abstract here so UiFont/UiAtlas stay free of the baker's headers.
	class ORKIGE_ENGINE_DLL UiGlyphProvider
	{
	public:
		virtual ~UiGlyphProvider() {}
		//! bake `codepoint` for `font`, store it in the font's sparse page
		//! and return it (NULL when the face has no such glyph)
		virtual UiGlyph const * provideGlyph(UiFont const & font,
			uint codepoint) = 0;
	};

	//! one [Font.N] section: all glyphs of one size + the line metrics
	class ORKIGE_ENGINE_DLL UiFont
	{
		friend class UiAtlas;
		friend class FontAtlas;			//!< the runtime TTF baker
		friend struct UiAtlasLoader;	//!< the cpp-local section loaders
	public:
		UiFont() : mRangeBegin(0), mRangeEnd(0), mSpaceLength(0),
			mLineHeight(0), mBaseline(0), mLetterSpacing(0), mMonoWidth(0) {}

		//! glyph of a character (do not free). NULL outside the range for a
		//! bitmap font; a runtime TTF font bakes the glyph on first use
		inline UiGlyph const * getGlyph(uint character) const
		{
			const uint safeCharacter = character - this->mRangeBegin;
			if(safeCharacter < this->mGlyphs.size())
			{
				return &this->mGlyphs[safeCharacter];
			}
			// baked-on-demand page (runtime TTF fonts): a codepoint outside
			// the eager base range lives in the sparse map, baked into free
			// atlas space on first use. This is the CJK/Cyrillic unblocker;
			// bitmap fonts have no provider and answer NULL as before.
			if(this->mProvider != NULL)
			{
				std::unordered_map<uint, UiGlyph>::const_iterator it =
					this->mSparseGlyphs.find(character);
				if(it != this->mSparseGlyphs.end())
				{
					return &it->second;
				}
				return this->mProvider->provideGlyph(*this, character);
			}
			return NULL;
		}

		inline uint getRangeBegin() const	{ return this->mRangeBegin; }
		inline uint getRangeEnd() const		{ return this->mRangeEnd; }

		Real getSpaceLengthScaled() const	{ return UiGlyph::scale.x * this->mSpaceLength; }
		Real getLineHeightScaled() const	{ return UiGlyph::scale.y * this->mLineHeight; }
		Real getBaselineScaled() const		{ return UiGlyph::scale.y * this->mBaseline; }
		Real getLetterSpacingScaled() const	{ return UiGlyph::scale.x * this->mLetterSpacing; }
		Real getMonoWidthScaled() const		{ return UiGlyph::scale.x * this->mMonoWidth; }
	protected:
		std::vector<UiGlyph>	mGlyphs;	//!< contiguous, index = code - rangeBegin
		uint	mRangeBegin, mRangeEnd;
		Real	mSpaceLength, mLineHeight, mBaseline, mLetterSpacing, mMonoWidth;
		//! baked-on-demand glyphs outside the eager base range (runtime TTF
		//! fonts only). mutable: getGlyph is const but bakes lazily
		mutable std::unordered_map<uint, UiGlyph>	mSparseGlyphs;
		//! the lazy baker (runtime TTF font), or NULL for a bitmap font
		UiGlyphProvider*		mProvider = NULL;
	};

	//! @brief a parsed .ogui atlas: ONE texture whose regions are sprites,
	//! font glyphs and the whitepixel every solid fill samples
	//! @remarks value-owned data throughout (glyphs live contiguously per
	//! font, sprites in one map) - no per-glyph heap objects. The screens
	//! bind mTextureName per DrawLayer2D batch; the atlas owns no
	//! materials or GPU objects on any backend.
	class ORKIGE_ENGINE_DLL UiAtlas
	{
		friend class FontAtlas;	//!< the runtime TTF/SVG baker builds one directly
	public:
		//! load "<name>.ogui" through the resource system; the texture
		//! size (all metrics normalize against it) comes from
		//! RenderSystem::getTextureSize
		UiAtlas(String const & oguiFile, String const & group = "General");
		//! @brief parser-level constructor: consume an already-loaded
		//! config against a KNOWN texture size
		//! @remarks the headless door - unit tests (GuiAtlasTests)
		//! verify the real parser without a render system; the resource
		//! constructor funnels into the same code path
		UiAtlas(Ogre::ConfigFile & configFile,
			Real textureWidth, Real textureHeight);
		~UiAtlas();

		//! resource name of the atlas texture (draw batches bind by name)
		inline String const & getTextureName() const { return this->mTextureName; }
		inline Vec2 getTextureSize() const { return this->mTextureSize; }
		//! normalized UV of the designated white pixel (solid fills)
		inline Vec2 getWhitePixel() const { return this->mWhitePixel; }

		//! font of a [Font.N] section index, NULL when absent (do not free)
		inline UiFont const * getFont(uint index) const
		{
			std::map<uint, UiFont>::const_iterator it = this->mFonts.find(index);
			if(it == this->mFonts.end())
			{
				return NULL;
			}
			return &it->second;
		}

		//! sprite by name, NULL when absent (do not free)
		inline UiSprite const * getSprite(String const & name) const
		{
			std::map<String, UiSprite>::const_iterator it =
				this->mSprites.find(name);
			if(it == this->mSprites.end())
			{
				return NULL;
			}
			return &it->second;
		}

		//! reset the ten markup colours to the defaults
		void refreshMarkupColours();
		//! change one of the ten markup colours (index 0..9)
		void setMarkupColour(uint index, Color const & colour);
		//! one of the ten markup colours (white outside 0..9)
		Color getMarkupColour(uint index) const;

		//! @brief exchange the atlas texture with another one of the SAME
		//! size (debug tool); the owning screens must resubmit afterwards
		//! (GuiManager::replaceAtlasTexture forces that)
		void replaceTexture(String const & texture);
	protected:
		//! @brief empty atlas over a KNOWN, named texture - the runtime baker
		//! (FontAtlas) builds one directly, filling fonts/sprites/whitepixel
		//! itself (its glyphs carry pre-normalized UVs, so _calculateCoordinates
		//! is not run over them). Friend-only.
		UiAtlas(String const & textureName, Vec2 const & textureSize);
		//! parse every section (the cpp-local section loaders fill the
		//! members) and normalize all coordinates
		void _load(Ogre::ConfigFile & configFile,
			Real textureWidth, Real textureHeight);
		void _calculateCoordinates();
		friend struct UiAtlasLoader;	//!< the cpp-local section loaders

		String					mTextureName;
		Vec2					mTextureSize;
		Vec2					mInverseTextureSize;
		Vec2					mWhitePixel;
		std::map<uint, UiFont>	mFonts;
		std::map<String, UiSprite>	mSprites;
		Color					mMarkupColour[10];
	};
}

#endif //__UiAtlas_h__8_7_2026__23_30_00__
