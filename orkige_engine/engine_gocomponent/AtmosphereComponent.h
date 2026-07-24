/********************************************************************
	created:	Friday 2026/07/24 at 18:00
	filename: 	AtmosphereComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __AtmosphereComponent_h__24_7_2026__18_00_00__
#define __AtmosphereComponent_h__24_7_2026__18_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include <core_util/AtmosphereDesc.h>

namespace Orkige
{
	//! @brief the scene-authored sky/fog atmosphere - a GameObject component
	//! over RenderWorld::setAtmosphere, so a scene CARRIES its sky instead of
	//! arming it from a script (@see AtmosphereDesc; both flavors by
	//! construction).
	//!
	//! AUTHORING MODEL (preset seeds, explicit fields override): `preset` is
	//! the friendly entry point - setting it to one of the tested looks
	//! ("day"/"sunset"/"night", @see AtmospherePreset) SEEDS every look field
	//! from that preset; the explicit fields (skyColour/skyPower/density/
	//! sunPower/ambientPower/fogDensity/fogColour) then override individual
	//! values and are the stored truth. The preset word stays as the label of
	//! the last seed (it never flips to "custom" on a field edit), and
	//! serialization writes preset BEFORE the fields (schema declaration
	//! order), so a loaded component reproduces exactly the seeded-then-
	//! overridden state. "custom" seeds nothing (the fields stand alone).
	//! `enabled` maps to AtmosphereDesc::enabled and is INDEPENDENT of the
	//! preset: an owning component with enabled=false arms a DISABLED
	//! atmosphere - the authored "this scene has no sky" statement. The sky
	//! VISUAL stays the procedural dome (skyType default); scripts keep
	//! engine:setAtmosphereSky for cubemap skies.
	//!
	//! TAKE-OVER CONTRACT (the atmosphere is GLOBAL render state): the FIRST
	//! instance in add order whose owner is activeInHierarchy OWNS the world
	//! atmosphere; every other instance is DORMANT (one log line) and only
	//! stores state. Deactivating/removing the owner PROMOTES the next
	//! eligible instance (multi-environment switching by object activation);
	//! reactivating an earlier instance takes the ownership back (the owner
	//! is ALWAYS the first active instance - deterministic). When the LAST
	//! eligible instance goes away the component hands back EXACTLY the
	//! atmosphere that was armed before any instance took over (a script-
	//! armed sky, or the disabled default).
	//!
	//! Runtime layering: the component is the authored BASE, applied when it
	//! becomes owner (scene load - before scripts ever tick - activation,
	//! promotion, property edit). It never re-applies per frame, so a
	//! script's engine:setAtmosphere/setAtmosphereBlend freely overrides it
	//! afterwards and WINS until the next ownership change. In the editor the
	//! same apply path arms the sky on scene load and re-arms live on every
	//! Inspector edit (render state only - no gameplay), so the Scene view
	//! and the Game Preview show the authored sky.
	class ORKIGE_ENGINE_DLL AtmosphereComponent : public GameObjectComponent
	{
		OOBJECT(AtmosphereComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		AtmosphereDesc	mDesc;		//!< the authored look fields (mDesc.enabled unused - @see mEnabled)
		bool			mEnabled;	//!< the armed desc's master switch (AtmosphereDesc::enabled)
		String			mPreset;	//!< the last seed word ("custom"/"day"/"sunset"/"night")
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor - seeds the DAY preset, enabled (a placed component
		//! means "this scene has a sky")
		AtmosphereComponent();
		//! destructor
		virtual ~AtmosphereComponent();

		//! does THIS instance currently own the world atmosphere?
		bool isAtmosphereOwner() const;
		//! the owning instance, or NULL while no eligible instance exists
		static AtmosphereComponent * getAtmosphereOwner();

		//! master switch of the ARMED desc (false = the owner arms a plain
		//! clear background - the authored "no sky" statement)
		void setEnabled(bool enabled);
		//! @see AtmosphereComponent::mEnabled
		inline bool getEnabled() const;
		//! @brief seed every look field from a named preset ("day"/"sunset"/
		//! "night"; "custom" seeds nothing). An unknown word warns and keeps
		//! the current state. @see the class authoring model
		void setPreset(String const & preset);
		//! @see AtmosphereComponent::mPreset
		inline String const & getPreset() const;
		//! zenith sky tint (linear; alpha ignored) - @see AtmosphereDesc::skyRed
		void setSkyColour(float red, float green, float blue);
		//! sky tint as a Color (@see setSkyColour)
		inline Color getSkyColour() const;
		//! HDR sky-dome brightness multiplier (>= 0; 1 = neutral)
		void setSkyPower(float power);
		//! @see AtmosphereDesc::skyPower
		inline float getSkyPower() const;
		//! sky Rayleigh density coefficient (>= 0; thicker = hazier horizon)
		void setDensity(float density);
		//! @see AtmosphereDesc::density
		inline float getDensity() const;
		//! linked directional light power - the exposure knob (>= 0)
		void setSunPower(float power);
		//! @see AtmosphereDesc::sunPower
		inline float getSunPower() const;
		//! scales the atmosphere-driven hemisphere ambient fill (>= 0)
		void setAmbientPower(float power);
		//! @see AtmosphereDesc::ambientPower
		inline float getAmbientPower() const;
		//! per-object exponential distance fog (>= 0; 0 = none)
		void setFogDensity(float density);
		//! @see AtmosphereDesc::fogDensity
		inline float getFogDensity() const;
		//! fog colour (linear; classic fixed-function fallback fog)
		void setFogColour(float red, float green, float blue);
		//! fog colour as a Color (@see setFogColour)
		inline Color getFogColour() const;

		//--- reflected property accessors ---
		//! reflected sky-colour setter (Color -> the three-float setter)
		inline void setSkyColourValue(Color const & colour) { this->setSkyColour(colour.r, colour.g, colour.b); }
		//! reflected fog-colour setter (Color -> the three-float setter)
		inline void setFogColourValue(Color const & colour) { this->setFogColour(colour.r, colour.g, colour.b); }

		//! the desc this instance would arm (look fields + the enabled switch)
		AtmosphereDesc buildDesc() const;
	protected:
		//! joins the ownership registry; the first active instance takes over
		virtual void onAdd();
		//! leaves the registry; an owner hands back / promotes (@see class)
		virtual void onRemove();
		//! (de)activation moves the ownership (@see the take-over contract)
		virtual void onSetActive(bool activeInHierarchy);
		//! re-apply the armed desc when THIS instance owns the atmosphere
		void applyIfOwner();
		//--- SERIALIZATION ---
		//! save enabled, preset and the look fields by name off the schema
		virtual void save(optr<IArchive> const & ar);
		//! load the atmosphere state (preset seeds first, fields override -
		//! the schema declaration order @see the class authoring model)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline bool AtmosphereComponent::getEnabled() const
	{
		return this->mEnabled;
	}
	//---------------------------------------------------------------
	inline String const & AtmosphereComponent::getPreset() const
	{
		return this->mPreset;
	}
	//---------------------------------------------------------------
	inline Color AtmosphereComponent::getSkyColour() const
	{
		return Color(this->mDesc.skyRed, this->mDesc.skyGreen,
			this->mDesc.skyBlue, 1.0f);
	}
	//---------------------------------------------------------------
	inline float AtmosphereComponent::getSkyPower() const
	{
		return this->mDesc.skyPower;
	}
	//---------------------------------------------------------------
	inline float AtmosphereComponent::getDensity() const
	{
		return this->mDesc.density;
	}
	//---------------------------------------------------------------
	inline float AtmosphereComponent::getSunPower() const
	{
		return this->mDesc.sunPower;
	}
	//---------------------------------------------------------------
	inline float AtmosphereComponent::getAmbientPower() const
	{
		return this->mDesc.ambientPower;
	}
	//---------------------------------------------------------------
	inline float AtmosphereComponent::getFogDensity() const
	{
		return this->mDesc.fogDensity;
	}
	//---------------------------------------------------------------
	inline Color AtmosphereComponent::getFogColour() const
	{
		return Color(this->mDesc.fogRed, this->mDesc.fogGreen,
			this->mDesc.fogBlue, 1.0f);
	}
	//---------------------------------------------------------------
}

#endif //__AtmosphereComponent_h__24_7_2026__18_00_00__
