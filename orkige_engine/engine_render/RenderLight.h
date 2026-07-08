/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderLight.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderLight_h__8_7_2026__12_00_00__
#define __RenderLight_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"

namespace Orkige
{
	//! @brief a dynamic light placed via RenderNode
	//! @remarks The audit found NO live dynamic-light call site today (the
	//! apps only set the world ambient, @see RenderWorld::setAmbientLight;
	//! the old Light serialization in engine_util/SerializationUtil is
	//! dead code). This class is therefore the deliberate MINIMUM with
	//! room: type, colour, attenuation range, spot angles, shadows -
	//! matching what the dead serializer round-tripped, so scenes can grow
	//! lights back without a facade break.
	//!
	//! Backend mapping (whole class): classic/next = Ogre::Light attached
	//! to a node (direction from node orientation); filament =
	//! LightManager::Builder on an entity.
	class ORKIGE_ENGINE_DLL RenderLight
	{
		//--- Types -------------------------------------------------
	public:
		//! light kind
		//! map: classic/next=Ogre::Light::LightTypes | filament=LightManager::Type
		enum LightType
		{
			LT_DIRECTIONAL = 0,	//!< parallel light, position ignored (sun)
			LT_POINT,			//!< radiates from the node position
			LT_SPOT				//!< cone along the node's -Z direction
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend light guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the backend light
		~RenderLight();

		//--- placement ---
		//! map: classic/next=SceneNode::attachObject | filament=light entity parented to node
		void attachTo(optr<RenderNode> const & node);
		void detach();

		//--- parameters ---
		//! map: classic/next=Light::setType | filament=fixed at Builder time (impl recreates)
		void setType(LightType type);
		LightType getType() const;
		//! map: classic/next=Light::setDiffuseColour | filament=LightManager::setColor(+intensity)
		void setDiffuseColour(Color const & colour);
		//! map: classic/next=Light::setSpecularColour | filament=folded into color/intensity (impl ignores or approximates)
		void setSpecularColour(Color const & colour);
		//! @brief distance the light reaches (simple range attenuation;
		//! backends derive their native falloff terms from it)
		//! map: classic/next=Light::setAttenuation(range,...) | filament=LightManager::setFalloff
		void setRange(Real range);
		//! spot cone (only for LT_SPOT)
		//! map: classic/next=Light::setSpotlightRange | filament=LightManager::setSpotLightCone
		void setSpotAngles(Degree const & inner, Degree const & outer);
		//! map: classic/next=Light::setCastShadows | filament=LightManager::setShadowCaster
		void setCastShadows(bool cast);
	protected:
		//! lights are created by RenderWorld::createLight only
		RenderLight();
	private:
		RenderLight(RenderLight const &);				// non-copyable
		RenderLight & operator=(RenderLight const &);	// non-copyable
	};
}

#endif //__RenderLight_h__8_7_2026__12_00_00__
