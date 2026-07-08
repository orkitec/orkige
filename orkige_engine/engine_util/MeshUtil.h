/**************************************************************
	created:	2010/08/31 at 0:20
	filename: 	MeshUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __MeshUtil_h__31_8_2010__0_20_13__
#define __MeshUtil_h__31_8_2010__0_20_13__

#include "engine_module/EnginePrerequisites.h"
#include <OgreLodStrategy.h>

namespace Orkige
{
	//! mesh utilities
	namespace MeshUtil
	{
		//! @brief setup custom lod mesh for given mesh
		//! @param meshFileName mesh filename to whom the lod should be applied
		//! @param distance distance at wich this lod should be applied
		//! @param useLodBoundingBoxAsDefault use the boundingbox of the lod mesh as default boundingbox
		//! @param lodMeshFileNameSuffix filename suffix for custom lod mesh
		//! @param onlyApplyToMeshWithNoLod only apply lod if mesh has not already lod
		//! @return true on success false on error
		static inline bool setupCustomLodMesh(String const & meshFileName, Ogre::Real distance, bool useLodBoundingBoxAsDefault = false, String const & lodMeshFileNameSuffix = "_lod", bool onlyApplyToMeshWithNoLod = true, String const & groupName = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)
		{
			Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().load(meshFileName, groupName);
			if(mesh)
			{
				if(onlyApplyToMeshWithNoLod && mesh->getNumLodLevels() > 1)
				{
					return false;
				}

				String lodMeshFileName = meshFileName.substr(0, meshFileName.length()-5) + lodMeshFileNameSuffix + ".mesh";
				
				if(!Ogre::ResourceGroupManager::getSingleton().resourceExists(groupName, lodMeshFileName))
				{
					return false;
				}

				Ogre::MeshPtr lodMesh = Ogre::MeshManager::getSingleton().load(lodMeshFileName, groupName);
				if(lodMesh)
				{
					if(useLodBoundingBoxAsDefault)
					{
						mesh->_setBounds(lodMesh->getBounds(), false);
					}

					// OGRE 14: createManualLodLevel() is gone; register the manual
					// LOD level the way the mesh serializer does
					Ogre::MeshLodUsage lodUsage;
					lodUsage.userValue = distance;
					lodUsage.value = mesh->getLodStrategy()->transformUserValue(distance);
					lodUsage.manualName = lodMeshFileName;
					lodUsage.manualMesh = lodMesh;
					lodUsage.edgeData = NULL;
					mesh->_setLodInfo(2);
					mesh->_setLodUsage(1, lodUsage);

					return true;
				}
				else
				{
					return false;
				}
			}
			return false;
		}
		//---------------------------------------------------------
		//! Get the mesh information for the given mesh.
		static inline void getMeshInformation(const Ogre::MeshPtr mesh,
			size_t &vertex_count,
			Ogre::Vector3* &vertices,
			size_t &index_count,
			Ogre::uint32* &indices,
			const Ogre::Vector3 &position,
			const Ogre::Quaternion &orient,
			const Ogre::Vector3 &scale)
		{
			bool added_shared = false;
			size_t current_offset = 0;
			size_t shared_offset = 0;
			size_t next_offset = 0;
			size_t index_offset = 0;

			vertex_count = index_count = 0;

			// Calculate how many vertices and indices we're going to need
			for (unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh( i );

				// We only need to add the shared vertices once		
				if(submesh->useSharedVertices)
				{
					if( !added_shared )
					{
						vertex_count += mesh->sharedVertexData->vertexCount;
						added_shared = true;
					}
				}
				else
				{
					vertex_count += submesh->vertexData->vertexCount;
				}

				// Add the indices
				index_count += submesh->indexData->indexCount;
			}


			// Allocate space for the vertices and indices
			vertices = new Ogre::Vector3[vertex_count];
			indices = new Ogre::uint32[index_count];

			added_shared = false;

			// Run through the submeshes again, adding the data into the arrays
			for ( unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh(i);

				Ogre::VertexData* vertex_data = submesh->useSharedVertices ? mesh->sharedVertexData : submesh->vertexData;

				if((!submesh->useSharedVertices)||(submesh->useSharedVertices && !added_shared))
				{
					if(submesh->useSharedVertices)
					{
						added_shared = true;
						shared_offset = current_offset;
					}

					const Ogre::VertexElement* posElem =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_POSITION);

					Ogre::HardwareVertexBufferSharedPtr vbuf =
						vertex_data->vertexBufferBinding->getBuffer(posElem->getSource());

					unsigned char* vertex =
						static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					// There is _no_ baseVertexPointerToElement() which takes an Ogre::Ogre::Real or a double
					//  as second argument. So make it float, to avoid trouble when Ogre::Ogre::Real will
					//  be comiled/typedefed as double:
					//      Ogre::Ogre::Real* pOgre::Real;
					float* pReal;

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						posElem->baseVertexPointerToElement(vertex, &pReal);

						Ogre::Vector3 pt(pReal[0], pReal[1], pReal[2]);

						vertices[current_offset + j] = (orient * (pt * scale)) + position;
					}

					vbuf->unlock();
					next_offset += vertex_data->vertexCount;
				}


				Ogre::IndexData* index_data = submesh->indexData;
				size_t numTris = index_data->indexCount / 3;
				Ogre::HardwareIndexBufferSharedPtr ibuf = index_data->indexBuffer;

				bool use32bitindexes = (ibuf->getType() == Ogre::HardwareIndexBuffer::IT_32BIT);

				Ogre::uint32*  pLong = static_cast<Ogre::uint32*>(ibuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));
				unsigned short* pShort = reinterpret_cast<unsigned short*>(pLong);


				size_t offset = (submesh->useSharedVertices)? shared_offset : current_offset;

				if ( use32bitindexes )
				{
					for ( size_t k = 0; k < numTris*3; ++k)
					{
						indices[index_offset++] = pLong[k] + static_cast<Ogre::uint32>(offset);
					}
				}
				else
				{
					for ( size_t k = 0; k < numTris*3; ++k)
					{
						indices[index_offset++] = static_cast<Ogre::uint32>(pShort[k]) +
							static_cast<Ogre::uint32>(offset);
					}
				}

				ibuf->unlock();
				current_offset = next_offset;
			}
		}
		//---------------------------------------------------------
		//! Get the mesh information for the given mesh.
		static inline void getMeshInformationWithNormals(const Ogre::MeshPtr mesh,
			size_t &vertex_count,
			Ogre::Vector3* &vertices,
			Ogre::Vector3* &normals,
			Ogre::Vector3* &texcoords,
			size_t &index_count,
			Ogre::uint32* &indices,
			const Ogre::Vector3 &position,
			const Ogre::Quaternion &orient,
			const Ogre::Vector3 &scale)
		{
			bool added_shared = false;
			size_t current_offset = 0;
			size_t shared_offset = 0;
			size_t next_offset = 0;
			size_t index_offset = 0;

			vertex_count = index_count = 0;

			// Calculate how many vertices and indices we're going to need
			for (unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh( i );

				// We only need to add the shared vertices once		
				if(submesh->useSharedVertices)
				{
					if( !added_shared )
					{
						vertex_count += mesh->sharedVertexData->vertexCount;
						added_shared = true;
					}
				}
				else
				{
					vertex_count += submesh->vertexData->vertexCount;
				}

				// Add the indices
				index_count += submesh->indexData->indexCount;
			}


			// Allocate space for the vertices and indices
			vertices = new Ogre::Vector3[vertex_count];
			normals = new Ogre::Vector3[vertex_count];
			texcoords = new Ogre::Vector3[vertex_count];
			indices = new Ogre::uint32[index_count];

			added_shared = false;

			// Run through the submeshes again, adding the data into the arrays
			for ( unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh(i);

				Ogre::VertexData* vertex_data = submesh->useSharedVertices ? mesh->sharedVertexData : submesh->vertexData;

				if((!submesh->useSharedVertices)||(submesh->useSharedVertices && !added_shared))
				{
					if(submesh->useSharedVertices)
					{
						added_shared = true;
						shared_offset = current_offset;
					}

					const Ogre::VertexElement* posElem =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_POSITION);

					Ogre::HardwareVertexBufferSharedPtr vbuf =
						vertex_data->vertexBufferBinding->getBuffer(posElem->getSource());

					unsigned char* vertex =
						static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					// There is _no_ baseVertexPointerToElement() which takes an Ogre::Ogre::Real or a double
					//  as second argument. So make it float, to avoid trouble when Ogre::Ogre::Real will
					//  be comiled/typedefed as double:
					//      Ogre::Ogre::Real* pOgre::Real;
					float* pReal;

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						posElem->baseVertexPointerToElement(vertex, &pReal);

						Ogre::Vector3 pt(pReal[0], pReal[1], pReal[2]);

						vertices[current_offset + j] = (orient * (pt * scale)) + position;
					}

					vbuf->unlock();

					float* pReal2;
					// Read normal data
					const Ogre::VertexElement* normElm =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_NORMAL);

					vbuf = vertex_data->vertexBufferBinding->getBuffer(normElm->getSource());

					vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						normElm->baseVertexPointerToElement(vertex, &pReal2);
						Ogre::Vector3 pt(pReal2[0], pReal2[1], pReal2[2]);
						normals[current_offset + j] = pt;
					}

					vbuf->unlock();

					float* pReal3;
					// Read normal data
					const Ogre::VertexElement* tcElm =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_TEXTURE_COORDINATES);

					vbuf = vertex_data->vertexBufferBinding->getBuffer(tcElm->getSource());

					vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						tcElm->baseVertexPointerToElement(vertex, &pReal3);
						Ogre::Vector3 pt(pReal3[0], pReal3[1], pReal3[2]);
						texcoords[current_offset + j] = pt;
					}

					vbuf->unlock();

					next_offset += vertex_data->vertexCount;
				}


				Ogre::IndexData* index_data = submesh->indexData;
				size_t numTris = index_data->indexCount / 3;
				Ogre::HardwareIndexBufferSharedPtr ibuf = index_data->indexBuffer;

				bool use32bitindexes = (ibuf->getType() == Ogre::HardwareIndexBuffer::IT_32BIT);

				Ogre::uint32*  pLong = static_cast<Ogre::uint32*>(ibuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));
				unsigned short* pShort = reinterpret_cast<unsigned short*>(pLong);


				size_t offset = (submesh->useSharedVertices)? shared_offset : current_offset;

				if ( use32bitindexes )
				{
					for ( size_t k = 0; k < numTris*3; ++k)
					{
						indices[index_offset++] = pLong[k] + static_cast<Ogre::uint32>(offset);
					}
				}
				else
				{
					for ( size_t k = 0; k < numTris*3; ++k)
					{
						indices[index_offset++] = static_cast<Ogre::uint32>(pShort[k]) +
							static_cast<Ogre::uint32>(offset);
					}
				}

				ibuf->unlock();
				current_offset = next_offset;
			}
		}
		//---------------------------------------------------------
		//! Get the mesh information for the given mesh in current animation position.
		static inline void getMeshInformationWithAnimation(
			Ogre::Entity *entity,
			size_t &vertex_count,
			Ogre::Vector3* &vertices,
			size_t &index_count,
			Ogre::uint32* &indices,
			const Ogre::Vector3 &position,
			const Ogre::Quaternion &orient,
			const Ogre::Vector3 &scale)
		{
			bool added_shared = false;
			size_t current_offset = 0;
			size_t shared_offset = 0;
			size_t next_offset = 0;
			size_t index_offset = 0;
			vertex_count = index_count = 0;
			
			Ogre::MeshPtr mesh = entity->getMesh();
			
			
			bool useSoftwareBlendingVertices = entity->hasSkeleton();
			
			if (useSoftwareBlendingVertices)
			{
				entity->_updateAnimation();
			}
			
			// Calculate how many vertices and indices we're going to need
			for (unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh( i );
				
				// We only need to add the shared vertices once
				if(submesh->useSharedVertices)
				{
					if( !added_shared )
					{
						vertex_count += mesh->sharedVertexData->vertexCount;
						added_shared = true;
					}
				}
				else
				{
					vertex_count += submesh->vertexData->vertexCount;
				}
				
				// Add the indices
				index_count += submesh->indexData->indexCount;
			}
			
			
			// Allocate space for the vertices and indices
			vertices = new Ogre::Vector3[vertex_count];
			indices = new Ogre::uint32[index_count];
			
			added_shared = false;
			
			// Run through the submeshes again, adding the data into the arrays
			for ( unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh(i);
				
				//----------------------------------------------------------------
				// GET VERTEXDATA
				//----------------------------------------------------------------
				Ogre::VertexData* vertex_data;
				
				//When there is animation:
				if(useSoftwareBlendingVertices)
					vertex_data = submesh->useSharedVertices ? entity->_getSkelAnimVertexData() : entity->getSubEntity(i)->_getSkelAnimVertexData();
				else
					vertex_data = submesh->useSharedVertices ? mesh->sharedVertexData : submesh->vertexData;
				
				
				if((!submesh->useSharedVertices)||(submesh->useSharedVertices && !added_shared))
				{
					if(submesh->useSharedVertices)
					{
						added_shared = true;
						shared_offset = current_offset;
					}
					
					const Ogre::VertexElement* posElem =
					vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_POSITION);
					
					Ogre::HardwareVertexBufferSharedPtr vbuf =
					vertex_data->vertexBufferBinding->getBuffer(posElem->getSource());
					
					unsigned char* vertex =
					static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));
					
					// There is _no_ baseVertexPointerToElement() which takes an Ogre::Real or a double
					//  as second argument. So make it float, to avoid trouble when Ogre::Real will
					//  be comiled/typedefed as double:
					//      Ogre::Real* pReal;
					float* pReal;
					
					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						posElem->baseVertexPointerToElement(vertex, &pReal);
						
						Ogre::Vector3 pt(pReal[0], pReal[1], pReal[2]);
						
						vertices[current_offset + j] = (orient * (pt * scale)) + position;
					}
					
					vbuf->unlock();
					next_offset += vertex_data->vertexCount;
				}
				
				
				Ogre::IndexData* index_data = submesh->indexData;
				size_t numTris = index_data->indexCount / 3;
				Ogre::HardwareIndexBufferSharedPtr ibuf = index_data->indexBuffer;
				
				bool use32bitindexes = (ibuf->getType() == Ogre::HardwareIndexBuffer::IT_32BIT);
				
				void* hwBuf = ibuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY);
				
				size_t offset = (submesh->useSharedVertices)? shared_offset : current_offset;
				size_t index_start = index_data->indexStart;
				size_t last_index = numTris*3 + index_start;
				
				if (use32bitindexes)
				{
					Ogre::uint32* hwBuf32 = static_cast<Ogre::uint32*>(hwBuf);
					for (size_t k = index_start; k < last_index; ++k)
					{
						indices[index_offset++] = hwBuf32[k] + static_cast<Ogre::uint32>( offset );
					}
				}
				else
				{
					Ogre::uint16* hwBuf16 = static_cast<Ogre::uint16*>(hwBuf);
					for (size_t k = index_start; k < last_index; ++k)
					{
						indices[ index_offset++ ] = static_cast<Ogre::uint32>( hwBuf16[k] ) +
						static_cast<Ogre::uint32>( offset );
					}
				}
				
				ibuf->unlock();
				current_offset = next_offset;
			}
		}

		//---------------------------------------------------------
		//! Get the mesh information for the given mesh with vertex color information.
		static inline void getMeshInformationWithColors(const Ogre::MeshPtr mesh,
			size_t &vertex_count,
			Ogre::Vector3* &vertices,
			Ogre::Vector3* &normals,
			Ogre::Vector3* &texcoords,
			Ogre::RGBA* &vertexColors,
			Ogre::Vector3* &tangents,
			size_t &index_count,
			Ogre::uint32* &indices,
			const Ogre::Vector3 &position,
			const Ogre::Quaternion &orient,
			const Ogre::Vector3 &scale)
		{
			bool added_shared = false;
			size_t current_offset = 0;
			size_t shared_offset = 0;
			size_t next_offset = 0;
			size_t index_offset = 0;

			vertex_count = index_count = 0;

			// Calculate how many vertices and indices we're going to need
			for (unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh( i );

				// We only need to add the shared vertices once		
				if(submesh->useSharedVertices)
				{
					if( !added_shared )
					{
						vertex_count += mesh->sharedVertexData->vertexCount;
						added_shared = true;
					}
				}
				else
				{
					vertex_count += submesh->vertexData->vertexCount;
				}

				// Add the indices
				index_count += submesh->indexData->indexCount;
			}


			// Allocate space for the vertices and indices
			vertices = new Ogre::Vector3[vertex_count];
			normals = new Ogre::Vector3[vertex_count];
			texcoords = new Ogre::Vector3[vertex_count];
			indices = new Ogre::uint32[index_count];
			vertexColors = new Ogre::RGBA[index_count];
			tangents = new Ogre::Vector3[vertex_count];

			added_shared = false;

			// Run through the submeshes again, adding the data into the arrays
			for ( unsigned short i = 0; i < mesh->getNumSubMeshes(); ++i)
			{
				Ogre::SubMesh* submesh = mesh->getSubMesh(i);

				Ogre::VertexData* vertex_data = submesh->useSharedVertices ? mesh->sharedVertexData : submesh->vertexData;

				if((!submesh->useSharedVertices)||(submesh->useSharedVertices && !added_shared))
				{
					if(submesh->useSharedVertices)
					{
						added_shared = true;
						shared_offset = current_offset;
					}

					const Ogre::VertexElement* posElem =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_POSITION);

					Ogre::HardwareVertexBufferSharedPtr vbuf =
						vertex_data->vertexBufferBinding->getBuffer(posElem->getSource());

					unsigned char* vertex =
						static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					// There is _no_ baseVertexPointerToElement() which takes an Ogre::Ogre::Real or a double
					//  as second argument. So make it float, to avoid trouble when Ogre::Ogre::Real will
					//  be comiled/typedefed as double:
					//      Ogre::Ogre::Real* pOgre::Real;
					float* pReal;

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						posElem->baseVertexPointerToElement(vertex, &pReal);

						Ogre::Vector3 pt(pReal[0], pReal[1], pReal[2]);

						vertices[current_offset + j] = (orient * (pt * scale)) + position;
					}

					vbuf->unlock();

					float* pReal2;
					// Read normal data
					const Ogre::VertexElement* normElm =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_NORMAL);

					vbuf = vertex_data->vertexBufferBinding->getBuffer(normElm->getSource());

					vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						normElm->baseVertexPointerToElement(vertex, &pReal2);
						Ogre::Vector3 pt(pReal2[0], pReal2[1], pReal2[2]);
						normals[current_offset + j] = pt;
					}

					vbuf->unlock();

					float* pReal3;
					// Read texture data
					const Ogre::VertexElement* tcElm =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_TEXTURE_COORDINATES);

					vbuf = vertex_data->vertexBufferBinding->getBuffer(tcElm->getSource());

					vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

					for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
					{
						tcElm->baseVertexPointerToElement(vertex, &pReal3);
						Ogre::Vector3 pt(pReal3[0], pReal3[1], pReal3[2]);
						texcoords[current_offset + j] = pt;
					}

					vbuf->unlock();


					Ogre::RGBA* pReal4;
					// Read color data
					const Ogre::VertexElement* vcElm =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_DIFFUSE);

					if (vcElm != NULL)
					{
						vbuf = vertex_data->vertexBufferBinding->getBuffer(vcElm->getSource());

						vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

						for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
						{
							vcElm->baseVertexPointerToElement(vertex, &pReal4);
							vertexColors[current_offset + j] = *pReal4;
						}

						vbuf->unlock();
						
					}

					float* pReal5;
					// Read tangent data
					const Ogre::VertexElement* tgElm =
						vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_TANGENT);

					if (tgElm != NULL)
					{
						vbuf = vertex_data->vertexBufferBinding->getBuffer(tgElm->getSource());

						vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

						for( size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
						{
							tgElm->baseVertexPointerToElement(vertex, &pReal5);
							// OGRE 14: Vector3 lost its scalar assignment operator
							tangents[current_offset + j] = Ogre::Vector3(pReal5[0], pReal5[1], pReal5[2]);
						}

						vbuf->unlock();

					}

					next_offset += vertex_data->vertexCount;
				}


				Ogre::IndexData* index_data = submesh->indexData;
				size_t numTris = index_data->indexCount / 3;
				Ogre::HardwareIndexBufferSharedPtr ibuf = index_data->indexBuffer;

				bool use32bitindexes = (ibuf->getType() == Ogre::HardwareIndexBuffer::IT_32BIT);

				Ogre::uint32*  pLong = static_cast<Ogre::uint32*>(ibuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));
				unsigned short* pShort = reinterpret_cast<unsigned short*>(pLong);


				size_t offset = (submesh->useSharedVertices)? shared_offset : current_offset;

				if ( use32bitindexes )
				{
					for ( size_t k = 0; k < numTris*3; ++k)
					{
						indices[index_offset++] = pLong[k] + static_cast<Ogre::uint32>(offset);
					}
				}
				else
				{
					for ( size_t k = 0; k < numTris*3; ++k)
					{
						indices[index_offset++] = static_cast<Ogre::uint32>(pShort[k]) +
							static_cast<Ogre::uint32>(offset);
					}
				}

				ibuf->unlock();
				current_offset = next_offset;
			}
		}
	}
}

#endif //__MeshUtil_h__31_8_2010__0_20_13__