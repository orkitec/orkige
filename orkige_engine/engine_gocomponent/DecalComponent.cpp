/**************************************************************
	created:	2026/07/18 at 00:30
	filename: 	DecalComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/DecalComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_debug/DebugMacros.h>

#include <algorithm>

namespace Orkige
{
	// the engine decal media (Util/make_decal_textures.py writes them to
	// orkige_engine/media/decals/; the player/editor register that dir like the
	// water/font dirs and bundle it to exports)
	char const * const DecalComponent::DEFAULT_TEXTURE = "decal_mark.png";
	char const * const DecalComponent::BLOB_TEXTURE = "decal_blob.png";
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DecalComponent::DecalComponent()
	{
		this->mTexture = DEFAULT_TEXTURE;
		this->mTextureAssetId = "";
		this->mSizeX = 2.0f;
		this->mSizeZ = 2.0f;
		this->mProjectionDepth = 4.0f;
		this->mOpacity = 1.0f;
		this->mLifetime = 0.0f;		// permanent by default (a placed editor mark)
		this->mFadeDuration = 0.5f;
		this->mAge = 0.0f;
		this->mManualFadeDuration = 0.0f;
		this->mManualFadeElapsed = 0.0f;
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	DecalComponent::~DecalComponent()
	{
	}
	//---------------------------------------------------------
	void DecalComponent::setTexture(String const & texture)
	{
		this->mTexture = texture;
		this->mTextureAssetId = texture.empty()
			? String("")
			: AssetDatabase::referenceIdForValue(texture, "",
				AssetDatabase::REF_FILE_NAME);
		if(this->mDecal)
		{
			this->mDecal->setDiffuseTexture(this->mTexture);
		}
	}
	//---------------------------------------------------------
	void DecalComponent::setSizeX(float sizeX)
	{
		this->mSizeX = sizeX;
		if(this->mDecal)
		{
			this->mDecal->setSize(this->mSizeX, this->mSizeZ,
				this->mProjectionDepth);
		}
	}
	//---------------------------------------------------------
	void DecalComponent::setSizeZ(float sizeZ)
	{
		this->mSizeZ = sizeZ;
		if(this->mDecal)
		{
			this->mDecal->setSize(this->mSizeX, this->mSizeZ,
				this->mProjectionDepth);
		}
	}
	//---------------------------------------------------------
	void DecalComponent::setProjectionDepth(float projectionDepth)
	{
		this->mProjectionDepth = std::max(projectionDepth, 0.001f);
		if(this->mDecal)
		{
			this->mDecal->setSize(this->mSizeX, this->mSizeZ,
				this->mProjectionDepth);
		}
	}
	//---------------------------------------------------------
	void DecalComponent::setOpacity(float opacity)
	{
		this->mOpacity = std::max(0.0f, std::min(opacity, 1.0f));
		this->applyOpacity();
	}
	//---------------------------------------------------------
	void DecalComponent::setLifetime(float lifetime)
	{
		this->mLifetime = std::max(lifetime, 0.0f);
		this->applyOpacity();
	}
	//---------------------------------------------------------
	void DecalComponent::setFadeDuration(float fadeDuration)
	{
		this->mFadeDuration = std::max(fadeDuration, 0.0f);
		this->applyOpacity();
	}
	//---------------------------------------------------------
	void DecalComponent::place(Vec3 const & position, Vec3 const & upNormal)
	{
		GameObject* componentOwner = this->getComponentOwner();
		if(!componentOwner)
		{
			return;
		}
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		if(transformComponent)
		{
			// orient the owner so its +Y faces the surface normal (the decal
			// hangs off a child node projecting down the owner's -Y). A degenerate
			// normal falls back to straight-down (identity).
			Vec3 normal = upNormal;
			if(normal.squaredLength() < 1e-6f)
			{
				normal = Vec3::UNIT_Y;
			}
			normal.normalise();
			const Quat orientation = Vec3::UNIT_Y.getRotationTo(normal);
			transformComponent->teleport(position, orientation);
		}
		// re-stamp: reset the age + any running fade, so the mark reads fresh
		this->mAge = 0.0f;
		this->mManualFadeDuration = 0.0f;
		this->mManualFadeElapsed = 0.0f;
		if(this->mDecal && componentOwner->isActiveInHierarchy())
		{
			this->mDecal->setVisible(true);
		}
		this->applyOpacity();
	}
	//---------------------------------------------------------
	void DecalComponent::fade(float durationSeconds)
	{
		// an immediate fade-out; a non-positive duration is an instant hide
		this->mManualFadeDuration = std::max(durationSeconds, 0.0f);
		this->mManualFadeElapsed = 0.0f;
		this->applyOpacity();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DecalComponent::onAdd()
	{
		oAssert(!this->mDecal);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".DecalComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
		// build now: a placed/loaded decal shows its static preview immediately
		// (the editor never ticks it - dormancy)
		this->buildDecal();
	}
	//---------------------------------------------------------
	void DecalComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mDecal.reset();
		this->setWantsUpdates(false);
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void DecalComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mDecal)
		{
			// the facade decal's own (owner) visibility gate - ANDed with the
			// world budget inside the facade (@see RenderDecal)
			this->mDecal->setVisible(activeInHierarchy);
		}
	}
	//---------------------------------------------------------
	void DecalComponent::onUpdateComponent(float deltaTime)
	{
		if(!this->mDecal)
		{
			return;
		}
		// the single per-frame animation site: age the mark and ramp the fade.
		// Only reached under a GameObject-ticking runtime (the player), so the
		// editor leaves the mark static.
		this->mAge += deltaTime;
		if(this->mManualFadeDuration > 0.0f)
		{
			this->mManualFadeElapsed += deltaTime;
		}
		this->applyOpacity();
	}
	//---------------------------------------------------------
	void DecalComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: the texture AssetRef (its stable
		// id rides the record for rename survival), the size/opacity knobs and the
		// lifetime/fade timings; the live age + manual fade are not serialized
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void DecalComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		// the property setters run here; a set BEFORE onAdd only records state
		// (no decal yet), and buildDecal in onAdd applies the resolved state
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void DecalComponent::buildDecal()
	{
		if(this->mDecal || !this->mNode)
		{
			return;	// already built, or no scene node yet (a detached component)
		}
		// no world (a UI-only host / a detached unit test): keep the state on the
		// component and build when a decal later exists
		if(!RenderSystem::get() || !RenderSystem::get()->getWorld())
		{
			return;
		}
		this->mDecal = RenderSystem::get()->getWorld()->createDecal();
		this->mDecal->attachTo(this->getNode());
		this->applyStateToDecal();
		GameObject* componentOwner = this->getComponentOwner();
		this->mDecal->setVisible(
			!componentOwner || componentOwner->isActiveInHierarchy());
		// only a built decal ticks (like WaterComponent, only a GameObject-ticking
		// runtime reaches onUpdateComponent)
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	void DecalComponent::applyStateToDecal()
	{
		oAssert(this->mDecal);
		this->mDecal->setDiffuseTexture(this->mTexture);
		this->mDecal->setSize(this->mSizeX, this->mSizeZ,
			this->mProjectionDepth);
		this->applyOpacity();
	}
	//---------------------------------------------------------
	float DecalComponent::fadeFactor(float age, float lifetime,
		float fadeDuration, float manualFadeDuration, float manualFadeElapsed)
	{
		// a running manual fade wins over the lifetime ramp
		if(manualFadeDuration > 0.0f)
		{
			const float t = manualFadeElapsed / manualFadeDuration;
			return t >= 1.0f ? 0.0f : (1.0f - t);
		}
		if(lifetime > 0.0f)
		{
			if(age >= lifetime)
			{
				return 0.0f;	// expired - the mark is gone
			}
			const float remaining = lifetime - age;
			if(fadeDuration > 0.0f && remaining < fadeDuration)
			{
				return remaining / fadeDuration;
			}
		}
		return 1.0f;	// permanent (lifetime 0) or before the fade window
	}
	//---------------------------------------------------------
	float DecalComponent::currentOpacity() const
	{
		return this->mOpacity * DecalComponent::fadeFactor(this->mAge,
			this->mLifetime, this->mFadeDuration, this->mManualFadeDuration,
			this->mManualFadeElapsed);
	}
	//---------------------------------------------------------
	void DecalComponent::applyOpacity()
	{
		if(this->mDecal)
		{
			this->mDecal->setOpacity(this->currentOpacity());
		}
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(DecalComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(hasDecal)
		OFUNCCR(getTexture)
		OFUNC(setSizeX)
		OFUNC(getSizeX)
		OFUNC(setSizeZ)
		OFUNC(getSizeZ)
		OFUNC(setOpacity)
		OFUNC(getOpacity)
		OFUNC(setLifetime)
		OFUNC(getLifetime)
		OFUNC(setFadeDuration)
		OFUNC(getFadeDuration)
		// the Lua drive surface (self.decal:place(...) / :fade(...))
		OFUNC(place)
		OFUNC(fade)
		// reflected schema: the mark texture AssetRef (its stable id rides the
		// record for rename survival), the footprint size, the projection depth,
		// the base opacity and the lifetime/fade timings. Every field flows
		// through the ONE property registry (inspector, serialization, MCP, Lua).
		OPROPERTY_REF("texture", Orkige::PropertyKind::AssetRef, "texture", getTexture, setTexture, Orkige::PROP_NONE)
		OPROPERTY("sizeX", Orkige::PropertyKind::Float, getSizeX, setSizeX, Orkige::PROP_NONE)
		OPROPERTY("sizeZ", Orkige::PropertyKind::Float, getSizeZ, setSizeZ, Orkige::PROP_NONE)
		OPROPERTY("projectionDepth", Orkige::PropertyKind::Float, getProjectionDepth, setProjectionDepth, Orkige::PROP_NONE)
		OPROPERTY("opacity", Orkige::PropertyKind::Float, getOpacity, setOpacity, Orkige::PROP_NONE)
		OPROPERTY("lifetime", Orkige::PropertyKind::Float, getLifetime, setLifetime, Orkige::PROP_NONE)
		OPROPERTY("fadeDuration", Orkige::PropertyKind::Float, getFadeDuration, setFadeDuration, Orkige::PROP_NONE)
	OOBJECT_END
}
