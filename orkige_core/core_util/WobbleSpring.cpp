/**************************************************************
	created:	2026/07/11 at 09:00
	filename: 	WobbleSpring.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file WobbleSpring.cpp
//! @brief the scalar damped-spring integrator (@see WobbleSpring.h)

#include "core_util/WobbleSpring.h"

#include <cmath>

namespace Orkige
{
	namespace
	{
		//! the largest slice fed to one Euler sub-step: a stiff spring stays
		//! stable across a big frame delta by iterating this fixed slice
		const float SPRING_SUBSTEP = 1.0f / 240.0f;
		//! settle threshold: below this in BOTH value and velocity the spring is
		//! snapped to an exact zero (so the deform returns to rest with no drift)
		const float SPRING_REST_EPSILON = 1.0e-4f;
	}
	//---------------------------------------------------------
	WobbleSpring::WobbleSpring()
		: mValue(0.0f), mVelocity(0.0f), mStiffness(120.0f), mDamping(6.0f)
	{
	}
	//---------------------------------------------------------
	void WobbleSpring::setParams(float stiffness, float damping)
	{
		this->mStiffness = stiffness > 0.0f ? stiffness : 0.0f;
		this->mDamping = damping > 0.0f ? damping : 0.0f;
	}
	//---------------------------------------------------------
	void WobbleSpring::kick(float impulse)
	{
		this->mVelocity += impulse;
	}
	//---------------------------------------------------------
	void WobbleSpring::setValue(float value)
	{
		this->mValue = value;
	}
	//---------------------------------------------------------
	void WobbleSpring::update(float deltaTime)
	{
		if(deltaTime <= 0.0f)
		{
			return;
		}
		// semi-implicit Euler in fixed sub-steps: a = -k*x - c*v, integrate v
		// then x. Sub-stepping keeps a stiff/underdamped spring from exploding
		// when the frame delta is large (paused frame catch-up, slow devices).
		float remaining = deltaTime;
		while(remaining > 0.0f)
		{
			const float step = remaining < SPRING_SUBSTEP
				? remaining : SPRING_SUBSTEP;
			const float accel =
				-this->mStiffness * this->mValue - this->mDamping * this->mVelocity;
			this->mVelocity += accel * step;
			this->mValue += this->mVelocity * step;
			remaining -= step;
		}
		// snap to an EXACT rest once settled - the deform delta then becomes a
		// hard zero and the mesh returns bit-for-bit to its rest pose
		if(this->atRest(SPRING_REST_EPSILON))
		{
			this->mValue = 0.0f;
			this->mVelocity = 0.0f;
		}
	}
	//---------------------------------------------------------
	void WobbleSpring::reset()
	{
		this->mValue = 0.0f;
		this->mVelocity = 0.0f;
	}
	//---------------------------------------------------------
	bool WobbleSpring::atRest(float epsilon) const
	{
		return std::fabs(this->mValue) <= epsilon &&
			std::fabs(this->mVelocity) <= epsilon;
	}
}
