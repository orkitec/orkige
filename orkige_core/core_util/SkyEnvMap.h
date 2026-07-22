/**************************************************************
	created:	2026/07/19 at 10:00
	filename: 	SkyEnvMap.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __SkyEnvMap_h__19_7_2026__10_00_00__
#define __SkyEnvMap_h__19_7_2026__10_00_00__

#include "core_util/AtmosphereDesc.h"
#include <vector>

namespace Orkige
{
	//! @brief pure, renderer-independent synthesis of an environment cubemap
	//! from a PROCEDURAL atmosphere - the source that lets image-based
	//! lighting light a scene under a procedural sky (no authored skybox
	//! cubemap to sample). Both render flavors feed the SAME synthesized
	//! chain into the SAME downstream IBL consumer (next = HlmsPbs reflection
	//! map, classic = the RTSS image-based-lighting stage), so there is ONE
	//! IBL path with TWO sources: the offline-baked skybox chain
	//! (Util/make_sky_assets.py) and this runtime capture.
	//!
	//! The sky colour model is a CPU port of the EXACT pixel formula the next
	//! flavor's native sky dome shades (density coupled to the sun elevation,
	//! Rayleigh-style channel absorption, the horizon diffusion and the sun
	//! disk - the model knobs the facade does not expose keep the native
	//! preset defaults). The classic flavor draws its visible procedural dome
	//! through THIS function too, so the two flavors' skies - and the
	//! reflections captured from them - agree by construction (the residual
	//! gap is vertex-gradient resolution vs per-pixel shading, tolerance
	//! parity - @see Docs/materials.md). Any sky-look retune must change this
	//! model and the next flavor's dome together, or neither.
	//!
	//! Deliberately LDR + CPU-cheap: the environment is stored 8-bit and the
	//! roughness mip chain is a per-face box downsample (@see halveFaceRgba8) -
	//! an approximation of a ground-truth GGX prefilter, the same box-filter
	//! stand-in the offline skybox chain uses. Capture is ON-DEMAND (rebuild
	//! only when the sun moves materially or the sky colours change - @see
	//! CaptureKey), never per frame.
	namespace SkyEnvMap
	{
		//! a plain linear colour triple (this header sits below the render math
		//! layer, like AtmosphereDesc, so it carries no engine Color type)
		struct Colour
		{
			float r;
			float g;
			float b;
		};

		//! @brief the sky colour for a unit view direction @p (dx,dy,dz) under
		//! @p desc, with @p (sx,sy,sz) the unit direction TOWARD the sun (the
		//! negative of a directional light's travel direction). The result is
		//! saturated to [0;1] per channel (the un-tonemapped sun glow can push
		//! a bright sky past 1). Pure - the one sky model both flavors read.
		Colour skyColour(float dx, float dy, float dz,
			AtmosphereDesc const & desc, float sx, float sy, float sz);

		//! @brief the world-space unit direction sampled by cube face @p face
		//! (0..5 = +X,-X,+Y,-Y,+Z,-Z, the canonical cubemap order both the
		//! next Image2 and the classic Image index) at texel-centre fractions
		//! @p u, @p v in [0;1] (u across the face width, v down its height).
		//! The mapping is the standard cubemap convention, so a GPU sampler
		//! reads the synthesized faces back through the reflection vector
		//! correctly.
		void faceDirection(unsigned int face, float u, float v,
			float & outX, float & outY, float & outZ);

		//! @brief fill @p out (edge*edge RGBA8, row-major, tightly packed) for
		//! cube face @p face at resolution @p edge from the sky model. Alpha is
		//! 255. @p out must hold edge*edge*4 bytes.
		void renderFaceRgba8(unsigned int face, unsigned int edge,
			AtmosphereDesc const & desc, float sx, float sy, float sz,
			unsigned char * out);

		//! @brief box-downsample one RGBA8 face from @p srcEdge to srcEdge/2
		//! (2x2 average, per face - no cross-face seam handling, matching the
		//! offline skybox chain). @p src holds srcEdge*srcEdge*4 bytes, @p dst
		//! holds (srcEdge/2)*(srcEdge/2)*4. @p srcEdge must be even.
		void halveFaceRgba8(unsigned char const * src, unsigned int srcEdge,
			unsigned char * dst);

		//! @brief number of mip levels a cube of edge @p edge carries
		//! (floor(log2(edge))+1; the base level plus the downsample tail to
		//! 1x1). @p edge must be a power of two >= 1.
		unsigned int mipCountForEdge(unsigned int edge);

		//! @brief build the WHOLE RGBA8 cubemap mip chain for @p edge into
		//! @p out, tightly packed mip-major then face-major then row-major:
		//! for each mip (edge, edge/2, ..., 1), the six faces in cubemap order,
		//! each face edge_mip rows of edge_mip*4 bytes. @p outMips receives the
		//! mip count. This is exactly the byte layout the next backend hands to
		//! Image2::loadDynamicImage and the classic backend blits per
		//! (face, mip). Base faces come from renderFaceRgba8; each finer level
		//! from halveFaceRgba8 of the previous.
		void buildCubemapChainRgba8(unsigned int edge,
			AtmosphereDesc const & desc, float sx, float sy, float sz,
			std::vector<unsigned char> & out, unsigned int & outMips);

		//! @brief byte offset of cube face @p face at mip @p mip inside a chain
		//! built by buildCubemapChainRgba8 for base @p edge (the classic
		//! per-face blit needs the pointer; also the layout's single source of
		//! truth, unit-tested against the total size).
		size_t faceMipOffset(unsigned int edge, unsigned int mip,
			unsigned int face);

		//! @brief the recapture key: the atmosphere inputs whose material
		//! change means the synthesized environment no longer matches the sky.
		//! The sun direction is the toward-the-sun unit vector.
		struct CaptureKey
		{
			float sunX;
			float sunY;
			float sunZ;
			float skyR;
			float skyG;
			float skyB;
			float skyPower;
			float density;
			float sunPower;

			CaptureKey()
				: sunX(0.0f), sunY(1.0f), sunZ(0.0f)
				, skyR(0.0f), skyG(0.0f), skyB(0.0f)
				, skyPower(0.0f), density(0.0f), sunPower(0.0f)
			{
			}
		};

		//! @brief the capture key for @p desc under a toward-sun @p (sx,sy,sz)
		CaptureKey keyFor(AtmosphereDesc const & desc,
			float sx, float sy, float sz);

		//! @brief has the sky changed enough since @p last (the captured key)
		//! to warrant a fresh capture? True when the sun rotated past
		//! @p sunMoveCosThreshold (the cosine of the max tolerated sun swing:
		//! dot(lastSun, nowSun) below it recaptures) OR any sky colour / power /
		//! density / sun-power moved by more than a small epsilon. Pure, so the
		//! recapture cadence is unit-tested headlessly.
		bool materiallyDiffers(CaptureKey const & last, CaptureKey const & now,
			float sunMoveCosThreshold);
	}
}

#endif //__SkyEnvMap_h__19_7_2026__10_00_00__
