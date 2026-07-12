/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderLightNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderLightNext.cpp
//! @brief Ogre-Next implementation of the RenderLight facade
//! @remarks wraps a v2 Ogre::Light; exactly like classic the direction
//! comes from the node orientation (the facade's placement model - on
//! Next lights MUST hang off a scene node to render at all). setRange
//! derives the same falloff terms as the classic backend so a lit
//! scene reads comparably across the flavors.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreLight.h>

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	optr<RenderLight> RenderBackend::createLight(Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		Ogre::Light* light = sceneManager->createLight();
		light->setName(RenderBackend::generateName("RenderFacade/Light"));
		// facade rule: a light casts shadows only when explicitly asked
		// (setCastShadows(true) - LightComponent.castsShadows); Ogre's
		// default-on would silently join every light into the caster tally
		light->setCastShadows(false);
		optr<RenderLight> handle(new RenderLight());
		handle->mImpl->light = light;
		handle->mImpl->creator = sceneManager;
		return handle;
	}
	//---------------------------------------------------------
	RenderLight::RenderLight()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderLight::~RenderLight()
	{
		// late destruction guard, same rule as RenderNode
		if(this->mImpl->light && RenderBackend::system())
		{
			// leave the directional registry BEFORE the light dies so the
			// atmosphere never keeps a dangling sun pointer (relinks if needed)
			RenderBackend::noteDirectionalLight(this->mImpl->light, false);
			// a dying caster leaves the tally (may detach the shadow node)
			if(this->mImpl->castingShadows)
			{
				RenderBackend::shadowCasterCountChanged(-1);
			}
			if(this->mImpl->light->isAttached())
			{
				this->mImpl->light->detachFromParent();
			}
			this->mImpl->creator->destroyLight(this->mImpl->light);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderLight::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->light->isAttached())
		{
			this->mImpl->light->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->light);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void RenderLight::detach()
	{
		if(this->mImpl->light->isAttached())
		{
			this->mImpl->light->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	void RenderLight::setType(LightType type)
	{
		switch(type)
		{
		case LT_DIRECTIONAL:
			this->mImpl->light->setType(Ogre::Light::LT_DIRECTIONAL);
			break;
		case LT_POINT:
			this->mImpl->light->setType(Ogre::Light::LT_POINT);
			break;
		case LT_SPOT:
			this->mImpl->light->setType(Ogre::Light::LT_SPOTLIGHT);
			break;
		}
		// keep the directional-light registry (the atmosphere's sun source) in
		// step with this light's kind
		RenderBackend::noteDirectionalLight(this->mImpl->light,
			type == LT_DIRECTIONAL);
	}
	//---------------------------------------------------------
	RenderLight::LightType RenderLight::getType() const
	{
		switch(this->mImpl->light->getType())
		{
		case Ogre::Light::LT_DIRECTIONAL:	return LT_DIRECTIONAL;
		case Ogre::Light::LT_SPOTLIGHT:		return LT_SPOT;
		default:							return LT_POINT;
		}
	}
	//---------------------------------------------------------
	void RenderLight::setDiffuseColour(Color const & colour)
	{
		this->mImpl->light->setDiffuseColour(colour);
	}
	//---------------------------------------------------------
	void RenderLight::setSpecularColour(Color const & colour)
	{
		this->mImpl->light->setSpecularColour(colour);
	}
	//---------------------------------------------------------
	void RenderLight::setRange(Real range)
	{
		// same single-range approximation as the classic backend
		// (linear 4.5/r, quadratic 75/r^2)
		const Real safeRange = std::max(range, Real(0.001));
		this->mImpl->light->setAttenuation(safeRange, 1.0f,
			4.5f / safeRange, 75.0f / (safeRange * safeRange));
	}
	//---------------------------------------------------------
	void RenderLight::setSpotAngles(Degree const & inner, Degree const & outer)
	{
		this->mImpl->light->setSpotlightRange(inner, outer);
	}
	//---------------------------------------------------------
	void RenderLight::setCastShadows(bool cast)
	{
		if(cast == this->mImpl->castingShadows)
		{
			return;
		}
		this->mImpl->castingShadows = cast;
		this->mImpl->light->setCastShadows(cast);
		// the caster tally drives whether the workspaces carry a shadow node
		RenderBackend::shadowCasterCountChanged(cast ? +1 : -1);
	}
}
