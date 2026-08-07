/********************************************************************
	created:	Thursday 2026/08/07 at 12:00
	filename: 	DecalComposeSrs.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __DecalComposeSrs_h__7_8_2026__12_00_00__
#define __DecalComposeSrs_h__7_8_2026__12_00_00__

//! @file DecalComposeSrs.h
//! @brief the engine-owned decal-coverage RTSS sub-render-state: rewrites a
//! decal quad's output alpha so the mark REMOVES the same fraction of the
//! surface's light both flavors remove.
//! @remarks The two flavors mark a surface by different mechanisms, and the
//! mechanisms compose in different SPACES. The other backend projects a true
//! decal and lerps the SURFACE's albedo toward the decal's before shading, so
//! a mark of coverage @c a leaves @c (1-a) of the surface's LINEAR radiance and
//! the one display transfer encodes the result. This flavor draws a
//! surface-aligned quad and alpha-blends it over the framebuffer, which is
//! already DISPLAY-ENCODED, so the same @c a leaves @c (1-a) of the DISPLAY
//! value - a much darker, wider-reading mark (at the shipped blob's 0.75 core
//! coverage: 0.25 against 0.50).
//!
//! The correction is the transfer itself, not a tuned curve. The engine-wide
//! display transfer is @c sqrt (@see Orkige_DisplayTransfer), so the display
//! image of a linear survival @c (1-a) is @c sqrt(1-a), and the alpha that
//! leaves exactly that much of a display-encoded destination is
//!
//!     a' = 1 - sqrt(1 - a)
//!
//! which this stage computes per fragment on the composed alpha (texture alpha
//! times the quad's vertex alpha, i.e. the authored opacity), after texturing
//! and before the blend. @c a is in [0;1] by construction, so the root is real.

#include "engine_module/EnginePrerequisitesClassic.h"

#ifdef USE_RTSHADER_SYSTEM
#include <OgreRTShaderSystem.h>

namespace Orkige
{
	//! @brief register the decal-coverage sub-render-state factory with the
	//! generator (idempotent) and add a fresh instance to @p renderState.
	//! @remarks Mirrors how the lighting stages are added in
	//! configureSurfaceShaderState; the instance is owned by the render state.
	void addDecalComposeSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState);
}

#endif // USE_RTSHADER_SYSTEM
#endif // __DecalComposeSrs_h__7_8_2026__12_00_00__
