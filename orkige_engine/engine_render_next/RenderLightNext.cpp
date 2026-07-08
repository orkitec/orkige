/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderLightNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderLightNext.cpp
//! @brief B1 STUB of the RenderLight facade on Ogre-Next
//! @remarks No backend object exists yet (RenderWorld::createLight
//! returns NULL and logs once) - safe-default no-ops so the facade
//! links. B2 (WP-A2.2) implements the v2 Ogre::Light per the header's
//! mapping comments.

#include "engine_render_next/NextBackend.h"

namespace Orkige
{
	//---------------------------------------------------------
	RenderLight::RenderLight()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderLight::~RenderLight()
	{
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderLight::attachTo(optr<RenderNode> const & node)
	{
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void RenderLight::detach()
	{
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	void RenderLight::setType(LightType type)
	{
		this->mImpl->type = type;
	}
	//---------------------------------------------------------
	RenderLight::LightType RenderLight::getType() const
	{
		return this->mImpl->type;
	}
	//---------------------------------------------------------
	void RenderLight::setDiffuseColour(Color const & colour)
	{
		(void)colour;
	}
	//---------------------------------------------------------
	void RenderLight::setSpecularColour(Color const & colour)
	{
		(void)colour;
	}
	//---------------------------------------------------------
	void RenderLight::setRange(Real range)
	{
		(void)range;
	}
	//---------------------------------------------------------
	void RenderLight::setSpotAngles(Degree const & inner, Degree const & outer)
	{
		(void)inner; (void)outer;
	}
	//---------------------------------------------------------
	void RenderLight::setCastShadows(bool cast)
	{
		(void)cast;
	}
}
