/**************************************************************
	created:	2010/08/30 at 21:38
	filename: 	ModelComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_util/MaterialAsset.h>
#include <core_debug/DebugMacros.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(ModelComponent, ModelSetEvent);
	IMPL_OWNED_EVENTTYPE(ModelComponent, ModelRemovedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ModelComponent::ModelComponent()
	{
		this->modelFileName = "";
		this->modelAssetId = "";
		this->materialFileName = "";
		this->materialAssetId = "";
		this->mCastShadows = true;
		this->mReceiveShadows = true;
		this->addDependency<TransformComponent>();
		this->eventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
	}
	//---------------------------------------------------------
	ModelComponent::~ModelComponent()
	{
	}
	//---------------------------------------------------------
	void ModelComponent::loadModel(String const & modelFileName)
	{
		oAssert(!modelFileName.empty());
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		oAssert(this->mNode);

		// callers may pass this->modelFileName itself (the material-clear
		// reload does) and removeModel() clears that member - copy before
		// anything can mutate what the parameter aliases
		const String fileName = modelFileName;

		if(this->mesh)
		{
			this->removeModel();
		}

		if(fileName.empty())
			return;

		// the facade resolves the mesh through every resource group
		// (engine media AND project assets); a load failure was already
		// logged - the component honestly stays empty then
		optr<MeshInstance> loaded =
			RenderSystem::get()->getWorld()->createMeshInstance(fileName);
		if(!loaded)
		{
			return;
		}

		this->mesh = loaded;
		this->modelFileName = fileName;
		// the asset id tracks the mesh: the open project's database knows it
		// ("" without a project, or for engine media - honest either way)
		this->modelAssetId = AssetDatabase::referenceIdForValue(
			fileName, "", AssetDatabase::REF_FILE_NAME);
		this->mesh->attachTo(this->getNode());
		// content attached to an already-hidden node does not inherit the
		// visibility - re-apply the owner's active state
		if(!componentOwner->isActiveInHierarchy())
		{
			this->setVisible(false);
		}
		// a recorded material reference renders over the imported materials,
		// then the shadow participation flags (a material apply resets the
		// backend's receive state, so the flags go LAST)
		this->applyMaterial();
		this->applyShadowFlags();
		this->eventData->setValue(fileName);
		componentOwner->triggerEvent(Event(ModelComponent::ModelSetEvent, this->eventData));
	}
	//---------------------------------------------------------
	void ModelComponent::removeModel()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);

		// RAII: dropping the handle detaches and destroys the backend entity
		// (the historical NodeUtil wipe chain)
		this->mesh.reset();

		this->eventData->setValue(this->modelFileName);
		componentOwner->triggerEvent(Event(ModelComponent::ModelRemovedEvent, this->eventData));
		this->modelFileName = "";
		this->modelAssetId = "";
	}
	//---------------------------------------------------------
	void ModelComponent::setModelReference(String const & modelFileName)
	{
		if(modelFileName.empty())
		{
			// a live mesh tears down through removeModel (needs the owner); a
			// detached component (no mesh) just clears the recorded reference
			if(this->mesh)
			{
				this->removeModel();
			}
			else
			{
				this->modelFileName = "";
				this->modelAssetId = "";
			}
			return;
		}
		if(this->mNode)
		{
			this->loadModel(modelFileName);
			return;
		}
		// a detached load (no scene node yet): record the reference so a re-save
		// keeps it; the live mesh is built when the component gets its node
		this->modelFileName = modelFileName;
		this->modelAssetId = AssetDatabase::referenceIdForValue(
			modelFileName, "", AssetDatabase::REF_FILE_NAME);
	}
	//---------------------------------------------------------
	void ModelComponent::setMaterialReference(String const & materialFileName)
	{
		if(materialFileName.empty())
		{
			this->materialFileName = "";
			this->materialAssetId = "";
			// the only road back to the IMPORTED materials is a fresh mesh
			// instance - reload the model (a detached component has nothing
			// live to restore)
			if(this->mesh && !this->modelFileName.empty())
			{
				this->loadModel(this->modelFileName);
			}
			return;
		}
		this->materialFileName = materialFileName;
		this->materialAssetId = AssetDatabase::referenceIdForValue(
			materialFileName, "", AssetDatabase::REF_FILE_NAME);
		// applies live when a mesh exists; otherwise the recorded reference
		// applies when the model loads (loadModel calls applyMaterial). The
		// shadow flags re-apply after it - a fresh material assignment
		// resets the backend's receive state.
		this->applyMaterial();
		this->applyShadowFlags();
	}
	//---------------------------------------------------------
	void ModelComponent::setCastShadows(bool casts)
	{
		this->mCastShadows = casts;
		this->applyShadowFlags();
	}
	//---------------------------------------------------------
	void ModelComponent::setReceiveShadows(bool receives)
	{
		this->mReceiveShadows = receives;
		this->applyShadowFlags();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ModelComponent::applyMaterial()
	{
		if(!this->mesh || this->materialFileName.empty())
		{
			return;
		}
		RenderSystem* render = RenderSystem::get();
		oAssert(render);	// there is a mesh, so a render system is up
		String text;
		if(!render->readResourceText(this->materialFileName, text))
		{
			oDebugError("engine", 0, "ModelComponent: material '"
				<< this->materialFileName << "' not found - the mesh keeps "
				"its current materials");
			return;
		}
		MaterialAsset::ParsedMaterial parsed;
		String parseError;
		if(!MaterialAsset::parse(text, parsed, &parseError))
		{
			oDebugError("engine", 0, "ModelComponent: material '"
				<< this->materialFileName << "' failed to parse (" << parseError
				<< ") - the mesh keeps its current materials");
			return;
		}
		// ONE live renderer material per asset, shared by every component
		// referencing it (create-or-update: a re-apply after an asset edit
		// updates all of them)
		RenderMaterialDesc desc;
		desc.albedo = Color(parsed.albedo.r, parsed.albedo.g, parsed.albedo.b,
			parsed.albedo.a);
		desc.albedoTexture = parsed.albedoTexture;
		desc.metalness = parsed.metalness;
		desc.roughness = parsed.roughness;
		desc.normalTexture = parsed.normalTexture;
		desc.emissive = Color(parsed.emissive.r, parsed.emissive.g,
			parsed.emissive.b, 1.0f);
		desc.emissiveTexture = parsed.emissiveTexture;
		const String materialName = "Omat/" + this->materialFileName;
		render->createMaterial(materialName, desc);	// texture misses logged
		this->mesh->setMaterial(materialName);		// refusals logged + kept out
	}
	//---------------------------------------------------------
	void ModelComponent::applyShadowFlags()
	{
		if(!this->mesh)
		{
			return;	// a detached component records the flags for the load
		}
		this->mesh->setCastShadows(this->mCastShadows);
		this->mesh->setReceiveShadows(this->mReceiveShadows);
	}
	//---------------------------------------------------------
	void ModelComponent::onAdd()
	{
		oAssert(this->modelFileName.empty());
		oAssert(!this->mesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(componentOwner->getObjectID() + ".ModelComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
	}
	//---------------------------------------------------------
	void ModelComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mesh.reset();
		this->modelFileName = "";
		this->modelAssetId = "";
		this->materialFileName = "";
		this->materialAssetId = "";
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void ModelComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			// only over the model's OWN node (child GameObjects gate themselves)
			this->setVisible(activeInHierarchy);
		}
	}
	//---------------------------------------------------------
	void ModelComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: the mesh and material
		// AssetRefs (their stable asset ids ride in the records' reference
		// fields for rename survival) are the serialized fields; runtime
		// tweaks applied to the mesh instance after loadModel (unlit fixup,
		// visibility, ...) are not serialized
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void ModelComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		// the mesh AssetRef is resolved against the active AssetDatabase (a
		// resolving id wins over a stale name - rename survival) then set through
		// setModelReference, which loads the model when the scene node exists
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(ModelComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadModel)
		OFUNCCR(getCurrentModelFileName)
		OFUNC(setMaterialReference)
		OFUNCCR(getMaterialFileName)
		// reflected schema: the mesh reference (AssetRef, asset-kind
		// "mesh"); its stable id rides the record so a project rename
		// survives. The material reference (AssetRef, asset-kind "material" -
		// a `.omat`) follows the same rules and is declared AFTER the mesh so
		// an in-order apply loads the model first (either order works: a
		// material set before the mesh is recorded and applied by loadModel).
		OPROPERTY_REF("mesh", Orkige::PropertyKind::AssetRef, "mesh", getCurrentModelFileName, setModelReference, Orkige::PROP_NONE)
		OPROPERTY_REF("material", Orkige::PropertyKind::AssetRef, "material", getMaterialFileName, setMaterialReference, Orkige::PROP_NONE)
		// per-instance shadow participation (@see class remarks) - the flags
		// follow the mesh/material fields so an in-order apply loads first
		OPROPERTY("castShadows", Orkige::PropertyKind::Bool, getCastShadows, setCastShadows, Orkige::PROP_NONE)
		OPROPERTY("receiveShadows", Orkige::PropertyKind::Bool, getReceiveShadows, setReceiveShadows, Orkige::PROP_NONE)

		// self.model / world.getModel(id) hand Lua a WEAK handle: locks per call,
		// raises an honest error naming the owner once gone. @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::ModelComponent, "ModelComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(loadModel)
			OWEAKHANDLE_BASEMETHOD(getCurrentModelFileName)
			OWEAKHANDLE_BASEMETHOD(setMaterialReference)
			OWEAKHANDLE_BASEMETHOD(getMaterialFileName)
		OWEAKHANDLE_END
	OOBJECT_END
}
