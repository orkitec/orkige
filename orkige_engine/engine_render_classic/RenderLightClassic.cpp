/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderLightClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderLightClassic.cpp
//! @brief classic-OGRE implementation of the RenderLight facade
//! @remarks wraps an Ogre::Light; direction comes from the node
//! orientation (OGRE 14 dropped Light::setDirection), which is exactly
//! the facade's placement model

#include "engine_render_classic/ClassicBackend.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	optr<RenderLight> RenderBackend::createLight(Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		Ogre::Light* light = sceneManager->createLight(
			RenderBackend::generateName("RenderFacade/Light"));
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
		if(this->mImpl->light)
		{
			// leave the directional registry BEFORE the light dies so the sky
			// dome never keeps a dangling sun pointer (re-resolves if needed)
			RenderBackend::noteDirectionalLight(this->mImpl->light, false);
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
		// keep the directional-light registry (the sky dome's sun source) in
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
		// derive the classic falloff terms from the single range value
		// (the usual approximation: linear 4.5/r, quadratic 75/r^2)
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
		this->mImpl->light->setCastShadows(cast);
	}
}
