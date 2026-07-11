/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	FontAtlas.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __FontAtlas_h__11_7_2026__03_30_00__
#define __FontAtlas_h__11_7_2026__03_30_00__

//! @file FontAtlas.h
//! @brief the runtime-baked gui atlas: bakes TrueType glyph pages and
//! rasterised vector (SVG) sprites into ONE GPU texture at boot, at the
//! display's device resolution, and exposes them through the UNCHANGED
//! UiAtlas / UiFont / UiGlyph vocabulary so every widget renders verbatim.
//! @remarks Where UiAtlas is a load-once immutable parse of a fixed .png,
//! FontAtlas is mutable: it owns a shelf-packed A8/RGBA page and a UiAtlas
//! VIEW the renderer consumes. Glyph metrics are stored in DESIGN pixels
//! (UiGlyph::scale multiplies them to device px at draw time, 1:1 with the
//! baked texels -> crisp, point-filtered, no facade change). Lazy paging:
//! ASCII + Latin-1 bake eagerly at boot; any codepoint beyond that bakes on
//! demand into free page space (UiGlyphProvider), which is the CJK/Cyrillic
//! localisation unblocker.
//!
//! The .ogui format is extended in place (one format, one loader): a
//! [Font.N] section carrying `ttf <asset>` + `size <designPx>` (+ optional
//! `range first last`) is a runtime font instead of glyph_* rects; a
//! [Sprites] entry `name svg <asset> <designWidth>` is a vector sprite. A
//! classic bitmap .ogui (glyph_* rects + a .png [Texture] file) has neither
//! and stays on the UiAtlas path untouched.

#include "engine_gui/UiAtlas.h"
#include "engine_gui/FontPacker.h"

#include <unordered_map>

namespace Ogre
{
	class ConfigFile;
}

namespace Orkige
{
	namespace FontBake { struct Face; }

	//! @brief the runtime TTF/SVG atlas baker (see the file comment)
	class ORKIGE_ENGINE_DLL FontAtlas : public UiGlyphProvider
	{
	public:
		//! @brief build from a .ogui declaring runtime (ttf/svg) content; the
		//! ttf/svg blobs are read through the resource system (group), baked
		//! at the display's integer content scale (UiGlyph::scale) and, when a
		//! render system is up, uploaded to the GPU. Falls back to a 1x bake
		//! headlessly (no render system) so unit tests build the view.
		FontAtlas(String const & oguiFile,
			String const & group = "General");
		//! @brief headless/programmatic page of an explicit size + bake scale
		//! (the unit tests feed faces/sprites directly, without a .ogui or a
		//! render system). @param textureName the name draw batches bind by
		FontAtlas(String const & textureName, uint texturePx, float bakeScale);
		virtual ~FontAtlas();

		//! @brief register a TrueType face at a design pixel height as font
		//! `fontIndex` and eagerly bake [rangeFirst, rangeLast]. The bytes are
		//! copied. @returns false when the blob will not parse.
		bool addFace(uint fontIndex, unsigned char const * ttf, int ttfSize,
			float designPixelHeight,
			uint rangeFirst = 32, uint rangeLast = 255);

		//! @brief rasterise an SVG blob into a named sprite whose natural
		//! width maps to `designWidth` design px (device px = x bake scale).
		//! @returns false when the blob will not parse.
		bool addSvgSprite(String const & name,
			unsigned char const * svg, int svgSize, float designWidth);

		//! the UiAtlas view the renderer/UiScreen consumes (owned here)
		inline UiAtlas const * atlas() const { return &this->mAtlas; }
		//! did anything bake (at least one font or sprite)?
		inline bool isValid() const { return this->mValid; }
		//! resource name the baked page is uploaded under
		inline String const & getTextureName() const { return this->mTextureName; }

		//! @brief upload the baked page to the GPU if it changed since the last
		//! upload (needs a render system; a no-op headless). Called at boot and
		//! once per frame by GuiManager so lazily-baked glyphs reach the GPU.
		void flush();

		//! UiGlyphProvider: bake a codepoint on demand into the sparse page
		virtual UiGlyph const * provideGlyph(UiFont const & font,
			uint codepoint) override;

		//! @brief does this .ogui declare runtime (ttf/svg) content, i.e. must
		//! it be built by FontAtlas rather than the plain UiAtlas parser?
		static bool oguiDeclaresRuntimeContent(String const & oguiFile,
			String const & group = "General");
	private:
		//! per-font baking state (design + device scales, uniform cell height)
		struct FaceInfo
		{
			FontBake::Face*	handle = NULL;
			float	scaleDesign = 0.0f;		//!< stb scale at design px
			float	scaleDevice = 0.0f;		//!< stb scale at design px x bake
			float	ascentDevice = 0.0f;	//!< cell-top to baseline, device px
			int		cellHeightDevice = 0;	//!< uniform glyph cell height, device
		};

		//! shared setup for both constructors: empty page + reserved whitepixel
		void _init(String const & textureName, uint texturePx, float bakeScale);
		//! bake one codepoint into `out` (design-px metrics + normalized UVs);
		//! false when the page is full. An ink-less codepoint (space, missing)
		//! consumes no page space and yields a zero-size advancing glyph.
		bool _bakeGlyph(FaceInfo & face, uint codepoint, UiGlyph & out);
		//! coverage (A8) -> white RGBA with coverage alpha, into the page;
		//! clipped to [destX,clipRight) x [destY,clipBottom) so a glyph whose
		//! ink overruns its cell never bleeds into a neighbour
		void _blitCoverage(unsigned char const * coverage, int cw, int ch,
			int destX, int destY, int clipRight, int clipBottom);
		//! straight RGBA copy into the page
		void _blitRgba(unsigned char const * rgba, int cw, int ch,
			int destX, int destY);
		//! fill a UiGlyph's normalized UV rect from a device-pixel box
		void _setGlyphUv(UiGlyph & glyph, uint x, uint y, uint w, uint h) const;

		UiAtlas			mAtlas;			//!< the view (built via the friend ctor)
		FontPacker		mPacker;
		std::vector<unsigned char>	mPixels;	//!< texturePx^2 RGBA page
		uint			mTexturePx;
		float			mBakeScale;		//!< integer device scale glyphs bake at
		String			mTextureName;
		bool			mTextureDirty;	//!< page changed since the last upload
		bool			mUploaded;		//!< a GPU texture exists under the name
		bool			mValid;			//!< at least one font/sprite baked
		bool			mReportedFull;	//!< logged the page-full drop once
		//! font index -> its baking state, and the reverse (UiFont* -> index)
		//! for the const provideGlyph path (UiFont addresses are stable in the
		//! atlas' std::map)
		std::map<uint, FaceInfo>	mFaces;
		std::unordered_map<UiFont const *, uint>	mFontToIndex;

		FontAtlas(FontAtlas const &);				// non-copyable
		FontAtlas & operator=(FontAtlas const &);	// non-copyable
	};
}

#endif //__FontAtlas_h__11_7_2026__03_30_00__
