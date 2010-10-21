////////////////////////////////////////////////////////////////////////////////
// skeleton.cpp
// Author     : Francesco Giordana
// Start Date : January 13, 2005
// Copyright  : (C) 2006 by Francesco Giordana
// Email      : fra.giordana@tiscali.it
////////////////////////////////////////////////////////////////////////////////
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

#include "skeleton.h"
#include "submesh.h"
#include "OrkigeMaxExporterLog.h"
#include "decomp.h"


// Removes scaling from originalMatrix.
inline Matrix3 RemoveNonUniformScaling( const Matrix3& originalMatrix )
{          
	AffineParts matrixParts;
	decomp_affine(originalMatrix, &matrixParts);
	Matrix3 nodeMatrixNoScale;
	matrixParts.q.MakeMatrix(nodeMatrixNoScale);
	nodeMatrixNoScale.SetTrans(matrixParts.t);
	return nodeMatrixNoScale;
}

namespace OrkigeMaxExporter
{
	// Constructor
	Skeleton::Skeleton()
	{
		m_joints.clear();
		m_animations.clear();
		m_restorePose = "";
	}


	// Destructor
	Skeleton::~Skeleton()
	{
		clear();
	}


	// Clear skeleton data
	void Skeleton::clear()
	{
		m_joints.clear();
		m_animations.clear();
		m_restorePose = "";
	}
	int Skeleton::getJointIndex(IGameNode* pGameNode)
	{
		if(pGameNode)
		{
			for (int i=0; i<m_joints.size(); i++)
			{
				if (m_joints[i].nodeID == pGameNode->GetNodeID())
					return i;
			}
		}
		return -1;
	}


