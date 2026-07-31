/********************************************************************
	created:	Friday 2026/07/31 at 15:00
	filename: 	ExportTextureCook.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportTextureCook_h__31_7_2026__15_00_00__
#define __ExportTextureCook_h__31_7_2026__15_00_00__

#include "ExportImage.h"

#include <core_project/AssetDatabase.h>
#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file ExportTextureCook.h
//! @brief the export-time texture cook: the resize/premultiply/compress step
//! run over a staged project payload, per target platform.
//!
//! The DEV loop stays raw - textures load straight from the project, because
//! iteration must not wait on a cook. Only EXPORT conditions the shipped
//! pixels, honoring each texture's import settings
//! (`core_project/AssetDatabase.h`) resolved for the target platform:
//!
//!   * maxSize      downscale so the longest side <= maxSize (area-averaged)
//!   * premultiply  fold alpha into RGB (for premultiplied-alpha blending)
//!   * format       GPU block compression - "auto" resolves the platform's
//!                  best format, "none" ships the PNG, an explicit token
//!                  forces one
//!   * quality      encoder effort (and the ASTC block size under "auto")
//!   * generateMips bake an offline mip chain into the compressed container
//!
//! A compressed texture REPLACES its `.png` in the payload
//! (ball.png -> ball.dds/.ktx/.oitd) and its sidecar is renamed along with it,
//! so the asset-id machinery resolves scene references to the shipped name;
//! the render backends also fall back from a missing `.png` to its cooked
//! siblings for bare-name references. A texture whose sidecar is id-only cooks
//! with the DEFAULT settings (format "auto"); a texture without any sidecar
//! ships untouched. Full model: Docs/textures.md.
//!
//! CUBEMAPS ride the same cook: a sidecar-carrying `.dds` whose container
//! marks it a six-face cubemap block-compresses through the SAME format matrix
//! and encoder, preserving the six faces (order +X,-X,+Y,-Y,+Z,-Z) and the
//! BAKED mip chain exactly - a skybox's chain is the prefiltered roughness
//! chain the image-based-lighting samplers index, so it is re-encoded level by
//! level, never regenerated. The BC container reuses the `.dds` name (in
//! place); the mobile containers rename `.dds` -> `.oitd`/`.ktx`. A
//! non-cubemap `.dds` (or one already compressed) ships verbatim.
//!
//! Sampler settings (filter/wrap) are NOT cooked: they are honored LIVE at
//! material/datablock creation from the same sidecar (which ships alongside
//! the texture), so the cook leaves them for the runtime.

namespace OrkigeExport
{
	//! @brief what one texture resolves to for a (platform, flavor) pair
	struct TextureCookTarget
	{
		//! the encoder format token ("bc1", "astc-6x6", "etc2-rgba", ...);
		//! EMPTY means "ship the source file untouched"
		Orkige::String	format;
		//! "dds" | "ktx" | "oitd"; empty alongside an empty format
		Orkige::String	container;
	};

	//! @brief resolve a sidecar's format/quality for one (platform, flavor)
	//! pair into a concrete encoder format, or an empty target to ship the
	//! source.
	//!
	//! The auto table (see Docs/textures.md for the rationale):
	//!   desktop+next        opaque bc1 (bc7 at quality high), alpha bc7
	//!   desktop+classic     opaque bc1, alpha bc3 (the classic default GL
	//!                       renderer has no BC7 support everywhere it runs)
	//!   ios/android+next    astc (quality high 4x4 / normal 6x6 / low 8x8) -
	//!                       every Metal-capable iPhone AND every
	//!                       Vulkan-capable Android at the API-28 arm64 floor
	//!                       decodes ASTC LDR; etc2 stays a reachable explicit
	//!                       override
	//!   ios/android+classic none - ETC2/ASTC are NOT guaranteed in the
	//!                       classic flavor's GLES2 context, so auto ships PNG
	//!   web                 none - compressed-texture support in the browser
	//!                       is a property of the visitor's GPU; none is
	//!                       guaranteed
	//!
	//! @param alpha does the image carry any texel below full opacity (drives
	//!        the ETC2/BCn variant)
	//! @param warn receives permitted-but-lossy override notes (web, classic
	//!        GLES2 mobile - support there is a per-device lottery)
	//! @return false with an @p error on an IMPOSSIBLE explicit pair - the
	//!         export must refuse rather than ship a texture nothing can load
	bool resolveTextureFormat(Orkige::TextureImportSettings const & settings,
		Orkige::String const & platform, Orkige::String const & flavor,
		bool alpha, TextureCookTarget & out,
		std::function<void(Orkige::String const &)> const & warn,
		Orkige::String * error);

	//! @brief the RGBA level chain an encoder consumes: the base image, then -
	//! when @p generateMips - area-averaged downscales to 1x1, level i sized
	//! base>>i (min 1), matching the encoder's level layout exactly.
	std::vector<ExportImage> buildMipLevels(ExportImage const & image,
		bool generateMips);

	//! @brief one decoded uncompressed cubemap `.dds`
	struct DdsCubemap
	{
		int		size = 0;		//!< face edge length
		int		mips = 0;		//!< BAKED mip levels (read, never regenerated)
		//! faces[face][level], face order +X,-X,+Y,-Y,+Z,-Z, RGBA8
		std::vector<std::vector<std::vector<unsigned char> > > faces;
	};

	//! @brief decode an UNCOMPRESSED masked-32bpp cubemap `.dds` into its faces
	//! and baked mip chain. False when the file is not an uncompressed cubemap
	//! that can be re-encoded (already block-compressed, a DX10/fourCC
	//! container, a 2D texture, an odd bit layout) - such a `.dds` ships
	//! verbatim, which is not an error.
	bool decodeDdsCubemap(Orkige::String const & path, DdsCubemap & out);

	//! @brief does any texel of any face carry an alpha below 255
	bool cubemapHasAlpha(DdsCubemap const & cube);

	//! @brief the cook over a whole staged payload.
	struct TextureCookResult
	{
		int		cooked = 0;		//!< textures actually rewritten
	};

	//! @brief cook every sidecar-carrying `*.png` and cubemap `*.dds` under
	//! @p payloadDirectory in place.
	//! @param platform "" (desktop) | "ios" | "android" | "web"
	//! @param flavor the render backend the export packages ("next" |
	//!        "classic") - it picks the container and the auto formats
	//! @param log receives one line per cooked texture and every lossy-override
	//!        warning
	//! @return false with an @p error when a texture cannot ship as its
	//!         settings demand - the export must refuse, never half-cook
	bool cookTexturePayload(Orkige::String const & payloadDirectory,
		Orkige::String const & platform, Orkige::String const & flavor,
		TextureCookResult & out,
		std::function<void(Orkige::String const &)> const & log,
		Orkige::String * error);
}

#endif //__ExportTextureCook_h__31_7_2026__15_00_00__
