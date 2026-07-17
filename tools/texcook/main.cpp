/**************************************************************
	created:	2026/07/17 at 09:00
	filename: 	main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//! @brief texcook - the export-time GPU texture encoder.
//!
//! A small host CLI the export cook (Util/cook_textures.py) shells out to.
//! Input is RAW RGBA8 level data (the Python cook owns PNG decode, downscale,
//! premultiply and the mip-chain downsamples); output is one block-compressed
//! texture in a container the target runtime already loads:
//!
//!   * .dds   BC1/BC3/BC7  - both render flavors register a DDS codec
//!   * .ktx   ETC2/ASTC    - KTX1, the classic flavor's compressed loader
//!   * .oitd  ETC2/ASTC    - Ogre-Next's native container (any GPU format)
//!
//! Encoding rides libktx alone: the vendored ASTC encoder handles every block
//! size directly, and the vendored universal encoder/transcoder pair yields
//! ETC2 and BCn blocks (RGBA -> intermediate -> transcode). The containers are
//! written by hand below - they are fixed headers over the raw block payload,
//! and owning the writers keeps the tool independent of any renderer headers.
//!
//!   texcook --input <levels.rgba> --output <file> --width W --height H
//!           --levels N --format <fmt> --quality low|normal|high
//!           --container dds|ktx|oitd
//!   texcook --selftest
//!
//! The input file is the concatenation of all mip levels' RGBA8 pixels, level
//! i sized max(1, W>>i) x max(1, H>>i). Formats: bc1 bc3 bc7 etc2-rgb
//! etc2-rgba astc-4x4 astc-6x6 astc-8x8. Exit 0 on success, 1 with a message
//! on stderr otherwise - the cook treats any failure as a refused export.

#include <ktx.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	//---------------------------------------------------------
	// the format vocabulary (mirrors the sidecar's explicit format tokens)
	//---------------------------------------------------------
	enum class BlockFormat
	{
		BC1, BC3, BC7, ETC2_RGB, ETC2_RGBA, ASTC_4x4, ASTC_6x6, ASTC_8x8
	};

	struct FormatInfo
	{
		const char*	token;			//!< CLI/sidecar spelling
		BlockFormat	format;
		int			blockWidth;		//!< texel block footprint
		int			blockHeight;
		int			blockBytes;		//!< bytes per encoded block
		bool		isAstc;			//!< direct ASTC encode (vs transcode)
		//! KTX1 glInternalFormat (0 = format never ships in a .ktx)
		uint32_t	glInternalFormat;
		//! KTX1 glBaseInternalFormat (GL_RGB/GL_RGBA)
		uint32_t	glBaseInternalFormat;
		//! Ogre-Next PixelFormatGpu value for the .oitd header - PINNED to the
		//! ports/ogre-next checkout (REF ef2e8f35c3ac929b06f67c76cbc80c5577016b30,
		//! OgrePixelFormatGpu.h); the player_cooked_textures ctest loads a
		//! cooked .oitd through the real runtime, so an enum drift on a port
		//! upgrade fails a test instead of shipping garbage.
		uint16_t	oitdPixelFormat;
		//! DDS fourCC ("DXT1"/"DXT5"; 0 = needs the DX10 extension header)
		uint32_t	ddsFourCC;
		//! DXGI format for the DX10 header path (BC7)
		uint32_t	dxgiFormat;
	};

	constexpr uint32_t fourCC(char a, char b, char c, char d)
	{
		return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
			(uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
	}

	constexpr uint32_t GL_RGB = 0x1907;
	constexpr uint32_t GL_RGBA = 0x1908;

	const FormatInfo FORMATS[] =
	{
		// token        format                 bw bh bytes astc  glInternal glBase   oitd  ddsFourCC              dxgi
		{ "bc1",        BlockFormat::BC1,       4, 4,  8, false, 0,         0,        55,  fourCC('D','X','T','1'), 0 },
		{ "bc3",        BlockFormat::BC3,       4, 4, 16, false, 0,         0,        59,  fourCC('D','X','T','5'), 0 },
		{ "bc7",        BlockFormat::BC7,       4, 4, 16, false, 0,         0,        74,  0,                      98 },
		// ETC2 RGB8 0x9274 / RGBA8 EAC 0x9278 (the KTX1 GL internal formats)
		{ "etc2-rgb",   BlockFormat::ETC2_RGB,  4, 4,  8, false, 0x9274,    GL_RGB,  113,  0,                       0 },
		{ "etc2-rgba",  BlockFormat::ETC2_RGBA, 4, 4, 16, false, 0x9278,    GL_RGBA, 115,  0,                       0 },
		// ASTC LDR: 4x4 0x93B0, 6x6 0x93B4, 8x8 0x93B7
		{ "astc-4x4",   BlockFormat::ASTC_4x4,  4, 4, 16, true,  0x93B0,    GL_RGBA, 126,  0,                       0 },
		{ "astc-6x6",   BlockFormat::ASTC_6x6,  6, 6, 16, true,  0x93B4,    GL_RGBA, 130,  0,                       0 },
		{ "astc-8x8",   BlockFormat::ASTC_8x8,  8, 8, 16, true,  0x93B7,    GL_RGBA, 133,  0,                       0 },
	};

	const FormatInfo* findFormat(std::string const& token)
	{
		for (FormatInfo const& info : FORMATS)
		{
			if (token == info.token)
			{
				return &info;
			}
		}
		return nullptr;
	}

	int levelDimension(int base, int level)
	{
		const int dimension = base >> level;
		return dimension > 0 ? dimension : 1;
	}

	size_t blockDataSize(FormatInfo const& info, int width, int height)
	{
		const size_t blocksX = size_t((width + info.blockWidth - 1) / info.blockWidth);
		const size_t blocksY = size_t((height + info.blockHeight - 1) / info.blockHeight);
		return blocksX * blocksY * size_t(info.blockBytes);
	}

	[[noreturn]] void fail(std::string const& message)
	{
		std::fprintf(stderr, "texcook: %s\n", message.c_str());
		std::exit(1);
	}

	//---------------------------------------------------------
	// encoding (libktx): RGBA levels in, per-level block payloads out
	//---------------------------------------------------------
	//! encode the RGBA8 mip levels to the requested block format; returns one
	//! byte vector per level (base first)
	std::vector<std::vector<uint8_t>> encodeLevels(FormatInfo const& info,
		std::string const& quality, int width, int height,
		std::vector<std::vector<uint8_t>> const& rgbaLevels)
	{
		constexpr uint32_t VK_FORMAT_R8G8B8A8_UNORM = 37;	// non-sRGB: the
		// engine samples every content texture raw (gamma-space passthrough
		// on both flavors), so the encoders must not apply transfer curves

		ktxTextureCreateInfo createInfo = {};
		createInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
		createInfo.baseWidth = uint32_t(width);
		createInfo.baseHeight = uint32_t(height);
		createInfo.baseDepth = 1;
		createInfo.numDimensions = 2;
		createInfo.numLevels = uint32_t(rgbaLevels.size());
		createInfo.numLayers = 1;
		createInfo.numFaces = 1;
		createInfo.isArray = KTX_FALSE;
		createInfo.generateMipmaps = KTX_FALSE;

		ktxTexture2* texture = nullptr;
		ktx_error_code_e result = ktxTexture2_Create(&createInfo,
			KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
		if (result != KTX_SUCCESS)
		{
			fail(std::string("could not create the working texture: ") +
				ktxErrorString(result));
		}
		for (size_t level = 0; level < rgbaLevels.size(); ++level)
		{
			result = ktxTexture_SetImageFromMemory(ktxTexture(texture),
				ktx_uint32_t(level), 0, 0, rgbaLevels[level].data(),
				rgbaLevels[level].size());
			if (result != KTX_SUCCESS)
			{
				fail("could not stage level " + std::to_string(level) + ": " +
					ktxErrorString(result));
			}
		}

		const uint32_t threads = std::thread::hardware_concurrency()
			? std::thread::hardware_concurrency() : 1u;
		if (info.isAstc)
		{
			// direct ASTC encode (the vendored encoder handles every block
			// size); quality maps to encoder effort
			ktxAstcParams params = {};
			params.structSize = sizeof(params);
			params.threadCount = threads;
			params.blockDimension =
				info.format == BlockFormat::ASTC_4x4 ? KTX_PACK_ASTC_BLOCK_DIMENSION_4x4 :
				info.format == BlockFormat::ASTC_6x6 ? KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 :
				KTX_PACK_ASTC_BLOCK_DIMENSION_8x8;
			params.mode = KTX_PACK_ASTC_ENCODER_MODE_LDR;
			params.qualityLevel =
				quality == "low" ? KTX_PACK_ASTC_QUALITY_LEVEL_FAST :
				quality == "high" ? KTX_PACK_ASTC_QUALITY_LEVEL_THOROUGH :
				KTX_PACK_ASTC_QUALITY_LEVEL_MEDIUM;
			params.normalMap = KTX_FALSE;
			result = ktxTexture2_CompressAstcEx(texture, &params);
			if (result != KTX_SUCCESS)
			{
				fail(std::string("ASTC encode failed: ") + ktxErrorString(result));
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
			if (result != KTX_SUCCESS)
			{
				fail(std::string("intermediate encode failed: ") +
					ktxErrorString(result));
			}
			const ktx_transcode_fmt_e target =
				info.format == BlockFormat::BC1 ? KTX_TTF_BC1_RGB :
				info.format == BlockFormat::BC3 ? KTX_TTF_BC3_RGBA :
				info.format == BlockFormat::BC7 ? KTX_TTF_BC7_RGBA :
				// ETC1 blocks are valid ETC2-RGB8 payloads - the opaque
				// transcode target for the etc2-rgb container format
				info.format == BlockFormat::ETC2_RGB ? KTX_TTF_ETC1_RGB :
				KTX_TTF_ETC2_RGBA;
			result = ktxTexture2_TranscodeBasis(texture, target, 0);
			if (result != KTX_SUCCESS)
			{
				fail(std::string("transcode failed: ") + ktxErrorString(result));
			}
		}

		std::vector<std::vector<uint8_t>> levels;
		levels.reserve(rgbaLevels.size());
		const uint8_t* base = ktxTexture_GetData(ktxTexture(texture));
		for (size_t level = 0; level < rgbaLevels.size(); ++level)
		{
			ktx_size_t offset = 0;
			result = ktxTexture_GetImageOffset(ktxTexture(texture),
				ktx_uint32_t(level), 0, 0, &offset);
			if (result != KTX_SUCCESS)
			{
				fail("could not locate encoded level " + std::to_string(level));
			}
			const ktx_size_t size =
				ktxTexture_GetImageSize(ktxTexture(texture), ktx_uint32_t(level));
			const size_t expected = blockDataSize(info,
				levelDimension(width, int(level)),
				levelDimension(height, int(level)));
			if (size_t(size) != expected)
			{
				fail("encoded level " + std::to_string(level) +
					" size mismatch (got " + std::to_string(size_t(size)) +
					", expected " + std::to_string(expected) + ")");
			}
			levels.emplace_back(base + offset, base + offset + size);
		}
		ktxTexture_Destroy(ktxTexture(texture));
		return levels;
	}

	//---------------------------------------------------------
	// container writers (fixed headers over the raw block payload)
	//---------------------------------------------------------
	void appendU32(std::vector<uint8_t>& out, uint32_t value)
	{
		out.push_back(uint8_t(value));
		out.push_back(uint8_t(value >> 8));
		out.push_back(uint8_t(value >> 16));
		out.push_back(uint8_t(value >> 24));
	}

	//! .dds: the legacy 124-byte header, plus the DX10 extension when the
	//! format has no legacy fourCC (BC7)
	std::vector<uint8_t> buildDds(FormatInfo const& info, int width, int height,
		std::vector<std::vector<uint8_t>> const& levels)
	{
		const uint32_t mipCount = uint32_t(levels.size());
		std::vector<uint8_t> out;
		appendU32(out, fourCC('D', 'D', 'S', ' '));
		appendU32(out, 124);							// header size
		uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000;	// CAPS|HEIGHT|
		// WIDTH|PIXELFORMAT|LINEARSIZE
		if (mipCount > 1)
		{
			flags |= 0x20000;							// MIPMAPCOUNT
		}
		appendU32(out, flags);
		appendU32(out, uint32_t(height));
		appendU32(out, uint32_t(width));
		appendU32(out, uint32_t(levels[0].size()));		// linear size (level 0)
		appendU32(out, 0);								// depth
		appendU32(out, mipCount > 1 ? mipCount : 0);
		for (int i = 0; i < 11; ++i)
		{
			appendU32(out, 0);							// reserved
		}
		appendU32(out, 32);								// pixel-format size
		appendU32(out, 0x4);							// DDPF_FOURCC
		appendU32(out, info.ddsFourCC ? info.ddsFourCC
			: fourCC('D', 'X', '1', '0'));
		for (int i = 0; i < 5; ++i)
		{
			appendU32(out, 0);							// bit count + masks
		}
		uint32_t caps1 = 0x1000;						// TEXTURE
		if (mipCount > 1)
		{
			caps1 |= 0x8 | 0x400000;					// COMPLEX | MIPMAP
		}
		appendU32(out, caps1);
		appendU32(out, 0);								// caps2
		appendU32(out, 0);								// caps3
		appendU32(out, 0);								// caps4
		appendU32(out, 0);								// reserved
		if (!info.ddsFourCC)
		{
			appendU32(out, info.dxgiFormat);			// DX10 extension
			appendU32(out, 3);							// TEXTURE2D
			appendU32(out, 0);							// misc
			appendU32(out, 1);							// array size
			appendU32(out, 0);							// misc2
		}
		for (std::vector<uint8_t> const& level : levels)
		{
			out.insert(out.end(), level.begin(), level.end());
		}
		return out;
	}

	//! .ktx: KTX1 - what the classic flavor's compressed-texture codec reads
	//! (it maps the ETC2 and ASTC glInternalFormat values)
	std::vector<uint8_t> buildKtx1(FormatInfo const& info, int width, int height,
		std::vector<std::vector<uint8_t>> const& levels)
	{
		static const uint8_t identifier[12] =
			{ 0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n' };
		std::vector<uint8_t> out(identifier, identifier + 12);
		appendU32(out, 0x04030201);						// endianness
		appendU32(out, 0);								// glType (compressed)
		appendU32(out, 1);								// glTypeSize
		appendU32(out, 0);								// glFormat (compressed)
		appendU32(out, info.glInternalFormat);
		appendU32(out, info.glBaseInternalFormat);
		appendU32(out, uint32_t(width));
		appendU32(out, uint32_t(height));
		appendU32(out, 0);								// pixelDepth (2D)
		appendU32(out, 0);								// arrayElements
		appendU32(out, 1);								// faces
		appendU32(out, uint32_t(levels.size()));
		appendU32(out, 0);								// key/value bytes
		for (std::vector<uint8_t> const& level : levels)
		{
			appendU32(out, uint32_t(level.size()));
			out.insert(out.end(), level.begin(), level.end());
			while (out.size() % 4)						// mip padding
			{
				out.push_back(0);
			}
		}
		return out;
	}

	//! .oitd: Ogre-Next's native container - a packed 17-byte header (magic,
	//! dimensions, mip count, texture type, PixelFormatGpu value, version 1)
	//! over the tightly packed level payloads
	std::vector<uint8_t> buildOitd(FormatInfo const& info, int width, int height,
		std::vector<std::vector<uint8_t>> const& levels)
	{
		std::vector<uint8_t> out;
		appendU32(out, fourCC('O', 'I', 'T', 'D'));
		appendU32(out, uint32_t(width));
		appendU32(out, uint32_t(height));
		appendU32(out, 1);								// depthOrSlices
		out.push_back(uint8_t(levels.size()));			// numMipmaps
		out.push_back(3);								// TextureTypes::Type2D
		out.push_back(uint8_t(info.oitdPixelFormat));	// PixelFormatGpu (LE)
		out.push_back(uint8_t(info.oitdPixelFormat >> 8));
		out.push_back(1);								// version
		for (std::vector<uint8_t> const& level : levels)
		{
			out.insert(out.end(), level.begin(), level.end());
		}
		return out;
	}

	//---------------------------------------------------------
	// the cook entry: read levels, encode, write the container
	//---------------------------------------------------------
	int cook(std::string const& inputPath, std::string const& outputPath,
		int width, int height, int levelCount, std::string const& formatToken,
		std::string const& quality, std::string const& container)
	{
		FormatInfo const* info = findFormat(formatToken);
		if (!info)
		{
			fail("unknown --format '" + formatToken + "'");
		}
		if (container == "dds" ? (info->ddsFourCC == 0 && info->dxgiFormat == 0)
			: container == "ktx" ? info->glInternalFormat == 0
			: container != "oitd")
		{
			fail("format '" + formatToken + "' cannot ship in a ." + container);
		}
		if (width <= 0 || height <= 0 || levelCount <= 0)
		{
			fail("--width/--height/--levels must be positive");
		}

		std::ifstream input(inputPath, std::ios::binary);
		if (!input)
		{
			fail("could not open input '" + inputPath + "'");
		}
		std::vector<std::vector<uint8_t>> rgbaLevels;
		for (int level = 0; level < levelCount; ++level)
		{
			const size_t bytes = size_t(levelDimension(width, level)) *
				size_t(levelDimension(height, level)) * 4;
			std::vector<uint8_t> pixels(bytes);
			input.read(reinterpret_cast<char*>(pixels.data()),
				std::streamsize(bytes));
			if (size_t(input.gcount()) != bytes)
			{
				fail("input '" + inputPath + "' is short at level " +
					std::to_string(level));
			}
			rgbaLevels.push_back(std::move(pixels));
		}

		const std::vector<std::vector<uint8_t>> encoded =
			encodeLevels(*info, quality, width, height, rgbaLevels);
		const std::vector<uint8_t> file =
			container == "dds" ? buildDds(*info, width, height, encoded) :
			container == "ktx" ? buildKtx1(*info, width, height, encoded) :
			buildOitd(*info, width, height, encoded);

		std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
		if (!output.write(reinterpret_cast<char const*>(file.data()),
			std::streamsize(file.size())))
		{
			fail("could not write output '" + outputPath + "'");
		}
		return 0;
	}

	//---------------------------------------------------------
	// self-test: encode a small gradient into every format/container pair and
	// assert the structural contract (magic, header fields, payload sizes) -
	// the real runtime-load proof lives in the player_cooked_textures ctest
	//---------------------------------------------------------
	int selftest()
	{
		const int width = 20;	// deliberately NOT a block multiple: partial
		const int height = 12;	// edge blocks must round up, never truncate
		std::vector<std::vector<uint8_t>> rgbaLevels;
		for (int level = 0; level < 2; ++level)
		{
			const int levelWidth = levelDimension(width, level);
			const int levelHeight = levelDimension(height, level);
			std::vector<uint8_t> pixels;
			pixels.reserve(size_t(levelWidth) * size_t(levelHeight) * 4);
			for (int y = 0; y < levelHeight; ++y)
			{
				for (int x = 0; x < levelWidth; ++x)
				{
					pixels.push_back(uint8_t(x * 255 / levelWidth));
					pixels.push_back(uint8_t(y * 255 / levelHeight));
					pixels.push_back(128);
					pixels.push_back(uint8_t(255 - x));
				}
			}
			rgbaLevels.push_back(std::move(pixels));
		}
		int failures = 0;
		auto check = [&failures](bool condition, std::string const& what)
		{
			if (!condition)
			{
				std::fprintf(stderr, "texcook: SELFTEST FAILED - %s\n",
					what.c_str());
				++failures;
			}
		};
		for (FormatInfo const& info : FORMATS)
		{
			const std::vector<std::vector<uint8_t>> encoded =
				encodeLevels(info, "low", width, height, rgbaLevels);
			check(encoded.size() == 2, std::string(info.token) + ": level count");
			check(encoded[0].size() == blockDataSize(info, width, height),
				std::string(info.token) + ": level 0 payload size");
			check(encoded[1].size() == blockDataSize(info, width / 2, height / 2),
				std::string(info.token) + ": level 1 payload size");
			// container round: every format must build its shippable container
			if (info.ddsFourCC || info.dxgiFormat)
			{
				const std::vector<uint8_t> dds =
					buildDds(info, width, height, encoded);
				check(dds.size() >= 128 && std::memcmp(dds.data(), "DDS ", 4) == 0,
					std::string(info.token) + ": DDS magic");
			}
			if (info.glInternalFormat)
			{
				const std::vector<uint8_t> ktx =
					buildKtx1(info, width, height, encoded);
				check(ktx.size() > 64 + encoded[0].size() &&
					std::memcmp(ktx.data() + 1, "KTX 11", 6) == 0,
					std::string(info.token) + ": KTX1 magic");
				const std::vector<uint8_t> oitd =
					buildOitd(info, width, height, encoded);
				check(oitd.size() == 4 + 17 + encoded[0].size() + encoded[1].size()
					&& std::memcmp(oitd.data(), "OITD", 4) == 0,
					std::string(info.token) + ": OITD layout");
			}
		}
		if (failures)
		{
			return 1;
		}
		std::printf("texcook: self-test OK (8 formats encoded, container "
			"layouts verified)\n");
		return 0;
	}
}

//---------------------------------------------------------
int main(int argc, char** argv)
{
	std::string input, output, format, container;
	std::string quality = "normal";
	int width = 0, height = 0, levels = 1;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--selftest")
		{
			return selftest();
		}
		if (i + 1 >= argc)
		{
			fail("missing value for '" + argument + "'");
		}
		const std::string value = argv[++i];
		if (argument == "--input") { input = value; }
		else if (argument == "--output") { output = value; }
		else if (argument == "--format") { format = value; }
		else if (argument == "--container") { container = value; }
		else if (argument == "--quality") { quality = value; }
		else if (argument == "--width") { width = std::atoi(value.c_str()); }
		else if (argument == "--height") { height = std::atoi(value.c_str()); }
		else if (argument == "--levels") { levels = std::atoi(value.c_str()); }
		else { fail("unknown argument '" + argument + "'"); }
	}
	if (input.empty() || output.empty() || format.empty() || container.empty())
	{
		std::fprintf(stderr,
			"usage: texcook --input <levels.rgba> --output <file> --width W "
			"--height H --levels N --format bc1|bc3|bc7|etc2-rgb|etc2-rgba|"
			"astc-4x4|astc-6x6|astc-8x8 --quality low|normal|high "
			"--container dds|ktx|oitd\n       texcook --selftest\n");
		return 2;
	}
	if (quality != "low" && quality != "normal" && quality != "high")
	{
		fail("unknown --quality '" + quality + "'");
	}
	return cook(input, output, width, height, levels, format, quality,
		container);
}
