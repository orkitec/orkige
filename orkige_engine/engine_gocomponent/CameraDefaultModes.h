/**************************************************************
	created:	2010/08/30 at 20:39
	filename: 	CameraDefaultModes.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __CameraDefaultModes_h__30_8_2010__20_39_18__
#define __CameraDefaultModes_h__30_8_2010__20_39_18__

#include "engine_gocomponent/CameraComponent.h"
#include "engine_physic/CollisionTools.h"

namespace Orkige
{
	//! some basic modes for CameraComponent
	namespace CameraDefaultModes
	{
		//! CameraComponent is a bit bloated for FPS Camera but it can be done too
		static inline void FirstPersonCamera(CameraComponent* cameraComponent, Ogre::Real timeSinceLastFrame, Ogre::Real tightness)
		{
			cameraComponent->instantSetCamera(cameraComponent->getControlNode()->_getDerivedPosition(),cameraComponent->getSightNode ()->_getDerivedPosition());
		}
		//---------------------------------------------------------
		//! 3rd Person camera with smoothing and basic collision detection
		static inline void ThirdPersonChaseCamera(CameraComponent* cameraComponent, Ogre::Real timeSinceLastFrame, Ogre::Real tightness)
		{
			static bool camdjusted = false;
			static Ogre::Quaternion actorOrientation = Ogre::Quaternion::IDENTITY;
			static Ogre::Vector3 actorPosition = Ogre::Vector3::ZERO;

			if(!camdjusted || cameraComponent->getControlNode()->_getDerivedPosition() != actorPosition || cameraComponent->getControlNode()->_getDerivedOrientation() != actorOrientation)
			{
				cameraComponent->setCamera (timeSinceLastFrame, cameraComponent->getCameraNode ()->_getDerivedPosition(),	cameraComponent->getSightNode ()->_getDerivedPosition(), 1*timeSinceLastFrame);
				camdjusted = false;
			}

			Ogre::Real i = 0.025f;//softness on collision
			while(i < 1 && CollisionTools::getSingleton().collidesWithEntity(cameraComponent->getActorNode()->_getDerivedPosition(), cameraComponent->getCameraPosition()))
			{
				cameraComponent->setCamera(timeSinceLastFrame, cameraComponent->getControlNode()->_getDerivedPosition(), cameraComponent->getSightNode ()->_getDerivedPosition(), i*timeSinceLastFrame);
				i += 0.025f;
				camdjusted = true;
			}	

			actorPosition = cameraComponent->getControlNode()->_getDerivedPosition();
			actorOrientation = cameraComponent->getControlNode()->_getDerivedOrientation();
		}
		//---------------------------------------------------------
		//! 3rd Person camera without smoothing and collision
		static inline void ThirdPersonFixedCamera(CameraComponent* cameraComponent, Ogre::Real timeSinceLastFrame, Ogre::Real tightness)
		{
			cameraComponent->setCamera (timeSinceLastFrame, Ogre::Vector3(0, 200, 0), cameraComponent->getSightNode()->_getDerivedPosition(), 0.01f);
		}
		//---------------------------------------------------------
	}
}

#endif //__CameraDefaultModes_h__30_8_2010__20_39_18__
