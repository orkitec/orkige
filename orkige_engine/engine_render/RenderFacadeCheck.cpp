/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderFacadeCheck.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @file RenderFacadeCheck.cpp
//! @brief compile check for the engine_render facade headers
//! @remarks The facade started design-only: no backend implemented it yet
//! (the classic backend was the first one, see
//! Docs/render-abstraction.md). This TU keeps every facade header honest -
//! self-contained, Ogre-free except through engine_render/RenderMath.h -
//! by compiling them all in one place. It defines nothing and can be
//! removed once the classic backend TUs include these headers themselves.

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "engine_render/MeshInstance.h"
#include "engine_render/RenderMaterial.h"
#include "engine_render/RenderWater.h"
#include "engine_render/SpriteQuad.h"
#include "engine_render/RenderCamera.h"
#include "engine_render/RenderLight.h"
#include "engine_render/RenderTexture.h"
#include "engine_render/DrawLayer2D.h"
#include "engine_render/DrawLayer2DClip.h"

namespace Orkige
{
	// exactly one backend links per binary (build-time selection - ODR);
	// the flavor macro must always be there and be unambiguous
#if !defined(ORKIGE_RENDER_CLASSIC) && !defined(ORKIGE_RENDER_NEXT)
#	error "no render backend selected - the build must define ORKIGE_RENDER_CLASSIC or ORKIGE_RENDER_NEXT (see ORKIGE_RENDER_BACKEND in CMake)"
#endif
#if defined(ORKIGE_RENDER_CLASSIC) && defined(ORKIGE_RENDER_NEXT)
#	error "both render backends selected - classic OGRE and Ogre-Next cannot link into one binary (ODR)"
#endif

	// the math vocabulary must stay layout-compatible with plain float
	// arrays (the later own-type swap in RenderMath.h relies on it)
	static_assert(sizeof(Vec3) == 3 * sizeof(Real), "Vec3 must stay 3 packed Reals");
	static_assert(sizeof(Quat) == 4 * sizeof(Real), "Quat must stay 4 packed Reals");
	static_assert(sizeof(Color) == 4 * sizeof(float), "Color must stay 4 packed floats");

	//---------------------------------------------------------
	// The ONE definition of the per-(texture,sampler) material/datablock cache
	// key, compiled into BOTH render flavors (this facade TU links in each) so
	// the classic material name and the next datablock name can NEVER drift.
	// Growing the key past the bare "Sprite/<tex>" is the correctness fix: two
	// sprites of the same texture but different sampling must not share (and so
	// stomp) one material.
	String SpriteQuad::samplerName(String const & textureName,
		FilterMode filter, AddressMode addressing)
	{
		const String filterToken =
			(filter == FILTER_POINT) ? "point" : "bilinear";
		const String addressToken =
			(addressing == ADDRESS_WRAP) ? "wrap" : "clamp";
		return "Sprite/" + textureName + "#" + filterToken + "-" + addressToken;
	}
}
