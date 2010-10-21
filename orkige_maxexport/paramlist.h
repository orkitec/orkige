////////////////////////////////////////////////////////////////////////////////
// paramlist.h
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

#ifndef PARAMLIST_H
#define PARAMLIST_H

#include "maxExportLayer.h"

// Length units multipliers from Maya internal unit (cm)

#define CM2MM 10.0
#define CM2CM 1.0
#define CM2M  0.01
#define CM2IN 0.393701
#define CM2FT 0.0328084
#define CM2YD 0.0109361


namespace FxOgreMaxExporter
{
	std::string StripToTopParent(const std::string& filepath);

	class Submesh;

	typedef struct clipInfoTag
	{
		float start;							//start time of the clip
		float stop;								//end time of the clip
		float rate;								//sample rate of anim curves, -1 means auto
		std::string name;						//clip name
	} clipInfo;

	typedef enum
	{
		NPT_CURFRAME,
		NPT_BINDPOSE
	} NeutralPoseType;

	typedef enum
	{
		TS_TEXCOORD,
		TS_TANGENT
	} TangentSemantic;

	/***** Class ParamList *****/
	class ParamList
	{
	public:
		// class members
		bool exportMesh, exportMaterial, exportAnimCurves, exportCameras, exportAll, exportVBA,
			exportVertNorm, exportVertCol, exportTexCoord, exportCamerasAnim,
			exportSkeleton, exportSkelAnims, exportBSAnims, exportVertAnims, exportBlendShapes, 
			exportWorldCoords, useSharedGeom, lightingOff, copyTextures, exportParticles,
			tangentsSplitMirrored, tangentsSplitRotated, tangentsUseParity, 
			buildTangents, buildEdges, skelBB, bsBB, vertBB, normalizeScale, yUpAxis, exportScene;

		float lum;	// Length Unit Multiplier

		std::string meshFilename, skeletonFilename, materialFilename, animFilename, camerasFilename, matPrefix,
			texOutputDir, particlesFilename, outputDir, sceneFilename;

		std::ofstream outMaterial, outAnim, outCameras, outParticles;

		std::vector<std::string> writtenMaterials;

		std::vector<clipInfo> skelClipList;
		std::vector<clipInfo> BSClipList;
		std::vector<clipInfo> vertClipList;

		NeutralPoseType neutralPoseType;
		TangentSemantic tangentSemantic;

		std::vector<Submesh*> loadedSubmeshes;
		std::vector<INode*> currentRootJoints;

		// constructor
		ParamList()	{
			lum = 1.0f;
			exportMesh = true;
			exportMaterial = true;
			exportSkeleton = true;
			exportSkelAnims = false;
			exportBSAnims = false;
			exportVertAnims = false;
			exportBlendShapes = true;
			exportAnimCurves = false;
			exportCameras = false;
			exportParticles = false;
			exportAll = true;
			exportWorldCoords = true;
			exportVBA = true;
			exportVertNorm = true;
			exportVertCol = false;
			exportTexCoord = true;
			exportCamerasAnim = false;
			useSharedGeom = true;
			lightingOff = false;
			copyTextures = true;
			skelBB = false;
			bsBB = false;
			vertBB = false;
			meshFilename = "";
			skeletonFilename = "";
			materialFilename = "";
			animFilename = "";
			camerasFilename = "";
			particlesFilename = "";
			matPrefix = "";
			texOutputDir = "";
			skelClipList.clear();
			BSClipList.clear();
			vertClipList.clear();
			neutralPoseType = NPT_CURFRAME;
			buildEdges = false;
			buildTangents = false;
			tangentsSplitMirrored = false;
			tangentsSplitRotated = false;
			tangentsUseParity = false;
			tangentSemantic = TS_TANGENT;
			loadedSubmeshes.clear();
			currentRootJoints.clear();
			normalizeScale = true; // Remove's scale from animation (to solve for Max's unorthodox use of non-uniform scale)
			yUpAxis = false;
			exportScene = true;
			sceneFilename = "";
		}

