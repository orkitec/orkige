/********************************************************************
	created:	Wednesday 2026/07/08 at 23:30
	filename: 	UiAtlas.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	.ogui atlas parsing + metrics (@see UiAtlas.h). The
				format and semantics are inherited from the retired
				Gorilla library (MIT, (c) 2010 Robin Southern); the
				parser here is its ported, value-owned successor.
*********************************************************************/

#include "engine_fastgui/UiAtlas.h"
#include "engine_render/RenderSystem.h"
#include "engine_util/ConfigFileUtil.h"
#include <core_debug/DebugMacros.h>

#include <OgreConfigFile.h>
#include <OgreResourceGroupManager.h>
#include <OgreString.h>
#include <OgreStringConverter.h>
#include <OgreStringVector.h>

#include <sstream>

namespace Orkige
{
	Vec2 UiGlyph::scale = Vec2(1.0f, 1.0f);
	//---------------------------------------------------------
	Color rgb(unsigned char r, unsigned char g, unsigned char b,
		unsigned char a)
	{
		static const Real inv255 = Real(1.0 / 255.0);
		return Color(Real(r) * inv255, Real(g) * inv255,
			Real(b) * inv255, Real(a) * inv255);
	}
	//---------------------------------------------------------
	Color webcolour(Colours::Colour colour, Real alpha)
	{
		static const Real inv255 = Real(1.0 / 255.0);
		Color result;
		result.b = Real(colour & 0xFF) * inv255;
		result.g = Real((colour >> 8) & 0xFF) * inv255;
		result.r = Real((colour >> 16) & 0xFF) * inv255;
		result.a = alpha;
		return result;
	}
	//---------------------------------------------------------
	//--- the section loaders ----------------------------------
	//---------------------------------------------------------
	struct UiAtlasLoader
	{
		typedef Ogre::ConfigFile::SettingsMultiMap Settings;

		//! strip "#..." and "//..." comments off a value
		static String cleanValue(String const & value)
		{
			String data = value;
			size_t comment = data.find_first_of('#');
			if(comment != String::npos)
			{
				data = data.substr(0, comment);
			}
			comment = data.find("//");
			if(comment != String::npos)
			{
				data = data.substr(0, comment);
			}
			return data;
		}

		//! [Texture]: file (name [~group - historical, the facade resolves
		//! across all groups]) + whitepixel "x y" in pixels
		static void loadTexture(UiAtlas & atlas, Settings const & settings,
			Real textureWidth, Real textureHeight)
		{
			for(Settings::const_iterator i = settings.begin();
				i != settings.end(); ++i)
			{
				String name = i->first;
				const String data = cleanValue(i->second);
				Ogre::StringUtil::toLowerCase(name);

				if(name == "file")
				{
					String textureName = data;
					const size_t groupSplit = data.find_first_of('~');
					if(groupSplit != String::npos)
					{
						textureName = data.substr(0, groupSplit);
						Ogre::StringUtil::trim(textureName);
					}
					// texture size: all atlas metrics normalize against it.
					// The headless (unit-test) constructor provides it; the
					// resource path asks the render facade
					Real width = textureWidth, height = textureHeight;
					if(width <= 0 || height <= 0)
					{
						unsigned int texelWidth = 0, texelHeight = 0;
						const bool loaded = RenderSystem::get() &&
							RenderSystem::get()->getTextureSize(
								textureName, texelWidth, texelHeight);
						oAssertDesc(loaded, "UiAtlas: atlas texture not found: "
							<< textureName);
						if(!loaded)
						{
							continue;
						}
						width = Real(texelWidth);
						height = Real(texelHeight);
					}
					atlas.mTextureName = textureName;
					atlas.mTextureSize = Vec2(width, height);
					atlas.mInverseTextureSize =
						Vec2(1.0f / width, 1.0f / height);
				}
				else if(name == "whitepixel")
				{
					atlas.mWhitePixel =
						Ogre::StringConverter::parseVector2(data);
					atlas.mWhitePixel.x *= atlas.mInverseTextureSize.x;
					atlas.mWhitePixel.y *= atlas.mInverseTextureSize.y;
				}
			}
		}

