/********************************************************************
	created:	Thursday 2026/07/23 at 12:00
	filename: 	MetalRoughLightingSrs.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __MetalRoughLightingSrs_h__23_7_2026__12_00_00__
#define __MetalRoughLightingSrs_h__23_7_2026__12_00_00__

//! @file MetalRoughLightingSrs.h
//! @brief the engine-owned metal-rough lighting sub-render-state of the
//! classic flavor's generated materials - the drop-in replacement for the
//! stock Cook-Torrance lighting stage that reproduces the OTHER backend's
//! per-light response, so one authored material/light pair reads the same on
//! both flavors.
//! @remarks Four response-level differences separate the stock stage from the
//! Ogre-Next HlmsPbs default response; this stage (with its shader library,
//! media/rtss/OrkigeLib_MetalRough.glsl) removes all four at the formula
//! level:
//!  1. albedo colour space: the stock stage pow(2.2)-decodes the material
//!     colour (CPU-side, via the linear-colours program flag) AND every
//!     sampled texel (the texturing stage's decode macro); HlmsPbs consumes
//!     both raw. This stage defines neither, so albedo stays raw - the
//!     engine-wide convention.
//!  2. diffuse energy: HlmsPbs multiplies the Lambert term by the
//!     renormalised-diffuse factor (energy bias/factor + light/view grazing
//!     scatter over perceptual roughness); the stock stage is plain Lambert.
//!  3. display transfer: the stock stage encodes the lit output with
//!     pow(1/2.2); HlmsPbs uses sqrt() on a non-sRGB target. This stage
//!     appends the SAME sqrt() at the post-process stage.
//!  4. specular details: the stock stage caps the dielectric grazing fresnel
//!     (f90 from f0), evaluates it at NdotH and multiplies a multi-scatter
//!     energy compensation; HlmsPbs uses VdotH, f90 = 1 and no compensation.
//! The GGX distribution and height-correlated Smith visibility terms are
//! formula-identical between the two backends already and stay verbatim.
//! Verified by the render_facade_selfcheck light probe (ORKIGE_LIGHT_PROBE)
//! sweeping albedo x intensity x angle x roughness across both flavors.

#include "engine_module/EnginePrerequisitesClassic.h"

#ifdef USE_RTSHADER_SYSTEM
#include <OgreRTShaderSystem.h>

namespace Orkige
{
	//! @brief register the metal-rough lighting sub-render-state factory with
	//! the generator (idempotent) and add a fresh instance to @p renderState.
	//! @remarks The stage reads ROUGHNESS from the pass specular's red channel
	//! and METALNESS from its green channel (the orm layout), exactly like the
	//! stock Cook-Torrance stage it replaces; the integrated-PSSM receiver and
	//! the hemisphere-ambient / image-based-lighting stages compose with it
	//! unchanged (@see configureSurfaceShaderState).
	void addMetalRoughLightingSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState);
}

#endif // USE_RTSHADER_SYSTEM
#endif // __MetalRoughLightingSrs_h__23_7_2026__12_00_00__
