/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	LightComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/LightComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>

#include <algorithm>

namespace Orkige
{
	namespace
	{
		//! map the component's reflection enum onto the facade's light kind
		RenderLight::LightType toFacadeType(LightComponent::LightType type)
		{
			switch(type)
			{
			case LightComponent::LT_DIRECTIONAL:	return RenderLight::LT_DIRECTIONAL;
			case LightComponent::LT_SPOT:			return RenderLight::LT_SPOT;
			default:								return RenderLight::LT_POINT;
			}
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	LightComponent::LightComponent()
	{
		this->mType = LightComponent::LT_POINT;
		this->mColour = Color::White;
		this->mIntensity = 1.0f;
		this->mRange = 20.0f;
		this->mInnerAngle = 30.0f;
		this->mOuterAngle = 40.0f;
		this->mCastsShadows = false;
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	LightComponent::~LightComponent()
	{
	}
	//---------------------------------------------------------
	void LightComponent::setType(LightType type)
	{
		this->mType = type;
		if(this->mLight)
		{
			this->mLight->setType(toFacadeType(this->mType));
		}
	}
	//---------------------------------------------------------
	void LightComponent::setColour(float red, float green, float blue)
	{
		this->mColour = Color(red, green, blue, 1.0f);
		this->applyColour();
	}
	//---------------------------------------------------------
	void LightComponent::setIntensity(float intensity)
	{
		this->mIntensity = std::max(intensity, 0.0f);
		this->applyColour();
	}
	//---------------------------------------------------------
	void LightComponent::setRange(float range)
	{
		this->mRange = std::max(range, 0.001f);
		if(this->mLight)
		{
			this->mLight->setRange(this->mRange);
		}
	}
	//---------------------------------------------------------
	void LightComponent::setInnerAngle(float degrees)
	{
		this->mInnerAngle = degrees;
		if(this->mLight)
		{
			this->mLight->setSpotAngles(Degree(this->mInnerAngle),
				Degree(this->mOuterAngle));
		}
	}
	//---------------------------------------------------------
	void LightComponent::setOuterAngle(float degrees)
	{
		this->mOuterAngle = degrees;
		if(this->mLight)
		{
			this->mLight->setSpotAngles(Degree(this->mInnerAngle),
				Degree(this->mOuterAngle));
		}
	}
	//---------------------------------------------------------
	void LightComponent::setCastsShadows(bool casts)
	{
		this->mCastsShadows = casts;
		if(this->mLight)
		{
			this->mLight->setCastShadows(this->mCastsShadows);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void LightComponent::onAdd()
	{
		oAssert(!this->mLight);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".LightComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);

		// create the facade light when a world is up (a UI-only host - or a
		// detached unit test - has none: the state stays on the component and
		// applies when a light later exists)
		if(RenderSystem::get() && RenderSystem::get()->getWorld())
		{
			this->mLight = RenderSystem::get()->getWorld()->createLight();
			this->mLight->attachTo(this->getNode());
			this->applyStateToLight();
		}
		this->applyVisibility();
	}
	//---------------------------------------------------------
	void LightComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mLight.reset();
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void LightComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	void LightComponent::applyStateToLight()
	{
		oAssert(this->mLight);
		this->mLight->setType(toFacadeType(this->mType));
		this->mLight->setRange(this->mRange);
		this->mLight->setSpotAngles(Degree(this->mInnerAngle),
			Degree(this->mOuterAngle));
		this->mLight->setCastShadows(this->mCastsShadows);
		this->applyColour();
	}
	//---------------------------------------------------------
	void LightComponent::applyColour()
	{
		if(!this->mLight)
		{
			return;
		}
		// intensity is the backend-neutral power knob: the facade takes colour
		// only, so scale the colour by it for both the diffuse and specular term
		const Color lit(this->mColour.r * this->mIntensity,
			this->mColour.g * this->mIntensity,
			this->mColour.b * this->mIntensity, 1.0f);
		this->mLight->setDiffuseColour(lit);
		this->mLight->setSpecularColour(lit);
	}
	//---------------------------------------------------------
	void LightComponent::applyVisibility()
	{
		oAssert(this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		const bool ownerActive =
			!componentOwner || componentOwner->isActiveInHierarchy();
		// a hidden node stops its attached light from contributing
		this->setVisible(ownerActive);
	}
	//---------------------------------------------------------
	void LightComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: type (enum), colour, intensity,
		// range, spot angles and the shadow flag are written by name off the
		// declared schema (@see loadComponentProperties)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void LightComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(LightComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(hasLight)
		OFUNC(setType)
		OFUNC(getType)
		OFUNC(setColour)
		OFUNC(setIntensity)
		OFUNC(getIntensity)
		OFUNC(setRange)
		OFUNC(getRange)
		OFUNC(setInnerAngle)
		OFUNC(getInnerAngle)
		OFUNC(setOuterAngle)
		OFUNC(getOuterAngle)
		OFUNC(setCastsShadows)
		OFUNC(getCastsShadows)
		// neutral enum value<->label table so the reflected `type` property can
		// resolve labels in every scripting config (the registry feed; there is
		// no sol enum block because LightType rides only the property registry)
		OENUM_REGISTER_START("LightType", LightComponent::LightType)
			OENUM_REGISTER_VALUE(LT_DIRECTIONAL)
			OENUM_REGISTER_VALUE(LT_POINT)
			OENUM_REGISTER_VALUE(LT_SPOT)
		OENUM_REGISTER_END
		// reflected schema: kind, colour, the intensity/range scalars, the spot
		// cone angles and the forward-compatible shadow flag. Order-independent
		// (matched by name on load); every field drives its component setter.
		OPROPERTY_ENUM("type", "LightType", getType, setType, Orkige::PROP_NONE)
		OPROPERTY("colour", Orkige::PropertyKind::Color, getColour, setColourValue, Orkige::PROP_NONE)
		OPROPERTY("intensity", Orkige::PropertyKind::Float, getIntensity, setIntensity, Orkige::PROP_NONE)
		OPROPERTY("range", Orkige::PropertyKind::Float, getRange, setRange, Orkige::PROP_NONE)
		OPROPERTY("innerAngle", Orkige::PropertyKind::Float, getInnerAngle, setInnerAngle, Orkige::PROP_NONE)
		OPROPERTY("outerAngle", Orkige::PropertyKind::Float, getOuterAngle, setOuterAngle, Orkige::PROP_NONE)
		OPROPERTY("castsShadows", Orkige::PropertyKind::Bool, getCastsShadows, setCastsShadows, Orkige::PROP_NONE)
	OOBJECT_END
}
