/********************************************************************
	created:	Saturday 2026/07/12 at 20:00
	filename: 	RenderWater.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderWater_h__12_7_2026__20_00_00__
#define __RenderWater_h__12_7_2026__20_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief description of ONE animated water surface - the facade's water
	//! authoring surface, the transparent-lit sibling of RenderMaterialDesc
	//! @remarks Consumed by RenderSystem::createWaterMaterial, which turns it
	//! into a named backend material MeshInstance::setMaterial can assign, and
	//! animated cheaply per frame through RenderSystem::setWaterTime (a
	//! material-parameter scroll - no per-vertex CPU work, so a single plane is
	//! mobile-safe). Plain data on purpose, like the surface material: water is
	//! GENERATED, not script-authored. The normal-map name resolves through the
	//! resource groups (engine media AND project assets), like every texture.
	//!
	//! Backend mapping / capability (Docs/render-abstraction.md has the full
	//! matrix):
	//! next = an HLMS PBS datablock with TWO detail normal maps scrolling in
	//! different directions/speeds (the ripple animation), realistic
	//! transparency that preserves the fresnel edge reflection, the deep colour
	//! as the water-body albedo and a subtle shallow-colour scatter term. The
	//! plane needs mesh TANGENTS (the water plane mesh is UV-mapped, so the
	//! importer builds them; classic builds tangents on demand). classic = a
	//! transparent metal-rough plane through RTSS: Cook-Torrance lighting on the
	//! deep/shallow tint (opacity = alpha) with an intrinsic Fresnel edge, plus a
	//! COMPOSITE of two cues from the one normal map - it is sampled by the RTSS
	//! normal-map stage to LIGHT the ripples (a static relief that catches the
	//! sun) AND bound a second time as a scrolling colour shimmer for visible
	//! MOTION. Classic can light a normal map OR scroll a texture on one unit,
	//! not both (the normal-map stage samples the RAW texcoord), so the lit
	//! ripple detail is STATIC; fully animated normal-mapped water - and the two
	//! detail-normal ripple - is next-only.
	//!
	//! Screen-space refraction (opt-in, capability-gated - RenderCaps::
	//! ScreenSpaceRefraction): with `screenSpaceRefraction` on, the opaque scene
	//! colour is captured BEFORE the water draws and the surface samples it at a
	//! normal-map-perturbed screen UV, so what sits under the water bends/wobbles
	//! (`refractionStrength` scales the offset). This is BASIC distortion only -
	//! NOT depth-graded transmission (still a later stage). next = the HlmsPbs
	//! Refractive transparency mode fed by a compositor scene-colour+depth pass;
	//! classic = a grab-pass RenderTexture of the scene (water hidden) sampled by
	//! the water shader at the perturbed screen UV - basic RTT, so it reaches the
	//! GLES2/WebGL1 mobile/web path where a context can render to a texture. When
	//! the capability is absent OR the flag is off the surface renders EXACTLY as
	//! the Stage-1 look (a byte-stable fallback), and a requested-but-unsupported
	//! refraction logs one honest line.
	//!
	//! Planar reflection (opt-in, capability-gated - RenderCaps::PlanarReflection):
	//! with `planarReflection` on, the surface shows a MIRROR of the actual scene
	//! (sky + terrain + objects) rather than just the sky IBL cubemap it already
	//! samples. classic = the textbook mirror pass: a dedicated camera reflected
	//! across the surface plane (y=`planeHeightY`, normal +Y) renders the scene
	//! (the water surface itself hidden, geometry below the plane clipped) into a
	//! reflection RenderTexture, which the water shader samples at the fragment's
	//! screen UV perturbed by the ripple normal and blends over the base look by
	//! `reflectionStrength`; it composes with screen-space refraction. next = the
	//! reflection stays the sky IBL cubemap (the native HlmsPbs planar-reflection
	//! subsystem is not compiled into this build) - a requested-but-unsupported
	//! planar reflection logs one honest line and the surface renders the
	//! byte-stable IBL look. When the capability is absent OR the flag is off the
	//! surface is byte-identical to the sky-reflection look.
	//!
	//! Honest v1 boundaries (both flavors): NO true depth-graded deep->shallow
	//! transmission (still needs a depth-graded pass - a future desktop quality
	//! knob, see Docs/render-abstraction.md); vertex waves are out (the surface
	//! stays flat, the ripple lives entirely in the scrolling normal maps).
	struct ORKIGE_ENGINE_DLL RenderWaterDesc
	{
		Color	deepColour = Color(0.02f, 0.10f, 0.18f, 1.0f);		//!< colour of the water body (deep water)
		Color	shallowColour = Color(0.10f, 0.36f, 0.48f, 1.0f);	//!< colour of shallow water / surface scatter
		float	opacity = 0.72f;		//!< surface transparency 0..1 (1 = opaque)
		float	waveScale = 6.0f;		//!< detail-normal tiling factor across the plane's UVs (higher = smaller ripples)
		float	waveSpeed = 0.04f;		//!< ripple scroll speed (UV units per second, driven by setWaterTime)
		float	fresnelPower = 1.0f;	//!< edge-reflection strength knob (next scales F0; classic scales the specular)
		String	normalTexture;			//!< tiling water normal map resource name ("" = a flat, non-rippling surface)
		//! whether cast shadows darken the surface (the water material is
		//! per-instance, so this is a per-surface knob; water never CASTS -
		//! WaterComponent turns its plane's caster flag off by design)
		//! map: classic=Material::setReceiveShadows (the RTSS receiver stage skips
		//! the material) | next=HlmsDatablock::setReceiveShadows
		bool	receiveShadows = true;
		//! whether the surface refracts the scene behind it (screen-space
		//! distortion of the opaque colour under the water). Opt-in and
		//! capability-gated (RenderCaps::ScreenSpaceRefraction); default OFF so
		//! existing water is byte-stable. @see the struct remarks
		bool	screenSpaceRefraction = false;
		//! screen-UV perturbation strength of the refraction (roughly the
		//! fraction of the screen the normal displaces the sampled colour by;
		//! a small value keeps the bend subtle). Inert unless
		//! screenSpaceRefraction is on and the capability is present
		float	refractionStrength = 0.02f;
		//! whether the surface shows a MIRROR reflection of the actual scene
		//! (sky + terrain + objects) instead of just the sky IBL cubemap it
		//! already samples. Opt-in and capability-gated (RenderCaps::
		//! PlanarReflection); default OFF so existing water is byte-stable.
		//! @see the struct remarks
		bool	planarReflection = false;
		//! blend weight of the planar reflection over the base water look
		//! (0 = none, 1 = a full mirror). Inert unless planarReflection is on
		//! and the capability is present
		float	reflectionStrength = 1.0f;
		//! world-space Y of the mirror plane (the water surface, normal +Y);
		//! WaterComponent fills it from the surface node's world Y at material
		//! (re)apply. Only consulted when planarReflection is on
		float	planeHeightY = 0.0f;
	};
}

#endif //__RenderWater_h__12_7_2026__20_00_00__
