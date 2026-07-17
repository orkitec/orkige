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
	X(ScreenSpaceRefraction, "screenSpaceRefraction", PlannedAbsent, "screen-space refraction distortion through transparent surfaces (a compositor refraction pass) - absent on both flavors") \
	X(IblReflections, "iblReflections", PlannedAbsent, "image-based lighting: environment/reflection cubemaps on PBS materials - absent on both flavors")

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
