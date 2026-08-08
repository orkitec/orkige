/********************************************************************
	created:	Saturday 2026/08/08 at 12:00
	filename: 	RenderWaterTuning.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "engine_render/RenderWaterTuning.h"
#include "engine_render/RenderSystem.h"

#include "core_debug/CVarManager.h"

#include <algorithm>

namespace Orkige
{
	namespace WaterTuning
	{
		namespace
		{
			//! read one knob and hold it inside its band (a value the type
			//! accepted but the look cannot use is clamped, never refused -
			//! the cvar keeps the string the owner typed)
			float clampedRead(char const * name, float fallback,
				float lowest, float highest)
			{
				return std::clamp(
					CVarManager::getSingleton().getFloat(name, fallback),
					lowest, highest);
			}
			//! the ONE re-apply hook every knob in this tier installs: hand the
			//! change to the render facade's single water-look refresh road. No
			//! render system (a headless process, or a set after teardown) means
			//! there is nothing to re-apply - an honest no-op.
			void refreshWaterLook(CVar const &)
			{
				if (RenderSystem* renderSystem = RenderSystem::get())
				{
					renderSystem->refreshWaterLook();
				}
			}
		}
		//---------------------------------------------------------
		void registerCVars()
		{
			CVarManager & cvars = CVarManager::getSingleton();
			// NO CVAR_PERSIST anywhere in this tier on purpose: these are
			// session-scoped look-dev overrides, so dialling one in never
			// writes itself into a project manifest.
			cvars.registerCVar(CVAR_MIRROR_SPECULAR, CVarType::Float,
				cvarToString(DEFAULT_MIRROR_SPECULAR), CVAR_NONE,
				"the planar water mirror's weight (kS) in the environment "
				"specular term, in [0;1] - the one angle-independent dial on "
				"how much mirror the surface carries; BOTH flavors",
				&refreshWaterLook);
			cvars.registerCVar(CVAR_MIRROR_FRESNEL_SCALE, CVarType::Float,
				cvarToString(DEFAULT_MIRROR_FRESNEL_SCALE), CVAR_NONE,
				"multiplier on the mirror surface's fresnel F0 (1 = the "
				"flavor's own law unchanged) - scales the grazing-edge "
				"reflectivity; BOTH flavors",
				&refreshWaterLook);
			cvars.registerCVar(CVAR_MIRROR_ALBEDO_SCALE, CVarType::Float,
				cvarToString(DEFAULT_MIRROR_ALBEDO_SCALE), CVAR_NONE,
				"multiplier on the mirror surface's body albedo (1 = the "
				"flavor's own law unchanged) - how bright the water body reads "
				"under the mirror; BOTH flavors",
				&refreshWaterLook);
			cvars.registerCVar(CVAR_MIRROR_LOD, CVarType::Float,
				cvarToString(DEFAULT_MIRROR_LOD), CVAR_NONE,
				"the mirror sample's sharpness as a mip fraction in [0;1] "
				"(0 = the sharpest mirror); the NEXT flavor only, baked into "
				"its planar-reflection shader piece",
				&refreshWaterLook);
			cvars.registerCVar(CVAR_MIRROR_DISTORT, CVarType::Float,
				cvarToString(DEFAULT_MIRROR_DISTORT), CVAR_NONE,
				"how far the ripple slope shifts the mirror sample, in planar "
				"UV units per unit of slope, in [0;1]; the NEXT flavor only, "
				"baked into its planar-reflection shader piece",
				&refreshWaterLook);
			cvars.registerCVar(CVAR_MIRROR_ROUGHNESS, CVarType::Float,
				cvarToString(DEFAULT_MIRROR_ROUGHNESS), CVAR_NONE,
				"the roughness the sky-mirror sample's environment LOD is "
				"derived from, in [0;1]; the CLASSIC flavor's LOD lane only - "
				"the water program's own shader literal is compiled in and "
				"does not follow this knob",
				&refreshWaterLook);
		}
		//---------------------------------------------------------
		float mirrorSpecular()
		{
			return clampedRead(CVAR_MIRROR_SPECULAR, DEFAULT_MIRROR_SPECULAR,
				0.0f, 1.0f);
		}
		//---------------------------------------------------------
		float mirrorFresnelScale()
		{
			return clampedRead(CVAR_MIRROR_FRESNEL_SCALE,
				DEFAULT_MIRROR_FRESNEL_SCALE, 0.0f, MAX_SCALE);
		}
		//---------------------------------------------------------
		float mirrorAlbedoScale()
		{
			return clampedRead(CVAR_MIRROR_ALBEDO_SCALE,
				DEFAULT_MIRROR_ALBEDO_SCALE, 0.0f, MAX_SCALE);
		}
		//---------------------------------------------------------
		float mirrorLod()
		{
			return clampedRead(CVAR_MIRROR_LOD, DEFAULT_MIRROR_LOD, 0.0f, 1.0f);
		}
		//---------------------------------------------------------
		float mirrorDistort()
		{
			return clampedRead(CVAR_MIRROR_DISTORT, DEFAULT_MIRROR_DISTORT,
				0.0f, 1.0f);
		}
		//---------------------------------------------------------
		float mirrorRoughness()
		{
			return clampedRead(CVAR_MIRROR_ROUGHNESS, DEFAULT_MIRROR_ROUGHNESS,
				0.0f, 1.0f);
		}
	}
}
