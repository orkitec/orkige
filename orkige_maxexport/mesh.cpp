////////////////////////////////////////////////////////////////////////////////
// mesh.cpp
// Author     : Francesco Giordana
// Start Date : January 13, 2005
// Copyright  : (C) 2006 by Francesco Giordana
// Email      : fra.giordana@tiscali.it
////////////////////////////////////////////////////////////////////////////////
// Port to 3D Studio Max - Modified original version - Modified original version
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

#include "mesh.h"
#include "OrkigeMaxExporterLog.h"
namespace OrkigeMaxExporter
{
	/***** Class Mesh *****/
	// constructor
	Mesh::Mesh(const std::string& name)
	{
		m_name = name;
		m_numTriangles = 0;
		m_pSkeleton = NULL;
		m_sharedGeom.vertices.clear();
		m_sharedGeom.dagMap.clear();
		m_vertexClips.clear();
		m_BSClips.clear();
	}

	// destructor
	Mesh::~Mesh()
	{
		clear();
	}

	// clear data
	void Mesh::clear()
	{
		int i;
		m_name = "";
		m_numTriangles = 0;

		std::map<BlendShape*, bool> deletedBlendShapes;
		
		for (i=0; i<m_submeshes.size(); i++)
		{
			deletedBlendShapes[m_submeshes[i]->getBlendShape()] = true;
			delete m_submeshes[i];
		}

		for (i=0; i<m_sharedGeom.dagMap.size(); i++)
		{
			deletedBlendShapes[m_sharedGeom.dagMap[i].pBlendShape] = true;
		}
		
		std::map<BlendShape*, bool>::iterator it  = deletedBlendShapes.begin();
		for(it = deletedBlendShapes.begin(); it!= deletedBlendShapes.end(); ++it)
		{
			if((*it).first)
			{
				delete (*it).first;
			}
		}

		m_sharedGeom.vertices.clear();
		m_sharedGeom.dagMap.clear();
		m_vertexClips.clear();
		m_BSClips.clear();
		m_uvsets.clear();
		m_submeshes.clear();
		if (m_pSkeleton)
			delete m_pSkeleton;
		m_pSkeleton = NULL;
	}

	// get pointer to linked skeleton
	Skeleton* Mesh::getSkeleton()
	{
		return m_pSkeleton;
	}

