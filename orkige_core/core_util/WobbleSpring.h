/********************************************************************
	created:	Friday 2026/07/11 at 09:00
	filename: 	WobbleSpring.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __WobbleSpring_h__11_7_2026__09_00_00__
#define __WobbleSpring_h__11_7_2026__09_00_00__

//! @file WobbleSpring.h
//! @brief a single scalar spring-damper that always relaxes toward ZERO - the
//! jiggle primitive behind soft, deformable organic shapes
//! @remarks Pure, headless, allocation-free (orkige_core) so the unit suite
//! pins the dynamics (decays to rest, stays bounded, snaps EXACTLY to zero once
//! settled, impulse response) without a renderer or a clock. A deformable shape
//! composes many of these: two per control point (an XY offset) plus one for
//! the squash amount. It integrates with a semi-implicit Euler sub-step so a
//! stiff spring stays stable across a large frame delta, and it SNAPS to a hard
//! zero once |value| and |velocity| fall under the rest epsilon, so a shape
//! returns to its rest pose with no residual float drift (the "no drift after
//! decay" contract the soft-body selfcheck asserts).

namespace Orkige
{
	//! @brief a scalar damped spring relaxing toward 0 (@see WobbleSpring.h)
	class WobbleSpring
	{
		//--- Variables ---------------------------------------------
	public:
	protected:
		float	mValue;		//!< current displacement from the zero rest value
		float	mVelocity;	//!< current rate of change
		float	mStiffness;	//!< restoring force per unit displacement (k)
		float	mDamping;	//!< velocity drag (c); higher = less overshoot
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief a spring at rest (value 0, velocity 0) with a soft-blob default
		//! stiffness/damping (underdamped, so a kick visibly wobbles then settles)
		WobbleSpring();
		//! @brief set the dynamics; stiffness and damping are clamped >= 0. Live
		//! tunable (the shape's wobble stiffness/damping properties drive these).
		void setParams(float stiffness, float damping);
		//! @see WobbleSpring::mStiffness
		inline float getStiffness() const { return this->mStiffness; }
		//! @see WobbleSpring::mDamping
		inline float getDamping() const { return this->mDamping; }

		//! @brief add to the velocity - the impulse hook (a contact kicks the
		//! wobble). A larger impulse means a larger initial swing.
		void kick(float impulse);
		//! @brief overwrite the displacement outright (rarely needed; tests use it)
		void setValue(float value);
		//! current displacement from rest
		inline float value() const { return this->mValue; }
		//! current velocity
		inline float velocity() const { return this->mVelocity; }

		//! @brief integrate toward the zero rest value over deltaTime. Sub-stepped
		//! for stability under stiff springs / large deltas; once the motion falls
		//! under the rest epsilon it snaps to an EXACT zero (no residual drift).
		void update(float deltaTime);
		//! @brief force back to the exact rest state (value 0, velocity 0)
		void reset();
		//! @brief is the spring within epsilon of the zero rest state (settled)
		bool atRest(float epsilon) const;
	protected:
	private:
	};
}

#endif //__WobbleSpring_h__11_7_2026__09_00_00__
