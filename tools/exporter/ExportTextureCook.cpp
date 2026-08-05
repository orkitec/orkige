/********************************************************************
	created:	Friday 2026/07/31 at 15:00
	filename: 	ExportTextureCook.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportTextureCook.h"

#include "ExportFiles.h"

#include <TextureEncode.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace OrkigeExport
{
	namespace
	{
		//! the BCn family: the formats that ship in a .dds
		bool isBlockCompressedFormat(Orkige::String const & format)
		{
			return format == "bc1" || format == "bc3" || format == "bc7";
		}
		//---------------------------------------------------------
		//! the sidecar's explicit format tokens ("etc2" is a FAMILY: the cook
		//! picks the RGB8 or RGBA8 variant per texture from its alpha channel)
		bool isExplicitFormat(Orkige::String const & format)
		{
			return format == "none" || format == "astc-4x4" ||
				format == "astc-6x6" || format == "astc-8x8" ||
				format == "etc2" || isBlockCompressedFormat(format);
		}
		//---------------------------------------------------------
		bool isMobilePlatform(Orkige::String const & platform)
		{
			return platform == "ios" || platform == "android";
		}
		//---------------------------------------------------------
		//! the desktop slot: the default block, whether spelled "" or by the
		//! name of any one desktop system - they all resolve to the same block,
		//! because a desktop GPU wants no mobile container
		bool isDesktopPlatform(Orkige::String const & platform)
		{
			return platform.empty() || platform == "macos" ||
				platform == "linux" || platform == "windows";
		}
		//---------------------------------------------------------
		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void emit(std::function<void(Orkige::String const &)> const & sink,
			Orkige::String const & message)
		{
			if(sink)
			{
				sink(message);
			}
		}
		//---------------------------------------------------------
		std::uint32_t readU32(unsigned char const * data, std::size_t offset)
		{
			std::uint32_t value = 0;
			std::memcpy(&value, data + offset, 4);
			return value;
		}
		//---------------------------------------------------------
		//! the byte index of a 0xFF-aligned channel mask in a little-endian
		//! pixel, or -1 when the mask is not a whole aligned byte
		int channelShift(std::uint32_t mask)
		{
			for(int byte = 0; byte < 4; ++byte)
			{
				if(mask == (0xFFu << (byte * 8)))
				{
					return byte;
				}
			}
			return -1;
		}
	}
	//---------------------------------------------------------
	bool resolveTextureFormat(Orkige::TextureImportSettings const & settings,
		Orkige::String const & platform, Orkige::String const & flavor,
		bool alpha, TextureCookTarget & out,
		std::function<void(Orkige::String const &)> const & warn,
		Orkige::String * error)
	{
		out = TextureCookTarget();
		Orkige::String format = settings.format;
		const Orkige::String quality = settings.quality;
		if(format == "none")
		{
			return true;	// ship the source untouched
		}
		if(format == "auto")
		{
			if(platform == "web")
			{
				return true;	// no compressed format is guaranteed in a browser
			}
			if(flavor == "classic" && isMobilePlatform(platform))
			{
				// ETC2/ASTC are not guaranteed in a GLES2 context
				return true;
			}
			if(isMobilePlatform(platform))
			{
				format = (quality == "high") ? "astc-4x4"
					: (quality == "low") ? "astc-8x8" : "astc-6x6";
			}
			else if(flavor == "classic")
			{
				format = alpha ? "bc3" : "bc1";
			}
			else
			{
				format = (alpha || quality == "high") ? "bc7" : "bc1";
			}
		}
		else if(!isExplicitFormat(format))
		{
			return report(error, "unknown texture format '" + format + "'");
		}
		else
		{
			// an explicit format: validate the (platform, flavor) pair
			if(isBlockCompressedFormat(format) && isMobilePlatform(platform))
			{
				return report(error, "format '" + format +
					"' cannot ship to '" + platform + "' - mobile GPUs have "
					"no BCn support (use astc/etc2 or none)");
			}
			if(!isBlockCompressedFormat(format) && isDesktopPlatform(platform))
			{
				return report(error, "format '" + format + "' cannot ship on "
					"a desktop export - the desktop runtimes load only the BC "
					"family (classic desktop GL exposes neither ASTC nor ETC2, "
					"and the next flavor's desktop renderer maps them only in "
					"its mobile builds); use bc1/bc3" +
					(flavor == "classic" ? "" : "/bc7") + " or none");
			}
			if(flavor == "classic" && isDesktopPlatform(platform) &&
				format == "bc7")
			{
				return report(error, "format 'bc7' cannot ship on the classic "
					"desktop flavor - its default GL renderer has no BC7 "
					"support (use bc1/bc3)");
			}
			if(platform == "web")
			{
				emit(warn, "WARNING: explicit format '" + format + "' on the "
					"web build only loads on visitor GPUs exposing the "
					"matching compressed-texture extension - none is "
					"guaranteed in a browser");
			}
			if(flavor == "classic" && isMobilePlatform(platform))
			{
				emit(warn, "WARNING: explicit format '" + format + "' on the "
					"classic GLES2 mobile flavor is unproven - ETC2/ASTC are "
					"GLES3-tier and may not load in a GLES2 context on every "
					"device");
			}
		}
		// the etc2 family resolves per texture: EAC alpha only when needed
		if(format == "etc2")
		{
			format = alpha ? "etc2-rgba" : "etc2-rgb";
		}
		out.format = format;
		out.container = isBlockCompressedFormat(format) ? "dds"
			: (flavor == "next" ? "oitd" : "ktx");
		return true;
	}
	//---------------------------------------------------------
	std::vector<ExportImage> buildMipLevels(ExportImage const & image,
		bool generateMips)
	{
		std::vector<ExportImage> levels;
		levels.push_back(image);
		if(!generateMips)
		{
			return levels;
		}
		int level = 1;
		while((image.width >> level) > 0 || (image.height >> level) > 0)
		{
			const int targetWidth = std::max(1, image.width >> level);
			const int targetHeight = std::max(1, image.height >> level);
			levels.push_back(
				downscaleImage(image, targetWidth, targetHeight));
			if(targetWidth == 1 && targetHeight == 1)
			{
				break;
			}
			++level;
		}
		return levels;
	}
	//---------------------------------------------------------
	bool decodeDdsCubemap(Orkige::String const & path, DdsCubemap & out)
	{
		std::vector<unsigned char> raw;
		if(!ExportFiles::readBytes(path, raw, 0))
		{
			return false;
		}
		unsigned char const * data = raw.data();
		const std::size_t size = raw.size();
		if(size < 128 || std::memcmp(data, "DDS ", 4) != 0)
		{
			return false;
		}
		const std::uint32_t headerSize = readU32(data, 4);
		if(headerSize != 124)
		{
			return false;
		}
		const std::uint32_t height = readU32(data, 4 + 8);
		const std::uint32_t width = readU32(data, 4 + 12);
		const std::uint32_t mips = readU32(data, 4 + 24);
		const std::uint32_t pixelFlags = readU32(data, 76 + 4);
		const std::uint32_t bitCount = readU32(data, 76 + 12);
		const std::uint32_t redMask = readU32(data, 76 + 16);
		const std::uint32_t greenMask = readU32(data, 76 + 20);
		const std::uint32_t blueMask = readU32(data, 76 + 24);
		const std::uint32_t alphaMask = readU32(data, 76 + 28);
		const std::uint32_t caps2 = readU32(data, 108 + 4);
		// a real six-face cubemap (CUBEMAP bit + all six face bits)
		if((caps2 & 0xFE00u) != 0xFE00u)
		{
			return false;
		}
		// uncompressed 32bpp RGB(A) only: a fourCC/DX10 payload is already
		// compressed (or an unsupported layout) and re-cooks from nothing
		if((pixelFlags & 0x4u) != 0 || (pixelFlags & 0x40u) == 0 ||
			bitCount != 32)
		{
			return false;
		}
		if(width != height || mips < 1)
		{
			return false;
		}
		const int redIndex = channelShift(redMask);
		const int greenIndex = channelShift(greenMask);
		const int blueIndex = channelShift(blueMask);
		const int alphaIndex = ((pixelFlags & 0x1u) != 0)
			? channelShift(alphaMask) : -1;
		if(redIndex < 0 || greenIndex < 0 || blueIndex < 0)
		{
			return false;
		}

		auto levelBytes = [width](std::uint32_t level)
		{
			const std::uint32_t dimension = std::max(1u, width >> level);
			return static_cast<std::size_t>(dimension) * dimension * 4;
		};
		std::size_t faceStride = 0;
		for(std::uint32_t level = 0; level < mips; ++level)
		{
			faceStride += levelBytes(level);
		}
		const std::size_t bodyOffset = 4 + 124;
		if(size - bodyOffset < faceStride * 6)
		{
			return false;
		}
		DdsCubemap decoded;
		decoded.size = static_cast<int>(width);
		decoded.mips = static_cast<int>(mips);
		decoded.faces.resize(6);
		for(int face = 0; face < 6; ++face)
		{
			std::size_t offset = bodyOffset + face * faceStride;
			for(std::uint32_t level = 0; level < mips; ++level)
			{
				const std::size_t count = levelBytes(level);
				std::vector<unsigned char> rgba(count);
				for(std::size_t pixel = 0; pixel < count; pixel += 4)
				{
					rgba[pixel] = data[offset + pixel + redIndex];
					rgba[pixel + 1] = data[offset + pixel + greenIndex];
					rgba[pixel + 2] = data[offset + pixel + blueIndex];
					rgba[pixel + 3] = (alphaIndex >= 0)
						? data[offset + pixel + alphaIndex] : 255;
				}
				decoded.faces[static_cast<std::size_t>(face)].push_back(rgba);
				offset += count;
			}
		}
		out = decoded;
		return true;
	}
	//---------------------------------------------------------
	bool cubemapHasAlpha(DdsCubemap const & cube)
	{
		for(std::vector<std::vector<unsigned char> > const & face : cube.faces)
		{
			for(std::vector<unsigned char> const & level : face)
			{
				for(std::size_t index = 3; index < level.size(); index += 4)
				{
					if(level[index] != 255)
					{
						return true;
					}
				}
			}
		}
		return false;
	}
	//---------------------------------------------------------
	namespace
	{
		//! the sidecar travels with the texture (the documented keep-the-id
		//! rule), so a cooked PROJECT directory keeps resolving its ids to the
		//! compressed file. A packaged payload sheds its sidecars right after
		//! the cook (@see bakeTextureSamplers) - there the cook's own reads are
		//! the last ones.
		void moveSidecar(Orkige::String const & sourcePath,
			Orkige::String const & outPath)
		{
			const Orkige::String metaExtension =
				Orkige::AssetDatabase::META_FILE_EXTENSION;
			const Orkige::String from = sourcePath + metaExtension;
			const Orkige::String to = outPath + metaExtension;
			if(from == to || !ExportFiles::isRegularFile(from))
			{
				return;
			}
			if(ExportFiles::copyFile(from, to, 0))
			{
				ExportFiles::removeTree(from, 0);
			}
		}
		//---------------------------------------------------------
		//! cook one payload PNG in place; @p report receives a short line when
		//! anything changed
		bool cookOneTexture(Orkige::String const & pngPath,
			Orkige::TextureImportSettings const & settings,
			Orkige::String const & platform, Orkige::String const & flavor,
			std::function<void(Orkige::String const &)> const & warn,
			bool & changed, Orkige::String & note, Orkige::String * error)
		{
			changed = false;
			ExportImage image;
			if(!decodeImageFile(pngPath, image, error))
			{
				return false;
			}
			const int originalWidth = image.width;
			const int originalHeight = image.height;
			int targetWidth = 0;
			int targetHeight = 0;
			fitWithin(image.width, image.height, settings.maxSize, targetWidth,
				targetHeight);
			std::vector<Orkige::String> steps;
			if(targetWidth != originalWidth || targetHeight != originalHeight)
			{
				image = downscaleImage(image, targetWidth, targetHeight);
				steps.push_back("resized " + std::to_string(originalWidth) +
					"x" + std::to_string(originalHeight) + "->" +
					std::to_string(targetWidth) + "x" +
					std::to_string(targetHeight));
			}
			if(settings.premultiply)
			{
				premultiplyImage(image);
				steps.push_back("premultiplied");
			}
			TextureCookTarget target;
			if(!resolveTextureFormat(settings, platform, flavor,
				imageHasAlpha(image), target, warn, error))
			{
				return false;
			}
			if(!target.format.empty())
			{
				const std::vector<ExportImage> levels =
					buildMipLevels(image, settings.generateMips);
				TextureLevels rgbaLevels;
				rgbaLevels.reserve(levels.size());
				for(ExportImage const & level : levels)
				{
					rgbaLevels.push_back({ level.pixels });
				}
				std::vector<unsigned char> encoded;
				std::string encodeError;
				if(!TextureEncode::encodeToContainer(target.format,
					settings.quality, image.width, image.height, 1, rgbaLevels,
					target.container, encoded, &encodeError))
				{
					return report(error, "cannot encode '" +
						ExportFiles::stem(pngPath) + ".png': " + encodeError);
				}
				const Orkige::String outPath =
					ExportFiles::replaceExtension(pngPath, target.container);
				if(!ExportFiles::writeBytes(outPath, encoded, error))
				{
					return false;
				}
				ExportFiles::removeTree(pngPath, 0);
				moveSidecar(pngPath, outPath);
				steps.push_back(target.format + " -> " +
					ExportFiles::fileName(outPath));
			}
			else if(!steps.empty())
			{
				if(!encodePngFile(image, pngPath, error))
				{
					return false;
				}
			}
			if(steps.empty())
			{
				return true;
			}
			note.clear();
			for(std::size_t index = 0; index < steps.size(); ++index)
			{
				note += (index == 0 ? "" : ", ") + steps[index];
			}
			changed = true;
			return true;
		}
		//---------------------------------------------------------
		//! block-compress one cubemap .dds per its resolved format. Only the
		//! format/quality settings apply - a cubemap's size and prefiltered
		//! mip chain are AUTHORED, so maxSize/premultiply/generateMips are
		//! ignored here.
		bool cookOneCubemap(Orkige::String const & ddsPath,
			Orkige::TextureImportSettings const & settings,
			Orkige::String const & platform, Orkige::String const & flavor,
			std::function<void(Orkige::String const &)> const & warn,
			bool & changed, Orkige::String & note, Orkige::String * error)
		{
			changed = false;
			DdsCubemap cube;
			if(!decodeDdsCubemap(ddsPath, cube))
			{
				// not an uncompressed cubemap this cook re-encodes - it is
				// final artwork and ships verbatim
				return true;
			}
			TextureCookTarget target;
			if(!resolveTextureFormat(settings, platform, flavor,
				cubemapHasAlpha(cube), target, warn, error))
			{
				return false;
			}
			if(target.format.empty())
			{
				return true;	// auto/none on this platform ships the .dds
			}
			// the encoder indexes [level][face]; the decode is [face][level]
			TextureLevels rgbaLevels(static_cast<std::size_t>(cube.mips));
			for(int level = 0; level < cube.mips; ++level)
			{
				for(int face = 0; face < 6; ++face)
				{
					rgbaLevels[static_cast<std::size_t>(level)].push_back(
						cube.faces[static_cast<std::size_t>(face)]
							[static_cast<std::size_t>(level)]);
				}
			}
			std::vector<unsigned char> encoded;
			std::string encodeError;
			if(!TextureEncode::encodeToContainer(target.format,
				settings.quality, cube.size, cube.size, 6, rgbaLevels,
				target.container, encoded, &encodeError))
			{
				return report(error, "cannot encode the cubemap '" +
					ExportFiles::stem(ddsPath) + ".dds': " + encodeError);
			}
			const Orkige::String outPath =
				ExportFiles::replaceExtension(ddsPath, target.container);
			// write to a temp then move into place - the BC container reuses
			// the source's own .dds name, so a direct write would clobber the
			// input mid-encode
			const Orkige::String tempPath = outPath + ".cooking";
			if(!ExportFiles::writeBytes(tempPath, encoded, error))
			{
				return false;
			}
			if(outPath != ddsPath)
			{
				ExportFiles::removeTree(ddsPath, 0);
			}
			if(!ExportFiles::copyFile(tempPath, outPath, error))
			{
				return false;
			}
			ExportFiles::removeTree(tempPath, 0);
			moveSidecar(ddsPath, outPath);
			note = target.format + " -> " +
				ExportFiles::fileName(outPath) +
				" (cubemap, 6 faces, " + std::to_string(cube.mips) + " mips)";
			changed = true;
			return true;
		}
	}
	//---------------------------------------------------------
	bool cookTexturePayload(Orkige::String const & payloadDirectory,
		Orkige::String const & platform, Orkige::String const & flavor,
		TextureCookResult & out,
		std::function<void(Orkige::String const &)> const & log,
		Orkige::String * error)
	{
		out = TextureCookResult();
		const Orkige::String metaExtension =
			Orkige::AssetDatabase::META_FILE_EXTENSION;
		// a sorted walk, so a cook reports in the same order on every host
		const std::vector<Orkige::String> files =
			ExportFiles::listFilesRecursive(payloadDirectory);
		for(Orkige::String const & relative : files)
		{
			Orkige::String lowered = relative;
			std::transform(lowered.begin(), lowered.end(), lowered.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			const bool isPng = lowered.size() > 4 &&
				lowered.compare(lowered.size() - 4, 4, ".png") == 0;
			const bool isDds = lowered.size() > 4 &&
				lowered.compare(lowered.size() - 4, 4, ".dds") == 0;
			if(!isPng && !isDds)
			{
				continue;
			}
			const Orkige::String sourcePath =
				ExportFiles::join(payloadDirectory, relative);
			const Orkige::String metaPath = sourcePath + metaExtension;
			if(!ExportFiles::isRegularFile(metaPath))
			{
				continue;	// no sidecar at all: no import intent, ships as is
			}
			Orkige::String assetId;
			if(!Orkige::AssetDatabase::readMetaFile(metaPath, assetId))
			{
				continue;	// an unreadable sidecar carries no intent either
			}
			// an id-only sidecar resolves to the DEFAULT settings (every
			// id-tracked texture ships with format "auto")
			Orkige::TextureImport import;
			Orkige::AssetDatabase::readImportSettings(metaPath, import);
			Orkige::TextureImportSettings const & settings =
				import.resolvedFor(platform);

			bool changed = false;
			Orkige::String note;
			const bool ok = isPng
				? cookOneTexture(sourcePath, settings, platform, flavor, log,
					changed, note, error)
				: cookOneCubemap(sourcePath, settings, platform, flavor, log,
					changed, note, error);
			if(!ok)
			{
				return false;
			}
			if(changed)
			{
				++out.cooked;
				emit(log, "cooked " +
					ExportFiles::fileName(relative) +
					" (" + note + ")");
			}
		}
		return true;
	}
}
