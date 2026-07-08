/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	MeshInstanceNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MeshInstanceNext.cpp
//! @brief B1 STUB of the MeshInstance facade on Ogre-Next
//! @remarks No backend object exists yet (RenderWorld::createMeshInstance
//! returns NULL and logs once) - these bodies exist so the facade links;
//! every method is a safe-default no-op. B2 (WP-A2.3) implements the v2
//! Item + Mesh::importV1 path per the header's mapping comments.

#include "engine_render_next/NextBackend.h"

namespace Orkige
{
	//---------------------------------------------------------
	MeshInstance::MeshInstance()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	MeshInstance::~MeshInstance()
	{
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void MeshInstance::attachTo(optr<RenderNode> const & node)
	{
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void MeshInstance::detach()
	{
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	String const & MeshInstance::getMeshName() const
	{
		return this->mImpl->meshName;
	}
	//---------------------------------------------------------
	void MeshInstance::setVisible(bool visible)
	{
		(void)visible;
	}
	//---------------------------------------------------------
	void MeshInstance::setCastShadows(bool cast)
	{
		(void)cast;
	}
	//---------------------------------------------------------
	AABB MeshInstance::getLocalBounds() const
	{
		return AABB();	// null box
	}
	//---------------------------------------------------------
	void MeshInstance::setQueryFlags(unsigned int flags)
	{
		(void)flags;
	}
	//---------------------------------------------------------
	void MeshInstance::setVertexColourUnlit()
	{
	}
	//---------------------------------------------------------
	size_t MeshInstance::getNumSubMeshes() const
	{
		return 0;
	}
	//---------------------------------------------------------
	bool MeshInstance::subMeshHasTexture(size_t index) const
	{
		(void)index;
		return false;
	}
	//---------------------------------------------------------
	StringVector MeshInstance::getAnimationNames() const
	{
		return StringVector();
	}
	//---------------------------------------------------------
	bool MeshInstance::hasAnimation(String const & name) const
	{
		(void)name;
		return false;
	}
	//---------------------------------------------------------
	StringVector MeshInstance::getEnabledAnimations() const
	{
		return StringVector();
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationEnabled(String const & name, bool enabled)
	{
		(void)name; (void)enabled;
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationLoop(String const & name, bool loop)
	{
		(void)name; (void)loop;
	}
	//---------------------------------------------------------
	void MeshInstance::addAnimationTime(String const & name, float deltaSeconds)
	{
		(void)name; (void)deltaSeconds;
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationTime(String const & name, float seconds)
	{
		(void)name; (void)seconds;
	}
	//---------------------------------------------------------
	float MeshInstance::getAnimationTime(String const & name) const
	{
		(void)name;
		return 0.0f;
	}
	//---------------------------------------------------------
	float MeshInstance::getAnimationLength(String const & name) const
	{
		(void)name;
		return 0.0f;
	}
	//---------------------------------------------------------
	bool MeshInstance::hasAnimationEnded(String const & name) const
	{
		(void)name;
		return true;
	}
}
