/********************************************************************
	created:	Thursday 2026/07/23 at 12:00
	filename: 	ImageLightingSrs.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __ImageLightingSrs_h__23_7_2026__12_00_00__
#define __ImageLightingSrs_h__23_7_2026__12_00_00__

//! @file ImageLightingSrs.h
//! @brief the engine-owned image-based-lighting RTSS sub-render-state: adds
//! the environment chain's specular reflection + diffuse fill to the lit
//! output with EXACTLY the terms the other backend's PBS pixel shader runs
//! (@see media/rtss/OrkigeLib_MetalRough.glsl, Orkige_ImageLighting), so one
//! authored scene reads the same environment fill on both flavors.
//! @remarks It replaces the stock image-based-lighting stage, which differed
//! from the other backend at four response levels: it sampled the chain
//! through a hardware-sRGB view (the chain stores clamped LINEAR radiance),
//! weighted the reflection by a multi-scatter split-sum lookup instead of the
//! roughness-remapped fresnel that backend actually evaluates (it loads no
//! LTC table, so its env BRDF is the constant (1,0,1)), weighted the diffuse
//! fill by (1 - E) instead of 1, and bent the reflection vector toward the
//! normal by roughness^2 (no counterpart there). The engine stage needs no
//! lookup table at all - the fresnel is analytic - and binds ONE texture
//! unit: the environment chain, sampled raw. The luminance parameter carries
//! the authored intensity times the shared fill weight
//! (core_util/IblPreset.h fillScale) - the SAME number the other backend's
//! envmapScale lane carries - so the scale plumbing is one formula too.

#include "engine_module/EnginePrerequisitesClassic.h"

#ifdef USE_RTSHADER_SYSTEM
#include <OgreRTShaderSystem.h>

namespace Orkige
{
	//! @brief register the image-lighting sub-render-state factory with the
	//! generator (idempotent) and add a fresh instance to @p renderState,
	//! bound to the environment chain cubemap @p envTexture at @p luminance
	//! (the authored intensity x IblPreset::fillScale - the effective scale,
	//! matching the other backend's envmapScale).
	//! @remarks Mirrors how the metal-rough and hemisphere stages are added in
	//! configureSurfaceShaderState; the instance is owned by the render state.
	void addImageLightingSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState,
		Ogre::String const & envTexture, float luminance);
}

#endif // USE_RTSHADER_SYSTEM
#endif // __ImageLightingSrs_h__23_7_2026__12_00_00__
