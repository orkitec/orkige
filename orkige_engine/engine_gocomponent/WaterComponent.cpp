/**************************************************************
	created:	2026/07/12 at 20:30
	filename: 	WaterComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/WaterComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_debug/DebugMacros.h>

namespace Orkige
{
	// the shared engine water assets (Util/make_water_mesh.py writes them to
	// orkige_engine/media/water/; the player/editor register that dir like the
	// engine-default font dir)
	char const * const WaterComponent::PLANE_MESH_NAME = "water_plane.glb";
	char const * const WaterComponent::DEFAULT_NORMAL_TEXTURE = "water_normal.png";
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	WaterComponent::WaterComponent()
	{
		this->mMaterialName = "";
		this->mSizeX = 20.0f;
		this->mSizeZ = 20.0f;
		// the engine-default tiling water normal map (an AssetRef default: a
		// project may swap it, "" = a flat non-rippling surface)
		this->mDesc.normalTexture = DEFAULT_NORMAL_TEXTURE;
		this->mNormalAssetId = "";
		this->mScrollTime = 0.0f;
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	WaterComponent::~WaterComponent()
	{
	}
	//---------------------------------------------------------
	void WaterComponent::setSizeX(float sizeX)
	{
		this->mSizeX = sizeX;
		this->applyScale();
	}
	//---------------------------------------------------------
	void WaterComponent::setSizeZ(float sizeZ)
	{
		this->mSizeZ = sizeZ;
		this->applyScale();
	}
	//---------------------------------------------------------
	void WaterComponent::setDeepColour(Color const & colour)
	{
		this->mDesc.deepColour = colour;
		this->applyMaterial();
	}
	//---------------------------------------------------------
	void WaterComponent::setShallowColour(Color const & colour)
	{
		this->mDesc.shallowColour = colour;
		this->applyMaterial();
	}
	//---------------------------------------------------------
	void WaterComponent::setOpacity(float opacity)
	{
		this->mDesc.opacity = opacity;
		this->applyMaterial();
	}
	//---------------------------------------------------------
	void WaterComponent::setWaveScale(float waveScale)
	{
		this->mDesc.waveScale = waveScale;
		this->applyMaterial();
	}
	//---------------------------------------------------------
	void WaterComponent::setWaveSpeed(float waveSpeed)
	{
		this->mDesc.waveSpeed = waveSpeed;
		this->applyMaterial();
	}
	//---------------------------------------------------------
	void WaterComponent::setFresnelPower(float fresnelPower)
	{
		this->mDesc.fresnelPower = fresnelPower;
		this->applyMaterial();
	}
	//---------------------------------------------------------
	void WaterComponent::setNormalTexture(String const & normalTexture)
	{
		this->mDesc.normalTexture = normalTexture;
		this->mNormalAssetId = normalTexture.empty()
			? String("")
			: AssetDatabase::referenceIdForValue(normalTexture, "",
				AssetDatabase::REF_FILE_NAME);
		this->applyMaterial();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void WaterComponent::onAdd()
	{
		oAssert(!this->mMesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".WaterComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
		// build the surface now: a placed/loaded water object shows its static
		// preview immediately (the editor never ticks it - dormancy)
		this->buildSurface();
	}
	//---------------------------------------------------------
	void WaterComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mMesh.reset();
		this->mMaterialName = "";
		this->setWantsUpdates(false);
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void WaterComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			// only over the surface's OWN node (child GameObjects gate themselves)
			this->setVisible(activeInHierarchy);
		}
	}
	//---------------------------------------------------------
	void WaterComponent::onUpdateComponent(float deltaTime)
	{
		if(!this->mMesh || this->mMaterialName.empty())
		{
			return;
		}
		// the single per-frame animation site: advance the scroll clock and
		// drive the material's ripple (a cheap parameter update - no vertex
		// work). Only reached under a GameObject-ticking runtime (the player),
		// so the editor leaves the surface static.
		this->mScrollTime += deltaTime;
		RenderSystem::get()->setWaterTime(this->mMaterialName, this->mScrollTime);
	}
	//---------------------------------------------------------
	void WaterComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: the size, surface knobs and the
		// normal-map AssetRef (its stable id rides the record for rename
		// survival) are the serialized fields; the live scroll clock is not
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void WaterComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		// the property setters run here; a set BEFORE onAdd only records state
		// (no node yet), and buildSurface in onAdd applies the resolved state
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void WaterComponent::buildSurface()
	{
		if(this->mMesh || !this->mNode)
		{
			return;	// already built, or no scene node yet (a detached component)
		}
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		// the facade resolves the shared engine water plane through every
		// resource group; a load failure was already logged - stay empty then
		optr<MeshInstance> loaded =
			RenderSystem::get()->getWorld()->createMeshInstance(PLANE_MESH_NAME);
		if(!loaded)
		{
			oDebugError("engine", 0, "WaterComponent: the water plane mesh '"
				<< PLANE_MESH_NAME << "' did not load - the engine water media "
				"dir is not registered");
			return;
		}
		this->mMesh = loaded;
		this->mMaterialName = "Water/" + componentOwner->getObjectID();
		this->mMesh->attachTo(this->getNode());
		this->applyScale();
		this->applyMaterial();
		// content attached to an already-hidden node does not inherit the
		// visibility - re-apply the owner's active state
		if(!componentOwner->isActiveInHierarchy())
		{
			this->setVisible(false);
		}
		// only a built surface ticks (like ScriptComponent, only a
		// GameObject-ticking runtime reaches onUpdateComponent)
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	void WaterComponent::applyMaterial()
	{
		if(!this->mMesh || this->mMaterialName.empty())
		{
			return;	// nothing live to (re)skin yet
		}
		RenderSystem* render = RenderSystem::get();
		oAssert(render);
		// ONE per-instance water material (create-or-update: a re-apply after a
		// knob change updates the LIVE material). A missing normal map is logged
		// and the surface renders flat.
		render->createWaterMaterial(this->mMaterialName, this->mDesc);
		this->mMesh->setMaterial(this->mMaterialName);	// refusals logged + kept out
	}
	//---------------------------------------------------------
	void WaterComponent::applyScale()
	{
		if(this->mNode)
		{
			this->mNode->setScale(Vec3(this->mSizeX, 1.0f, this->mSizeZ));
		}
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(WaterComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getSizeX)
		OFUNC(setSizeX)
		OFUNC(getSizeZ)
		OFUNC(setSizeZ)
		OFUNC(getOpacity)
		OFUNC(setOpacity)
		OFUNC(getWaveScale)
		OFUNC(setWaveScale)
		OFUNC(getWaveSpeed)
		OFUNC(setWaveSpeed)
		OFUNC(getFresnelPower)
		OFUNC(setFresnelPower)
		OFUNCCR(getNormalTexture)
		// reflected schema: the plane size, the surface knobs and the tiling
		// normal-map AssetRef (its stable id rides the record for rename
		// survival). Every field flows through the ONE property registry
		// (inspector, serialization, MCP, Lua).
		OPROPERTY("sizeX", Orkige::PropertyKind::Float, getSizeX, setSizeX, Orkige::PROP_NONE)
		OPROPERTY("sizeZ", Orkige::PropertyKind::Float, getSizeZ, setSizeZ, Orkige::PROP_NONE)
		OPROPERTY("deepColour", Orkige::PropertyKind::Color, getDeepColour, setDeepColour, Orkige::PROP_NONE)
		OPROPERTY("shallowColour", Orkige::PropertyKind::Color, getShallowColour, setShallowColour, Orkige::PROP_NONE)
		OPROPERTY("opacity", Orkige::PropertyKind::Float, getOpacity, setOpacity, Orkige::PROP_NONE)
		OPROPERTY("waveScale", Orkige::PropertyKind::Float, getWaveScale, setWaveScale, Orkige::PROP_NONE)
		OPROPERTY("waveSpeed", Orkige::PropertyKind::Float, getWaveSpeed, setWaveSpeed, Orkige::PROP_NONE)
		OPROPERTY("fresnelPower", Orkige::PropertyKind::Float, getFresnelPower, setFresnelPower, Orkige::PROP_NONE)
		OPROPERTY_REF("normalTexture", Orkige::PropertyKind::AssetRef, "texture", getNormalTexture, setNormalTexture, Orkige::PROP_NONE)
	OOBJECT_END
}