		//! [Font.N] metrics + glyph_<code> "x y w h [advance]" entries
		static void loadGlyphs(Settings const & settings, UiFont & font)
		{
			Vec2 offset(0, 0);
			for(Settings::const_iterator i = settings.begin();
				i != settings.end(); ++i)
			{
				String name = i->first;
				const String data = cleanValue(i->second);
				Ogre::StringUtil::toLowerCase(name);

				if(name == "offset")
				{
					offset = Ogre::StringConverter::parseVector2(data);
				}
				else if(name == "lineheight")
				{
					font.mLineHeight = Ogre::StringConverter::parseReal(data);
				}
				else if(name == "spacelength")
				{
					font.mSpaceLength = Ogre::StringConverter::parseReal(data);
				}
				else if(name == "baseline")
				{
					font.mBaseline = Ogre::StringConverter::parseReal(data);
				}
				else if(name == "monowidth")
				{
					font.mMonoWidth = Ogre::StringConverter::parseReal(data);
				}
				else if(name == "range")
				{
					const Vec2 range =
						Ogre::StringConverter::parseVector2(data);
					font.mRangeBegin = uint(range.x);
					font.mRangeEnd = uint(range.y);
				}
				else if(name == "letterspacing")
				{
					font.mLetterSpacing =
						Ogre::StringConverter::parseReal(data);
				}
			}

			// one glyph SLOT for every code in the range (contiguous - a
			// missing glyph_<n> entry stays a zero-sized glyph)
			font.mGlyphs.clear();
			font.mGlyphs.resize(font.mRangeEnd >= font.mRangeBegin
				? font.mRangeEnd - font.mRangeBegin + 1 : 0);
			for(uint code = font.mRangeBegin; code <= font.mRangeEnd; ++code)
			{
				std::stringstream key;
				key << "glyph_" << code;
				Settings::const_iterator i = settings.find(key.str());
				if(i == settings.end())
				{
					continue;
				}
				const Ogre::StringVector values =
					Ogre::StringUtil::split(i->second, " ", 5);
				if(values.size() < 4)
				{
					continue;	// not enough properties - stays empty
				}
				UiGlyph & glyph = font.mGlyphs[code - font.mRangeBegin];
				glyph.uvLeft = offset.x +
					Ogre::StringConverter::parseReal(values[0]);
				glyph.uvTop = offset.y +
					Ogre::StringConverter::parseReal(values[1]);
				glyph.uvWidth = Ogre::StringConverter::parseReal(values[2]);
				glyph.uvHeight = Ogre::StringConverter::parseReal(values[3]);
				glyph.uvRight = glyph.uvLeft + glyph.uvWidth;
				glyph.uvBottom = glyph.uvTop + glyph.uvHeight;
				if(values.size() == 5)
				{
					glyph.setGlyphAdvance(Real(
						Ogre::StringConverter::parseInt(values[4])));
				}
				else
				{
					glyph.setGlyphAdvance(glyph.uvWidth);
				}
			}
		}

		//! [Font.N] kerning_<left> "<right> <amount>" entries
		static void loadKerning(Settings const & settings, UiFont & font)
		{
			for(Settings::const_iterator i = settings.begin();
				i != settings.end(); ++i)
			{
				String leftName = i->first;
				Ogre::StringUtil::toLowerCase(leftName);
				if(leftName.substr(0, 8) != "kerning_")
				{
					continue;
				}
				const String data = cleanValue(i->second);
				const uint leftGlyphId = Ogre::StringConverter
					::parseUnsignedInt(leftName.substr(8));
				const Ogre::StringVector values =
					Ogre::StringUtil::split(data, " ", 2);
				if(values.size() != 2)
				{
					continue;
				}
				const uint rightGlyphId =
					Ogre::StringConverter::parseUnsignedInt(values[0]);
				const int kerning =
					Ogre::StringConverter::parseInt(values[1]);
				const uint slot = rightGlyphId - font.mRangeBegin;
				if(slot < font.mGlyphs.size())
				{
					font.mGlyphs[slot].kerning.push_back(
						UiKerning(leftGlyphId, Real(kerning)));
				}
			}
		}

