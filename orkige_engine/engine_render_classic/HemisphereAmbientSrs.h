/********************************************************************
	created:	Wednesday 2026/07/22 at 12:00
	filename: 	HemisphereAmbientSrs.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __HemisphereAmbientSrs_h__22_7_2026__12_00_00__
#define __HemisphereAmbientSrs_h__22_7_2026__12_00_00__

//! @file HemisphereAmbientSrs.h
//! @brief a custom RTSS sub-render-state that folds a per-pixel two-colour
//! sky/ground hemisphere ambient into the generated lit result, mirroring the
//! Ogre-Next HlmsPbs ambient-hemisphere response so both flavors light a
//! surface's ambient fill from the same sky/ground split.
//! @remarks Model (from HlmsPbs AmbientLighting_piece_ps): the diffuse ambient
//! is @c lerp(lowerHemi, upperHemi, dot(hemisphereDir, N) * 0.5 + 0.5) scaled by
//! the surface diffuse reflectance (albedo * (1 - metalness)) - single-albedo,
//! evaluated in view space against the same view-space normal the Cook-Torrance
//! stage lights with. It runs AFTER the Cook-Torrance lighting stage and ADDS
//! its term to the lit output (the analytic lights are untouched, exactly as
//! next adds the ambient on top of the direct lighting). The generated
//! material's flat scene-ambient term is zeroed at its source (the pass ambient
//! reflectance is set black in configureSurfaceShaderState), so this is the
//! sole ambient path for generated surface materials - no double count.

#include "engine_module/EnginePrerequisitesClassic.h"

#ifdef USE_RTSHADER_SYSTEM
#include <OgreRTShaderSystem.h>

namespace Orkige
{
	//! @brief cache the current two hemisphere colours (linear) so the
	//! per-material sub-render-states push them to their generated shaders each
	//! frame. Called from RenderWorld::setAmbientHemisphere (the atmosphere
	//! drive re-pushes ambient every frame, so this stays live). The blend axis
	//! resets to straight up - the authored-ambient convention on both flavors.
	void noteHemisphereAmbientColours(Ogre::ColourValue const & upperHemisphere,
		Ogre::ColourValue const & lowerHemisphere);

	//! @brief the driven-atmosphere overload: also sets the WORLD-space
	//! hemisphere axis. The other backend's sun linkage tilts the axis toward
	//! the mirrored sun (normalize(up + halfTurnAboutUp(toSun))) so a
	//! horizon-facing surface fills from the warm horizon band; the classic
	//! drive hands the SAME axis here (@see driveSunExposure).
	void noteHemisphereAmbientColours(Ogre::ColourValue const & upperHemisphere,
		Ogre::ColourValue const & lowerHemisphere,
		Ogre::Vector3 const & worldDirection);

	//! @brief read back the cached two hemisphere colours (linear). The water
	//! program - a hand-written pass outside the RTSS scheme - lights its body
	//! from the SAME sky/ground fill the generated surface materials receive, so
	//! its diffuse body reads at the calibrated ambient level instead of unlit.
	void hemisphereAmbientColours(Ogre::ColourValue & outUpper,
		Ogre::ColourValue & outLower);

	//! @brief register the hemisphere-ambient sub-render-state factory with the
	//! generator (idempotent) and add a fresh instance to @p renderState.
	//! @remarks Mirrors how the Cook-Torrance and image-based-lighting stages are
	//! added in configureSurfaceShaderState; the returned instance is owned by
	//! the render state.
	void addHemisphereAmbientSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState);
}

#endif // USE_RTSHADER_SYSTEM
#endif // __HemisphereAmbientSrs_h__22_7_2026__12_00_00__
