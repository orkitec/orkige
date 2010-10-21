////////////////////////////////////////////////////////////////////////////////
// mesh.h
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

#ifndef _MESH_H
#define _MESH_H

#include "submesh.h"
#include "skeleton.h"
#include "maxExportLayer.h"
#include "vertex.h"
#include "paramList.h"


namespace FxOgreMaxExporter
{
	
	/***** structures to store shared geometry *****/
	typedef struct dagInfotag
	{
		long offset;
		long numVertices;
		IGameNode* pGameNode;
		BlendShape* pBlendShape;
	} dagInfo;

	typedef struct sharedGeometrytag
	{
		std::vector<vertex> vertices;
		std::vector<dagInfo> dagMap;
	} sharedGeometry;

	typedef stdext::hash_map<int,int> submeshPoseRemapping;

	typedef stdext::hash_map<int,submeshPoseRemapping> poseRemapping; 

	/***** Class Mesh *****/
	class Mesh
	{
	public:
		//constructor
		Mesh(const std::string& name = "");
		//destructor
		~Mesh();
		//clear data
		void clear();
		//get pointer to linked skeleton
		Skeleton* getSkeleton();
		//load mesh data from a maya Fn
		bool load(IGameNode* pGameNode,IGameObject* pGameObject,IGameMesh* pGameMesh,ParamList &params);
		//load vertex animations
		bool loadAnims(ParamList &params);
		//load blend shape deformers
		bool loadBlendShapes(ParamList &params);
		//load blend shape animations
		bool loadBlendShapeAnimations(ParamList& params);
		//write to a OGRE binary mesh
		bool writeOgreBinary(ParamList &params);

	protected:
		//get uvsets info from the mesh
		bool getUVSets(IGameMesh* pGameMesh);
		//get modifiers (skin and morph)
		bool getModifiers(IGameNode* pGameNode, IGameObject* pGameObject,ParamList& params); 
		//get connected shaders
		bool getShaders(IGameMesh* pGameMesh);

		//get vertex data
		bool getVertices(IGameMesh* pGameMesh,ParamList& params);
		//get vertex bone assignements
		bool getVertexBoneWeights(IGameMesh* pGameMesh,FxOgreMaxExporter::ParamList &params);
		//get faces data
		bool getFaces(IGameMesh* pGameMesh,ParamList& params);
		// Set default values for data (rigidly skinned meshes, bone weights, position indices)
		bool cleanupData(IGameNode* pGameNode, IGameMesh* pGameMesh, ParamList& params);
		//build shared geometry
		bool buildSharedGeometry(IGameNode* pGameMesh,ParamList& params);
		//create submeshes
		bool createSubmeshes(IGameNode* pGameNode, IGameMesh* pGameMesh,ParamList& params);
		//load a vertex animation clip
		bool loadClip(std::string& clipName,float start,float stop,float rate,ParamList& params);
		//load a vertex animation track for the whole mesh
		bool loadMeshTrack(Animation& a,std::vector<float>& times,ParamList& params);
		//load all submesh animation tracks (one for each submesh)
		bool loadSubmeshTracks(Animation& a,std::vector<float>& times,ParamList& params);
		//load a keyframe for the whole mesh
		bool loadKeyframe(Track& t,float time,ParamList& params);
		
		//write shared geometry data to an Ogre compatible mesh
		bool createOgreSharedGeometry(Ogre::MeshPtr pMesh,ParamList& params);
		//create an Ogre compatible vertex buffer
		bool createOgreVertexBuffer(Ogre::MeshPtr pMesh,Ogre::VertexDeclaration* pDecl,const std::vector<vertex>& vertices);
		//create Ogre poses for pose animation
		bool createOgrePoses(Ogre::MeshPtr pMesh,ParamList& params);
		//create vertex animations for an Ogre mesh
		bool createOgreVertexAnimations(Ogre::MeshPtr pMesh,ParamList& params);
		//create pose animations for an Ogre mesh
		bool createOgrePoseAnimations(Ogre::MeshPtr pMesh,ParamList& params);

		//internal members
		std::string m_name;
		long m_numTriangles;
		std::vector<uvset> m_uvsets;
		std::vector<Submesh*> m_submeshes;
		Skeleton* m_pSkeleton;
		sharedGeometry m_sharedGeom;
		std::vector<Animation> m_vertexClips;
		std::vector<Animation> m_BSClips;
		//temporary members (existing only during translation from max mesh)
		std::vector<vertexInfo> newvertices;
		std::vector< std::vector<float> > newweights;
		std::vector< std::vector<int> > newjointIds;
		std::vector<Point3> newpoints;
		std::vector<Point3> newnormals;
		std::vector<int> newuvsets;
		IGameSkin* pSkinCluster;
		BlendShape* pBlendShape;

		std::vector<IGameMaterial*> shaders;
		std::vector<int> shaderPolygonMapping;
		std::vector<faceArray> polygonSets;
		
		bool opposite;
		poseRemapping m_poseRemapping;
	};

}; // end of namespace

#endif