	// Load a joint
	bool Skeleton::loadJoint(IGameNode* pGameNode,ParamList& params)
	{
		int i;
		joint newJoint;
		if( !pGameNode )
		{
			OrkigeMaxExporterLog( "Failed to load joint.\n");
			return false;
		}
		int index = getJointIndex(pGameNode);

		if(index == -1)
		{
			// Make sure we don't have a duplicate bone
			for( unsigned short i = 0; i < m_joints.size(); ++i )
			{
				if( pGameNode->GetName() == m_joints[i].name )
				{
					OrkigeMaxExporterLog( "Found bone with duplicate name %s.  Bone will not be exported!.\n", m_joints[i].name.c_str());
					return false;
				}
			}
			// If this is a new joint, push one back to the end of the array.
			// Otherwise we still continue in case we had previously thought
			// this bone was a root bone (incorrectly).
			m_joints.push_back(newJoint);
			index = m_joints.size() - 1;
		}
		else 
		{
			bool bShouldReExport = false;
			for(int i = 0; i < m_roots.size(); i++)
			{
				if(m_joints[m_roots[i]].pGameNode == pGameNode)
				{
					int newParentIndex =  getJointIndex(pGameNode->GetNodeParent());
					if(-1 != newParentIndex)
					{
						bShouldReExport = true;
						m_roots.erase(m_roots.begin()+i);
						i--;
					}
				}
			}	
			if(!bShouldReExport)
			{
				// no sense in going further as we've already exported this joint and
				// it hasn't changed from a root to a non-root.
				return true;
			}
		}


		// Frame zero should contain the bind pose.
		Matrix3 nodeTM = pGameNode->GetWorldTM(0).ExtractMatrix3();
		Matrix3 parentTM, localTM;
		IGameNode* pGameNodeParent = pGameNode->GetNodeParent();
		// Get parent index
		int parentIdx = getJointIndex(pGameNodeParent);
		if (pGameNodeParent)
		{
			parentTM = pGameNodeParent->GetWorldTM(0).ExtractMatrix3();
		}
		if( params.normalizeScale )
		{
			nodeTM = RemoveNonUniformScaling(nodeTM);
			parentTM = RemoveNonUniformScaling(parentTM);
		}
		if( parentIdx >= 0 )
		{
			localTM = nodeTM * Inverse(parentTM);
		}
		else
		{
			localTM = nodeTM;
		}
		
		AffineParts ap;
		decomp_affine(localTM, &ap);

		Point3 translation(ap.t.x, ap.t.y, ap.t.z);
		if (fabs(translation.x) < PRECISION)
			translation.x = 0;
		if (fabs(translation.y) < PRECISION)
			translation.y = 0;
		if (fabs(translation.z) < PRECISION)
			translation.z = 0;
		Point3 scale(ap.k.x, ap.k.y, ap.k.z);
		if (fabs(scale.x) < PRECISION)
			scale.x = 0;
		if (fabs(scale.y) < PRECISION)
			scale.y = 0;
		if (fabs(scale.z) < PRECISION)
			scale.z = 0;
		AngAxis angAxis(ap.q);
		if (fabs(angAxis.axis.x) < PRECISION)
			angAxis.axis.x = 0;
		if (fabs(angAxis.axis.y) < PRECISION)
			angAxis.axis.y = 0;
		if (fabs(angAxis.axis.z) < PRECISION)
			angAxis.axis.z = 0;
		angAxis.axis.Normalize();
		if (fabs(angAxis.angle) < PRECISION)
			angAxis.angle = 0;
		if (angAxis.axis.Length() < 0.5)
		{
			angAxis.axis.x = 0;
			angAxis.axis.y = 1;
			angAxis.axis.z = 0;
			angAxis.angle = 0;
		}

		if(index == m_joints.size() - 1)
		{
			OrkigeMaxExporterLog( "Exporting joint %s. Trans( %f,%f,%f) AngAxis( %f,%f,%f,%f), Scale(%f,%f,%f).\n", pGameNode->GetName(), translation.x, translation.y, translation.z, angAxis.angle, angAxis.axis.x,angAxis.axis.y,angAxis.axis.z,scale.x, scale.y, scale.z);
		}


		// Set joint info
		m_joints[index].pGameNode = pGameNode;
		m_joints[index].name = pGameNode->GetName();
		m_joints[index].nodeID = pGameNode->GetNodeID();
		m_joints[index].id = index;
		m_joints[index].parentIndex = parentIdx;
		m_joints[index].bindMatrix = nodeTM;
		m_joints[index].localMatrix = localTM;
		m_joints[index].posx = translation.x * params.lum;
		m_joints[index].posy = translation.y * params.lum;
		m_joints[index].posz = translation.z * params.lum;
		m_joints[index].angle = -angAxis.angle; // not sure why I need to negate the angle.


		m_joints[index].axisx = angAxis.axis.x;
		m_joints[index].axisy = angAxis.axis.y;
		m_joints[index].axisz = angAxis.axis.z;
		m_joints[index].scalex = scale.x;
		m_joints[index].scaley = scale.y;
		m_joints[index].scalez = scale.z;
		// If root is a root joint, save it's index in the roots list
		if (parentIdx < 0)
		{
			m_roots.push_back(m_joints.size() - 1);
		}
		// Load child joints
		for (i=0; i<pGameNode->GetChildCount();i++)
		{
			IGameNode* pGameChildNode = pGameNode->GetNodeChild(i);
			if( pGameChildNode )
			{
				loadJoint(pGameChildNode,params);
			}
		}
		return true;
	}

	// Load animations
	bool Skeleton::loadAnims(ParamList& params)
	{
		bool stat;
		int i;

		// save current time for later restore
		TimeValue curTime = GetCOREInterface()->GetTime();
		OrkigeMaxExporterLog( "Loading joint animations...\n");
		

		// clear animations list
		m_animations.clear();
		// load skeleton animation clips for the whole skeleton
		for (i=0; i<params.skelClipList.size(); i++)
		{
			stat = loadClip(params.skelClipList[i].name,params.skelClipList[i].start,
				params.skelClipList[i].stop,params.skelClipList[i].rate,params);
			if (stat == true)
			{
				OrkigeMaxExporterLog( "Clip successfully loaded\n");
				
			}
			else
			{
				OrkigeMaxExporterLog( "Failed loading clip\n");
				
			}
		}
		//restore current time
		GetCOREInterface()->SetTime(curTime);
		return true;
	}

