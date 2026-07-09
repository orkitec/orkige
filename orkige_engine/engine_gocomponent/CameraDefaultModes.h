/**************************************************************
	created:	2010/08/30 at 20:39
	filename: 	CameraDefaultModes.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CameraDefaultModes_h__30_8_2010__20_39_18__
#define __CameraDefaultModes_h__30_8_2010__20_39_18__

#include "engine_gocomponent/CameraComponent.h"

namespace Orkige
{
	//! @brief some basic modes for CameraComponent
	//! @remarks runs on the
	//! facade RenderNode rig. The historical CollisionTools camera-vs-entity
	//! test in the chase camera was dropped with CollisionTools' retirement
	//! (superseded by PhysicsWorld::castRay - wire a physics-based camera
	//! collision when a game needs one).
	namespace CameraDefaultModes
	{
		//! CameraComponent is a bit bloated for FPS Camera but it can be done too
		static inline void FirstPersonCamera(CameraComponent* cameraComponent, Real timeSinceLastFrame, Real tightness)
		{
			cameraComponent->instantSetCamera(cameraComponent->getControlNode()->getWorldPosition(), cameraComponent->getSightNode()->getWorldPosition());
		}
		//---------------------------------------------------------
		//! 3rd Person camera with smoothing
		static inline void ThirdPersonChaseCamera(CameraComponent* cameraComponent, Real timeSinceLastFrame, Real tightness)
		{
			static bool camdjusted = false;
			static Quat actorOrientation = Quat::IDENTITY;
			static Vec3 actorPosition = Vec3::ZERO;

			if(!camdjusted || cameraComponent->getControlNode()->getWorldPosition() != actorPosition || cameraComponent->getControlNode()->getWorldOrientation() != actorOrientation)
			{
				cameraComponent->setCamera(timeSinceLastFrame, cameraComponent->getCameraNode()->getWorldPosition(), cameraComponent->getSightNode()->getWorldPosition(), 1*timeSinceLastFrame);
				camdjusted = false;
			}

			actorPosition = cameraComponent->getControlNode()->getWorldPosition();
			actorOrientation = cameraComponent->getControlNode()->getWorldOrientation();
		}
		//---------------------------------------------------------
		//! 3rd Person camera without smoothing and collision
		static inline void ThirdPersonFixedCamera(CameraComponent* cameraComponent, Real timeSinceLastFrame, Real tightness)
		{
			cameraComponent->setCamera(timeSinceLastFrame, Vec3(0, 200, 0), cameraComponent->getSightNode()->getWorldPosition(), 0.01f);
		}
		//---------------------------------------------------------
	}
}

#endif //__CameraDefaultModes_h__30_8_2010__20_39_18__