		ParamList& operator=(ParamList& source)	
		{
			int i;
			lum = source.lum;
			exportMesh = source.exportMesh;
			exportMaterial = source.exportMaterial;
			exportSkeleton = source.exportSkeleton;
			exportSkelAnims = source.exportSkelAnims;
			exportBSAnims = source.exportBSAnims;
			exportVertAnims = source.exportVertAnims;
			exportBlendShapes = source.exportBlendShapes;
			exportAnimCurves = source.exportAnimCurves;
			exportCameras = source.exportCameras;
			exportAll = source.exportAll;
			exportWorldCoords = source.exportWorldCoords;
			exportVBA = source.exportVBA;
			exportVertNorm = source.exportVertNorm;
			exportVertCol = source.exportVertCol;
			exportTexCoord = source.exportTexCoord;
			exportCamerasAnim = source.exportCamerasAnim;
			exportParticles = source.exportParticles;
			useSharedGeom = source.useSharedGeom;
			lightingOff = source.lightingOff;
			copyTextures = source.copyTextures;
			skelBB = source.skelBB;
			bsBB = source.bsBB;
			vertBB = source.vertBB;
			meshFilename = source.meshFilename;
			skeletonFilename = source.skeletonFilename;
			materialFilename = source.materialFilename;
			animFilename = source.animFilename;
			camerasFilename = source.camerasFilename;
			particlesFilename = source.particlesFilename;
			matPrefix = source.matPrefix;
			texOutputDir = source.texOutputDir;
			buildEdges = source.buildEdges;
			buildTangents = source.buildTangents;
			tangentsSplitMirrored = source.tangentsSplitMirrored;
			tangentsSplitRotated = source.tangentsSplitRotated;
			tangentsUseParity = source.tangentsUseParity;
			tangentSemantic = source.tangentSemantic;
			skelClipList.resize(source.skelClipList.size());
			normalizeScale = source.normalizeScale;
			yUpAxis = source.yUpAxis;
			exportScene = source.exportScene;
			sceneFilename = source.sceneFilename;

			for (i=0; i< skelClipList.size(); i++)
			{
				skelClipList[i].name = source.skelClipList[i].name;
				skelClipList[i].start = source.skelClipList[i].start;
				skelClipList[i].stop = source.skelClipList[i].stop;
				skelClipList[i].rate = source.skelClipList[i].rate;
			}
			BSClipList.resize(source.BSClipList.size());
			for (i=0; i< BSClipList.size(); i++)
			{
				BSClipList[i].name = source.BSClipList[i].name;
				BSClipList[i].start = source.BSClipList[i].start;
				BSClipList[i].stop = source.BSClipList[i].stop;
				BSClipList[i].rate = source.BSClipList[i].rate;
			}
			vertClipList.resize(source.vertClipList.size());
			for (i=0; i< vertClipList.size(); i++)
			{
				vertClipList[i].name = source.vertClipList[i].name;
				vertClipList[i].start = source.vertClipList[i].start;
				vertClipList[i].stop = source.vertClipList[i].stop;
				vertClipList[i].rate = source.vertClipList[i].rate;
			}
			neutralPoseType = source.neutralPoseType;
			for (i=0; i<source.loadedSubmeshes.size(); i++)
				loadedSubmeshes.push_back(source.loadedSubmeshes[i]);
			for (i=0; i<source.currentRootJoints.size(); i++)
				currentRootJoints.push_back(source.currentRootJoints[i]);
			return *this;
		}

		// destructor
		~ParamList() {
			if (outMaterial)
				outMaterial.close();
			if (outAnim)
				outAnim.close();
			if (outCameras)
				outCameras.close();
			if (outParticles)
				outParticles.close();
		}
		// method to open files for writing
		bool openFiles();
		// method to close open output files
		bool closeFiles();
	};

};	//end namespace

#endif
