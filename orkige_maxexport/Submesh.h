////////////////////////////////////////////////////////////////////////////////
// submesh.h
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

#ifndef _SUBMESH_H
#define _SUBMESH_H

#include "MaxExportLayer.h"
#include "ParamList.h"
#include "MaterialSet.h"
#include "Animation.h"
#include "Vertex.h"
#include "Blendshape.h"

namespace OrkigeMaxExporter
{
	/***** Class Submesh *****/
	class Submesh
	{
	public:
		//constructor
		Submesh(const std::string& name = "");
		//destructor
		~Submesh();
		//clear data
		void clear();
		//load data
		bool loadMaterial(IGameMaterial* pGameMaterial,std::vector<int>& uvsets,ParamList& params);
		bool load(IGameNode* pGameNode,std::vector<face>& faces, std::vector<vertexInfo>& vertInfo, std::vector<Point3> points,
			std::vector<Point3> normals, std::vector<int>& texcoordsets,ParamList& params,bool opposite = false); 
		//load a keyframe for the whole mesh
		bool loadKeyframe(Track& t,float time,ParamList& params);
		//get number of triangles composing the submesh
		long numTriangles();
		//get number of vertices
		long numVertices();
		//get submesh name
		std::string& name();
		//write submesh data to an Ogre compatible mesh
		bool createOgreSubmesh(Ogre::MeshPtr pMesh,const ParamList& params);
		//create an Ogre compatible vertex buffer
		bool createOgreVertexBuffer(Ogre::SubMesh* pSubmesh,Ogre::VertexDeclaration* pDecl,const std::vector<vertex>& vertices);
		BlendShape* getBlendShape(){return m_pBlendShape;}
	public:
		//public members
		std::string m_name;
		Material* m_pMaterial;
		long m_numTriangles;
		long m_numVertices;
		std::vector<vertex> m_vertices;
		std::vector<face> m_faces;
		std::vector<uvset> m_uvsets;
		bool m_use32bitIndexes;
		IGameNode* m_pGameNode;
		int m_matID;
		BlendShape* m_pBlendShape;
		Box3 m_boundingBox;
	};

}; // end of namespace

#endif