	// Load an animation clip
	bool Skeleton::loadClip(std::string clipName,float start,float stop,float rate,ParamList& params)
	{
		int i,j;
		std::string msg;
		std::vector<float> times;
		// if skeleton has no joints we can't load the clip
		if (m_joints.size() < 0)
			return false;
		// display clip name
		OrkigeMaxExporterLog( "clip \"%s\"\n", clipName.c_str());
		
		// calculate times from clip sample rate
		times.clear();
		if (rate <= 0)
		{
			OrkigeMaxExporterLog( "invalid sample rate for the clip (must be >0), we skip it\n");
			return false;
		}
		for (float t=start; t<stop; t+=rate)
			times.push_back(t);
		times.push_back(stop);
		// get animation length
		float length=0;
		if (times.size() >= 0)
			length = times[times.size()-1] - times[0];
		if (length < 0)
		{
			OrkigeMaxExporterLog( "invalid time range for the clip, we skip it\n");
			return false;
		}
		// create the animation
		Animation a;
		a.m_name = clipName.c_str();
		a.m_tracks.clear();
		a.m_length = length;
		m_animations.push_back(a);
		int animIdx = m_animations.size() - 1;
		// create a track for current clip for all joints
		std::vector<Track> animTracks;
		for (i=0; i<m_joints.size(); i++)
		{
			Track t;
			t.m_type = TT_SKELETON;
			t.m_bone = m_joints[i].name;
			t.m_skeletonKeyframes.clear();
			animTracks.push_back(t);
		}
		// evaluate animation curves at selected times
		for (i=0; i<times.size(); i++)
		{

			int closestFrame = (int)(.5f + times[i]* GetFrameRate());
			//set time to wanted sample time
			GetCOREInterface()->SetTime(closestFrame * GetTicksPerFrame());
			//load a keyframe for every joint at current time
			for (j=0; j<m_joints.size(); j++)
			{
				skeletonKeyframe key = loadKeyframe(m_joints[j],times[i]-times[0],params);
				//add keyframe to joint track
				animTracks[j].addSkeletonKeyframe(key);
			}
			if (params.skelBB)
			{
				// Update bounding boxes of loaded submeshes
				for (j=0; j<params.loadedSubmeshes.size(); j++)
				{
					IGameNode *pGameNode = params.loadedSubmeshes[j]->m_pGameNode;
					if(pGameNode)
					{
						IGameObject* pGameObject = pGameNode->GetIGameObject();
						if(pGameObject)
						{
							Box3 bbox;
							pGameObject->GetBoundingBox(bbox);
							if (params.exportWorldCoords)
								bbox = bbox * pGameNode->GetWorldTM(GetCOREInterface()->GetTime()).ExtractMatrix3();
							Point3 min = bbox.Min() * params.lum;
							Point3 max = bbox.Max() * params.lum;
							Box3 newbbox(min,max);
							params.loadedSubmeshes[j]->m_boundingBox += newbbox;
						}
						pGameNode->ReleaseIGameObject();
					}
				}
			}
		}
		// add created tracks to current clip
		for (i=0; i<animTracks.size(); i++)
		{
			m_animations[animIdx].addTrack(animTracks[i]);
		}
		// display info
		OrkigeMaxExporterLog( "length: %f\n", m_animations[animIdx].m_length);
		OrkigeMaxExporterLog( "num keyframes: %d\n", animTracks[0].m_skeletonKeyframes.size());
		
		// clip successfully loaded
		return true;
	}