	/*******************************************************************************
	 *                    Load mesh data from a node                          *
	 *******************************************************************************/
	bool Mesh::load(IGameNode* pGameNode,IGameObject* pGameObject,IGameMesh* pGameMesh,ParamList &params)
	{
		bool stat;
		// Set mesh name
		m_name = pGameNode->GetName();

		// Initialise temporary variables
		newvertices.clear();
		newweights.clear();
		newjointIds.clear();

		newuvsets.clear();
		newpoints.clear();
		newnormals.clear();
		params.currentRootJoints.clear();
		opposite = false;
		shaders.clear();
		shaderPolygonMapping.clear();
		polygonSets.clear();
		pSkinCluster = NULL;
		pBlendShape = NULL;

		// Get mesh uvsets
		stat = getUVSets(pGameMesh);		
		if (stat != true)
		{
			OrkigeMaxExporterLog("Error retrieving uvsets for current mesh\n");
		}
		// Get skin and blendShape modifiers
		stat = getModifiers(pGameNode,pGameObject,params);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error retrieving skin cluster linked to current mesh\n");
		}
		// Get connected shaders
		stat = getShaders(pGameMesh);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error getting shaders connected to current mesh\n");
		}
		// Get vertex data
		stat = getVertices(pGameMesh,params);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error retrieving vertex data for current mesh\n");
		}
		// Get vertex bone weights
		if (pSkinCluster)
		{
			getVertexBoneWeights(pGameMesh, params);
			if (stat != true)
			{
				OrkigeMaxExporterLog( "Error retrieving veretex bone assignements for current mesh\n");
			}
		}
		// Get faces data
		stat = getFaces(pGameMesh,params);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error retrieving faces data for current mesh\n");
		}
		// Set default values for data (rigidly skinned meshes, bone weights, position indices)
		stat = cleanupData(pGameNode, pGameMesh, params);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error cleaning up data for current mesh\n");
		}
		// Build shared geometry
		if (params.useSharedGeom)
		{
			stat = buildSharedGeometry(pGameNode,params);
			if (stat != true)
			{
				OrkigeMaxExporterLog( "Error building shared geometry for current mesh\n");				
			}
		}
		// Create submeshes (a different submesh for every different shader linked to the mesh)
		stat = createSubmeshes(pGameNode, pGameMesh,params);
		if (stat != true)
		{
			OrkigeMaxExporterLog( "Error creating submeshes for current mesh\n");	
		}
		// Free up memory
		newvertices.clear();
		newweights.clear();
		newjointIds.clear();
		newpoints.clear();
		newnormals.clear();
		newuvsets.clear();
		shaders.clear();
		shaderPolygonMapping.clear();
		polygonSets.clear();
		pSkinCluster = NULL;
		pBlendShape = NULL;
		return true;
	}

	bool Mesh::getModifiers( IGameNode* pGameNode, IGameObject* pGameObject, ParamList &params )
	{
		if( pGameObject )
		{
			int numModifiers = pGameObject->GetNumModifiers();
			for( int i = 0; i < numModifiers; ++i )
			{
				IGameModifier* pGameModifier = pGameObject->GetIGameModifier(i);
				if( pGameModifier )
				{
					if( pGameModifier->IsSkin() )
					{
						IGameSkin* pGameSkin = (IGameSkin*)pGameModifier;
						if( pGameSkin )
						{
							if (params.exportVBA || params.exportSkeleton)
							{
								// Save this for future use.
								pSkinCluster = pGameSkin;
								if (pSkinCluster)
								{
									// create the skeleton if it hasn't been created.
									OrkigeMaxExporterLog( "Creating skeleton ...\n");
									if (!m_pSkeleton)
										m_pSkeleton = new Skeleton();
								}
							}
						}
					}
					else if( pGameModifier->IsMorpher() )
					{
						IGameMorpher* pGameMorpher = (IGameMorpher*)pGameModifier;
						if( pGameMorpher )
						{
							MorphR3* pMorphR3 = NULL;
							Modifier* pModifier = pGameMorpher->GetMaxModifier();
							// Sanity check.
							if( MR3_CLASS_ID == pModifier->ClassID() )
							{
								pMorphR3 = (MorphR3*)pModifier;
								if(!pBlendShape)
									pBlendShape = new BlendShape(pMorphR3, pGameNode);
							}
						}
					}			
				}
			}
		}
		return true;
	}
	/*******************************************************************************
	 *                    Load mesh animations from Max                           *
	 *******************************************************************************/
	// Load vertex animations
	bool Mesh::loadAnims(ParamList& params)
	{
		bool stat;
		int i;
		// save current time for later restore
		TimeValue curTime = GetCOREInterface()->GetTime();
		OrkigeMaxExporterLog( "Loading vertex animations...\n");
		
		// clear animations data
		m_vertexClips.clear();
		// load the requested clips
		for (i=0; i<params.vertClipList.size(); i++)
		{
			OrkigeMaxExporterLog( "Loading clip %s.\n", params.vertClipList[i].name.c_str() );
			
			stat = loadClip(params.vertClipList[i].name,params.vertClipList[i].start,
				params.vertClipList[i].stop,params.vertClipList[i].rate,params);
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

	// Load blend shape deformers
	bool Mesh::loadBlendShapes(ParamList &params)
	{
		int i;
		OrkigeMaxExporterLog( "Loading blend shape poses...\n");
		
		// Set envelopes of all blend shape deformers to 0
		if (params.useSharedGeom)
		{
			for  (i=0; i<m_sharedGeom.dagMap.size(); i++)
			{
				dagInfo di = m_sharedGeom.dagMap[i];
				if (di.pBlendShape)
					di.pBlendShape->setEnvelope(0);
			}
		}
		else
		{
			for (i=0; i<m_submeshes.size(); i++)
			{
				Submesh* pSubmesh = m_submeshes[i];
				if (pSubmesh->m_pBlendShape)
					pSubmesh->m_pBlendShape->setEnvelope(0);
			}
		}
		// Get the blend shape poses
		if (params.useSharedGeom)
		{
			for  (i=0; i<m_sharedGeom.dagMap.size(); i++)
			{
				dagInfo di = m_sharedGeom.dagMap[i];
				if (di.pBlendShape)
					di.pBlendShape->loadPoses(params,m_sharedGeom.vertices,di.numVertices,di.offset);
			}
		}
		else
		{
			for (i=0; i<m_submeshes.size(); i++)
			{
				Submesh* pSubmesh = m_submeshes[i];
				if (pSubmesh->m_pBlendShape)
					pSubmesh->m_pBlendShape->loadPoses(params,pSubmesh->m_vertices,
						pSubmesh->m_vertices.size(),0,i+1);
			}
		}
		// Restore blend shape deformers original envelopes
		if (params.useSharedGeom)
		{
			for  (i=0; i<m_sharedGeom.dagMap.size(); i++)
			{
				dagInfo di = m_sharedGeom.dagMap[i];
				if (di.pBlendShape)
					di.pBlendShape->restoreEnvelope();
			}
		}
		else
		{
			for (i=0; i<m_submeshes.size(); i++)
			{
				Submesh* pSubmesh = m_submeshes[i];
				if (pSubmesh->m_pBlendShape)
					pSubmesh->m_pBlendShape->restoreEnvelope();
			}
		}
		// Read blend shape animations
		if (params.exportBSAnims)
		{
			loadBlendShapeAnimations(params);
		}
		return true;
	}

	// Load blend shape animations
	bool Mesh::loadBlendShapeAnimations(ParamList& params)
	{
		int i,j,k;
		OrkigeMaxExporterLog( "Loading blend shape animations...\n");
		
		// Read the list of blend shape clips to export
		for (i=0; i<params.BSClipList.size(); i++)
		{
			int startPoseId = 0;
			clipInfo ci = params.BSClipList[i];
			// Create a new animation for every clip
			Animation a;
			a.m_name = ci.name;
			a.m_length = ci.stop - ci.start;
			a.m_tracks.clear();
			OrkigeMaxExporterLog( "clip %s\n", ci.name.c_str());
			// Read animation tracks from the blend shape deformer
			if (params.useSharedGeom)
			{
				// Create a track for each blend shape
				std::vector<Track> tracks;
				for  (j=0; j<m_sharedGeom.dagMap.size(); j++)
				{
					dagInfo di = m_sharedGeom.dagMap[j];
					if (di.pBlendShape)
					{
						Track t = di.pBlendShape->loadTrack(ci.start,ci.stop,ci.rate,params,0,startPoseId);
						tracks.push_back(t);
						startPoseId += di.pBlendShape->getPoseGroups().find(0)->second.poses.size();
					}
				}
				// Merge the tracks into a single track (shared geometry must have a single animation track)
				if (tracks.size() > 0)
				{
					Track newTrack;
					// Merge keyframes at the same time position from all tracks
					for (j=0; j<tracks[0].m_vertexKeyframes.size(); j++)
					{
						// Create a new keyframe
						vertexKeyframe newKeyframe;
						newKeyframe.time = tracks[0].m_vertexKeyframes[j].time;
						// Get keyframe at current position from all tracks
						for (k=0; k<tracks.size(); k++)
						{
							vertexKeyframe* pSrcKeyframe = &tracks[k].m_vertexKeyframes[j];
							int pri;
							// Add pose references from this keyframe to the new keyframe for the joined track
							for (pri=0; pri<pSrcKeyframe->poserefs.size(); pri++)
							{
								// Create a new pose reference
								vertexPoseRef poseref;
								// Copy pose reference settings from source keyframe
								poseref.poseIndex = pSrcKeyframe->poserefs[pri].poseIndex;
								poseref.poseWeight = pSrcKeyframe->poserefs[pri].poseWeight;
								// Add the new pose reference to the new keyframe
								newKeyframe.poserefs.push_back(poseref);
							}
						}
						// Add the keyframe to the new joined track
						newTrack.m_vertexKeyframes.push_back(newKeyframe);
					}
					// Add the joined track to current animation clip
					a.addTrack(newTrack);
					OrkigeMaxExporterLog( "num keyframes: %d.\n", a.m_tracks[0].m_vertexKeyframes.size());
				}
			}
			else
			{
				// Create a track for each submesh
				std::vector<Track> tracks;
				for (j=0; j<m_submeshes.size(); j++)
				{
					Submesh* pSubmesh = m_submeshes[j];
					if (pSubmesh->m_pBlendShape)
					{
						int startPoseId = 0;
						Track t = pSubmesh->m_pBlendShape->loadTrack(ci.start,ci.stop,ci.rate,params,j+1,startPoseId);
						a.addTrack(t);
					}
				}
			}
			OrkigeMaxExporterLog( "length: %f.\n", a.m_length);
			
			m_BSClips.push_back(a);
		}
		return true;
	}
/******************** Methods to parse geometry data from Max ************************/
	// Get uvsets info from the mesh
	bool Mesh::getUVSets(IGameMesh* pGameMesh)
	{
		Tab<int> mapChannels = pGameMesh->GetActiveMapChannelNum();
		for( int i = 0; i < mapChannels.Count(); ++i)
		{
			newuvsets.push_back(mapChannels[i]);
			OrkigeMaxExporterLog( "Adding UV mapChannel index %i with value %i and %i verts.\n", i, mapChannels[i], pGameMesh->GetNumberOfMapVerts(mapChannels[i]));
		}
		for( int i = m_uvsets.size(); i < mapChannels.Count(); ++i)
		{
			uvset uv;
			uv.size = 2;
			m_uvsets.push_back(uv);
		}
		return true;
	}

	// Get connected shaders
	bool Mesh::getShaders(IGameMesh* pGameMesh)
	{
		Tab<int> materialIDs;
		materialIDs = pGameMesh->GetActiveMatIDs();
		polygonSets.clear();
		polygonSets.resize(materialIDs.Count());

		std::map<int, int> matIDMap;
		for(int i =0; i < materialIDs.Count(); ++i)
		{
			matIDMap[materialIDs[i]] = i;
		}
		for(int i = 0; i < pGameMesh->GetNumberOfFaces(); ++i)
		{
			FaceEx *faceex = pGameMesh->GetFace(i);
			if(faceex)
			{
				shaderPolygonMapping.push_back(matIDMap[faceex->matID]);		
			}
			else
			{
				shaderPolygonMapping.push_back(-1);
			}
		}
		return true;
	}


	// Get vertex data
	bool Mesh::getVertices(IGameMesh* pGameMesh, OrkigeMaxExporter::ParamList &params)
	{
		int i;
		int numVertices = pGameMesh->GetNumberOfVerts();
		// prepare vertex table
		newvertices.resize(numVertices);
		newweights.resize(numVertices);
		newjointIds.resize(numVertices);
		
		OrkigeMaxExporterLog( "Num verticies: %i\n", numVertices);
		for (i=0; i<numVertices; i++)
		{
			newvertices[i].pointIdx = -1;
			newvertices[i].normalIdx = -1;
			newvertices[i].next = -2;
		}
		//get vertex positions from mesh
		newpoints.resize(numVertices);
		bool bObjectSpace = !(params.exportWorldCoords || (pSkinCluster && params.exportSkeleton));
		for (i=0; i<numVertices; i++)
		{
			newpoints[i] = pGameMesh->GetVertex(i, bObjectSpace);
		}
		bObjectSpace = !params.exportWorldCoords;
		int numNormals = pGameMesh->GetNumberOfNormals();

		newnormals.resize(numNormals);
		for (i=0; i<numNormals; i++)
		{
			newnormals[i] = pGameMesh->GetNormal(i, bObjectSpace);
		}
		// TODO - is there a Max equivalent to this?
		//check the "opposite" attribute to see if we have to flip normals
		//mesh.findPlug("opposite",true).getValue(opposite);
		return true;
	}

	// Get vertex bone assignements
	bool Mesh::getVertexBoneWeights(IGameMesh* pGameMesh, OrkigeMaxExporter::ParamList &params)
	{
		if (!pSkinCluster)
		{
			return false;
		}
		IGameSkin* pGameSkin = pSkinCluster;
		OrkigeMaxExporterLog( "Get vbas\n");
		
		std::vector<IGameNode*> rootbones;
		for(int i = 0; i < pGameSkin->GetTotalBoneCount(); ++i )
		{
			// pass true to only get bones used by vertices
			IGameNode* rootbone = pGameSkin->GetIGameBone (i, true);
			while(rootbone->GetNodeParent())
			{
				rootbone = rootbone->GetNodeParent();
			}
			bool bNewRootBone = true;
			for(int j = 0; j < rootbones.size(); ++j)
			{
				if(rootbones[j] == rootbone)
				{
					// this bone isn't a root.
					bNewRootBone = false;
				}
			}
			if(bNewRootBone)
			{
				rootbones.push_back(rootbone);
			}
		}
		if( rootbones.size() == 0 && params.exportSkeleton )
		{
			// FaceFX requires a skeleton, so add a root bone so one will be created.	
			IGameNode* pRandomRootNode = GetIGameInterface()->GetTopLevelNode(0);
			if( pRandomRootNode )
			{
				rootbones.push_back(pRandomRootNode);
			}
		}
		for(int i = 0; i <rootbones.size(); ++i )
		{
			OrkigeMaxExporterLog( "Exporting root bone: %s\n", rootbones[i]->GetName());
			m_pSkeleton->loadJoint(rootbones[i],params);
		}

		int numSkinnedVertices = pSkinCluster->GetNumOfSkinnedVerts();
		OrkigeMaxExporterLog( "Num. Skinned Vertices: %d\n", numSkinnedVertices);
		for( int i = 0; i < numSkinnedVertices; ++i )
		{
			int type = pGameSkin->GetVertexType(i);
			// Rigid vertices.
			if( type == IGameSkin::IGAME_RIGID )
			{
				IGameNode* pBoneGameNode = pGameSkin->GetIGameBone(i, 0);
				if( pBoneGameNode )
				{
					int boneIndex = m_pSkeleton->getJointIndex(pBoneGameNode);
					if(boneIndex >= 0)
					{
						newweights[i].push_back(1.0f);
						newjointIds[i].push_back(boneIndex);
					}
				}
			}
			// Blended vertices.
			else
			{
				int numWeights = pGameSkin->GetNumberOfBones(i);
				for( int j = 0; j < numWeights; ++j )
				{
					IGameNode* pBoneGameNode = pGameSkin->GetIGameBone(i, j);
					if( pBoneGameNode )
					{
						int boneIndex = m_pSkeleton->getJointIndex(pBoneGameNode);
						if(boneIndex >= 0)
						{
							newweights[i].push_back(pGameSkin->GetWeight(i, j));
							newjointIds[i].push_back(boneIndex);
						}
					}
				}
			}
		}
		return true;
	}

	// Get faces data
	bool Mesh::getFaces(IGameMesh* pGameMesh, ParamList &params)
	{
		int i,j,k;
		int numfaces = pGameMesh->GetNumberOfFaces();
		// create an iterator to go through mesh polygons
		if (numfaces > 0)
		{
			OrkigeMaxExporterLog( "Iterate over mesh faces\n");
			OrkigeMaxExporterLog( "num polygons = %d\n", numfaces);

			bool different;
			int vtxIdx, nrmIdx;
			FaceEx* faceex; 
			Tab<int> mapChannels = pGameMesh->GetActiveMapChannelNum();
			int fCount = pGameMesh->GetNumberOfFaces(); 
			for( int iFCount = 0; iFCount < fCount; ++iFCount)
			{
				faceex = pGameMesh->GetFace(iFCount);
				if(!faceex)
				{
					OrkigeMaxExporterLog( "Warning! Null Face returned from IGameMesh::GetFace at index %i!", iFCount); 
				}
				else
				{
					int idx;
					face newFace;
					for (i=0; i<3; i++)
					{
						different = true;
						vtxIdx = faceex->vert[i];
						nrmIdx = faceex->norm[i];
						assert(vtxIdx >= 0 && vtxIdx < newvertices.size());
						assert(nrmIdx >= 0 && nrmIdx < newnormals.size());
						Point3 color = pGameMesh->GetColorVertex(vtxIdx);
						float alpha = pGameMesh->GetAlphaVertex(vtxIdx);
						if (newvertices[vtxIdx].next == -2)	// first time we encounter a vertex in this position
						{
							// save vertex position
							newvertices[vtxIdx].pointIdx = vtxIdx;
							// save vertex normal
							newvertices[vtxIdx].normalIdx = nrmIdx;
							// save vertex colour
							newvertices[vtxIdx].r = color.x;
							newvertices[vtxIdx].g = color.y;
							newvertices[vtxIdx].b = color.z;
							newvertices[vtxIdx].a = alpha;
							// save vertex texture coordinates
							newvertices[vtxIdx].u.resize(newuvsets.size());
							newvertices[vtxIdx].v.resize(newuvsets.size());
			
							// save vbas
							newvertices[vtxIdx].vba.resize(newweights[vtxIdx].size());
							for (j=0; j<newweights[vtxIdx].size(); j++)
							{
								newvertices[vtxIdx].vba[j] = (newweights[vtxIdx])[j];
							}
							// save joint ids
							newvertices[vtxIdx].jointIds.resize(newjointIds[vtxIdx].size());
							for (j=0; j<newjointIds[vtxIdx].size(); j++)
							{
								newvertices[vtxIdx].jointIds[j] = (newjointIds[vtxIdx])[j];
							}

							// save uv sets data
							for (j=0; j<newuvsets.size(); j++)
							{
								newvertices[vtxIdx].u[j] = 0;
								newvertices[vtxIdx].v[j] = 0;
								if(j < mapChannels.Count())
								{
									DWORD index[3];				
									Point3 uv;
									uv.x = 0;
									uv.y = 0;
									if(pGameMesh->GetMapFaceIndex(mapChannels[j], iFCount, index))
									{
										uv = pGameMesh->GetMapVertex(mapChannels[j], index[i]);						
									}
									else
									{
										uv = pGameMesh->GetMapVertex(mapChannels[j], faceex->vert[i]);	
									}
									uv.y = 1.0f - uv.y;
									newvertices[vtxIdx].u[j] = uv.x;
									newvertices[vtxIdx].v[j] = uv.y;
								}
							}
							// save vertex index in face info
							newFace.v[i] = m_sharedGeom.vertices.size() + vtxIdx;
							// update value of index to next vertex info (-1 means nothing next)
							newvertices[vtxIdx].next = -1;
						}
						else	// already found at least 1 vertex in this position
						{
							// check if a vertex with same attributes has been saved already
							for (k=vtxIdx; k!=-1 && different; k=newvertices[k].next)
							{
								different = false;

								if (params.exportVertNorm)
								{
									Point3 n1 = newnormals[newvertices[k].normalIdx];
									Point3 n2 = newnormals[nrmIdx];
									if (n1.x!=n2.x || n1.y!=n2.y || n1.z!=n2.z)
									{
										different = true;
									}
								}
								if ((params.exportVertCol) &&
									(newvertices[k].r!=color.x || newvertices[k].g!=color.y || newvertices[k].b!= color.z || newvertices[k].a!=alpha))
								{
									different = true;
								}

								if (params.exportTexCoord)
								{
									for (j=0; j<newuvsets.size(); j++)
									{
										Point3 uv;
										uv.x = 0;
										uv.y = 0;
										if(j < mapChannels.Count())
										{
											DWORD indices[3];	
											if(pGameMesh->GetMapFaceIndex(mapChannels[j], iFCount, indices))
											{
												uv = pGameMesh->GetMapVertex(mapChannels[j], indices[i]);
											}
											else
											{
												uv = pGameMesh->GetMapVertex(mapChannels[j], faceex->vert[i]);
											}
											uv.y = 1.0f - uv.y;
										}

										if (newvertices[k].u[j]!=uv.x || newvertices[k].v[j]!=uv.y)
										{
											//OrkigeMaxExporterLog( "Different UV.  j: %d (%f,%f) vs. (%f,%f)\n", j, newvertices[k].u[j], newvertices[k].v[j], uv.x, uv.y);
											different = true;
										}
									}
								}
								idx = k;
							}
							// if no identical vertex has been saved, then save the vertex info
							if (different)
							{
								vertexInfo vtx;
								// save vertex position
								vtx.pointIdx = vtxIdx;
								// save vertex normal
								vtx.normalIdx = nrmIdx;
								// save vertex colour
								vtx.r = color.x;
								vtx.g = color.y;
								vtx.b = color.z;
								vtx.a = alpha;
								// save vertex vba
								vtx.vba.resize(newweights[vtxIdx].size());
								for (j=0; j<newweights[vtxIdx].size(); j++)
								{
									vtx.vba[j] = (newweights[vtxIdx])[j];
								}
								// save joint ids
								vtx.jointIds.resize(newjointIds[vtxIdx].size());
								for (j=0; j<newjointIds[vtxIdx].size(); j++)
								{
									vtx.jointIds[j] = (newjointIds[vtxIdx])[j];
								}
								// save vertex texture coordinates
								vtx.u.resize(newuvsets.size());
								vtx.v.resize(newuvsets.size());
								for (j=0; j<newuvsets.size(); j++)
								{
									// Setup default
									vtx.u[j] = 0;
									vtx.v[j] = 0;
									if(j < mapChannels.Count())
									{
										DWORD index[3];
										Point3 uv;
										if(pGameMesh->GetMapFaceIndex(mapChannels[j], iFCount, index))
										{
											uv = pGameMesh->GetMapVertex(mapChannels[j], index[i]);
										}
										else
										{
											uv = pGameMesh->GetMapVertex(mapChannels[j], faceex->vert[i]);
										}
										uv.y = 1.0f - uv.y;
										vtx.u[j] = uv.x;
										vtx.v[j] = uv.y;		
									}
								}
								vtx.next = -1;
								newvertices.push_back(vtx);
								// save vertex index in face info
								newFace.v[i] = m_sharedGeom.vertices.size() + newvertices.size()-1;
								newvertices[idx].next = newvertices.size()-1;
							} 
							else //	not different
							{
								newFace.v[i] = m_sharedGeom.vertices.size() + idx;
							}
						}
					}
					// add face info to the array corresponding to the submesh it belongs
					// skip faces with no shaders assigned
					assert(iFCount < shaderPolygonMapping.size());
					if (shaderPolygonMapping[iFCount] >= 0)
					{
						assert(shaderPolygonMapping[iFCount] < polygonSets.size());
						polygonSets[shaderPolygonMapping[iFCount]].push_back(newFace);
					}
				} // end iteration of triangles
			}
		}
		OrkigeMaxExporterLog( "done reading mesh triangles\n");
		return true;
	}

	// Set default values for data (rigidly skinned meshes, bone weights, position indices)
	bool Mesh::cleanupData(IGameNode* pGameNode, IGameMesh* pGameMesh, ParamList& params)
	{
		// Make sure there are no invalid vertex or normal indexes.
		assert(newpoints.size() != 0);
		assert(newnormals.size() !=0);
		for(int i = 0; i < newvertices.size(); i++)
		{
			if(newvertices[i].pointIdx < 0)
			{
				newvertices[i].pointIdx = 0;
			}
			if(newvertices[i].normalIdx < 0)
			{
				newvertices[i].normalIdx = 0;
			}
		}
		// Rigidly skin meshes
		int defaultJointIndex = -1;
		if(m_pSkeleton)
		{
			std::vector<joint> joints = m_pSkeleton->getJoints();

			defaultJointIndex =m_pSkeleton->getJointIndex(pGameNode);
			IGameNode* parent = pGameNode->GetNodeParent();
			while(defaultJointIndex == -1 && parent != NULL)
			{
				defaultJointIndex = m_pSkeleton->getJointIndex(parent);
				parent = parent->GetNodeParent();
			}
			if(defaultJointIndex != -1 )
			{
				OrkigeMaxExporterLog("Rigidly skinning mesh %s to joint %s.\n", pGameNode->GetName(), joints[defaultJointIndex].name.c_str());
			}
			else 
			{
				// Max allows verts not weighted to any bones, Ogre does not.
				// Weight unskinned verticies to the root bone. 
				if(joints.size() > 0)
				{
					defaultJointIndex = 0;
				}
			}

			// Perform cleanup to prevent OGRE from crashing, the exporter from crashing
			// and to prevent artists from having to set things up a specific way.
			for(int i = 0; i < newvertices.size(); i++)
			{
				if(defaultJointIndex != -1)
				{
					// Actually do the rigid skinning.
					if(newvertices[i].jointIds.size() == 0)
					{
						newvertices[i].jointIds.push_back(defaultJointIndex);				
					}
					if(newvertices[i].vba.size() == 0)
					{
						newvertices[i].vba.push_back(1.0f);
					}
				}
			}
		}
		return true;
	}
	// Build shared geometry
	bool Mesh::buildSharedGeometry(IGameNode* pGameNode,ParamList& params)
	{
		int i,j,k;
		OrkigeMaxExporterLog( "Create list of shared vertices\n");
		
		// save a new entry in the shared geometry map: we associate the index of the first 
		// vertex we're loading with the dag path from which it has been read
		dagInfo di;
		di.offset = m_sharedGeom.vertices.size();
		di.pGameNode = pGameNode;
		di.pBlendShape = pBlendShape;
		// load shared vertices
		for (i=0; i<newvertices.size(); i++)
		{
			vertex v;
			vertexInfo vInfo = newvertices[i];
			assert(vInfo.pointIdx >= 0 &&  vInfo.pointIdx < newpoints.size());
			assert(vInfo.normalIdx >= 0 &&  vInfo.normalIdx < newnormals.size());
			// save vertex coordinates (rescale to desired length unit)
			Point3 point = newpoints[vInfo.pointIdx] * params.lum;
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
			Point3 normal = newnormals[vInfo.normalIdx];
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
				assert(k < vInfo.jointIds.size());
				newVba.jointIdx = vInfo.jointIds[k];
				newVba.weight = vInfo.vba[k];
				v.vbas.push_back(newVba);
			}
			// save texture coordinates
			for (k=0; k<vInfo.u.size(); k++)
			{
				texcoord newTexCoords;
				assert(k < vInfo.v.size());
				newTexCoords.u = vInfo.u[k];
				newTexCoords.v = vInfo.v[k];
				newTexCoords.w = 0;
				v.texcoords.push_back(newTexCoords);
			}
			// save vertex index in mesh, to retrieve future positions of the same vertex
			v.index = vInfo.pointIdx;
			// add newly created vertex to vertices list
			m_sharedGeom.vertices.push_back(v);
		}
		// Make sure all vertices have the same number of texture coordinates
		for (i=0; i<m_sharedGeom.vertices.size(); i++)
		{
			vertex* pV = &m_sharedGeom.vertices[i];
			for (j=pV->texcoords.size(); j<m_uvsets.size(); j++)
			{
				texcoord newTexCoords;
				newTexCoords.u = 0;
				newTexCoords.v = 0;
				newTexCoords.w = 0;
				pV->texcoords.push_back(newTexCoords);
			}
		}
		// save number of vertices referring to this mesh dag in the dag path map
		di.numVertices = m_sharedGeom.vertices.size() - di.offset;
		m_sharedGeom.dagMap.push_back(di);
		OrkigeMaxExporterLog( "done creating vertices list\n");
		
		return true;
	}


	// Create submeshes
	bool Mesh::createSubmeshes(IGameNode* pGameNode, IGameMesh* pGameMesh,ParamList& params)
	{
		bool stat;
		Tab<int> materialIDs;
		materialIDs = pGameMesh->GetActiveMatIDs();
		for( int i = 0; i < materialIDs.Count(); ++i )
		{
	
			Tab<FaceEx*> faces;
			faces = pGameMesh->GetFacesFromMatID(materialIDs[i]);
			// check if the submesh has at least 1 triangle
			if (polygonSets[i].size() > 0 && faces.Count() > 0)
			{
				//load linked shader
				IGameMaterial *shader = pGameMesh->GetMaterialFromFace(faces[0]);
				if(!shader)
				{
					OrkigeMaxExporterLog( "Warning: Could not load material with ID %i. Pointer is NULL.\n", i);					
				}

				//create new submesh
				Submesh* pSubmesh = new Submesh();

				shaders.push_back(shader);
				stat = pSubmesh->loadMaterial(shader,newuvsets,params);
				if (stat != true)
				{
					OrkigeMaxExporterLog( "Error loading submesh linked to shader %s\n", shader->GetMaterialName());					
					return false;
				}
				//load vertex and face data
				stat = pSubmesh->load(pGameNode,polygonSets[i],newvertices,newpoints,newnormals,newuvsets,params,opposite);
				//if we're not using shared geometry, save a pointer to the blend shape deformer
				if (pBlendShape && !params.useSharedGeom)
					pSubmesh->m_pBlendShape = pBlendShape;
				//add submesh to current mesh
				m_submeshes.push_back(pSubmesh);
				//update number of triangles composing the mesh
				m_numTriangles += pSubmesh->numTriangles();
			}
		}
		return true;
	}





/******************** Methods to read vertex animations from Max ************************/
	//load a vertex animation clip
	bool Mesh::loadClip(std::string& clipName,float start,float stop,float rate,ParamList& params)
	{
		bool stat;
		std::string msg;
		std::vector<float> times;
		// calculate times from clip sample rate
		times.clear();
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
		// create a new animation
		Animation a;
		a.m_name = clipName;
		a.m_length = length;
		a.m_tracks.clear();
		// if we're using shared geometry, create a single animation track for the whole mesh
		if (params.useSharedGeom)
		{
			// load the animation track
			stat = loadMeshTrack(a,times,params);
			if (stat != true)
			{
				OrkigeMaxExporterLog( "Error loading mesh vertex animation\n");
			}
		}
		// else creae a different animation track for each submesh
		else
		{
			// load all tracks (one for each submesh)
			stat = loadSubmeshTracks(a,times,params);
			if (stat != true)
			{
				OrkigeMaxExporterLog( "Error loading submeshes vertex animation\n");
				return false;
			}
		}
		// add newly created animation to animations list
		m_vertexClips.push_back(a);
		// display info
		OrkigeMaxExporterLog( "length: %f.\n",a.m_length );
		OrkigeMaxExporterLog( "num keyframes: %d.\n", a.m_tracks[0].m_vertexKeyframes.size());
		// clip successfully loaded
		return true;
	}


	//load an animation track for the whole mesh (using shared geometry)
	bool Mesh::loadMeshTrack(Animation& a,std::vector<float> &times, OrkigeMaxExporter::ParamList &params)
	{
		int i;
		bool stat;
		// create a new track
		Track t;
		t.m_type = TT_MORPH;
		t.m_target = T_MESH;
		t.m_vertexKeyframes.clear();
		// get keyframes at given times
		for (i=0; i<times.size(); i++)
		{
			int closestFrame = (int)(.5f + times[i]* GetFrameRate());
			//set time to wanted sample time
			GetCOREInterface()->SetTime(closestFrame * GetTicksPerFrame());
			//load a keyframe for the mesh at current time
			stat = loadKeyframe(t,times[i]-times[0],params);
			if (stat != true)
			{
				OrkigeMaxExporterLog( "Error reading animation keyframe at time: %f.\n", times[i] );
			}
		}
		// add track to given animation
		a.addTrack(t);
		// track sucessfully loaded
		return true;
	}


	//load all submesh animation tracks (one for each submesh)
	bool Mesh::loadSubmeshTracks(Animation& a,std::vector<float> &times, OrkigeMaxExporter::ParamList &params)
	{
		int i,j;
		bool stat;
		// create a new track for each submesh
		std::vector<Track> tracks;
		for (i=0; i<m_submeshes.size(); i++)
		{
			Track t;
			t.m_type = TT_MORPH;
			t.m_target = T_SUBMESH;
			t.m_index = i;
			t.m_vertexKeyframes.clear();
			tracks.push_back(t);
		}
		// get keyframes at given times
		for (i=0; i<times.size(); i++)
		{
			int closestFrame = (int)(.5f + times[i]* GetFrameRate());
			//set time to wanted sample time
			GetCOREInterface()->SetTime(closestFrame * GetTicksPerFrame());
			//load a keyframe for each submesh at current time
			for (j=0; j<m_submeshes.size(); j++)
			{
				stat = m_submeshes[j]->loadKeyframe(tracks[j],times[i]-times[0],params);
				if (stat != true)
				{
					OrkigeMaxExporterLog( "Error reading animation keyframe at time: %f for submesh: %d.\n", times[i],  j);
				}
			}
		}
		// add tracks to given animation
		for (i=0; i< tracks.size(); i++)
			a.addTrack(tracks[i]);
		// track sucessfully loaded
		return true;
	}


	// Load a keyframe for the whole mesh
	bool Mesh::loadKeyframe(Track& t,float time,ParamList& params)
	{
		int i,j;
		// create a new keyframe
		vertexKeyframe k;
		// set keyframe time
		k.time = time;
		for (i=0; i<m_sharedGeom.dagMap.size(); i++)
		{
			// get the mesh Fn
			dagInfo di = m_sharedGeom.dagMap[i];
			IGameMesh* pGameMesh = (IGameMesh*)di.pGameNode->GetIGameObject();
			if(!pGameMesh)
				return false;
			if(!pGameMesh->InitializeData() )
				return false;
			std::string name = di.pGameNode->GetName();
			int meshVertNum = pGameMesh->GetNumberOfVerts();
			bool bIsObjectSpace = true;
			if (params.exportWorldCoords)
				bIsObjectSpace = false;
			// calculate vertex offsets
			for (j=0; j<di.numVertices; j++)
			{
				vertexPosition pos;
				vertex v = m_sharedGeom.vertices[di.offset+j];
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
			di.pGameNode->ReleaseIGameObject();
		}
		// add keyframe to given track
		t.addVertexKeyframe(k);
		// keyframe successfully loaded
		return true;
	}

/*********************************** Export mesh data **************************************/
	// Write to a OGRE binary mesh
	bool Mesh::writeOgreBinary(ParamList &params)
	{
		int i;
		// If no mesh have been exported, skip mesh creation
		if (m_submeshes.size() <= 0)
		{
			OrkigeMaxExporterLog( "Warning: No meshes selected for export\n");
			return false;
		}
		// Construct mesh
		Ogre::MeshPtr pMesh = Ogre::MeshManager::getSingleton().createManual(m_name.c_str(), 
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		// Write shared geometry data
		if (params.useSharedGeom)
		{
			createOgreSharedGeometry(pMesh,params);
		}
		// Write submeshes data
		for (i=0; i<m_submeshes.size(); i++)
		{
			m_submeshes[i]->createOgreSubmesh(pMesh,params);
		}
		// Set skeleton link (if present)
		if (m_pSkeleton && params.exportSkeleton)
		{
			std::string filename = StripToTopParent(params.skeletonFilename);
			pMesh->setSkeletonName(filename.c_str());
		}
		// Write poses
		if (params.exportBlendShapes)
		{
			createOgrePoses(pMesh,params);
		}
		// Write vertex animations
		if (params.exportVertAnims)
		{
			createOgreVertexAnimations(pMesh,params);
		}
		// Write pose animations
		if (params.exportBSAnims)
		{
			createOgrePoseAnimations(pMesh,params);
		}
	
		// Create a bounding box for the mesh
		Ogre::AxisAlignedBox bbox = pMesh->getBounds();
		for(i=0; i<m_submeshes.size(); i++)
		{
			Point3 min1 = m_submeshes[i]->m_boundingBox.Min();
			Point3 max1 = m_submeshes[i]->m_boundingBox.Max();
			Ogre::Vector3 min2(min1.x,min1.y,min1.z);
			Ogre::Vector3 max2(max1.x,max1.y,max1.z);
			Ogre::AxisAlignedBox newbbox;
			newbbox.setExtents(min2,max2);
			bbox.merge(newbbox);
		}
		// Define mesh bounds
		pMesh->_setBounds(bbox,false);
		// Build edges list
		if (params.buildEdges)
		{
			pMesh->buildEdgeList();
		}
		// Build tangents
		if (params.buildTangents)
		{
			Ogre::VertexElementSemantic targetSemantic = params.tangentSemantic == TS_TANGENT ? 
				Ogre::VES_TANGENT : Ogre::VES_TEXTURE_COORDINATES;
			bool canBuild = true;
			unsigned short srcTex, destTex;
			try {
				canBuild = !pMesh->suggestTangentVectorBuildParams(targetSemantic, srcTex, destTex);
			} catch(Ogre::Exception e) {
				canBuild = false;
			}
			if (canBuild)
				pMesh->buildTangentVectors(targetSemantic, srcTex, destTex, 
				params.tangentsSplitMirrored, params.tangentsSplitRotated, params.tangentsUseParity);
		}
		// Export the binary mesh
		Ogre::MeshSerializer serializer;
		serializer.exportMesh(pMesh.getPointer(),params.meshFilename.c_str());
		pMesh.setNull();
		return true;
	}

	// Create shared geometry data for an Ogre mesh
	bool Mesh::createOgreSharedGeometry(Ogre::MeshPtr pMesh,ParamList& params)
	{
		int i,j;
		bool stat;
		pMesh->sharedVertexData = new Ogre::VertexData();
		pMesh->sharedVertexData->vertexCount = m_sharedGeom.vertices.size();
		// Define a new vertex declaration
		Ogre::VertexDeclaration* pDecl = new Ogre::VertexDeclaration();
		pMesh->sharedVertexData->vertexDeclaration = pDecl;
		unsigned buf = 0;
		size_t offset = 0;
		// Add vertex position
		pDecl->addElement(buf,offset,Ogre::VET_FLOAT3,Ogre::VES_POSITION);
		offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
		// Add vertex normal
		if (params.exportVertNorm)
		{
			pDecl->addElement(buf, offset, Ogre::VET_FLOAT3, Ogre::VES_NORMAL);
			offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
		}
		// Add vertex colour
		if (params.exportVertCol)
		{
			pDecl->addElement(buf, offset, Ogre::VET_COLOUR, Ogre::VES_DIFFUSE);
			offset += Ogre::VertexElement::getTypeSize(Ogre::VET_COLOUR);
		}
		// Add texture coordinates
		for (i=0; i<m_sharedGeom.vertices[0].texcoords.size(); i++)
		{
			Ogre::VertexElementType uvType = Ogre::VertexElement::multiplyTypeCount(Ogre::VET_FLOAT1, 2);
			pDecl->addElement(buf, offset, uvType, Ogre::VES_TEXTURE_COORDINATES, i);
			offset += Ogre::VertexElement::getTypeSize(uvType);
		}
		// Get optimal vertex declaration
		Ogre::VertexDeclaration* pOptimalDecl = pDecl->getAutoOrganisedDeclaration(params.exportVBA,params.exportBlendShapes || params.exportVertAnims);
		// Create the vertex buffer using the newly created vertex declaration
		stat = createOgreVertexBuffer(pMesh,pDecl,m_sharedGeom.vertices);
		// Write vertex bone assignements list
		if (params.exportVBA)
		{
			// Create a new vertex bone assignements list
			Ogre::Mesh::VertexBoneAssignmentList vbas;
			// Scan list of shared geometry vertices
			for (i=0; i<m_sharedGeom.vertices.size(); i++)
			{
				vertex v = m_sharedGeom.vertices[i];
				// Add all bone assignements for every vertex to the bone assignements list
				for (j=0; j<v.vbas.size(); j++)
				{
					Ogre::VertexBoneAssignment vba;
					vba.vertexIndex = i;
					vba.boneIndex = v.vbas[j].jointIdx;
					vba.weight = v.vbas[j].weight;
					if (vba.weight > 0.0f)
						vbas.insert(Ogre::Mesh::VertexBoneAssignmentList::value_type(i, vba));
				}
			}
			// Rationalise the bone assignements list
			pMesh->_rationaliseBoneAssignments(pMesh->sharedVertexData->vertexCount,vbas);
			// Add bone assignements to the mesh
			for (Ogre::Mesh::VertexBoneAssignmentList::iterator bi = vbas.begin(); bi != vbas.end(); bi++)
			{
				pMesh->addBoneAssignment(bi->second);
			}
			pMesh->_compileBoneAssignments();
			pMesh->_updateCompiledBoneAssignments();
		}
		// Reorganize vertex buffers
		pMesh->sharedVertexData->reorganiseBuffers(pOptimalDecl);
		
		return true;
	}

	// Create an Ogre compatible vertex buffer
	bool Mesh::createOgreVertexBuffer(Ogre::MeshPtr pMesh,Ogre::VertexDeclaration* pDecl,const std::vector<vertex>& vertices)
	{
		Ogre::HardwareVertexBufferSharedPtr vbuf = 
			Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(pDecl->getVertexSize(0),
			pMesh->sharedVertexData->vertexCount, 
			Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY);
		pMesh->sharedVertexData->vertexBufferBinding->setBinding(0, vbuf);
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

	// Create mesh poses for an Ogre mesh
	bool Mesh::createOgrePoses(Ogre::MeshPtr pMesh,ParamList& params)
	{
		int poseCounter = 0;
		if (params.useSharedGeom)
		{
			// Create an entry in the submesh pose remapping table for the shared geometry
			submeshPoseRemapping new_sbr;
			m_poseRemapping.insert(std::pair<int,submeshPoseRemapping>(0,new_sbr));
			submeshPoseRemapping& sbr = m_poseRemapping.find(0)->second;
			// Read poses associated from all blendshapes associated to the shared geometry
			for (int i=0; i<m_sharedGeom.dagMap.size(); i++)
			{
				BlendShape* pBS = m_sharedGeom.dagMap[i].pBlendShape;
				// Check if we have a blend shape associated to this subset of the shared geometry
				if (pBS)
				{
					// Get all poses from current blend shape deformer
					poseGroup& pg = pBS->getPoseGroups().find(0)->second;
					for (int j=0; j<pg.poses.size(); j++)
					{
						// Get the pose
						pose* p = &(pg.poses[j]);
						if (p->name == "")
						{
							p->name = "pose";
							p->name += poseCounter;
						}
						// Create a new pose for the ogre mesh
						Ogre::Pose* pPose = pMesh->createPose(0,p->name.c_str());
						// Set the pose attributes
						for (int k=0; k<p->offsets.size(); k++)
						{
							Ogre::Vector3 offset(p->offsets[k].x,p->offsets[k].y,p->offsets[k].z);
							pPose->addVertex(p->offsets[k].index,offset);
						}
						// Add a pose remapping for current pose
						sbr.insert(std::pair<int,int>(poseCounter,poseCounter));
						poseCounter++;
					}
				}
			}
		}
		else
		{
			// Get poses associated to the submeshes
			for (int i=0; i<m_submeshes.size(); i++)
			{
				BlendShape* pBS = m_submeshes[i]->m_pBlendShape;
				// Check if this submesh has a blend shape deformer associated
				if (pBS)
				{
					// Create an entry in the submesh pose remapping table for this submesh
					submeshPoseRemapping new_sbr;
					m_poseRemapping.insert(std::pair<int,submeshPoseRemapping>(i+1,new_sbr));
					submeshPoseRemapping& sbr = m_poseRemapping.find(i+1)->second;
					// Get the pose group corresponding to the current submesh
					poseGroup& pg = pBS->getPoseGroups().find(i+1)->second;
					// Get all poses from current blend shape deformer and current pose group
					for (int j=0; j<pg.poses.size(); j++)
					{
						// Get the pose
						pose* p = &(pg.poses[j]);
						if (p->name == "")
						{
							p->name = "pose";
							p->name += poseCounter;
						}
						// Create a new pose for the ogre mesh
						Ogre::Pose* pPose = pMesh->createPose(p->index,p->name.c_str());
						// Set the pose attributes
						for (int k=0; k<p->offsets.size(); k++)
						{
							Ogre::Vector3 offset(p->offsets[k].x,p->offsets[k].y,p->offsets[k].z);
							pPose->addVertex(p->offsets[k].index,offset);
						}
						// Add a pose remapping for current pose
						sbr.insert(std::pair<int,int>(j,poseCounter));

						poseCounter++;
					}
				}
			}
		}
		return true;
	}
	// Create vertex animations for an Ogre mesh
	bool Mesh::createOgreVertexAnimations(Ogre::MeshPtr pMesh,ParamList& params)
	{
		// Read the list of vertex animation clips
		for (int i=0; i<m_vertexClips.size(); i++)
		{
			// Create a new animation
			Ogre::Animation* pAnimation = pMesh->createAnimation(m_vertexClips[i].m_name.c_str(),m_vertexClips[i].m_length);
			// Create all tracks for current animation
			for (int j=0; j<m_vertexClips[i].m_tracks.size(); j++)
			{
				Track* t = &(m_vertexClips[i].m_tracks[j]);
				// Create a new track
				Ogre::VertexAnimationTrack* pTrack;
				if (t->m_target == T_MESH)
					pTrack = pAnimation->createVertexTrack(0,pMesh->sharedVertexData,Ogre::VAT_MORPH);
				else
				{
					pTrack = pAnimation->createVertexTrack(t->m_index+1,pMesh->getSubMesh(t->m_index)->vertexData,
						Ogre::VAT_MORPH);
				}
				// Create keyframes for current track
				for (int k=0; k<t->m_vertexKeyframes.size(); k++)
				{
					// Create a new keyframe
					Ogre::VertexMorphKeyFrame* pKeyframe = pTrack->createVertexMorphKeyFrame(t->m_vertexKeyframes[k].time);
					// Create vertex buffer for current keyframe
					Ogre::HardwareVertexBufferSharedPtr pBuffer = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
						Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3),
						t->m_vertexKeyframes[k].positions.size(),
						Ogre::HardwareBuffer::HBU_STATIC, true);
					float* pFloat = static_cast<float*>(pBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD));
					// Fill the vertex buffer with vertex positions
					int vi;
					std::vector<vertexPosition>& positions = t->m_vertexKeyframes[k].positions;
					for (vi=0; vi<positions.size(); vi++)
					{
						*pFloat++ = static_cast<float>(positions[vi].x);
						*pFloat++ = static_cast<float>(positions[vi].y);
						*pFloat++ = static_cast<float>(positions[vi].z);
					}
					// Unlock vertex buffer
					pBuffer->unlock();
					// Set vertex buffer for current keyframe
					pKeyframe->setVertexBuffer(pBuffer);
				}
			}
		}
		return true;
	}
	// Create pose animations for an Ogre mesh
	bool Mesh::createOgrePoseAnimations(Ogre::MeshPtr pMesh,ParamList& params)
	{
		// Get all loaded blend shape clips
		for (int i=0; i<m_BSClips.size(); i++)
		{
			// Create a new animation for each clip
			Ogre::Animation* pAnimation = pMesh->createAnimation(m_BSClips[i].m_name.c_str(),m_BSClips[i].m_length);
			// Create animation tracks for this animation
			for (int j=0; j<m_BSClips[i].m_tracks.size(); j++)
			{
				Track* t = &m_BSClips[i].m_tracks[j];
				// Create a new track
				Ogre::VertexAnimationTrack* pTrack;
				if (t->m_target == T_MESH)
					pTrack = pAnimation->createVertexTrack(0,pMesh->sharedVertexData,Ogre::VAT_POSE);
				else
				{
					pTrack = pAnimation->createVertexTrack(t->m_index+1,pMesh->getSubMesh(t->m_index)->vertexData,
						Ogre::VAT_POSE);
				}
				// Create keyframes for current track
				for (int k=0; k<t->m_vertexKeyframes.size(); k++)
				{
					Ogre::VertexPoseKeyFrame* pKeyframe = pTrack->createVertexPoseKeyFrame(t->m_vertexKeyframes[k].time);
					for (int pri=0; pri<t->m_vertexKeyframes[k].poserefs.size(); pri++)
					{
						vertexPoseRef* pr = &t->m_vertexKeyframes[k].poserefs[pri];
						pKeyframe->addPoseReference(pr->poseIndex,pr->poseWeight);
					}
				}
			}
		}
		return true;
	}

}; //end of namespace
