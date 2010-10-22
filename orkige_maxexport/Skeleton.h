////////////////////////////////////////////////////////////////////////////////
// skeleton.h
// Author     : Francesco Giordana
// Start Date : January 13, 2005
// Copyright  : (C) 2006 by Francesco Giordana
// Email      : fra.giordana@tiscali.it
////////////////////////////////////////////////////////////////////////////////
// Port to 3D Studio Max - Modified original version
// Author	  : Doug Perkowski - OC3 Entertainment, Inc.
// Start Date : December 10th, 2007
////////////////////////////////////////////////////////////////////////////////
/*********************************************************************************
*                                                                                *
*   This program is free software; you can redistribute it and/or modify         *
*   it under the terms of the GNU Lesser General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or            *
*   (at your option) any later version.                                          *
*                                                                                *
**********************************************************************************/

#ifndef _SKELETON_H
#define _SKELETON_H

#include "MaxExportLayer.h"
#include "ParamList.h"
#include "Animation.h"

namespace OrkigeMaxExporter
{
	/***** structure to hold joint info *****/
	typedef struct jointTag
	{
		std::string name;
		int id;
		ULONG nodeID; //Unique Max INode ID ;
		IGameNode *pGameNode;
		Matrix3 localMatrix;
		Matrix3 bindMatrix;
		int parentIndex;
		double posx,posy,posz;
		double angle;
		double axisx,axisy,axisz;
		float scalex,scaley,scalez;
	} joint;


	/*********** Class Skeleton **********************/
	class Skeleton
	{
	public:
		//constructor
		Skeleton();
		//destructor
		~Skeleton();
		//clear skeleton data
		void clear();
		//load skeleton data
//		bool load(IGameNode* pGameNode, IGameObject* pGameObject, IGameSkin* pGameSkin,ParamList& params);
		//load a joint
		bool loadJoint(IGameNode* pGameNode, ParamList& params);

		// returns the index of the bone in the skeleton.  -1 if it doesn't exist.
		int getJointIndex(IGameNode* pGameNode);

		//load skeletal animations
		bool loadAnims(ParamList& params);
		//get joints
		std::vector<joint>& getJoints();
		//get animations
		std::vector<Animation>& getAnimations();
		//restore skeleton pose
		void restorePose();
		//write to an OGRE binary skeleton
		bool writeOgreBinary(ParamList &params);

	protected:

		//load a clip
		bool loadClip(std::string clipName,float start,float stop,float rate,ParamList& params);
		//load a keyframe for a particular joint at current time
		skeletonKeyframe loadKeyframe(joint& j,float time,ParamList& params);
		//write joints to an Ogre skeleton
		bool createOgreBones(Ogre::SkeletonPtr pSkeleton,ParamList& params);
		// write skeleton animations to an Ogre skeleton
		bool createOgreSkeletonAnimations(Ogre::SkeletonPtr pSkeleton,ParamList& params);

		std::vector<joint> m_joints;
		std::vector<Animation> m_animations;
		std::vector<int> m_roots;
		std::string m_restorePose;
	};

}	//end namespace

#endif