	// Load a keyframe for a given joint at current time
	skeletonKeyframe Skeleton::loadKeyframe(joint& j,float time,ParamList& params)
	{
		Point3 position;
		int parentIdx = j.parentIndex;

		// Get the node's transformation matrix.
		TimeValue currentTime = time*TIME_TICKSPERSEC;
		Matrix3 nodeTM = j.pGameNode->GetWorldTM(currentTime).ExtractMatrix3();
		Matrix3 parentTM, localTM;
		IGameNode* pGameNodeParent = j.pGameNode->GetNodeParent();

		if (pGameNodeParent)
		{
			parentTM = pGameNodeParent->GetWorldTM(currentTime).ExtractMatrix3();
		}
		
		if( params.normalizeScale )
		{
			nodeTM = RemoveNonUniformScaling(nodeTM);
			parentTM = RemoveNonUniformScaling(parentTM);
		}

		if( parentIdx >= 0 )
		{
			localTM = nodeTM * Inverse(parentTM);
		}
		else
		{
			// Root node of skeleton
			if (params.exportWorldCoords)
			{
				localTM = nodeTM;
			}
			else
			{
				localTM = nodeTM * Inverse(j.bindMatrix);
			}
		}

		Matrix3 relMatrix = localTM * Inverse(j.localMatrix);

		// Decompose the node's transformation matrix.
		AffineParts ap;
		decomp_affine(relMatrix, &ap);

		Point3 translation;
		translation = localTM.GetTrans() - j.localMatrix.GetTrans();
		if (fabs(translation.x) < PRECISION)
			translation.x = 0;
		if (fabs(translation.y) < PRECISION)
			translation.y = 0;
		if (fabs(translation.z) < PRECISION)
			translation.z = 0;

		Point3 scale(ap.k.x, ap.k.y, ap.k.z);
		if (fabs(scale.x) < PRECISION)
			scale.x = 0;
		if (fabs(scale.y) < PRECISION)
			scale.y = 0;
		if (fabs(scale.z) < PRECISION)
			scale.z = 0;
		AngAxis angAxis(ap.q);
		if (fabs(angAxis.axis.x) < PRECISION)
			angAxis.axis.x = 0;
		if (fabs(angAxis.axis.y) < PRECISION)
			angAxis.axis.y = 0;
		if (fabs(angAxis.axis.z) < PRECISION)
			angAxis.axis.z = 0;
		angAxis.axis.Normalize();
		if (fabs(angAxis.angle) < PRECISION)
			angAxis.angle = 0;
		if (angAxis.axis.Length() < 0.5)
		{
			angAxis.axis.x = 0;
			angAxis.axis.y = 1;
			angAxis.axis.z = 0;
			angAxis.angle = 0;
		}
		//create keyframe
		skeletonKeyframe key;
		key.time = time;
		key.tx = translation.x * params.lum;
		key.ty = translation.y * params.lum;
		key.tz = translation.z * params.lum;
		key.angle = -angAxis.angle; // not sure why I need to negate the angle.	
		key.axis_x = angAxis.axis.x;
		key.axis_y = angAxis.axis.y;
		key.axis_z = angAxis.axis.z;
		key.sx = scale.x;
		key.sy = scale.y;
		key.sz = scale.z;
		return key;
	}

	// Restore skeleton pose
	void Skeleton::restorePose()
	{
		// TODO: required in Max?
	}

	// Get joint list
	std::vector<joint>& Skeleton::getJoints()
	{
		return m_joints;
	}

	// Get animations
	std::vector<Animation>& Skeleton::getAnimations()
	{
		return m_animations;
	}


	// Write to an OGRE binary skeleton
	bool Skeleton::writeOgreBinary(ParamList &params)
	{
		bool stat;
		// Construct skeleton
		std::string name = "maxExport";
		Ogre::SkeletonPtr pSkeleton = Ogre::SkeletonManager::getSingleton().create(name.c_str(), 
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		// Create skeleton bones
		stat = createOgreBones(pSkeleton,params);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error writing skeleton binary file\n");
		}
		// Create skeleton animation
		if (params.exportSkelAnims)
		{
			stat = createOgreSkeletonAnimations(pSkeleton,params);
			if (stat != true)
			{
				OrkigeMaxExporterLog( "Error writing ogre skeleton animations\n");
			}
		}
		pSkeleton->setBindingPose();
		// Optimise animations
		pSkeleton->optimiseAllAnimations();
		// Export skeleton binary
		Ogre::SkeletonSerializer serializer;
		serializer.exportSkeleton(pSkeleton.getPointer(),params.skeletonFilename.c_str());
		pSkeleton.setNull();
		// Skeleton successfully exported
		return true;
	}

