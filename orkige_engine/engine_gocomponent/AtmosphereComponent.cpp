/**************************************************************
	created:	2026/07/24 at 18:00
	filename: 	AtmosphereComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/AtmosphereComponent.h"
#include <core_script/ScriptRuntime.h>	// OSCRIPT_HANDLE: ScriptComponentAccess registry
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>

#include <algorithm>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! the ownership registry (add order = the "first active" order). Plain
		//! statics: components live on the ONE main thread, and the vector only
		//! holds instances between their onAdd and onRemove.
		std::vector<AtmosphereComponent*> gInstances;
		//! the instance currently driving the world atmosphere (never dangling:
		//! refreshed on every add/remove/activation change)
		AtmosphereComponent* gOwner = NULL;
		//! the NON-COMPONENT atmosphere that was armed when the first instance
		//! took over - what the last instance hands back (a script-armed sky,
		//! or the disabled default). Shared across promotions on purpose:
		//! switching owners never re-snapshots, so nesting stays exact.
		AtmosphereDesc gHandBack;
		bool gHandBackValid = false;

		//! the live render world, or NULL (headless tests, teardown)
		RenderWorld* liveWorld()
		{
			RenderSystem* renderSystem = RenderSystem::get();
			return renderSystem ? renderSystem->getWorld() : NULL;
		}

		//! is this instance allowed to own: attached to an object that is
		//! active in the hierarchy (the component's `enabled` field is NOT an
		//! eligibility gate - an enabled=false owner arms a disabled sky, the
		//! authored "no sky" statement)
		bool eligible(AtmosphereComponent* component)
		{
			GameObject* componentOwner = component->getGameObject();
			return componentOwner != NULL && componentOwner->isActiveInHierarchy();
		}

		//! the owning object's id for log lines ("<detached>" defensively)
		String ownerId(AtmosphereComponent* component)
		{
			GameObject* componentOwner = component->getGameObject();
			return componentOwner ? componentOwner->getObjectID()
				: String("<detached>");
		}
	}
	//---------------------------------------------------------
	//--- private helpers (file scope needs the class) --------
	//---------------------------------------------------------
	namespace
	{
		//! @brief recompute the owner = the FIRST eligible instance in add
		//! order, and move the world atmosphere across the transition:
		//! none->first captures the hand-back snapshot and arms the owner;
		//! owner->owner applies the new owner's desc (promotion or take-back);
		//! last->none restores the snapshot exactly.
		void refreshAtmosphereOwnership()
		{
			AtmosphereComponent* first = NULL;
			for(AtmosphereComponent* each : gInstances)
			{
				if(eligible(each))
				{
					first = each;
					break;
				}
			}
			if(first == gOwner)
			{
				return;	// no transition
			}
			RenderWorld* world = liveWorld();
			if(gOwner == NULL && first != NULL)
			{
				// first take-over: remember the non-component state to hand back
				gHandBack = world ? world->getAtmosphere() : AtmosphereDesc();
				gHandBackValid = true;
			}
			gOwner = first;
			if(first != NULL)
			{
				oDebugMsg("scene", 0, "AtmosphereComponent: '" << ownerId(first)
					<< "' now owns the scene atmosphere");
				if(world)
				{
					world->setAtmosphere(first->buildDesc());
				}
			}
			else
			{
				// the last eligible instance went away: hand back exactly what
				// was armed before any component took over
				if(world && gHandBackValid)
				{
					world->setAtmosphere(gHandBack);
				}
				gHandBackValid = false;
			}
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	AtmosphereComponent::AtmosphereComponent()
	{
		// a placed component means "this scene has a sky": seed the tested DAY
		// look, enabled - the pleasant default a fresh Environment ships with
		this->mDesc = AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
		this->mEnabled = true;
		this->mPreset = AtmospherePreset::skyName(AtmospherePreset::SKY_DAY);
	}
	//---------------------------------------------------------
	AtmosphereComponent::~AtmosphereComponent()
	{
		// defensive: onRemove unregisters; a component destroyed without it
		// must never leave a dangling registry entry
		std::vector<AtmosphereComponent*>::iterator found =
			std::find(gInstances.begin(), gInstances.end(), this);
		if(found != gInstances.end())
		{
			gInstances.erase(found);
			refreshAtmosphereOwnership();
		}
	}
	//---------------------------------------------------------
	bool AtmosphereComponent::isAtmosphereOwner() const
	{
		return gOwner == this;
	}
	//---------------------------------------------------------
	AtmosphereComponent * AtmosphereComponent::getAtmosphereOwner()
	{
		return gOwner;
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setEnabled(bool enabled)
	{
		this->mEnabled = enabled;
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setPreset(String const & preset)
	{
		AtmospherePreset::Sky sky = AtmospherePreset::SKY_CUSTOM;
		if(!AtmospherePreset::parseSky(preset, sky))
		{
			oDebugWarn("scene", 0, "AtmosphereComponent: unknown preset '"
				<< preset << "' (custom/day/sunset/night) - keeping '"
				<< this->mPreset << "'");
			return;
		}
		this->mPreset = AtmospherePreset::skyName(sky);
		if(sky != AtmospherePreset::SKY_CUSTOM)
		{
			// seed EVERY look field from the tested preset; the explicit field
			// setters then override individual values (the precedence rule).
			// mEnabled stays what it is - the master switch is independent. The
			// sky/fog PART switches are look fields the preset does NOT own, so
			// carry them across the reseed (forSky returns them at their true
			// defaults - a reseed must never silently turn a hidden dome/fog back
			// on).
			const bool keepSky = this->mDesc.sky;
			const bool keepFog = this->mDesc.fog;
			this->mDesc = AtmospherePreset::forSky(sky);
			this->mDesc.sky = keepSky;
			this->mDesc.fog = keepFog;
		}
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setSky(bool sky)
	{
		this->mDesc.sky = sky;
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setFog(bool fog)
	{
		this->mDesc.fog = fog;
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setSkyColour(float red, float green, float blue)
	{
		this->mDesc.skyRed = red;
		this->mDesc.skyGreen = green;
		this->mDesc.skyBlue = blue;
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setSkyPower(float power)
	{
		this->mDesc.skyPower = std::max(power, 0.0f);
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setDensity(float density)
	{
		this->mDesc.density = std::max(density, 0.0f);
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setSunPower(float power)
	{
		this->mDesc.sunPower = std::max(power, 0.0f);
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setAmbientPower(float power)
	{
		this->mDesc.ambientPower = std::max(power, 0.0f);
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setFogDensity(float density)
	{
		this->mDesc.fogDensity = std::max(density, 0.0f);
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::setFogColour(float red, float green, float blue)
	{
		this->mDesc.fogRed = red;
		this->mDesc.fogGreen = green;
		this->mDesc.fogBlue = blue;
		this->applyIfOwner();
	}
	//---------------------------------------------------------
	AtmosphereDesc AtmosphereComponent::buildDesc() const
	{
		AtmosphereDesc desc = this->mDesc;
		desc.enabled = this->mEnabled;
		return desc;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void AtmosphereComponent::onAdd()
	{
		gInstances.push_back(this);
		refreshAtmosphereOwnership();
		if(gOwner != this && eligible(this))
		{
			// the dormancy contract: exactly one honest line per instance
			oDebugMsg("scene", 0, "AtmosphereComponent: '" << ownerId(this)
				<< "' is dormant - '" << (gOwner ? ownerId(gOwner)
					: String("<none>")) << "' owns the atmosphere");
		}
	}
	//---------------------------------------------------------
	void AtmosphereComponent::onRemove()
	{
		std::vector<AtmosphereComponent*>::iterator found =
			std::find(gInstances.begin(), gInstances.end(), this);
		if(found != gInstances.end())
		{
			gInstances.erase(found);
		}
		refreshAtmosphereOwnership();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::onSetActive(bool activeInHierarchy)
	{
		refreshAtmosphereOwnership();
	}
	//---------------------------------------------------------
	void AtmosphereComponent::applyIfOwner()
	{
		if(gOwner != this)
		{
			return;	// dormant: only store (the live re-arm is the owner's)
		}
		if(RenderWorld* world = liveWorld())
		{
			world->setAtmosphere(this->buildDesc());
		}
	}
	//---------------------------------------------------------
	void AtmosphereComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization off the declared schema; the
		// schema declares preset BEFORE the look fields, so a load seeds the
		// preset first and the explicit fields override (the precedence rule)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void AtmosphereComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(AtmosphereComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(isAtmosphereOwner)
		OFUNC(setEnabled)
		OFUNC(getEnabled)
		OFUNC(setPreset)
		OFUNC(getPreset)
		OFUNC(setSky)
		OFUNC(getSky)
		OFUNC(setFog)
		OFUNC(getFog)
		OFUNC(setSkyColour)
		OFUNC(setSkyPower)
		OFUNC(getSkyPower)
		OFUNC(setDensity)
		OFUNC(getDensity)
		OFUNC(setSunPower)
		OFUNC(getSunPower)
		OFUNC(setAmbientPower)
		OFUNC(getAmbientPower)
		OFUNC(setFogDensity)
		OFUNC(getFogDensity)
		OFUNC(setFogColour)
		// reflected schema - DECLARATION ORDER IS THE PRECEDENCE RULE: preset
		// seeds every look field, the explicit fields after it override (loads
		// apply in this order, so seeded-then-overridden state round-trips)
		OPROPERTY("enabled", Orkige::PropertyKind::Bool, getEnabled, setEnabled, Orkige::PROP_NONE)
		OPROPERTY("preset", Orkige::PropertyKind::String, getPreset, setPreset, Orkige::PROP_NONE)
		// the two PART switches sit AFTER preset (so a load applies them after
		// the preset seed, never clobbered by it) but are NOT preset-seeded look
		// fields - the master enabled gates both, sky = the visible dome, fog =
		// the distance fog (@see AtmosphereDesc::sky / fog)
		OPROPERTY("sky", Orkige::PropertyKind::Bool, getSky, setSky, Orkige::PROP_NONE)
		OPROPERTY("fog", Orkige::PropertyKind::Bool, getFog, setFog, Orkige::PROP_NONE)
		OPROPERTY("skyColour", Orkige::PropertyKind::Color, getSkyColour, setSkyColourValue, Orkige::PROP_NONE)
		OPROPERTY("skyPower", Orkige::PropertyKind::Float, getSkyPower, setSkyPower, Orkige::PROP_NONE)
		OPROPERTY("density", Orkige::PropertyKind::Float, getDensity, setDensity, Orkige::PROP_NONE)
		OPROPERTY("sunPower", Orkige::PropertyKind::Float, getSunPower, setSunPower, Orkige::PROP_NONE)
		OPROPERTY("ambientPower", Orkige::PropertyKind::Float, getAmbientPower, setAmbientPower, Orkige::PROP_NONE)
		OPROPERTY("fogDensity", Orkige::PropertyKind::Float, getFogDensity, setFogDensity, Orkige::PROP_NONE)
		OPROPERTY("fogColour", Orkige::PropertyKind::Color, getFogColour, setFogColourValue, Orkige::PROP_NONE)

		// world.getAtmosphere(id) hands Lua a WEAK handle (locks per call,
		// honest error once the object is gone). @see CameraComponent.
		OWEAKHANDLE_BEGIN(Orkige::AtmosphereComponent, "AtmosphereComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(isAtmosphereOwner)
			OWEAKHANDLE_BASEMETHOD(setEnabled)
			OWEAKHANDLE_BASEMETHOD(getEnabled)
			OWEAKHANDLE_BASEMETHOD(setPreset)
			OWEAKHANDLE_BASEMETHOD(getPreset)
			OWEAKHANDLE_BASEMETHOD(setSky)
			OWEAKHANDLE_BASEMETHOD(getSky)
			OWEAKHANDLE_BASEMETHOD(setFog)
			OWEAKHANDLE_BASEMETHOD(getFog)
			OWEAKHANDLE_BASEMETHOD(setSkyColour)
			OWEAKHANDLE_BASEMETHOD(setSkyPower)
			OWEAKHANDLE_BASEMETHOD(getSkyPower)
			OWEAKHANDLE_BASEMETHOD(setDensity)
			OWEAKHANDLE_BASEMETHOD(getDensity)
			OWEAKHANDLE_BASEMETHOD(setSunPower)
			OWEAKHANDLE_BASEMETHOD(getSunPower)
			OWEAKHANDLE_BASEMETHOD(setAmbientPower)
			OWEAKHANDLE_BASEMETHOD(getAmbientPower)
			OWEAKHANDLE_BASEMETHOD(setFogDensity)
			OWEAKHANDLE_BASEMETHOD(getFogDensity)
			OWEAKHANDLE_BASEMETHOD(setFogColour)
		OWEAKHANDLE_END
		// world.getAtmosphere(id) + getComponent("atmosphere") (no self.<name>
		// - the environment is reached by id, not as a behavior sibling)
		OSCRIPT_HANDLE("atmosphere", false, "getAtmosphere")
	OOBJECT_END
}
