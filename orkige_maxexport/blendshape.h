////////////////////////////////////////////////////////////////////////////////
// blendshape.h
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

#ifndef _BLENDSHAPE_H
#define _BLENDSHAPE_H

#include "maxExportLayer.h"
#include "paramList.h"
#include "animation.h"
#include "vertex.h"

namespace FxOgreMaxExporter
{
	typedef struct
	{
		int targetIndex;
		std::vector<pose> poses;
	} poseGroup;
	// Blend Shape Class
	class BlendShape
	{
	public:
		// Constructor
		BlendShape(MorphR3* pMorphR3, IGameNode* pGameNode);
		// Destructor
		~BlendShape();
		// Clear blend shape data
		void clear();
		// Load blend shape poses
		bool loadPoses(ParamList &params,std::vector<vertex> &vertices,
			long numVertices,long offset=0,long targetIndex=0);
		//load a blend shape animation track
		Track loadTrack(float start,float stop,float rate,ParamList& params,int targetIndex, int startPoseId);
		// Get blend shape deformer name
		std::string getName();
		// Get blend shape poses
		stdext::hash_map<int, poseGroup>& getPoseGroups();
		// Set maya blend shape deformer envelope
		void setEnvelope(float envelope);
		// Restore maya blend shape deformer original envelope
		void restoreEnvelope();
		// Public members
		

	protected:
		// Internal methods

		// Protected members
		IGameNode* m_pGameNode;
		MorphR3 *m_pMorphR3;

		//original values to restore after export
		float m_origEnvelope;
		std::vector<float> m_origWeights;
		//blend shape poses
		stdext::hash_map<int, poseGroup> m_poseGroups;
		// An array of morphChannel* to correspond to each pose in m_poses 
		std::vector<morphChannel*> m_posesChannels;
		//blend shape target (shared geometry or submesh)
		target m_target;
	};


}	// end namespace

#endif