	// Write joints to an Ogre skeleton
	bool Skeleton::createOgreBones(Ogre::SkeletonPtr pSkeleton,ParamList& params)
	{
		// Doug Perkowski
		// 5/25/2010
		// To prevent a crash on content with more than  (256) bones, we need to check
		// to make sure there aren't more than OGRE_MAX_NUM_BONES bones.
		if( m_joints.size() > OGRE_MAX_NUM_BONES )
		{
			OrkigeMaxExporterLog( "Failure: Skeleton has more than OGRE_MAX_NUM_BONES.  No bones will be exported.\n");
			return false;
		}
		int i;
		// Create the bones
		for (i=0; i<m_joints.size(); i++)
		{
			joint* j = &m_joints[i];
			// Create a new bone
			Ogre::Bone* pBone = pSkeleton->createBone(m_joints[i].name.c_str(), m_joints[i].id);
			// Set bone position (relative to it's parent)
			pBone->setPosition(j->posx,j->posy,j->posz);
			// Set bone orientation (relative to it's parent)
			Ogre::Quaternion orient;
			orient.FromAngleAxis(Ogre::Radian(j->angle),Ogre::Vector3(j->axisx,j->axisy,j->axisz));
			pBone->setOrientation(orient);
			// Set bone scale (relative to it's parent
			pBone->setScale(j->scalex,j->scaley,j->scalez);
		}
		// Create the hierarchy
		for (i=0; i<m_joints.size(); i++)
		{
			int parentIdx = m_joints[i].parentIndex;
			if (parentIdx >= 0)
			{
				// Get the parent joint
				Ogre::Bone* pParent = pSkeleton->getBone(m_joints[parentIdx].id);
				// Get current joint from skeleton
				Ogre::Bone* pBone = pSkeleton->getBone(m_joints[i].id);
				// Place current bone in the parent's child list
				pParent->addChild(pBone);
			}
		}
		return true;
	}
	// Write skeleton animations to an Ogre skeleton
	bool Skeleton::createOgreSkeletonAnimations(Ogre::SkeletonPtr pSkeleton,ParamList& params)
	{
		int i,j,k;
		// Read loaded skeleton animations
		for (i=0; i<m_animations.size(); i++)
		{
			// Create a new animation
			Ogre::Animation* pAnimation = pSkeleton->createAnimation(m_animations[i].m_name.c_str(),
				m_animations[i].m_length);
			// Create tracks for current animation
			for (j=0; j<m_animations[i].m_tracks.size(); j++)
			{
				Track* t = &m_animations[i].m_tracks[j];
				// Create a new track
				Ogre::NodeAnimationTrack* pTrack = pAnimation->createNodeTrack(j,
					pSkeleton->getBone(t->m_bone.c_str()));
				// Create keyframes for current track
				for (k=0; k<t->m_skeletonKeyframes.size(); k++)
				{
					skeletonKeyframe* keyframe = &t->m_skeletonKeyframes[k];
					// Create a new keyframe
					Ogre::TransformKeyFrame* pKeyframe = pTrack->createNodeKeyFrame(keyframe->time);
					// Set translation
					pKeyframe->setTranslate(Ogre::Vector3(keyframe->tx,keyframe->ty,keyframe->tz));
					// Set rotation
					Ogre::Quaternion rot;
					rot.FromAngleAxis(Ogre::Radian(keyframe->angle),
						Ogre::Vector3(keyframe->axis_x,keyframe->axis_y,keyframe->axis_z));
					pKeyframe->setRotation(rot);
					// Set scale
					pKeyframe->setScale(Ogre::Vector3(keyframe->sx,keyframe->sy,keyframe->sz));
				}
			}
		}
		return true;
	}

};	//end namespace
