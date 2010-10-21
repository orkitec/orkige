////////////////////////////////////////////////////////////////////////////////
// submesh.cpp
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

#include "submesh.h"
#include "FxOgreMaxExporterLog.h"
namespace FxOgreMaxExporter
{
	/***** Class Submesh *****/
	// constructor
	Submesh::Submesh(const std::string& name)
	{
		m_pBlendShape = NULL;
		clear();
		m_name = name;
	}

	// destructor
	Submesh::~Submesh()
	{
		clear();
	}

	// clear data
	void Submesh::clear()
	{
		m_name = "";
		m_numTriangles = 0;
		m_pMaterial = NULL;
		m_vertices.clear();
		m_faces.clear();
		m_uvsets.clear();
		m_use32bitIndexes = false;
		// Doug Perkowski - Cleaning up BlendShapes in Mesh.cpp.
//		if (m_pBlendShape)
//			delete m_pBlendShape;
		m_pBlendShape = NULL;
	}

	// return number of triangles composing the mesh
	long Submesh::numTriangles()
	{
		return m_numTriangles;
	}

	// return number of vertices composing the mesh
	long Submesh::numVertices()
	{
		return m_numVertices;
	}

	// return submesh name
	std::string& Submesh::name()
	{
		return m_name;
	}

/***** load data *****/
	bool Submesh::loadMaterial(IGameMaterial* pGameMaterial,std::vector<int>& uvsets,ParamList& params)
	{
		//check if this material has already been created
		//fix material name, adding the requested prefix
		std::string tmpStr = params.matPrefix;
		if (tmpStr != "")
			tmpStr += "/";
		if(pGameMaterial)
			tmpStr.append(pGameMaterial->GetMaterialName());
		std::string name = "";
		for(size_t i = 0; i < tmpStr.size(); ++i)
		{
			if(tmpStr[i] == ':')
			{
				name.append("_");
			}
			else
			{
				name.append(tmpStr.substr(i, 1));
			}
		}
		Material* pMaterial = MaterialSet::getSingleton().getMaterial(name);
		//if the material has already been created, update the pointer
		if (pMaterial)
			m_pMaterial = pMaterial;
		//else create it and add it to the material set
		else
		{
			pMaterial = new Material();
			pMaterial->load(pGameMaterial,uvsets,params);
			m_pMaterial = pMaterial;
			MaterialSet::getSingleton().addMaterial(pMaterial);
		}
		//loading complete
		return true;
	}

