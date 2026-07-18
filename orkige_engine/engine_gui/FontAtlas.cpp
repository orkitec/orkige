/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	FontAtlas.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the runtime TrueType/SVG atlas baker (@see FontAtlas.h). Bakes
				glyph pages + rasterised vector sprites into ONE GPU texture
				at the display's device resolution; the renderer consumes the
				result through the unchanged UiAtlas/UiFont/UiGlyph surface.
*********************************************************************/

#include "engine_gui/FontAtlas.h"
#include "engine_gui/FontBake.h"
#include "engine_gui/SvgRaster.h"
#include "engine_render/RenderSystem.h"
#include "engine_util/ConfigFileUtil.h"
#include <core_debug/DebugMacros.h>

#include <OgreConfigFile.h>
#include <OgreResourceGroupManager.h>
#include <OgreString.h>
#include <OgreStringConverter.h>
#include <OgreStringVector.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! read a resource's whole byte content through the OGRE resource
		//! system (the LoadWavData/MusicStream path), so fonts/sprites resolve
		//! inside mounted pak archives and the APK asset extraction just the same
		bool readResourceBytes(String const & fileName,
			std::vector<unsigned char> & out)
		{
			Ogre::ResourceGroupManager * groupManager =
				Ogre::ResourceGroupManager::getSingletonPtr();
			if(groupManager == NULL)
			{
				return false;
			}
			try
			{
				const Ogre::String group =
					groupManager->findGroupContainingResource(fileName);
				Ogre::DataStreamPtr stream =
					groupManager->openResource(fileName, group);
				if(!stream)
				{
					return false;
				}
				const std::size_t size = stream->size();
				out.resize(size);
				if(size > 0)
				{
					stream->read(&out[0], size);
				}
				return true;
			}
			catch(...)
			{
				// unknown file -> an honest false, never a crash
				return false;
			}
		}

		//! first value of a key in a section's settings multimap, or default
		String settingValue(Ogre::ConfigFile::SettingsMultiMap const & settings,
			String const & key, String const & fallback = "")
		{
			Ogre::ConfigFile::SettingsMultiMap::const_iterator it =
				settings.find(key);
			return it != settings.end() ? it->second : fallback;
		}
	}
	//---------------------------------------------------------
	FontAtlas::FontAtlas(String const & textureName, uint texturePx,
		float bakeScale)
		: mAtlas("", Vec2(0, 0)), mTexturePx(0), mBakeScale(1.0f),
		mTextureDirty(false), mUploaded(false), mValid(false),
		mReportedFull(false)
	{
		this->_init(textureName, texturePx, bakeScale);
	}
	//---------------------------------------------------------
	FontAtlas::FontAtlas(String const & oguiFile, String const & group)
		: mAtlas("", Vec2(0, 0)), mTexturePx(0), mBakeScale(1.0f),
		mTextureDirty(false), mUploaded(false), mValid(false),
		mReportedFull(false)
	{
		Ogre::ConfigFile configFile;
		configFile.loadFromResourceSystem(oguiFile, group, " ", true);
		const ConfigFileUtil::SectionMap sections =
			ConfigFileUtil::getSections(configFile);

		// page size: [Texture] page <px> (default 1024 - ample for the eager
		// Latin-1 pages plus a healthy on-demand CJK/Cyrillic working set)
		uint pagePx = 1024;
		for(auto const & section : sections)
		{
			String name = section.first;
			Ogre::StringUtil::toLowerCase(name);
			if(name == "texture")
			{
				const String page = settingValue(section.second, "page");
				if(!page.empty())
				{
					pagePx = Ogre::StringConverter::parseUnsignedInt(page);
				}
			}
		}

		// bake at the display's integer content scale (GuiManager snaps it
		// into UiGlyph::scale before createView; default 1 headless)
		const float bakeScale = std::max(1.0f, UiGlyph::scale.x);
		// a texture name distinct from any on-disk .png the atlas might carry
		String textureName = oguiFile;
		const size_t dot = textureName.find_last_of('.');
		if(dot != String::npos)
		{
			textureName = textureName.substr(0, dot);
		}
		textureName += "__ttf_atlas";

		this->_init(textureName, pagePx, bakeScale);

		// fonts + sprites
		for(auto const & section : sections)
		{
			String name = section.first;
			Ogre::StringUtil::toLowerCase(name);
			if(Ogre::StringUtil::startsWith(name, "font.", false))
			{
				const String ttf = settingValue(section.second, "ttf");
				if(ttf.empty())
				{
					continue;	// a bitmap [Font.N] is not our business
				}
				const uint index = Ogre::StringConverter::parseUnsignedInt(
					name.substr(5));
				const float size = Ogre::StringConverter::parseReal(
					settingValue(section.second, "size", "16"));
				uint first = 32, last = 255;
				const String range = settingValue(section.second, "range");
				if(!range.empty())
				{
					const Ogre::StringVector parts =
						Ogre::StringUtil::split(range, " ", 2);
					if(parts.size() == 2)
					{
						first = Ogre::StringConverter::parseUnsignedInt(parts[0]);
						last = Ogre::StringConverter::parseUnsignedInt(parts[1]);
					}
				}
				std::vector<unsigned char> bytes;
				if(readResourceBytes(ttf, bytes) && !bytes.empty())
				{
					this->addFace(index, bytes.data(), int(bytes.size()),
						size, first, last);
				}
				else
				{
					oDebugWarning(false, "FontAtlas: font asset not found: " << ttf);
				}
			}
			else if(name == "sprites")
			{
				for(auto const & entry : section.second)
				{
					const Ogre::StringVector values =
						Ogre::StringUtil::split(entry.second, " ", 4);
					if(values.size() >= 3 &&
						Ogre::StringUtil::startsWith(values[0], "svg", false))
					{
						const String asset = values[1];
						const float designWidth =
							Ogre::StringConverter::parseReal(values[2]);
						std::vector<unsigned char> bytes;
						if(readResourceBytes(asset, bytes) && !bytes.empty())
						{
							this->addSvgSprite(entry.first, bytes.data(),
								int(bytes.size()), designWidth);
						}
						else
						{
							oDebugWarning(false, "FontAtlas: svg asset not found: "
								<< asset);
						}
					}
				}
			}
		}

		this->flush();
	}
	//---------------------------------------------------------
	FontAtlas::~FontAtlas()
	{
		if(this->mUploaded && RenderSystem::get() != NULL)
		{
			RenderSystem::get()->destroyTexture2D(this->mTextureName);
		}
		for(std::map<uint, FaceInfo>::iterator it = this->mFaces.begin();
			it != this->mFaces.end(); ++it)
		{
			if(it->second.handle != NULL)
			{
				FontBake::close(it->second.handle);
			}
		}
	}
	//---------------------------------------------------------
	void FontAtlas::_init(String const & textureName, uint texturePx,
		float bakeScale)
	{
		this->mTexturePx = std::max(1u, texturePx);
		this->mBakeScale = std::max(1.0f, bakeScale);
		this->mTextureName = textureName;
		this->mPacker.configure(this->mTexturePx, this->mTexturePx, 1);
		this->mPixels.assign(
			size_t(this->mTexturePx) * size_t(this->mTexturePx) * 4u, 0);

		// the atlas view binds this named page and normalizes against its size
		this->mAtlas.mTextureName = textureName;
		this->mAtlas.mTextureSize = Vec2(Real(this->mTexturePx),
			Real(this->mTexturePx));
		this->mAtlas.mInverseTextureSize = Vec2(1.0f / Real(this->mTexturePx),
			1.0f / Real(this->mTexturePx));

		// reserve a 2x2 opaque-white block: every solid UiRect fill samples it
		FontPacker::Rect white;
		if(this->mPacker.allocate(2, 2, white))
		{
			for(uint dy = 0; dy < 2; ++dy)
			{
				for(uint dx = 0; dx < 2; ++dx)
				{
					const size_t p = (size_t(white.y + dy) * this->mTexturePx +
						(white.x + dx)) * 4u;
					this->mPixels[p + 0] = 255;
					this->mPixels[p + 1] = 255;
					this->mPixels[p + 2] = 255;
					this->mPixels[p + 3] = 255;
				}
			}
			this->mAtlas.mWhitePixel = Vec2(
				(Real(white.x) + 0.5f) / Real(this->mTexturePx),
				(Real(white.y) + 0.5f) / Real(this->mTexturePx));
		}
		this->mTextureDirty = true;
	}
	//---------------------------------------------------------
	bool FontAtlas::addFace(uint fontIndex, unsigned char const * ttf,
		int ttfSize, float designPixelHeight, uint rangeFirst, uint rangeLast)
	{
		FontBake::Face* handle = FontBake::open(ttf, ttfSize);
		if(handle == NULL)
		{
			oDebugWarning(false, "FontAtlas: TrueType face would not parse (index "
				<< fontIndex << ")");
			return false;
		}
		if(rangeLast < rangeFirst)
		{
			rangeLast = rangeFirst;
		}

		FaceInfo face;
		face.handle = handle;
		face.scaleDesign =
			FontBake::scaleForPixelHeight(handle, designPixelHeight);
		face.scaleDevice = FontBake::scaleForPixelHeight(handle,
			designPixelHeight * this->mBakeScale);

		// uniform glyph cell height (device px): ascent..descent covers the em;
		// every glyph is baked into a cell of this height at its baseline so
		// the renderer's top-aligned quads line up (matches the bitmap path)
		float ascentDev = 0, descentDev = 0, lineGapDev = 0;
		FontBake::verticalMetrics(handle, face.scaleDevice,
			ascentDev, descentDev, lineGapDev);
		face.ascentDevice = ascentDev;
		face.cellHeightDevice = std::max(1,
			int(std::ceil(ascentDev - descentDev)));

		this->mFaces[fontIndex] = face;
		FaceInfo & stored = this->mFaces[fontIndex];

		// the UiFont: metrics in DESIGN px (UiGlyph::scale multiplies to device)
		UiFont & font = this->mAtlas.mFonts[fontIndex];
		float ascentDes = 0, descentDes = 0, lineGapDes = 0;
		FontBake::verticalMetrics(handle, stored.scaleDesign,
			ascentDes, descentDes, lineGapDes);
		font.mRangeBegin = rangeFirst;
		font.mRangeEnd = rangeLast;
		font.mLineHeight = ascentDes - descentDes + lineGapDes;
		font.mBaseline = ascentDes;
		font.mLetterSpacing = 0.0f;
		float spaceAdv = 0, lsb = 0;
		FontBake::horizontalMetrics(handle, ' ', stored.scaleDesign,
			spaceAdv, lsb);
		font.mSpaceLength = spaceAdv;
		float digitAdv = 0;
		FontBake::horizontalMetrics(handle, '0', stored.scaleDesign,
			digitAdv, lsb);
		font.mMonoWidth = digitAdv > 0 ? digitAdv : spaceAdv;
		font.mProvider = this;

		// eager base page: bake every codepoint in the range into the
		// contiguous slot vector (an ink-less code stays a zero-size glyph)
		font.mGlyphs.assign(rangeLast - rangeFirst + 1, UiGlyph());
		for(uint code = rangeFirst; code <= rangeLast; ++code)
		{
			UiGlyph glyph;
			if(this->_bakeGlyph(stored, code, glyph))
			{
				font.mGlyphs[code - rangeFirst] = glyph;
			}
		}

		this->mFontToIndex[&font] = fontIndex;
		this->mValid = true;
		this->mTextureDirty = true;
		return true;
	}
	//---------------------------------------------------------
	bool FontAtlas::addSvgSprite(String const & name,
		unsigned char const * svg, int svgSize, float designWidth)
	{
		if(designWidth <= 0.0f)
		{
			return false;
		}
		const int deviceWidth = std::max(1,
			int(std::lround(double(designWidth) * double(this->mBakeScale))));
		SvgRaster::Image image = SvgRaster::rasterize(svg, svgSize, deviceWidth);
		if(image.width <= 0 || image.height <= 0)
		{
			oDebugWarning(false, "FontAtlas: SVG sprite would not rasterise: " << name);
			return false;
		}
		FontPacker::Rect rect;
		if(!this->mPacker.allocate(uint(image.width), uint(image.height), rect))
		{
			oDebugWarning(false, "FontAtlas: page full, dropping SVG sprite: " << name);
			return false;
		}
		this->_blitRgba(image.rgba.data(), image.width, image.height,
			int(rect.x), int(rect.y));

		UiSprite sprite;
		sprite.spriteWidth = Real(image.width) / this->mBakeScale;
		sprite.spriteHeight = Real(image.height) / this->mBakeScale;
		sprite.uvLeft = Real(rect.x) / Real(this->mTexturePx);
		sprite.uvTop = Real(rect.y) / Real(this->mTexturePx);
		sprite.uvRight = Real(rect.x + rect.w) / Real(this->mTexturePx);
		sprite.uvBottom = Real(rect.y + rect.h) / Real(this->mTexturePx);
		sprite.texCoords[TopLeft] = Vec2(sprite.uvLeft, sprite.uvTop);
		sprite.texCoords[TopRight] = Vec2(sprite.uvRight, sprite.uvTop);
		sprite.texCoords[BottomRight] = Vec2(sprite.uvRight, sprite.uvBottom);
		sprite.texCoords[BottomLeft] = Vec2(sprite.uvLeft, sprite.uvBottom);
		this->mAtlas.mSprites[name] = sprite;

		this->mValid = true;
		this->mTextureDirty = true;
		return true;
	}
	//---------------------------------------------------------
	UiGlyph const * FontAtlas::provideGlyph(UiFont const & font, uint codepoint)
	{
		std::unordered_map<UiFont const *, uint>::const_iterator fit =
			this->mFontToIndex.find(&font);
		if(fit == this->mFontToIndex.end())
		{
			return NULL;
		}
		std::map<uint, FaceInfo>::iterator face = this->mFaces.find(fit->second);
		if(face == this->mFaces.end())
		{
			return NULL;
		}
		UiGlyph glyph;
		if(!this->_bakeGlyph(face->second, codepoint, glyph))
		{
			return NULL;	// page full: honest miss (logged once in _bakeGlyph)
		}
		// mSparseGlyphs is mutable: getGlyph is const but pages lazily
		UiGlyph & stored = font.mSparseGlyphs[codepoint];
		stored = glyph;
		return &stored;
	}
	//---------------------------------------------------------
	bool FontAtlas::_bakeGlyph(FaceInfo & face, uint codepoint, UiGlyph & out)
	{
		float advanceDesign = 0, lsbDesign = 0;
		FontBake::horizontalMetrics(face.handle, codepoint, face.scaleDesign,
			advanceDesign, lsbDesign);

		FontBake::Bitmap bitmap =
			FontBake::rasterize(face.handle, codepoint, face.scaleDevice);

		// ink-less codepoint (space, control, or a glyph the face lacks): a
		// zero-size glyph that still advances (consumes no page space)
		if(bitmap.width <= 0 || bitmap.height <= 0)
		{
			out.setGlyphWidth(0);
			out.setGlyphHeight(0);
			out.setGlyphAdvance(advanceDesign);
			return true;
		}

		const int advanceDevice = int(std::lround(
			double(advanceDesign) * double(this->mBakeScale)));
		// cell width holds at least the advance and the ink extent from the pen
		int cellW = std::max(advanceDevice, bitmap.xOffset + bitmap.width);
		cellW = std::max(cellW, bitmap.width);
		const int cellH = face.cellHeightDevice;

		FontPacker::Rect rect;
		if(!this->mPacker.allocate(uint(cellW), uint(cellH), rect))
		{
			if(!this->mReportedFull)
			{
				oDebugWarning(false, "FontAtlas: page full (" << this->mTexturePx << "x"
					<< this->mTexturePx << "); some glyphs will not render");
				this->mReportedFull = true;
			}
			return false;
		}

		// blit the coverage at (left side bearing, baseline - ascent) inside
		// the cell so all cells share a baseline; clamp to the cell box
		const int glyphX = std::max(0, bitmap.xOffset);
		const int glyphY = std::max(0,
			int(std::lround(double(face.ascentDevice))) + bitmap.yOffset);
		this->_blitCoverage(bitmap.coverage.data(), bitmap.width, bitmap.height,
			int(rect.x) + glyphX, int(rect.y) + glyphY,
			int(rect.x) + cellW, int(rect.y) + cellH);

		this->_setGlyphUv(out, rect.x, rect.y, uint(cellW), uint(cellH));
		out.setGlyphWidth(Real(cellW) / this->mBakeScale);
		out.setGlyphHeight(Real(cellH) / this->mBakeScale);
		out.setGlyphAdvance(advanceDesign);
		this->mTextureDirty = true;
		return true;
	}
	//---------------------------------------------------------
	void FontAtlas::_blitCoverage(unsigned char const * coverage,
		int cw, int ch, int destX, int destY, int clipRight, int clipBottom)
	{
		const int maxY = std::min(clipBottom, int(this->mTexturePx));
		const int maxX = std::min(clipRight, int(this->mTexturePx));
		for(int y = 0; y < ch; ++y)
		{
			const int py = destY + y;
			if(py < 0 || py >= maxY)
			{
				continue;
			}
			for(int x = 0; x < cw; ++x)
			{
				const int px = destX + x;
				if(px < 0 || px >= maxX)
				{
					continue;
				}
				const unsigned char a = coverage[y * cw + x];
				const size_t p = (size_t(py) * this->mTexturePx + px) * 4u;
				// white premultiplied by nothing: colour comes from the vertex,
				// alpha carries the coverage (same as the bitmap glyph atlas)
				this->mPixels[p + 0] = 255;
				this->mPixels[p + 1] = 255;
				this->mPixels[p + 2] = 255;
				this->mPixels[p + 3] = a;
			}
		}
	}
	//---------------------------------------------------------
	void FontAtlas::_blitRgba(unsigned char const * rgba, int cw, int ch,
		int destX, int destY)
	{
		for(int y = 0; y < ch; ++y)
		{
			const int py = destY + y;
			if(py < 0 || py >= int(this->mTexturePx))
			{
				continue;
			}
			for(int x = 0; x < cw; ++x)
			{
				const int px = destX + x;
				if(px < 0 || px >= int(this->mTexturePx))
				{
					continue;
				}
				const size_t s = (size_t(y) * cw + x) * 4u;
				const size_t p = (size_t(py) * this->mTexturePx + px) * 4u;
				this->mPixels[p + 0] = rgba[s + 0];
				this->mPixels[p + 1] = rgba[s + 1];
				this->mPixels[p + 2] = rgba[s + 2];
				this->mPixels[p + 3] = rgba[s + 3];
			}
		}
	}
	//---------------------------------------------------------
	void FontAtlas::_setGlyphUv(UiGlyph & glyph, uint x, uint y,
		uint w, uint h) const
	{
		glyph.uvLeft = Real(x) / Real(this->mTexturePx);
		glyph.uvTop = Real(y) / Real(this->mTexturePx);
		glyph.uvRight = Real(x + w) / Real(this->mTexturePx);
		glyph.uvBottom = Real(y + h) / Real(this->mTexturePx);
		glyph.uvWidth = glyph.uvRight - glyph.uvLeft;
		glyph.uvHeight = glyph.uvBottom - glyph.uvTop;
		glyph.texCoords[TopLeft] = Vec2(glyph.uvLeft, glyph.uvTop);
		glyph.texCoords[TopRight] = Vec2(glyph.uvRight, glyph.uvTop);
		glyph.texCoords[BottomRight] = Vec2(glyph.uvRight, glyph.uvBottom);
		glyph.texCoords[BottomLeft] = Vec2(glyph.uvLeft, glyph.uvBottom);
	}
	//---------------------------------------------------------
	void FontAtlas::flush()
	{
		if(!this->mTextureDirty)
		{
			return;
		}
		RenderSystem* renderSystem = RenderSystem::get();
		if(renderSystem == NULL)
		{
			return;	// headless: the CPU page + UiAtlas view are still valid
		}
		if(renderSystem->createTexture2D(this->mTextureName,
			this->mPixels.data(), this->mTexturePx, this->mTexturePx))
		{
			this->mUploaded = true;
			this->mTextureDirty = false;
		}
	}
	//---------------------------------------------------------
	bool FontAtlas::oguiDeclaresRuntimeContent(String const & oguiFile,
		String const & group)
	{
		Ogre::ConfigFile configFile;
		try
		{
			configFile.loadFromResourceSystem(oguiFile, group, " ", true);
		}
		catch(...)
		{
			return false;
		}
		const ConfigFileUtil::SectionMap sections =
			ConfigFileUtil::getSections(configFile);
		for(auto const & section : sections)
		{
			String name = section.first;
			Ogre::StringUtil::toLowerCase(name);
			if(Ogre::StringUtil::startsWith(name, "font.", false))
			{
				if(section.second.find("ttf") != section.second.end())
				{
					return true;
				}
			}
			else if(name == "sprites")
			{
				for(auto const & entry : section.second)
				{
					if(Ogre::StringUtil::startsWith(entry.second, "svg", false))
					{
						return true;
					}
				}
			}
		}
		return false;
	}
}
