/********************************************************************
	created:	Sunday 2026/07/12 at 18:00
	filename: 	RenderMaterial.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderMaterial_h__12_7_2026__18_00_00__
#define __RenderMaterial_h__12_7_2026__18_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief description of ONE opaque scene-content surface in the
	//! metal-rough PBS vocabulary - the facade's material authoring surface
	//! @remarks Consumed by RenderSystem::createMaterial, which turns it into
	//! a named backend material MeshInstance::setMaterial can assign. Plain
	//! data on purpose: materials stay simple and GENERATED (no graphs, no
	//! script formats). Texture names resolve through the resource groups
	//! (engine media AND project assets), like sprite textures.
	//!
	//! The usual source is a parsed `.omat` asset (core_util/MaterialAsset -
	//! ModelComponent's `material` reference does exactly that), but any
	//! generator can fill one in directly.
	//!
	//! Backend mapping / capability (Docs/materials.md has the full matrix):
	//! next = an HLMS PBS datablock, metallic workflow - every field is
	//! native (albedo/metalness/roughness/normal map/emissive). The normal
	//! map needs mesh TANGENTS; the mesh importer generates them for every
	//! UV-mapped import. classic = an RTSS metal-rough material: a
	//! Cook-Torrance lighting stage (albedo -> diffuse colour + texture,
	//! metalness/roughness read off specular.xy, emissive colour ->
	//! self-illumination), an RTSS normal-map stage when a normal map is
	//! present (tangents built on demand when the material is applied) and
	//! an additive pass for the emissive map (opaque materials only). The
	//! maps render on both flavors; the shading model and ambient still
	//! differ, so lit content is not pixel-parity-gated. filament = lit
	//! material instance (baseColor/metallic/roughness/normal/emissive are
	//! native there too).
	//!
	//! Honest v1 boundaries (both flavors): opaque only, no reflection
	//! cubemap yet (image-based lighting lands with the sky/atmosphere
	//! surface; RenderWorld::setAmbientHemisphere is the current stand-in).
	struct ORKIGE_ENGINE_DLL RenderMaterialDesc
	{
		Color	albedo = Color(1.0f, 1.0f, 1.0f, 1.0f);	//!< base colour factor (multiplies the albedo texture)
		String	albedoTexture;			//!< base-colour map resource name ("" = none)
		float	metalness = 0.0f;		//!< 0..1 (0 = dielectric, 1 = metal)
		float	roughness = 1.0f;		//!< 0..1 (1 = fully rough)
		String	normalTexture;			//!< tangent-space normal map ("" = none; classic ignores it)
		Color	emissive = Color(0.0f, 0.0f, 0.0f, 1.0f);	//!< emitted colour (lighting-independent)
		String	emissiveTexture;		//!< emissive map ("" = none; classic ignores it)
	};
}

#endif //__RenderMaterial_h__12_7_2026__18_00_00__