		//! [Sprites] name "x y w h [l r t b]" entries: the optional trailing
		//! four ints are nine-slice border insets (left/right/top/bottom, in
		//! sprite pixels); absent = a plain stretched sprite
		static void loadSprites(UiAtlas & atlas, Settings const & settings)
		{
			for(Settings::const_iterator i = settings.begin();
				i != settings.end(); ++i)
			{
				const String data = cleanValue(i->second);
				const Ogre::StringVector values =
					Ogre::StringUtil::split(data, " ", 8);
				if(values.size() != 4 && values.size() != 8)
				{
					continue;
				}
				UiSprite sprite;
				sprite.uvLeft = Real(
					Ogre::StringConverter::parseUnsignedInt(values[0]));
				sprite.uvTop = Real(
					Ogre::StringConverter::parseUnsignedInt(values[1]));
				sprite.spriteWidth = Real(
					Ogre::StringConverter::parseUnsignedInt(values[2]));
				sprite.spriteHeight = Real(
					Ogre::StringConverter::parseUnsignedInt(values[3]));
				if(values.size() == 8)
				{
					sprite.sliceLeft = Real(
						Ogre::StringConverter::parseUnsignedInt(values[4]));
					sprite.sliceRight = Real(
						Ogre::StringConverter::parseUnsignedInt(values[5]));
					sprite.sliceTop = Real(
						Ogre::StringConverter::parseUnsignedInt(values[6]));
					sprite.sliceBottom = Real(
						Ogre::StringConverter::parseUnsignedInt(values[7]));
				}
				atlas.mSprites[i->first] = sprite;
			}
		}
	};
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	UiAtlas::UiAtlas(String const & oguiFile, String const & group)
		: mTextureSize(0, 0), mInverseTextureSize(0, 0), mWhitePixel(0, 0)
	{
		this->refreshMarkupColours();
		Ogre::ConfigFile configFile;
		configFile.loadFromResourceSystem(oguiFile, group, " ", true);
		this->_load(configFile, 0, 0);
		this->_calculateCoordinates();
	}
	//---------------------------------------------------------
	UiAtlas::UiAtlas(Ogre::ConfigFile & configFile,
		Real textureWidth, Real textureHeight)
		: mTextureSize(0, 0), mInverseTextureSize(0, 0), mWhitePixel(0, 0)
	{
		this->refreshMarkupColours();
		this->_load(configFile, textureWidth, textureHeight);
		this->_calculateCoordinates();
	}
	//---------------------------------------------------------
	UiAtlas::UiAtlas(String const & textureName, Vec2 const & textureSize)
		: mTextureName(textureName), mTextureSize(textureSize),
		mInverseTextureSize(
			textureSize.x > 0 ? 1.0f / textureSize.x : 0.0f,
			textureSize.y > 0 ? 1.0f / textureSize.y : 0.0f),
		mWhitePixel(0, 0)
	{
		// the baker (FontAtlas) fills mFonts/mSprites/mWhitePixel with
		// already-normalized UVs; nothing here parses or normalizes further
		this->refreshMarkupColours();
	}
	//---------------------------------------------------------
	UiAtlas::~UiAtlas()
	{
	}
	//---------------------------------------------------------
	void UiAtlas::refreshMarkupColours()
	{
		this->mMarkupColour[0] = rgb(255, 255, 255);
		this->mMarkupColour[1] = rgb(0, 0, 0);
		this->mMarkupColour[2] = rgb(204, 204, 204);
		this->mMarkupColour[3] = rgb(254, 220, 129);
		this->mMarkupColour[4] = rgb(254, 138, 129);
		this->mMarkupColour[5] = rgb(123, 236, 110);
		this->mMarkupColour[6] = rgb(44,  192, 171);
		this->mMarkupColour[7] = rgb(199, 93,  142);
		this->mMarkupColour[8] = rgb(254, 254, 254);
		this->mMarkupColour[9] = rgb(13,  13,  13);
	}
	//---------------------------------------------------------
	void UiAtlas::setMarkupColour(uint index, Color const & colour)
	{
		if(index > 9)
		{
			return;
		}
		this->mMarkupColour[index] = colour;
	}
	//---------------------------------------------------------
	Color UiAtlas::getMarkupColour(uint index) const
	{
		if(index > 9)
		{
			return Color(1, 1, 1, 1);
		}
		return this->mMarkupColour[index];
	}
	//---------------------------------------------------------
	void UiAtlas::replaceTexture(String const & texture)
	{
		oAssertDesc(!this->mTextureName.empty(),
			"replaceTexture: no texture to replace present");
		if(texture == this->mTextureName)
		{
			return;
		}
		const Vec2 previousSize = this->mTextureSize;
		unsigned int width = 0, height = 0;
		const bool loaded = RenderSystem::get() &&
			RenderSystem::get()->getTextureSize(texture, width, height);
		oAssertDesc(loaded, "replaceTexture: texture replacement not loaded");
		if(!loaded)
		{
			return;
		}
		oAssertDesc(previousSize == Vec2(Real(width), Real(height)),
			"replaceTexture: replaced atlas texture has different size");
		// same size: every normalized coordinate stays valid, the screens
		// rebind by name on their forced resubmission
		this->mTextureName = texture;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void UiAtlas::_load(Ogre::ConfigFile & configFile,
		Real textureWidth, Real textureHeight)
	{
		// section iteration through the per-flavor seam (classic 14 and
		// Ogre-Next drifted apart here, @see engine_util/ConfigFileUtil.h)
		for(auto const & section : ConfigFileUtil::getSections(configFile))
		{
			String sectionName = section.first;
			Ogre::StringUtil::toLowerCase(sectionName);

			if(sectionName == "texture")
			{
				UiAtlasLoader::loadTexture(*this, section.second,
					textureWidth, textureHeight);
			}
			else if(Ogre::StringUtil::startsWith(sectionName, "font.", false))
			{
				const uint index = Ogre::StringConverter::parseUnsignedInt(
					sectionName.substr(5));
				UiFont & font = this->mFonts[index];
				UiAtlasLoader::loadGlyphs(section.second, font);
				UiAtlasLoader::loadKerning(section.second, font);
			}
			else if(sectionName == "sprites")
			{
				UiAtlasLoader::loadSprites(*this, section.second);
			}
		}
	}
	//---------------------------------------------------------
	void UiAtlas::_calculateCoordinates()
	{
		for(std::map<uint, UiFont>::iterator fontIt = this->mFonts.begin();
			fontIt != this->mFonts.end(); ++fontIt)
		{
			for(UiGlyph & glyph : fontIt->second.mGlyphs)
			{
				glyph.uvLeft	*= this->mInverseTextureSize.x;
				glyph.uvTop		*= this->mInverseTextureSize.y;
				glyph.uvRight	*= this->mInverseTextureSize.x;
				glyph.uvBottom	*= this->mInverseTextureSize.y;

				glyph.texCoords[TopLeft].x = glyph.uvLeft;
				glyph.texCoords[TopLeft].y = glyph.uvTop;
				glyph.texCoords[TopRight].x = glyph.uvRight;
				glyph.texCoords[TopRight].y = glyph.uvTop;
				glyph.texCoords[BottomRight].x = glyph.uvRight;
				glyph.texCoords[BottomRight].y = glyph.uvBottom;
				glyph.texCoords[BottomLeft].x = glyph.uvLeft;
				glyph.texCoords[BottomLeft].y = glyph.uvBottom;

				glyph.setGlyphWidth(glyph.uvWidth);
				glyph.setGlyphHeight(glyph.uvHeight);
			}
		}

		for(std::map<String, UiSprite>::iterator it = this->mSprites.begin();
			it != this->mSprites.end(); ++it)
		{
			UiSprite & sprite = it->second;
			sprite.uvRight	= sprite.uvLeft + sprite.spriteWidth;
			sprite.uvBottom	= sprite.uvTop + sprite.spriteHeight;

			sprite.uvLeft	*= this->mInverseTextureSize.x;
			sprite.uvTop	*= this->mInverseTextureSize.y;
			sprite.uvRight	*= this->mInverseTextureSize.x;
			sprite.uvBottom	*= this->mInverseTextureSize.y;

			sprite.texCoords[TopLeft].x = sprite.uvLeft;
			sprite.texCoords[TopLeft].y = sprite.uvTop;
			sprite.texCoords[TopRight].x = sprite.uvRight;
			sprite.texCoords[TopRight].y = sprite.uvTop;
			sprite.texCoords[BottomRight].x = sprite.uvRight;
			sprite.texCoords[BottomRight].y = sprite.uvBottom;
			sprite.texCoords[BottomLeft].x = sprite.uvLeft;
			sprite.texCoords[BottomLeft].y = sprite.uvBottom;
		}
	}
}
