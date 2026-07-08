/********************************************************************
	created:	Tuesday 2026/07/07 at 16:50
	filename: 	JumperLogic.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __JumperLogic_h__7_7_2026__16_50_00__
#define __JumperLogic_h__7_7_2026__16_50_00__

#include <OgreVector.h>
#include <algorithm>
#include <cmath>

namespace Orkige
{
	//! @brief pure gameplay math of the jumper sample - no engine state, no
	//! physics backend, so tests/jumper can verify it headlessly.
	//! @remarks main.cpp feeds these functions with live PhysicsWorld /
	//! TransformComponent data; keeping them pure is the pattern gameplay
	//! code should follow (logic testable, glue thin).
	namespace JumperLogic
	{
		//! a downward ray query for the grounded check (PhysicsWorld::castRay args)
		struct GroundProbe
		{
			Ogre::Vector3	origin;			//!< ray start, just below the capsule bottom
			Ogre::Vector3	direction;		//!< always straight down
			float			maxDistance;	//!< short probe length
		};

		//! @brief build the grounded-check ray for a capsule at the given center.
		//! @remarks the origin starts skin BELOW the capsule surface: Jolt's
		//! closest-hit CastRay treats convex shapes as solid, so a ray starting
		//! inside the player capsule would hit the player itself at fraction 0.
		//! Starting just outside also means a slightly penetrating ground plane
		//! still registers (fraction-0 hit on the ground body = grounded).
		inline GroundProbe makeGroundProbe(Ogre::Vector3 const & center,
			float capsuleHalfHeight, float capsuleRadius,
			float skin = 0.02f, float probeLength = 0.2f)
		{
			GroundProbe probe;
			probe.origin = Ogre::Vector3(center.x,
				center.y - (capsuleHalfHeight + capsuleRadius + skin), center.z);
			probe.direction = Ogre::Vector3::NEGATIVE_UNIT_Y;
			probe.maxDistance = probeLength;
			return probe;
		}

		//! has the player fallen out of the level?
		inline bool isBelowKillPlane(float y, float killY)
		{
			return y < killY;
		}

		//! @brief frame-rate independent exponential approach of current toward
		//! target (used for velocity control and the camera follow).
		//! @remarks never overshoots for any dt >= 0; rate is "how fast":
		//! after 1/rate seconds ~63% of the distance is gone.
		inline float approach(float current, float target, float rate, float dt)
		{
			const float blend = 1.0f - std::exp(-rate * std::max(dt, 0.0f));
			return current + (target - current) * blend;
		}

		//! is the player close enough to the goal marker to win?
		inline bool reachedGoal(Ogre::Vector3 const & playerPosition,
			Ogre::Vector3 const & goalPosition, float radius)
		{
			return playerPosition.squaredDistance(goalPosition) <= radius * radius;
		}
	}
}

#endif //__JumperLogic_h__7_7_2026__16_50_00__
