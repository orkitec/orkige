/********************************************************************
	created:	Saturday 2026/08/08 at 12:00
	filename: 	RenderWaterTuning.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderWaterTuning_h__8_8_2026__12_00_00__
#define __RenderWaterTuning_h__8_8_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"

namespace Orkige
{
	/** \addtogroup Render
	*  @{ */

	//! @brief the LIVE water/mirror look tier: the `water.*` cvars both render
	//! flavors read when they build a water surface.
	//! @remarks The reflective water look carries a handful of numbers that are
	//! neither authored per surface (RenderWaterDesc holds those) nor derivable
	//! from the scene - the mirror's weight, its fresnel and body-albedo laws,
	//! and, on the next flavor, the sample sharpness and ripple distortion baked
	//! into its planar-reflection shader piece. They are LOOK constants, and a
	//! look constant that cannot be moved without a rebuild cannot be dialled
	//! in. This is the one place their names, defaults and clamp bands live, so
	//! the two backends, the registration and the documentation cannot drift.
	//!
	//! Two shapes on purpose:
	//! * ABSOLUTES (`water.mirrorSpecular`, `water.mirrorLod`,
	//!   `water.mirrorDistort`, `water.mirrorRoughness`) default to the value
	//!   the flavor has always baked in, and
	//! * MULTIPLIERS (`water.mirrorFresnelScale`, `water.mirrorAlbedoScale`)
	//!   default to 1 and ride on top of each flavor's own law rather than
	//!   replacing it, so the two flavors keep describing ONE surface.
	//!
	//! Either way the DEFAULTS reproduce today's numbers exactly, so a run that
	//! sets nothing renders byte-identical pixels - the pixel gates depend on it.
	//!
	//! These are session-scoped look-dev overrides: deliberately NOT
	//! CVAR_PERSIST, so dialling a value in never leaks into a project manifest.
	//! A change re-applies through the ONE facade road
	//! `RenderSystem::refreshWaterLook()`.
	namespace WaterTuning
	{
		//--- the cvar names (the ONE vocabulary) ---------------
		//! the mirror's angle-independent weight (kS) - both flavors
		constexpr char CVAR_MIRROR_SPECULAR[] = "water.mirrorSpecular";
		//! multiplier on each flavor's mirror fresnel-F0 law - both flavors
		constexpr char CVAR_MIRROR_FRESNEL_SCALE[] = "water.mirrorFresnelScale";
		//! multiplier on each flavor's mirror body-albedo law - both flavors
		constexpr char CVAR_MIRROR_ALBEDO_SCALE[] = "water.mirrorAlbedoScale";
		//! the next flavor's baked mirror sample LOD (mip fraction)
		constexpr char CVAR_MIRROR_LOD[] = "water.mirrorLod";
		//! the next flavor's baked mirror ripple-distortion scale
		constexpr char CVAR_MIRROR_DISTORT[] = "water.mirrorDistort";
		//! the classic flavor's sky-mirror sample-LOD roughness
		constexpr char CVAR_MIRROR_ROUGHNESS[] = "water.mirrorRoughness";

		//--- the defaults: TODAY's baked constants -------------
		//! @see CVAR_MIRROR_SPECULAR (the probe-calibrated shared kS)
		constexpr float DEFAULT_MIRROR_SPECULAR = 0.43f;
		//! @see CVAR_MIRROR_FRESNEL_SCALE (identity)
		constexpr float DEFAULT_MIRROR_FRESNEL_SCALE = 1.0f;
		//! @see CVAR_MIRROR_ALBEDO_SCALE (identity)
		constexpr float DEFAULT_MIRROR_ALBEDO_SCALE = 1.0f;
		//! @see CVAR_MIRROR_LOD (near-mip-0: a sharp mirror)
		constexpr float DEFAULT_MIRROR_LOD = 0.05f;
		//! @see CVAR_MIRROR_DISTORT (planar UV units per unit of ripple slope)
		constexpr float DEFAULT_MIRROR_DISTORT = 0.09f;
		//! @see CVAR_MIRROR_ROUGHNESS (the shared water datablock roughness)
		constexpr float DEFAULT_MIRROR_ROUGHNESS = 0.16f;

		//--- the clamp bands ----------------------------------
		//! the upper bound of a multiplier knob (0 = the term switched off)
		constexpr float MAX_SCALE = 8.0f;

		//! @brief register the whole `water.*` tier on the CVarManager
		//! singleton, each cvar carrying the onChange hook that re-applies the
		//! look through `RenderSystem::refreshWaterLook()`.
		//! @remarks Idempotent (registerCVar keeps an existing value), no
		//! CVAR_PERSIST (session-scoped look-dev), and safe with no render
		//! system alive - the hook then changes nothing. Called from the app
		//! boot beside the `r.*` quality group.
		ORKIGE_ENGINE_DLL void registerCVars();

		//--- the live reads (clamped; unregistered = the default) ---
		//! the mirror's weight in the env specular term, clamped to [0;1]
		ORKIGE_ENGINE_DLL float mirrorSpecular();
		//! the multiplier on the mirror fresnel F0, clamped to [0;MAX_SCALE]
		ORKIGE_ENGINE_DLL float mirrorFresnelScale();
		//! the multiplier on the mirror body albedo, clamped to [0;MAX_SCALE]
		ORKIGE_ENGINE_DLL float mirrorAlbedoScale();
		//! the next flavor's mirror sample LOD fraction, clamped to [0;1]
		ORKIGE_ENGINE_DLL float mirrorLod();
		//! the next flavor's mirror distortion scale, clamped to [0;1]
		ORKIGE_ENGINE_DLL float mirrorDistort();
		//! the classic flavor's sky-mirror LOD roughness, clamped to [0;1]
		ORKIGE_ENGINE_DLL float mirrorRoughness();
	}

	/** @} */
}

#endif	// __RenderWaterTuning_h__8_8_2026__12_00_00__
