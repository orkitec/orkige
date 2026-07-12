/********************************************************************
	created:	Saturday 2026/07/12 at 10:00
	filename: 	LightComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __LightComponent_h__12_7_2026__10_00_00__
#define __LightComponent_h__12_7_2026__10_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderLight.h"
#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	//! @brief a dynamic light placed on a GameObject - directional, point or
	//! spot - the component consumer of the engine_render RenderLight facade
	//! @remarks Follows SpriteComponent's structure: needs a sibling
	//! TransformComponent, owns a child scene node (through SceneNodeGuard) and
	//! attaches a facade RenderLight to it, so the light follows the object's
	//! transform. The light DIRECTION comes from the node orientation (the
	//! facade's placement model - a directional/spot light aims down the node's
	//! -Z, so orient the owning object to aim it).
	//!
	//! All parameters are held on the component and pushed onto the facade light
	//! when one is live, so a DETACHED component (no scene node, the editor's or
	//! a unit test's) still round-trips every reflected property. `intensity`
	//! scales the light colour handed to the facade (colour * intensity for both
	//! the diffuse and specular term) - the backend-neutral way to brighten a
	//! light without growing the facade with a power term the shadow/PBS
	//! packages will revisit. `castsShadows` is REAL: it drives
	//! RenderLight::setCastShadows, and on a shadow-capable flavor a casting
	//! DIRECTIONAL light throws cascaded shadow maps whenever the world's
	//! quality knob is on (@see RenderWorld::setShadowQuality; a flavor
	//! without shadows accepts the flag and renders none).
	class ORKIGE_ENGINE_DLL LightComponent : public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(LightComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief light kind; mirrors RenderLight::LightType for the property
		//! registry (the component owns its own enum so reflection stays
		//! decoupled from the facade, like CameraComponent::FitMode mirrors
		//! CameraFit::FitMode). The math lives in the facade.
		enum LightType
		{
			LT_DIRECTIONAL = 0,	//!< parallel light, position ignored (the sun)
			LT_POINT = 1,		//!< radiates from the node position
			LT_SPOT = 2			//!< cone along the node's -Z direction
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<RenderLight>	mLight;			//!< the facade light or NULL (detached)
		LightType			mType;			//!< light kind (LT_POINT default)
		Color				mColour;		//!< light colour (default white)
		float				mIntensity;		//!< colour multiplier handed to the facade (default 1)
		float				mRange;			//!< reach in world units (point/spot attenuation)
		float				mInnerAngle;	//!< spot inner cone full angle in degrees
		float				mOuterAngle;	//!< spot outer cone full angle in degrees
		bool				mCastsShadows;	//!< forward-compatible shadow-caster flag
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		LightComponent();
		//! destructor
		virtual ~LightComponent();

		//! is a facade light currently live (false while detached)
		inline bool hasLight() const;

		//! select the light kind (applied live when a light exists)
		void setType(LightType type);
		//! @see LightComponent::mType
		inline LightType getType() const;
		//! set the light colour (multiplied by intensity onto the facade)
		void setColour(float red, float green, float blue);
		//! @see LightComponent::mColour
		inline Color const & getColour() const;
		//! @brief colour multiplier (>= 0); the facade receives colour*intensity
		//! for both the diffuse and specular term (the backend-neutral power knob)
		void setIntensity(float intensity);
		//! @see LightComponent::mIntensity
		inline float getIntensity() const;
		//! @brief the distance the point/spot light reaches (world units); the
		//! backends derive their native falloff terms from it
		void setRange(float range);
		//! @see LightComponent::mRange
		inline float getRange() const;
		//! spot inner cone FULL angle in degrees (only for LT_SPOT)
		void setInnerAngle(float degrees);
		//! @see LightComponent::mInnerAngle
		inline float getInnerAngle() const;
		//! spot outer cone FULL angle in degrees (only for LT_SPOT)
		void setOuterAngle(float degrees);
		//! @see LightComponent::mOuterAngle
		inline float getOuterAngle() const;
		//! @brief shadow-caster flag (directional lights throw cascaded maps
		//! on a shadow-capable flavor, @see class remarks)
		void setCastsShadows(bool casts);
		//! @see LightComponent::mCastsShadows
		inline bool getCastsShadows() const;

		//--- reflected property accessors ---
		//! reflected colour setter (Color -> the three-float setColour, alpha ignored)
		inline void setColourValue(Color const & colour) { this->setColour(colour.r, colour.g, colour.b); }
	protected:
		//! Component override gets called after the Component is attached to a GameObject
		virtual void onAdd();
		//! Component override gets called before the Component is removed from a GameObject
		virtual void onRemove();
		//! a deactivated GameObject turns its light off (the node's visibility)
		virtual void onSetActive(bool activeInHierarchy);
		//! push every stored parameter onto the facade light (needs a light)
		void applyStateToLight();
		//! push colour*intensity onto the facade light's diffuse+specular terms
		void applyColour();
		//! apply the EFFECTIVE light state to the node: on only while the owner
		//! is active in the hierarchy
		void applyVisibility();
		//--- SERIALIZATION ---
		//! save type, colour, intensity, range, spot angles and the shadow flag
		//! by name off the reflected schema
		virtual void save(optr<IArchive> const & ar);
		//! load the light state (re-applied when a light is live)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline bool LightComponent::hasLight() const
	{
		return this->mLight != nullptr;
	}
	//---------------------------------------------------------------
	inline LightComponent::LightType LightComponent::getType() const
	{
		return this->mType;
	}
	//---------------------------------------------------------------
	inline Color const & LightComponent::getColour() const
	{
		return this->mColour;
	}
	//---------------------------------------------------------------
	inline float LightComponent::getIntensity() const
	{
		return this->mIntensity;
	}
	//---------------------------------------------------------------
	inline float LightComponent::getRange() const
	{
		return this->mRange;
	}
	//---------------------------------------------------------------
	inline float LightComponent::getInnerAngle() const
	{
		return this->mInnerAngle;
	}
	//---------------------------------------------------------------
	inline float LightComponent::getOuterAngle() const
	{
		return this->mOuterAngle;
	}
	//---------------------------------------------------------------
	inline bool LightComponent::getCastsShadows() const
	{
		return this->mCastsShadows;
	}
	//---------------------------------------------------------------
}

#endif //__LightComponent_h__12_7_2026__10_00_00__