	bool Submesh::load(IGameNode* pGameNode,std::vector<face>& faces, std::vector<vertexInfo>& vertInfo, std::vector<Point3> points, 
		std::vector<Point3> normals, std::vector<int>& texcoordsets,ParamList& params,bool opposite)
	{
		//save the mesh from which this submesh will be created
		m_pGameNode = pGameNode;

		size_t i,j,k;
		FxOgreMaxExporterLog( "Loading submesh associated to material: %s ...\n", m_pMaterial->name().c_str());
		
		//save uvsets info
		for (i=m_uvsets.size(); i<texcoordsets.size(); i++)
		{
			uvset uv;
			uv.size = 2;
			m_uvsets.push_back(uv);
		}
		//iterate over faces array, to retrieve vertices info
		for (i=0; i<faces.size(); i++)
		{
			face newFace;
			// if we are using shared geometry, indexes refer to the vertex buffer of the whole mesh
			if (params.useSharedGeom)
			{
				if(opposite)
				{	// reverse order of face vertices for correct culling
					newFace.v[0] = faces[i].v[2];
					newFace.v[1] = faces[i].v[1];
					newFace.v[2] = faces[i].v[0];
				}
				else
				{
					newFace.v[0] = faces[i].v[0];
					newFace.v[1] = faces[i].v[1];
					newFace.v[2] = faces[i].v[2];
				}
			}
			// otherwise we create a vertex buffer for this submesh
			else
			{	// faces are triangles, so retrieve index of the three vertices
				for (j=0; j<3; j++)
				{
					vertex v;
					vertexInfo vInfo = vertInfo[faces[i].v[j]];
					// save vertex coordinates (rescale to desired length unit)
					assert(vInfo.pointIdx >= 0 && vInfo.pointIdx < points.size());
					Point3 point = points[vInfo.pointIdx] * params.lum;
					if (fabs(point.x) < PRECISION)
						point.x = 0;
					if (fabs(point.y) < PRECISION)
						point.y = 0;
					if (fabs(point.z) < PRECISION)
						point.z = 0;
					v.x = point.x;
					v.y = point.y;
					v.z = point.z;
					// save vertex normal
					assert(vInfo.normalIdx >= 0 && vInfo.normalIdx < normals.size());
					Point3 normal = normals[vInfo.normalIdx];
					if (fabs(normal.x) < PRECISION)
						normal.x = 0;
					if (fabs(normal.y) < PRECISION)
						normal.y = 0;
					if (fabs(normal.z) < PRECISION)
						normal.z = 0;
					if (opposite)
					{
						v.n.x = -normal.x;
						v.n.y = -normal.y;
						v.n.z = -normal.z;
					}
					else
					{
						v.n.x = normal.x;
						v.n.y = normal.y;
						v.n.z = normal.z;
					}
					v.n.Normalize();

					// save vertex color
					v.r = vInfo.r;
					v.g = vInfo.g;
					v.b = vInfo.b;
					v.a = vInfo.a;
					// save vertex bone assignements
					for (k=0; k<vInfo.vba.size(); k++)
					{
						vba newVba;
						newVba.jointIdx = vInfo.jointIds[k];
						newVba.weight = vInfo.vba[k];
						v.vbas.push_back(newVba);
					}
					// save texture coordinates
					for (k=0; k<vInfo.u.size(); k++)
					{
						texcoord newTexCoords;
						newTexCoords.u = vInfo.u[k];
						newTexCoords.v = vInfo.v[k];
						newTexCoords.w = 0;
						v.texcoords.push_back(newTexCoords);
					}
					// save vertex index in mesh, to retrieve future positions of the same vertex
					v.index = vInfo.pointIdx;
					// add newly created vertex to vertex list
					m_vertices.push_back(v);
					if (opposite)	// reverse order of face vertices to get correct culling
						newFace.v[2-j] = m_vertices.size() - 1;
					else
						newFace.v[j] = m_vertices.size() - 1;
				}
			}
			m_faces.push_back(newFace);
		}
		// set use32bitIndexes flag
		if (params.useSharedGeom || (m_vertices.size() > 65535) || (m_faces.size() > 65535))
			m_use32bitIndexes = true;
		else
			m_use32bitIndexes = false;

		if(pGameNode)
		{
			IGameObject* pGameObject = pGameNode->GetIGameObject();
			if(pGameObject)
			{
				Box3 bbox;
				pGameObject->GetBoundingBox(bbox);
				// The below code was ported over from Maya, but it appears to be messing
				// up my bounding box.  In both Worldspace and Object space I'm getting better results without this.
				// if (params.exportWorldCoords)
				//	bbox = bbox * pGameNode->GetWorldTM(GetCOREInterface()->GetTime()).ExtractMatrix3();
				Point3 min = bbox.Min() * params.lum;
				Point3 max = bbox.Max() * params.lum;
				Box3 newbbox(min,max);
				m_boundingBox = newbbox;
			}
			pGameNode->ReleaseIGameObject();
		}

		// add submesh pointer to FxOgreMaxExporter::FxOgreMaxExporterData::maxInterface.m_params list
		params.loadedSubmeshes.push_back(this);

		FxOgreMaxExporterLog( "DONE\n");
		
		return true;
	}
	// Load a keyframe for this submesh
	bool Submesh::loadKeyframe(Track& t,float time,ParamList& params)
	{
		int i;
		// create a new keyframe
		vertexKeyframe k;
		// set keyframe time
		k.time = time;

		bool bIsObjectSpace = true;
		if (params.exportWorldCoords)
			bIsObjectSpace = false;

		IGameMesh* pGameMesh = (IGameMesh*)m_pGameNode->GetIGameObject();
		if(!pGameMesh)
			return false;
		if(!pGameMesh->InitializeData() )
			return false;
		assert(pGameMesh->GetNumberOfVerts()==m_vertices.size());
		// calculate vertex offsets
		for (i=0; i<m_vertices.size(); i++)
		{
			vertexPosition pos;
			vertex v = m_vertices[i];
			assert(pGameMesh->GetNumberOfVerts() > v.index);
			Point3 point = pGameMesh->GetVertex(v.index, bIsObjectSpace);
			pos.x = point.x;
			pos.y = point.y;
			pos.z = point.z;
			if (fabs(pos.x) < PRECISION)
					pos.x = 0;
			if (fabs(pos.y) < PRECISION)
				pos.y = 0;
			if (fabs(pos.z) < PRECISION)
				pos.z = 0;
			k.positions.push_back(pos);
		}
		// add keyframe to given track
		t.addVertexKeyframe(k);
		if (params.vertBB)
		{
			Box3 bbox;
			pGameMesh->GetBoundingBox(bbox);
			if (params.exportWorldCoords)
				bbox = bbox * m_pGameNode->GetWorldTM(GetCOREInterface()->GetTime()).ExtractMatrix3();
			m_boundingBox += bbox;
		}
		m_pGameNode->ReleaseIGameObject();
		// keyframe successfully loaded
		return true;
	}



/***** Export data *****/
	// Write submesh data to an Ogre compatible mesh
	bool Submesh::createOgreSubmesh(Ogre::MeshPtr pMesh,const ParamList& params)
	{
		int i,j;
		bool stat;
		// Create a new submesh
		Ogre::SubMesh* pSubmesh;
		if (m_name != "")
			pSubmesh = pMesh->createSubMesh(m_name.c_str());
		else
			pSubmesh = pMesh->createSubMesh();
		// Set material
        pSubmesh->setMaterialName(m_pMaterial->name().c_str());
        // Set use shared geometry flag
		pSubmesh->useSharedVertices = params.useSharedGeom;
		// Create vertex data for current submesh
		pSubmesh->vertexData = new Ogre::VertexData();
        // Set number of indexes
        pSubmesh->indexData->indexCount = 3*m_faces.size();
		pSubmesh->vertexData->vertexCount = m_vertices.size();
		// Check if we need to use 32 bit indexes
		bool use32BitIndexes = false;
		if (m_vertices.size() > 65536 || params.useSharedGeom)
		{
			use32BitIndexes = true;
		}
		// Create a new index buffer
		pSubmesh->indexData->indexBuffer = 
			Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
				use32BitIndexes ? Ogre::HardwareIndexBuffer::IT_32BIT : Ogre::HardwareIndexBuffer::IT_16BIT,
				pSubmesh->indexData->indexCount,
				Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY);
		// Fill the index buffer with faces data
		if (use32BitIndexes)
        {
			Ogre::uint32* pIdx = static_cast<Ogre::uint32*>(
				pSubmesh->indexData->indexBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD));
			for (i=0; i<m_faces.size(); i++)
			{
				*pIdx++ = static_cast<Ogre::uint32>(m_faces[i].v[0]);
				*pIdx++ = static_cast<Ogre::uint32>(m_faces[i].v[1]);
				*pIdx++ = static_cast<Ogre::uint32>(m_faces[i].v[2]);
			}
			pSubmesh->indexData->indexBuffer->unlock();
        }
        else
        {
            Ogre::uint16* pIdx = static_cast<Ogre::uint16*>(
				pSubmesh->indexData->indexBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD));
            for (i=0; i<m_faces.size(); i++)
			{
				*pIdx++ = static_cast<Ogre::uint16>(m_faces[i].v[0]);
				*pIdx++ = static_cast<Ogre::uint16>(m_faces[i].v[1]);
				*pIdx++ = static_cast<Ogre::uint16>(m_faces[i].v[2]);
			}
			pSubmesh->indexData->indexBuffer->unlock();
		}
		// Define vertex declaration (only if we're not using shared geometry)
		if(!params.useSharedGeom)
		{
			Ogre::VertexDeclaration* pDecl = pSubmesh->vertexData->vertexDeclaration;
			unsigned buf = 0;
			size_t offset = 0;
			// Add vertex position
			pDecl->addElement(buf, offset, Ogre::VET_FLOAT3, Ogre::VES_POSITION);
			offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
			// Add vertex normal
			if (params.exportVertNorm)
			{
				pDecl->addElement(buf, offset, Ogre::VET_FLOAT3, Ogre::VES_NORMAL);
				offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
			}
			// Add vertex colour
			if(params.exportVertCol)
			{
				pDecl->addElement(buf, offset, Ogre::VET_COLOUR, Ogre::VES_DIFFUSE);
				offset += Ogre::VertexElement::getTypeSize(Ogre::VET_COLOUR);
			}
			// Add texture coordinates
			for (i=0; i<m_vertices[0].texcoords.size(); i++)
			{
				Ogre::VertexElementType uvType = Ogre::VertexElement::multiplyTypeCount(Ogre::VET_FLOAT1, 2);
				pDecl->addElement(buf, offset, uvType, Ogre::VES_TEXTURE_COORDINATES, i);
				offset += Ogre::VertexElement::getTypeSize(uvType);
			}
			Ogre::VertexDeclaration* pOptimalDecl = pDecl->getAutoOrganisedDeclaration(
				params.exportVBA,params.exportBlendShapes || params.exportVertAnims);
			// Fill the vertex buffer using the newly created vertex declaration
			stat = createOgreVertexBuffer(pSubmesh,pDecl,m_vertices);
			// Write vertex bone assignements list
			if (params.exportVBA)
			{
				// Create a new vertex bone assignements list
				Ogre::SubMesh::VertexBoneAssignmentList vbas;
				// Scan list of shared geometry vertices
				for (i=0; i<m_vertices.size(); i++)
				{
					vertex v = m_vertices[i];
					// Add all bone assignemnts for every vertex to the bone assignements list
					for (j=0; j<v.vbas.size(); j++)
					{
						Ogre::VertexBoneAssignment vba;
						vba.vertexIndex = i;
						vba.boneIndex = v.vbas[j].jointIdx;
						vba.weight = v.vbas[j].weight;
						vbas.insert(Ogre::SubMesh::VertexBoneAssignmentList::value_type(i, vba));
					}
				}
				// Rationalise the bone assignements list
				pSubmesh->parent->_rationaliseBoneAssignments(pSubmesh->vertexData->vertexCount,vbas);
				// Add bone assignements to the submesh
				for (Ogre::SubMesh::VertexBoneAssignmentList::iterator bi = vbas.begin(); bi != vbas.end(); bi++)
				{
					pSubmesh->addBoneAssignment(bi->second);
				}
				pSubmesh->_compileBoneAssignments();
			}
			pSubmesh->vertexData->reorganiseBuffers(pOptimalDecl);
		}
		return true;
	}


	// Create an Ogre compatible vertex buffer
	bool Submesh::createOgreVertexBuffer(Ogre::SubMesh* pSubmesh,Ogre::VertexDeclaration* pDecl,const std::vector<vertex>& vertices)
	{
		Ogre::HardwareVertexBufferSharedPtr vbuf = 
			Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(pDecl->getVertexSize(0),
			pSubmesh->vertexData->vertexCount, 
			Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY );
		pSubmesh->vertexData->vertexBufferBinding->setBinding(0, vbuf);
		size_t vertexSize = pDecl->getVertexSize(0);
		char* pBase = static_cast<char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD));
		Ogre::VertexDeclaration::VertexElementList elems = pDecl->findElementsBySource(0);
		Ogre::VertexDeclaration::VertexElementList::iterator ei, eiend;
		eiend = elems.end();
		float* pFloat;
		Ogre::RGBA* pRGBA;
		// Fill the vertex buffer with shared geometry data
		long vi;
		Ogre::ColourValue col;
		float ucoord, vcoord;
		for (vi=0; vi<vertices.size(); vi++)
		{
			int iTexCoord = 0;
			vertex v = vertices[vi];
			for (ei = elems.begin(); ei != eiend; ++ei)
			{
				Ogre::VertexElement& elem = *ei;
				switch(elem.getSemantic())
				{
				case Ogre::VES_POSITION:
					elem.baseVertexPointerToElement(pBase, &pFloat);
					*pFloat++ = v.x;
					*pFloat++ = v.y;
					*pFloat++ = v.z;
					break;
				case Ogre::VES_NORMAL:
					elem.baseVertexPointerToElement(pBase, &pFloat);
					*pFloat++ = v.n.x;
					*pFloat++ = v.n.y;
					*pFloat++ = v.n.z;
					break;
				case Ogre::VES_DIFFUSE:
					{
						elem.baseVertexPointerToElement(pBase, &pRGBA);
						Ogre::ColourValue col(v.r, v.g, v.b, v.a);
						*pRGBA = Ogre::VertexElement::convertColourValue(col, 
							Ogre::VertexElement::getBestColourVertexElementType());
					}
					break;
				case Ogre::VES_TEXTURE_COORDINATES:
					elem.baseVertexPointerToElement(pBase, &pFloat);
					ucoord = v.texcoords[iTexCoord].u;
					vcoord = v.texcoords[iTexCoord].v;
					*pFloat++ = ucoord;
					*pFloat++ = vcoord;
					iTexCoord++;
					break;
				}
			}
			pBase += vertexSize;
		}
		vbuf->unlock();
		return true;
	}

}; //end of namespace
