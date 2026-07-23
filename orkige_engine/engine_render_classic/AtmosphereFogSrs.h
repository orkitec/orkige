/********************************************************************
	created:	Thursday 2026/07/23 at 11:00
	filename: 	AtmosphereFogSrs.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __AtmosphereFogSrs_h__23_7_2026__11_00_00__
#define __AtmosphereFogSrs_h__23_7_2026__11_00_00__

//! @file AtmosphereFogSrs.h
//! @brief a custom RTSS sub-render-state that fogs the generated lit result
//! with the Ogre-Next atmosphere's OBJECT FOG - the same formula, so fogged
//! content reads the same on both flavors.
//! @remarks Model (from the AtmosphereNprSkyHlms pieces): the fog COLOUR is
//! the sky model's haze evaluated per VERTEX along the camera->vertex ray (the
//! horizon-lifted variant the native vertex piece computes - warm toward a low
//! sun, cool away from it, HDR near sunset); the fog WEIGHT is the per-PIXEL
//! transmittance @c exp2(-distance * fogDensity) over the Euclidean
//! camera-to-fragment distance, eased by the luminance breakthrough (bright
//! pixels resist fog: @c lerp(1, weight, exp2(-falloff * (luminance -
//! minBrightness)))), and the blend runs in LINEAR before the display
//! transfer - each element exactly the native object fog's. This replaces the
//! fixed-function SRS_FOG stage on generated surface materials: that stage's
//! flat authored colour and gaussian EXP2 curve were the classic flavor's two
//! largest fogged-content divergences (the missing warm horizon band over
//! distant terrain, the veiled refraction-grab content behind water). The
//! scene-manager fog (FOG_EXP, curve-aligned) remains for fixed-function
//! fallback materials only. A disabled atmosphere (or fogDensity 0) drives
//! the weight to 1, so the stage is a neutral no-op by construction.

#include "engine_module/EnginePrerequisitesClassic.h"

#ifdef USE_RTSHADER_SYSTEM
#include <OgreRTShaderSystem.h>

namespace Orkige
{
	//! @brief everything the atmosphere-fog stage needs per frame: the
	//! view-independent sky-model terms (AtmosphereSunDrive::Detail::skyTerms
	//! under the NATIVE linkage conditioning - sun elevation floored at 0.02,
	//! the dusk fade folded into the sky power) plus the fog knobs. Pushed by
	//! the atmosphere drive each apply; one global state, like the hemisphere
	//! ambient it composes with.
	struct AtmosphereFogState
	{
		bool			enabled;		//!< false = the stage is neutral
		float			fogDensity;		//!< AtmosphereDesc::fogDensity (exp2 rate)
		Ogre::Vector3	sunDir;			//!< toward-the-sun, world space
		float			sunHeight;		//!< conditioned sun elevation
		Ogre::Vector3	skyColour;		//!< the desc's Rayleigh tint
		float			density;		//!< the desc's raw density coefficient
		float			lightDensity;	//!< density amplified by the low sun
		float			finalMultiplier;//!< haze exposure (sky power folded in)
		float			antiMie;		//!< sun-toward haze floor
		Ogre::Vector3	sunAbsorption;
		Ogre::Vector3	mieAbsorption;
		Ogre::Vector3	skyLightAbsorption;

		AtmosphereFogState()
			: enabled(false)
			, fogDensity(0.0f)
			, sunDir(Ogre::Vector3::UNIT_Y)
			, sunHeight(1.0f)
			, skyColour(Ogre::Vector3::UNIT_SCALE)
			, density(0.0f)
			, lightDensity(0.0f)
			, finalMultiplier(0.0f)
			, antiMie(0.08f)
			, sunAbsorption(Ogre::Vector3::ZERO)
			, mieAbsorption(Ogre::Vector3::ZERO)
			, skyLightAbsorption(Ogre::Vector3::ZERO)
		{
		}
	};

	//! @brief cache the live atmosphere-fog state so the per-material
	//! sub-render-states push it to their generated shaders each frame.
	//! Called from the atmosphere drive on every apply; hand a
	//! default-constructed (disabled) state to neutralise the stage.
	void noteAtmosphereFog(AtmosphereFogState const & state);

	//! @brief read back the cached fog state. The water programs - hand-written
	//! passes outside the RTSS scheme - fog their composed surface with the
	//! SAME terms the generated materials receive (the other backend fogs its
	//! water datablock through the one object-fog path too).
	AtmosphereFogState const & atmosphereFogState();

	//! @brief register the atmosphere-fog sub-render-state factory with the
	//! generator (idempotent) and add a fresh instance to @p renderState
	//! (@see addHemisphereAmbientSubRenderState - the same ownership rules).
	void addAtmosphereFogSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState);
}

#endif // USE_RTSHADER_SYSTEM
#endif // __AtmosphereFogSrs_h__23_7_2026__11_00_00__
