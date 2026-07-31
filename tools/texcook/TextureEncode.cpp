/********************************************************************
	created:	Friday 2026/07/31 at 14:00
	filename: 	TextureEncode.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "TextureEncode.h"

#include <ktx.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace OrkigeExport
{
	namespace
	{
		constexpr std::uint32_t fourCC(char a, char b, char c, char d)
		{
			return std::uint32_t(std::uint8_t(a)) |
				(std::uint32_t(std::uint8_t(b)) << 8) |
				(std::uint32_t(std::uint8_t(c)) << 16) |
				(std::uint32_t(std::uint8_t(d)) << 24);
		}

		constexpr std::uint32_t GL_RGB_FORMAT = 0x1907;
		constexpr std::uint32_t GL_RGBA_FORMAT = 0x1908;

		const TextureFormatInfo FORMAT_TABLE[] =
		{
			// token       bw bh bytes astc  glInternal  glBase          oitd  ddsFourCC                dxgi
			{ "bc1",        4, 4,  8, false, 0,          0,               55,  fourCC('D','X','T','1'),  0 },
			{ "bc3",        4, 4, 16, false, 0,          0,               59,  fourCC('D','X','T','5'),  0 },
			{ "bc7",        4, 4, 16, false, 0,          0,               74,  0,                       98 },
			// ETC2 RGB8 0x9274 / RGBA8 EAC 0x9278 (the KTX1 GL internal formats)
			{ "etc2-rgb",   4, 4,  8, false, 0x9274,     GL_RGB_FORMAT,  113,  0,                        0 },
			{ "etc2-rgba",  4, 4, 16, false, 0x9278,     GL_RGBA_FORMAT, 115,  0,                        0 },
			// ASTC LDR: 4x4 0x93B0, 6x6 0x93B4, 8x8 0x93B7
			{ "astc-4x4",   4, 4, 16, true,  0x93B0,     GL_RGBA_FORMAT, 126,  0,                        0 },
			{ "astc-6x6",   6, 6, 16, true,  0x93B4,     GL_RGBA_FORMAT, 130,  0,                        0 },
			{ "astc-8x8",   8, 8, 16, true,  0x93B7,     GL_RGBA_FORMAT, 133,  0,                        0 },
			{ 0,            0, 0,  0, false, 0,          0,                0,  0,                        0 },
		};

		bool report(std::string * error, std::string const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void appendU32(std::vector<unsigned char> & out, std::uint32_t value)
		{
			out.push_back(static_cast<unsigned char>(value));
			out.push_back(static_cast<unsigned char>(value >> 8));
			out.push_back(static_cast<unsigned char>(value >> 16));
			out.push_back(static_cast<unsigned char>(value >> 24));
		}
		//---------------------------------------------------------
		//! .dds: the legacy 124-byte header, plus the DX10 extension when the
		//! format has no legacy fourCC (BC7). A six-face payload writes the
		//! cubemap caps and stores the blocks FACE-major (each face's whole mip
		//! chain, in +X,-X,+Y,-Y,+Z,-Z order - the DDS cubemap layout both
		//! flavors' DDS codecs read).
		std::vector<unsigned char> buildDds(TextureFormatInfo const & info,
			int width, int height, TextureLevels const & levels)
		{
			const std::uint32_t mipCount =
				static_cast<std::uint32_t>(levels.size());
			const std::size_t faces = levels[0].size();
			std::vector<unsigned char> out;
			appendU32(out, fourCC('D', 'D', 'S', ' '));
			appendU32(out, 124);							// header size
			// CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
			std::uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000;
			if(mipCount > 1)
			{
				flags |= 0x20000;							// MIPMAPCOUNT
			}
			appendU32(out, flags);
			appendU32(out, static_cast<std::uint32_t>(height));
			appendU32(out, static_cast<std::uint32_t>(width));
			appendU32(out, static_cast<std::uint32_t>(levels[0][0].size()));
			appendU32(out, 0);								// depth
			appendU32(out, mipCount > 1 ? mipCount : 0);
			for(int index = 0; index < 11; ++index)
			{
				appendU32(out, 0);							// reserved
			}
			appendU32(out, 32);								// pixel-format size
			appendU32(out, 0x4);							// DDPF_FOURCC
			appendU32(out, info.ddsFourCC != 0 ? info.ddsFourCC
				: fourCC('D', 'X', '1', '0'));
			for(int index = 0; index < 5; ++index)
			{
				appendU32(out, 0);							// bit count + masks
			}
			std::uint32_t caps1 = 0x1000;					// TEXTURE
			if(mipCount > 1)
			{
				caps1 |= 0x8 | 0x400000;					// COMPLEX | MIPMAP
			}
			std::uint32_t caps2 = 0;
			if(faces == 6)
			{
				caps1 |= 0x8;								// COMPLEX
				caps2 = 0xFE00;								// CUBEMAP + six faces
			}
			appendU32(out, caps1);
			appendU32(out, caps2);
			appendU32(out, 0);								// caps3
			appendU32(out, 0);								// caps4
			appendU32(out, 0);								// reserved
			if(info.ddsFourCC == 0)
			{
				appendU32(out, info.dxgiFormat);			// DX10 extension
				// TEXTURE2D (a cube is a TEXTURE2D array of 6 with the CUBEMAP
				// misc flag below)
				appendU32(out, 3u);
				appendU32(out, faces == 6 ? 0x4u : 0u);		// misc: TEXTURECUBE
				appendU32(out, faces == 6 ? 6u : 1u);		// array size
				appendU32(out, 0);							// misc2
			}
			// FACE-major storage: face 0's whole mip chain, then face 1's, ...
			for(std::size_t face = 0; face < faces; ++face)
			{
				for(std::vector<std::vector<unsigned char> > const & level :
					levels)
				{
					out.insert(out.end(), level[face].begin(),
						level[face].end());
				}
			}
			return out;
		}
		//---------------------------------------------------------
		//! .ktx: KTX1 - what the classic flavor's compressed-texture codec
		//! reads (it maps the ETC2 and ASTC glInternalFormat values). A
		//! six-face payload sets numberOfFaces to 6 and, per the KTX1 layout,
		//! writes one imageSize (a SINGLE face's level size, the
		//! non-array-cubemap rule) then each face's blocks for that mip.
		std::vector<unsigned char> buildKtx1(TextureFormatInfo const & info,
			int width, int height, TextureLevels const & levels)
		{
			const std::size_t faces = levels[0].size();
			static const unsigned char IDENTIFIER[12] = { 0xAB, 'K', 'T', 'X',
				' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n' };
			std::vector<unsigned char> out(IDENTIFIER, IDENTIFIER + 12);
			appendU32(out, 0x04030201);						// endianness
			appendU32(out, 0);								// glType (compressed)
			appendU32(out, 1);								// glTypeSize
			appendU32(out, 0);								// glFormat (compressed)
			appendU32(out, info.glInternalFormat);
			appendU32(out, info.glBaseInternalFormat);
			appendU32(out, static_cast<std::uint32_t>(width));
			appendU32(out, static_cast<std::uint32_t>(height));
			appendU32(out, 0);								// pixelDepth (2D)
			appendU32(out, 0);								// arrayElements
			appendU32(out, static_cast<std::uint32_t>(faces));
			appendU32(out, static_cast<std::uint32_t>(levels.size()));
			appendU32(out, 0);								// key/value bytes
			for(std::vector<std::vector<unsigned char> > const & level : levels)
			{
				// imageSize is ONE face's level size
				appendU32(out,
					static_cast<std::uint32_t>(level[0].size()));
				for(std::vector<unsigned char> const & face : level)
				{
					out.insert(out.end(), face.begin(), face.end());
					while((out.size() % 4) != 0)			// cube/mip padding
					{
						out.push_back(0);
					}
				}
			}
			return out;
		}
		//---------------------------------------------------------
		//! .oitd: the Ogre-Next native container - a packed 17-byte header
		//! (magic, dimensions, mip count, texture type, PixelFormatGpu value,
		//! version 1) over the tightly packed payloads. A six-face payload sets
		//! TypeCube + depthOrSlices 6 and stores the blocks MIP-major with the
		//! six faces contiguous within each mip - the Image2/TextureBox slice
		//! layout its OITD codec bulk-reads.
		std::vector<unsigned char> buildOitd(TextureFormatInfo const & info,
			int width, int height, TextureLevels const & levels)
		{
			const std::size_t faces = levels[0].size();
			std::vector<unsigned char> out;
			appendU32(out, fourCC('O', 'I', 'T', 'D'));
			appendU32(out, static_cast<std::uint32_t>(width));
			appendU32(out, static_cast<std::uint32_t>(height));
			appendU32(out, faces == 6 ? 6u : 1u);			// depthOrSlices
			out.push_back(static_cast<unsigned char>(levels.size()));
			out.push_back(faces == 6 ? 5 : 3);				// TypeCube : Type2D
			out.push_back(static_cast<unsigned char>(info.oitdPixelFormat));
			out.push_back(
				static_cast<unsigned char>(info.oitdPixelFormat >> 8));
			out.push_back(1);								// version
			// MIP-major, faces contiguous within each mip: for compressed
			// formats a block row is already a multiple of 4 bytes, so version
			// 1's 4-byte row alignment needs no extra padding
			for(std::vector<std::vector<unsigned char> > const & level : levels)
			{
				for(std::vector<unsigned char> const & face : level)
				{
					out.insert(out.end(), face.begin(), face.end());
				}
			}
			return out;
		}
	}
	//---------------------------------------------------------
	TextureFormatInfo const * TextureEncode::formats()
	{
		return FORMAT_TABLE;
	}
	//---------------------------------------------------------
	int TextureEncode::formatCount()
	{
		int count = 0;
		while(FORMAT_TABLE[count].token != 0)
		{
			++count;
		}
		return count;
	}
	//---------------------------------------------------------
	TextureFormatInfo const * TextureEncode::findFormat(
		std::string const & token)
	{
		for(int index = 0; FORMAT_TABLE[index].token != 0; ++index)
		{
			if(token == FORMAT_TABLE[index].token)
			{
				return &FORMAT_TABLE[index];
			}
		}
		return 0;
	}
	//---------------------------------------------------------
	int TextureEncode::levelDimension(int base, int level)
	{
		const int dimension = base >> level;
		return dimension > 0 ? dimension : 1;
	}
	//---------------------------------------------------------
	std::size_t TextureEncode::blockDataSize(TextureFormatInfo const & info,
		int width, int height)
	{
		const std::size_t blocksX = static_cast<std::size_t>(
			(width + info.blockWidth - 1) / info.blockWidth);
		const std::size_t blocksY = static_cast<std::size_t>(
			(height + info.blockHeight - 1) / info.blockHeight);
		return blocksX * blocksY * static_cast<std::size_t>(info.blockBytes);
	}
	//---------------------------------------------------------
	bool TextureEncode::fitsContainer(TextureFormatInfo const & info,
		std::string const & container)
	{
		if(container == "dds")
		{
			return info.ddsFourCC != 0 || info.dxgiFormat != 0;
		}
		if(container == "ktx")
		{
			return info.glInternalFormat != 0;
		}
		return container == "oitd";
	}
	//---------------------------------------------------------
	bool TextureEncode::encodeLevels(TextureFormatInfo const & info,
		std::string const & quality, int width, int height, int faces,
		TextureLevels const & rgbaLevels, TextureLevels & out,
		std::string * error)
	{
		// non-sRGB: the engine samples every content texture raw (gamma-space
		// passthrough on both flavors), so the encoders must not apply a
		// transfer curve
		constexpr std::uint32_t VK_FORMAT_R8G8B8A8_UNORM = 37;

		ktxTextureCreateInfo createInfo = {};
		createInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
		createInfo.baseWidth = static_cast<std::uint32_t>(width);
		createInfo.baseHeight = static_cast<std::uint32_t>(height);
		createInfo.baseDepth = 1;
		createInfo.numDimensions = 2;
		createInfo.numLevels = static_cast<std::uint32_t>(rgbaLevels.size());
		createInfo.numLayers = 1;
		createInfo.numFaces = static_cast<std::uint32_t>(faces);
		createInfo.isArray = KTX_FALSE;
		createInfo.generateMipmaps = KTX_FALSE;

		ktxTexture2 * texture = 0;
		ktx_error_code_e result = ktxTexture2_Create(&createInfo,
			KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
		if(result != KTX_SUCCESS)
		{
			return report(error, std::string(
				"could not create the working texture: ") +
				ktxErrorString(result));
		}
		// one exit point past this line frees the texture
		struct Guard
		{
			ktxTexture2 * texture;
			~Guard() { ktxTexture_Destroy(ktxTexture(this->texture)); }
		} guard{ texture };

		for(std::size_t level = 0; level < rgbaLevels.size(); ++level)
		{
			for(int face = 0; face < faces; ++face)
			{
				result = ktxTexture_SetImageFromMemory(ktxTexture(texture),
					static_cast<ktx_uint32_t>(level), 0,
					static_cast<ktx_uint32_t>(face),
					rgbaLevels[level][static_cast<std::size_t>(face)].data(),
					rgbaLevels[level][static_cast<std::size_t>(face)].size());
				if(result != KTX_SUCCESS)
				{
					return report(error, "could not stage level " +
						std::to_string(level) + " face " +
						std::to_string(face) + ": " + ktxErrorString(result));
				}
			}
		}

		const std::uint32_t threads = std::thread::hardware_concurrency() != 0
			? std::thread::hardware_concurrency() : 1u;
		if(info.isAstc)
		{
			// direct ASTC encode (the vendored encoder handles every block
			// size); quality maps to encoder effort
			ktxAstcParams params = {};
			params.structSize = sizeof(params);
			params.threadCount = threads;
			params.blockDimension =
				info.blockWidth == 4 ? KTX_PACK_ASTC_BLOCK_DIMENSION_4x4 :
				info.blockWidth == 6 ? KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 :
				KTX_PACK_ASTC_BLOCK_DIMENSION_8x8;
			params.mode = KTX_PACK_ASTC_ENCODER_MODE_LDR;
			params.qualityLevel =
				quality == "low" ? KTX_PACK_ASTC_QUALITY_LEVEL_FAST :
				quality == "high" ? KTX_PACK_ASTC_QUALITY_LEVEL_THOROUGH :
				KTX_PACK_ASTC_QUALITY_LEVEL_MEDIUM;
			params.normalMap = KTX_FALSE;
			result = ktxTexture2_CompressAstcEx(texture, &params);
			if(result != KTX_SUCCESS)
			{
				return report(error, std::string("ASTC encode failed: ") +
					ktxErrorString(result));
			}
		}
		else
		{
			// ETC2/BCn: encode to the universal intermediate, then transcode
			// to the target blocks; quality maps to intermediate effort
			ktxBasisParams params = {};
			params.structSize = sizeof(params);
			params.threadCount = threads;
			params.uastc = KTX_TRUE;
			params.uastcFlags =
				quality == "low" ? KTX_PACK_UASTC_LEVEL_FASTER :
				quality == "high" ? KTX_PACK_UASTC_LEVEL_SLOWER :
				KTX_PACK_UASTC_LEVEL_DEFAULT;
			result = ktxTexture2_CompressBasisEx(texture, &params);
			if(result != KTX_SUCCESS)
			{
				return report(error,
					std::string("intermediate encode failed: ") +
					ktxErrorString(result));
			}
			const std::string token = info.token;
			const ktx_transcode_fmt_e target =
				token == "bc1" ? KTX_TTF_BC1_RGB :
				token == "bc3" ? KTX_TTF_BC3_RGBA :
				token == "bc7" ? KTX_TTF_BC7_RGBA :
				// ETC1 blocks are valid ETC2-RGB8 payloads - the opaque
				// transcode target for the etc2-rgb container format
				token == "etc2-rgb" ? KTX_TTF_ETC1_RGB :
				KTX_TTF_ETC2_RGBA;
			result = ktxTexture2_TranscodeBasis(texture, target, 0);
			if(result != KTX_SUCCESS)
			{
				return report(error, std::string("transcode failed: ") +
					ktxErrorString(result));
			}
		}

		TextureLevels encoded;
		encoded.reserve(rgbaLevels.size());
		const std::uint8_t * base = ktxTexture_GetData(ktxTexture(texture));
		for(std::size_t level = 0; level < rgbaLevels.size(); ++level)
		{
			// one image (face) size per level - identical across a cubemap's
			// faces (square faces, same format), so GetImageSize suffices
			const ktx_size_t size = ktxTexture_GetImageSize(
				ktxTexture(texture), static_cast<ktx_uint32_t>(level));
			const std::size_t expected = TextureEncode::blockDataSize(info,
				TextureEncode::levelDimension(width, static_cast<int>(level)),
				TextureEncode::levelDimension(height,
					static_cast<int>(level)));
			if(static_cast<std::size_t>(size) != expected)
			{
				return report(error, "encoded level " + std::to_string(level) +
					" size mismatch (got " +
					std::to_string(static_cast<std::size_t>(size)) +
					", expected " + std::to_string(expected) + ")");
			}
			std::vector<std::vector<unsigned char> > perFace;
			perFace.reserve(static_cast<std::size_t>(faces));
			for(int face = 0; face < faces; ++face)
			{
				ktx_size_t offset = 0;
				result = ktxTexture_GetImageOffset(ktxTexture(texture),
					static_cast<ktx_uint32_t>(level), 0,
					static_cast<ktx_uint32_t>(face), &offset);
				if(result != KTX_SUCCESS)
				{
					return report(error, "could not locate encoded level " +
						std::to_string(level) + " face " +
						std::to_string(face));
				}
				perFace.emplace_back(base + offset, base + offset + size);
			}
			encoded.push_back(perFace);
		}
		out.swap(encoded);
		return true;
	}
	//---------------------------------------------------------
	bool TextureEncode::buildContainer(std::string const & container,
		TextureFormatInfo const & info, int width, int height,
		TextureLevels const & levels, std::vector<unsigned char> & out,
		std::string * error)
	{
		if(levels.empty() || levels[0].empty())
		{
			return report(error, "nothing to write: no encoded levels");
		}
		if(container == "dds")
		{
			out = buildDds(info, width, height, levels);
			return true;
		}
		if(container == "ktx")
		{
			out = buildKtx1(info, width, height, levels);
			return true;
		}
		if(container == "oitd")
		{
			out = buildOitd(info, width, height, levels);
			return true;
		}
		return report(error, "unknown container '" + container + "'");
	}
	//---------------------------------------------------------
	bool TextureEncode::validate(std::string const & formatToken,
		std::string const & quality, int width, int height, int levelCount,
		int faces, std::string const & container, std::string * error)
	{
		TextureFormatInfo const * info = TextureEncode::findFormat(formatToken);
		if(info == 0)
		{
			return report(error, "unknown texture format '" + formatToken +
				"'");
		}
		if(!TextureEncode::fitsContainer(*info, container))
		{
			return report(error, "format '" + formatToken +
				"' cannot ship in a ." + container);
		}
		if(quality != "low" && quality != "normal" && quality != "high")
		{
			return report(error, "unknown quality '" + quality + "'");
		}
		if(width <= 0 || height <= 0 || levelCount <= 0)
		{
			return report(error,
				"width/height/levels must all be positive");
		}
		if(faces != 1 && faces != 6)
		{
			return report(error, "faces must be 1 (2D) or 6 (cubemap)");
		}
		if(faces == 6 && width != height)
		{
			return report(error, "a cubemap must have square faces");
		}
		return true;
	}
	//---------------------------------------------------------
	bool TextureEncode::encodeToContainer(std::string const & formatToken,
		std::string const & quality, int width, int height, int faces,
		TextureLevels const & rgbaLevels, std::string const & container,
		std::vector<unsigned char> & out, std::string * error)
	{
		if(!TextureEncode::validate(formatToken, quality, width, height,
			static_cast<int>(rgbaLevels.size()), faces, container, error))
		{
			return false;
		}
		TextureFormatInfo const * info = TextureEncode::findFormat(formatToken);
		TextureLevels encoded;
		if(!TextureEncode::encodeLevels(*info, quality, width, height, faces,
			rgbaLevels, encoded, error))
		{
			return false;
		}
		return TextureEncode::buildContainer(container, *info, width, height,
			encoded, out, error);
	}
	//---------------------------------------------------------
	bool TextureEncode::takeRgbaLevels(unsigned char const * data,
		std::size_t size, int width, int height, int levelCount, int faces,
		TextureLevels & out, std::string * error)
	{
		const std::vector<std::vector<unsigned char> > emptyFaces(
			static_cast<std::size_t>(faces));
		TextureLevels levels(static_cast<std::size_t>(levelCount), emptyFaces);
		std::size_t offset = 0;
		// on the wire the levels are FACE-major (each face's whole mip chain);
		// the encoder wants them indexed [level][face], so transpose here
		for(int face = 0; face < faces; ++face)
		{
			for(int level = 0; level < levelCount; ++level)
			{
				const std::size_t bytes =
					static_cast<std::size_t>(
						TextureEncode::levelDimension(width, level)) *
					static_cast<std::size_t>(
						TextureEncode::levelDimension(height, level)) * 4;
				if(offset + bytes > size)
				{
					return report(error, "the raw level data is short at face "
						+ std::to_string(face) + " level " +
						std::to_string(level));
				}
				levels[static_cast<std::size_t>(level)]
					[static_cast<std::size_t>(face)].assign(data + offset,
						data + offset + bytes);
				offset += bytes;
			}
		}
		out.swap(levels);
		return true;
	}
	//---------------------------------------------------------
	bool TextureEncode::readRgbaLevels(std::string const & path, int width,
		int height, int levelCount, int faces, TextureLevels & out,
		std::string * error)
	{
		std::ifstream input(path.c_str(), std::ios::binary);
		if(!input)
		{
			return report(error, "could not open the raw level data '" + path +
				"'");
		}
		const std::vector<unsigned char> data(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		return TextureEncode::takeRgbaLevels(data.data(), data.size(), width,
			height, levelCount, faces, out, error);
	}
}
