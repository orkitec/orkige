/********************************************************************
	created:	Friday 2026/07/31 at 14:00
	filename: 	TextureEncode.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __TextureEncode_h__31_7_2026__14_00_00__
#define __TextureEncode_h__31_7_2026__14_00_00__

#include <string>
#include <vector>

//! @file TextureEncode.h
//! @brief the export-time GPU texture encoder, as a library.
//!
//! Input is RAW RGBA8 level data (the caller owns decode, downscale,
//! premultiply and the mip-chain downsamples); output is the bytes of one
//! block-compressed texture in a container the target runtime already loads:
//!
//!   * .dds   BC1/BC3/BC7  - both render flavors register a DDS codec
//!   * .ktx   ETC2/ASTC    - KTX1, the classic flavor's compressed loader
//!   * .oitd  ETC2/ASTC    - the Ogre-Next native container
//!
//! Encoding rides libktx alone: its vendored ASTC encoder handles every block
//! size directly, and the universal encoder/transcoder pair yields ETC2 and
//! BCn blocks (RGBA -> intermediate -> transcode). The containers are written
//! by hand - they are fixed headers over the raw block payload, and owning the
//! writers keeps this independent of any renderer headers.
//!
//! A library rather than a spawned tool: the exporter that needs it links it
//! directly, so an export resolves no tool path and a distributed editor
//! stages no extra binary. Every entry point reports failure through an error
//! string; nothing here exits a process.
//!
//! Levels are indexed `[level][face]` throughout. `faces` is 1 for a 2D
//! texture or 6 for a cubemap (square faces, order +X,-X,+Y,-Y,+Z,-Z); the six
//! faces compress in one pass and each container carries the cubemap flags plus
//! the complete per-face mip chain, so a skybox's prefiltered roughness chain
//! survives the cook intact.

namespace OrkigeExport
{
	//! RGBA8 (or encoded block) payloads indexed [level][face]
	typedef std::vector<std::vector<std::vector<unsigned char> > >
		TextureLevels;

	//! @brief one block format's footprint and the per-container identifiers
	//! the writers stamp
	struct TextureFormatInfo
	{
		const char *	token;			//!< CLI/sidecar spelling
		int				blockWidth;		//!< texel block footprint
		int				blockHeight;
		int				blockBytes;		//!< bytes per encoded block
		bool			isAstc;			//!< direct ASTC encode (vs transcode)
		//! KTX1 glInternalFormat (0 = the format never ships in a .ktx)
		unsigned int	glInternalFormat;
		unsigned int	glBaseInternalFormat;	//!< GL_RGB / GL_RGBA
		//! Ogre-Next PixelFormatGpu value for the .oitd header - PINNED to the
		//! ports/ogre-next checkout (OgrePixelFormatGpu.h); the
		//! player_cooked_textures ctest loads a cooked .oitd through the real
		//! runtime, so an enum drift on a port upgrade fails a test instead of
		//! shipping garbage.
		unsigned short	oitdPixelFormat;
		//! DDS fourCC ("DXT1"/"DXT5"; 0 = needs the DX10 extension header)
		unsigned int	ddsFourCC;
		unsigned int	dxgiFormat;		//!< DXGI format for the DX10 path (BC7)
	};

	//! @brief the export-time texture encoder (@see TextureEncode.h)
	class TextureEncode
	{
	public:
		//! @brief the format table, terminated by a null-token entry - the ONE
		//! place the format vocabulary lives
		static TextureFormatInfo const * formats();
		//! @brief the number of entries in `formats()`
		static int formatCount();
		//! @brief the entry for a token, or 0 when nothing answers to it
		static TextureFormatInfo const * findFormat(std::string const & token);

		//! @brief mip level @p level of a @p base-sized axis (never below 1)
		static int levelDimension(int base, int level);
		//! @brief the encoded byte count of one @p width x @p height image in
		//! @p info's blocks (partial edge blocks round UP, never truncate)
		static std::size_t blockDataSize(TextureFormatInfo const & info,
			int width, int height);

		//! @brief can @p info ship inside a `.dds` / `.ktx` / `.oitd`
		static bool fitsContainer(TextureFormatInfo const & info,
			std::string const & container);

		//! @brief encode the RGBA8 levels to @p info's block format.
		//! @param quality "low" | "normal" | "high" - encoder effort
		//! @param rgbaLevels indexed [level][face], each level sized
		//!        levelDimension(width/height, level)
		//! @param out receives the per-level, per-face block payloads
		//! @return false with an honest @p error on any encoder failure
		static bool encodeLevels(TextureFormatInfo const & info,
			std::string const & quality, int width, int height, int faces,
			TextureLevels const & rgbaLevels, TextureLevels & out,
			std::string * error);

		//! @brief build the container bytes around already-encoded @p levels
		static bool buildContainer(std::string const & container,
			TextureFormatInfo const & info, int width, int height,
			TextureLevels const & levels, std::vector<unsigned char> & out,
			std::string * error);

		//! @brief the whole cook: validate, encode, build the container.
		//! @return false with an @p error naming the first thing that refused
		static bool encodeToContainer(std::string const & formatToken,
			std::string const & quality, int width, int height, int faces,
			TextureLevels const & rgbaLevels, std::string const & container,
			std::vector<unsigned char> & out, std::string * error);

		//! @brief read the FACE-major RGBA level stream a cook feeds in (each
		//! face's whole mip chain, faces in +X,-X,+Y,-Y,+Z,-Z order) and
		//! transpose it into the [level][face] indexing the encoder wants.
		static bool readRgbaLevels(std::string const & path, int width,
			int height, int levelCount, int faces, TextureLevels & out,
			std::string * error);

		//! @brief the same transpose over a memory buffer (an in-process cook
		//! never writes the raw levels to disk)
		static bool takeRgbaLevels(unsigned char const * data,
			std::size_t size, int width, int height, int levelCount, int faces,
			TextureLevels & out, std::string * error);

		//! @brief validate the shape arguments a cook was asked for (positive
		//! dimensions, a known format, a container that can carry it, 1 or 6
		//! square faces)
		static bool validate(std::string const & formatToken,
			std::string const & quality, int width, int height, int levelCount,
			int faces, std::string const & container, std::string * error);
	};
}

#endif //__TextureEncode_h__31_7_2026__14_00_00__
