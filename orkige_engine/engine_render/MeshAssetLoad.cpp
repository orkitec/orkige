/********************************************************************
	created:	Thursday 2026/07/30 at 10:00
	filename: 	MeshAssetLoad.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MeshAssetLoad.cpp
//! @brief the `.omesh` asset road: text -> a named mesh RESOURCE, implemented
//! ONCE over the facade rather than twice inside the backends
//! @remarks `RenderWorld::ensureMeshAsset` is the only facade method whose body
//! lives here instead of in a backend directory, and deliberately so: every step
//! it takes is already flavor-neutral (read the text through the resource
//! system, parse it with the pure `core_util/MeshAsset`, resolve each section's
//! `.omat` through `RenderSystem::createMaterial`, hand the geometry to
//! `RenderWorld::createMeshFromData`), so a second copy in
//! engine_render_classic/ and engine_render_next/ would be two chances for the
//! two flavors to disagree about what a `.omesh` means. Both backends call it
//! from `createMeshInstance`, which is what makes a `.omesh` reachable from
//! ModelComponent, the editor's preview stage, a scene load and a selfcheck
//! alike with no procedural branch in any of them.
//!
//! The `Omat/<file>` material naming is SHARED WITH ModelComponent on purpose:
//! one live renderer material per `.omat` asset, so a mesh section and a
//! component's material slot naming the same file get the same material.

#include "engine_render/RenderWorld.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderMaterial.h"

#include <core_debug/DebugMacros.h>
#include <core_util/MaterialAsset.h>
#include <core_util/MeshAsset.h>
#include <core_util/VectorShapeAsset.h>

#include <vector>

namespace Orkige
{
	namespace
	{
		//! @brief resolve `.oshape` references for `extrude`/`revolve` through
		//! the resource system - the injected seam that keeps the `.omesh`
		//! parser itself filesystem-free (@see MeshAsset::ShapeSource)
		struct ResourceShapeSource : public MeshAsset::ShapeSource
		{
			bool loadShape(String const & name,
				std::vector<VectorTessellator::Region> & outRegions)
				const override
			{
				RenderSystem* render = RenderSystem::get();
				String text;
				if(!render || !render->readResourceText(name, text))
				{
					return false;
				}
				return VectorShapeAsset::parse(text, outRegions);
			}
		};
		//! @brief turn a section's AUTHORED material reference into a live
		//! renderer material name, creating it from its `.omat` on first use.
		//! A bare `stone` means `stone.omat` (what the grammar documents); an
		//! unresolvable or unparsable reference logs once and leaves the section
		//! on the backend default rather than failing the whole mesh - a missing
		//! material must not cost the geometry.
		String resolveMaterial(String const & reference)
		{
			if(reference.empty())
			{
				return String();
			}
			RenderSystem* render = RenderSystem::get();
			if(!render)
			{
				return String();
			}
			const String file = (reference.find('.') == String::npos)
				? (reference + ".omat") : reference;
			const String materialName = "Omat/" + file;
			String text;
			if(!render->readResourceText(file, text))
			{
				oDebugWarn("engine", 0, "RenderWorld: .omesh material '"
					<< file << "' not found - the section keeps the default "
					"material");
				return String();
			}
			MaterialAsset::ParsedMaterial parsed;
			String parseError;
			if(!MaterialAsset::parse(text, parsed, &parseError))
			{
				oDebugWarn("engine", 0, "RenderWorld: .omesh material '"
					<< file << "' failed to parse (" << parseError
					<< ") - the section keeps the default material");
				return String();
			}
			RenderMaterialDesc desc;
			desc.albedo = Color(parsed.albedo.r, parsed.albedo.g,
				parsed.albedo.b, parsed.albedo.a);
			desc.albedoTexture = parsed.albedoTexture;
			desc.metalness = parsed.metalness;
			desc.roughness = parsed.roughness;
			desc.normalTexture = parsed.normalTexture;
			desc.emissive = Color(parsed.emissive.r, parsed.emissive.g,
				parsed.emissive.b, 1.0f);
			desc.emissiveTexture = parsed.emissiveTexture;
			desc.alphaTest = parsed.alphaTest;
			desc.twoSided = parsed.twoSided;
			// create-or-update, idempotent per name (the ModelComponent
			// convention: editing the .omat updates every user live)
			render->createMaterial(materialName, desc);
			return materialName;
		}
	}
	//---------------------------------------------------------
	bool RenderWorld::ensureMeshAsset(String const & meshName)
	{
		if(!MeshAsset::isMeshAssetName(meshName))
		{
			return true;	// not our asset kind - nothing to build
		}
		if(this->generatedMeshExists(meshName))
		{
			return true;	// idempotent: first use built it
		}
		RenderSystem* render = RenderSystem::get();
		String text;
		if(!render || !render->readResourceText(meshName, text))
		{
			oDebugError("engine", 0, "RenderWorld: mesh asset '" << meshName
				<< "' not found");
			return false;
		}
		MeshBuilder::Mesh data;
		String parseError;
		ResourceShapeSource shapes;
		if(!MeshAsset::parse(text, data, &shapes, &parseError))
		{
			oDebugError("engine", 0, "RenderWorld: mesh asset '" << meshName
				<< "': " << parseError);
			return false;
		}
		// the authored material references become live materials; the section
		// list the backend sees carries renderer material names only
		for(std::size_t each = 0; each < data.sections.size(); ++each)
		{
			data.sections[each].material =
				resolveMaterial(data.sections[each].material);
		}
		return this->createMeshFromData(meshName, data);
	}
}
