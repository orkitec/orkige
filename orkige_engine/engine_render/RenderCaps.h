/********************************************************************
	created:	Thursday 2026/07/16 at 09:00
	filename: 	RenderCaps.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderCaps_h__16_7_2026__09_00_00__
#define __RenderCaps_h__16_7_2026__09_00_00__

#include <core_util/String.h>

namespace Orkige
{
	//! @brief the render-capability vocabulary as an X-macro table - the SINGLE
	//! source of every capability's identity, name, kind and description. The
	//! enum, the name lookup and the parse all EXPAND from this one list, so
	//! they cannot drift from each other; there is no sidecar and no generated
	//! header. Per-FLAVOR values are deliberately NOT here: a backend fills its
	//! own capability bitset at boot (some caps are runtime-determined - e.g. a
	//! render-system pick), and that live fill is asserted against the committed
	//! snapshot tables (engine_render_classic/RenderCapsExpectedClassic.inc +
	//! engine_render_next/RenderCapsExpectedNext.inc), which also feed the
	//! GENERATED matrix in Docs/render-abstraction.md via Util/update_docs.py.
	//! Probe from code with RenderSystem::supports(RenderCaps::X), from Lua with
	//! engine:supports("name"), and over MCP from get_state's capabilities object.
	//!
	//!   X(Identifier, "name", Kind, "description")
	//!     Identifier    - the enum member (and the .inc snapshot key)
	//!     "name"        - the stable string (Lua / MCP / matrix column)
	//!     Kind          - Asymmetric (a real flavor delta) | PlannedAbsent
	//!                     (absent on BOTH flavors today - a v1 boundary,
	//!                     next-first when it lands)
	//!     "description" - the matrix "what it is" column
	//!
	//! Kind and description are consumed only by Util/update_docs.py (they are
	//! inert in C++ - no expansion below reads them); the runtime uses identity
	//! and name. Editing this table OR a snapshot .inc requires regenerating the
	//! matrix (python3 Util/update_docs.py --write; --check gates staleness).
#define ORKIGE_RENDER_CAPS(X) \
	X(SkyDome, "skyDome", Asymmetric, "a horizon-to-zenith sky dome behind the scene (sun-linked atmospheric on next, a vertex-colour gradient on classic) vs a flat clear colour; the dome is the `procedural` sky type - `AtmosphereDesc::skyType` also selects a cubemap `skybox` or a flat `colour` sky on both flavors") \
	X(DynamicShadows, "dynamicShadows", Asymmetric, "dynamic shadow maps cast by shadow-casting directional lights (next = compositor PSSM + PCF; classic = RTSS integrated PSSM folded into the one generated-material scheme - on GLES2/WebGL the bit is runtime-gated on depth-texture render targets)") \
	X(HemisphereAmbient, "hemisphereAmbient", Asymmetric, "a two-colour sky/ground ambient term; classic averages the two colours to one flat ambient") \
	X(SunExposureLinkage, "sunExposureLinkage", Asymmetric, "the atmosphere drives the linked sun's colour/power (an exposure the un-tonemapped pipeline can clip) - native on next, the same day/night curve evaluated on the CPU on classic (colour + averaged-flat ambient fill, tolerance parity)") \
	X(AnimatedNormalMappedWater, "animatedNormalMappedWater", Asymmetric, "fully animated normal-mapped water ripples; classic lights OR scrolls one normal map on a unit, not both, so its lit relief is static") \
	X(OffscreenOwnedLayers, "offscreenOwnedLayers", Asymmetric, "2D layers composited into an offscreen RenderTexture (the editor GUI Preview + preview_ui), not just the main window") \
	X(ProjectedDecals, "projectedDecals", Asymmetric, "surface marks (impact/splat/footprint + blob-shadow fallback) as TRUE projected decals wrapping over geometry (next = HlmsPbs forward-clustered Decal) vs a surface-aligned textured quad floating above the surface (classic - flat, does not wrap uneven geometry)") \
	X(Bloom, "bloom", Asymmetric, "an LDR highlight-glow post-process on the 3D scene only (bright-pass -> separable blur -> additive combine, per-scene opt-in via engine:setBloom, the r.bloomQuality tier) - the 2D tier (sprites/vector shapes/gui) is excluded so UI stays crisp. next = CompositorManager2 quad passes inserted between the 3D scene pass and the 2D/UI pass; classic = the same chain as a viewport compositor over the generated-material scheme, the `OgreUnifiedShader.h` bright/blur/combine quad passes authored once and run in the GLSL ES 3.0 profile on a GLES/WebGL context - so it reaches the WebGL2/GLES3 web+device path too, gated on the glsl300es probe like the IBL stage; the GLES2/WebGL1 floor answers false and an enabled bloom degrades to no pass with one log line") \
	X(ScreenSpaceRefraction, "screenSpaceRefraction", Asymmetric, "opt-in screen-space refraction distortion through the water surface: the opaque scene colour is captured before the water draws and sampled at a normal-perturbed screen UV so what is under the water bends/wobbles (basic distortion, NOT depth-graded transmission). next = the HlmsPbs Refractive transparency mode fed by a compositor scene-colour+depth pass; classic = a grab-pass RenderTexture of the scene (water hidden) sampled at the perturbed screen UV, authored in two GLSL variants (desktop GL core + GLSL ES 3.0), so it reaches the WebGL2/GLES3 web+device path too - gated on the glsl300es probe like the IBL stage; the GLES2/WebGL1 floor keeps the byte-stable Stage-1 look, a Vulkan/Metal context answers false pending its own variant") \
	X(IblReflections, "iblReflections", Asymmetric, "opt-in image-based lighting sourced from the scene's SKY: specular reflections + a diffuse fill ADDED to the analytic lights on PBS-lit facade materials (next = the HlmsPbs reflection map + diffuse-GI env feature; classic = the generated-shader image-based-lighting stage over the same cubemap - on a GLES context the bit is runtime-gated on GLSL ES 3.0), tiered by the `r.iblQuality` cvar (`core_util/IblPreset.h`). ONE path, TWO sources selected automatically: a skybox atmosphere feeds the offline-baked prefiltered chain (`Util/make_sky_assets.py`); a procedural atmosphere feeds a runtime CPU capture of the sky (`core_util/SkyEnvMap` - a small cubemap synthesized from the atmosphere + sun with a box-downsampled roughness chain, recaptured on-demand only when the sun swings past ~6 degrees or the sky colours change, never per frame; a tolerance-parity approximation of the AtmosphereNpr dome on next, exact for the classic gradient sky). Colour / disabled skies have no environment and refuse honestly (`Docs/materials.md`)") \
	X(PlanarReflection, "planarReflection", Asymmetric, "opt-in MIRROR reflection of the actual scene (sky + terrain + objects) in the water surface, rather than just the sky IBL cubemap it already samples: a camera reflected across the surface plane (normal +Y at `planeHeightY`) renders the scene into a reflection RenderTexture with the water surface hidden, which the water material samples at a FRESNEL-modulated blend (`reflectionStrength` boosts the base reflectivity and dims the body colour moderately - the surface stays water, never chrome). classic = the working path: a hand-authored GLSL program (desktop GL core + GLSL ES 3.0 variants) samples the mirror-camera RenderTexture at the fragment's ripple-perturbed screen UV with a Schlick fresnel + the sun's specular streak - reaching the WebGL2/GLES3 web+device path on the glsl300es probe; the GLES2/WebGL1 floor and a Vulkan/Metal context answer false pending their variant. next = the native planar-reflections subsystem: the hand-built reflection workspace renders the mirror RTT WITH its mip chain, and the mirror-on water material compensates HlmsPbs's physically-attenuated env term (unit specular, near-mip-0 roughness, a deeper body dim + halved scatter) so the mirrored scene reads at the classic paint's strength - probe-verified on both flavors by water_reflection_looks_right (diagnose with ORKIGE_DUMP_MIRROR=<png>). Composes with screen-space refraction; off/unsupported keeps the byte-stable sky-reflection look")

	//! the capability identities (expanded from ORKIGE_RENDER_CAPS)
	enum class RenderCaps
	{
#define ORKIGE_RENDER_CAP_ENUM(Ident, Name, Kind, Desc) Ident,
		ORKIGE_RENDER_CAPS(ORKIGE_RENDER_CAP_ENUM)
#undef ORKIGE_RENDER_CAP_ENUM
		Count	//!< sentinel: the number of capabilities
	};

	//! @brief the stable string name of a capability (matches the Lua/MCP keys
	//! and the matrix column). Returns "" for Count / out-of-range.
	inline char const * renderCapName(RenderCaps cap)
	{
		switch(cap)
		{
#define ORKIGE_RENDER_CAP_NAME(Ident, Name, Kind, Desc) case RenderCaps::Ident: return Name;
		ORKIGE_RENDER_CAPS(ORKIGE_RENDER_CAP_NAME)
#undef ORKIGE_RENDER_CAP_NAME
		case RenderCaps::Count: break;
		}
		return "";
	}

	//! @brief parse a capability name back to its enum; returns RenderCaps::Count
	//! (the invalid sentinel) for an unknown name.
	inline RenderCaps parseRenderCap(String const & name)
	{
		for(int each = 0; each < static_cast<int>(RenderCaps::Count); ++each)
		{
			RenderCaps const cap = static_cast<RenderCaps>(each);
			if(name == renderCapName(cap))
			{
				return cap;
			}
		}
		return RenderCaps::Count;
	}
}

#endif //__RenderCaps_h__16_7_2026__09_00_00__
