/********************************************************************
	created:	Monday 2010/08/23 at 19:35
	filename: 	LightMap.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
//#include <core_debug/DisableMemoryManager.h>
#define cimg_OS 0
#include "engine_util/CImg.h"      // Open source image library (http://cimg.sourceforge.net/)
//#include <core_debug/EnableMemoryManager.h>
#include "engine_graphic/LightMap.h"
#include "engine_physic/CollisionTools.h"

namespace Orkige
{
	int LightMap::StaticLightMapCounter = 0;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	LightMap::LightMap(Ogre::SubEntity* _subEntity, Ogre::Real _pixelsPerUnit, int _texSize, bool _debugLightmaps)
		: subEntity(_subEntity)
		, texSize(_texSize)
		, coordSet(0)
		, pixelsPerUnit(_pixelsPerUnit)
		, debugLightmaps(_debugLightmaps)
	{
		if (this->searchPattern.empty())
			buildSearchPattern();

		this->lightMapName = "LightMap" + Ogre::StringConverter::toString(StaticLightMapCounter++);
		bool lightMapCalculated = false;
		String lightMapFileName = "data/" + subEntity->getParent()->getName() + "_" + this->lightMapName + ".bmp";
		if(Ogre::ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(lightMapFileName))
		{
			this->createTexture();
			this->lightMap->load_bmp(lightMapFileName.c_str());
			lightMapCalculated = true;
		}
		else
		{
			lightMapCalculated = calculateLightMap();
			if(lightMapCalculated)
				this->lightMap->save_bmp(lightMapFileName.c_str());
		}
		
		if(lightMapCalculated)
		{
			assignMaterial();
		}
	}
	//---------------------------------------------------------
	LightMap::~LightMap(void)
	{
		// PROBLEM: THIS CAUSES CRASHES, FIND OUT WHY AND PUT IT BACK
		if (!this->material.isNull())
		{
			Ogre::MaterialManager::getSingleton().remove((Ogre::ResourcePtr&)this->material);
			this->material.setNull();
		}
		if (!this->texture.isNull())
		{
			Ogre::TextureManager::getSingleton().remove((Ogre::ResourcePtr&)this->texture);
			this->texture.setNull();
		}
	}
	//---------------------------------------------------------
	void LightMap::resetLightMapCounter()
	{
		LightMap::StaticLightMapCounter = 0;
	}
	//---------------------------------------------------------
	void LightMap::loadResource(Ogre::Resource *resource)
	{
		Ogre::Texture* texture = (Ogre::Texture*)resource;

		// Get the pixel buffer
		Ogre::HardwarePixelBufferSharedPtr pixelBuffer = texture->getBuffer();

		// Lock the pixel buffer and get a pixel box
		pixelBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD); // for best performance use HBL_DISCARD!
		const Ogre::PixelBox &pixelBox = pixelBuffer->getCurrentLock();

		Ogre::uint8* data = static_cast<Ogre::uint8*>(pixelBox.data);

		assert(pixelBox.getWidth() == pixelBox.getHeight());
		const int iTexSize = (int)pixelBox.getWidth();
		const int iRowPitch = (int)pixelBox.rowPitch;

		int i, j;

		for (j = 0; j < iTexSize; j++)
		{
			for(i = 0; i < iTexSize; i++)
			{
				data[iRowPitch*j + i] = (*this->lightMap)(i, j);
			}
		}

		// Unlock the pixel buffer
		pixelBuffer->unlock();
	}
	//---------------------------------------------------------
	String LightMap::getName()
	{
		return this->lightMapName;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	Ogre::Vector3 LightMap::getBarycentricCoordinates(const Ogre::Vector2 &P1, const Ogre::Vector2 &P2, const Ogre::Vector2 &P3, const Ogre::Vector2 &P)
	{
		Ogre::Vector3 Coordinates(0.0);
		Ogre::Real denom = (-P1.x * P3.y - P2.x * P1.y + P2.x * P3.y + P1.y * P3.x + P2.y * P1.x - P2.y * P3.x);

		if (fabs(denom) >= 1e-6)
		{
			Coordinates.x = (P2.x * P3.y - P2.y * P3.x - P.x * P3.y + P3.x * P.y - P2.x * P.y + P2.y * P.x) / denom;
			Coordinates.y = -(-P1.x * P.y + P1.x * P3.y + P1.y * P.x - P.x * P3.y + P3.x * P.y - P1.y * P3.x) / denom;
			//        Coordinates.z = (-P1.x * P.y + P2.y * P1.x + P2.x * P.y - P2.x * P1.y - P2.y * P.x + P1.y * P.x) / denom;
		}
		Coordinates.z = 1 - Coordinates.x - Coordinates.y;

		return Coordinates;
	}
	//---------------------------------------------------------
	Ogre::Real LightMap::getTriangleArea(const Ogre::Vector3 &P1, const Ogre::Vector3 &P2, const Ogre::Vector3 &P3)
	{
		return 0.5f*(P2-P1).crossProduct(P3-P1).length();
	}
	//---------------------------------------------------------
	bool LightMap::calculateLightMap()
	{
		// Reset the lightmap to all 0's
		if (this->lightMap)
			this->lightMap->fill(0);

		// Get the submesh
		Ogre::SubMesh* submesh = this->subEntity->getSubMesh();
		Ogre::Matrix4 WorldTransform;
		this->subEntity->getWorldTransforms(&WorldTransform);

		// Get vertex positions
		std::vector<Ogre::Vector3> MeshVertices;
		{
			Ogre::VertexData* vertex_data = submesh->useSharedVertices ? submesh->parent->sharedVertexData : submesh->vertexData;
			const Ogre::VertexElement* posElem = vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_POSITION);
			Ogre::HardwareVertexBufferSharedPtr vbuf = vertex_data->vertexBufferBinding->getBuffer(posElem->getSource());
			unsigned char* vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

			float* pReal;

			MeshVertices.resize(vertex_data->vertexCount);

			for (size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
			{
				posElem->baseVertexPointerToElement(vertex, &pReal);
				MeshVertices[j] = WorldTransform*Ogre::Vector3(pReal[0],pReal[1],pReal[2]);
			}

			vbuf->unlock();
		}

		// Get vertex normals
		Ogre::Quaternion Rotation = WorldTransform.extractQuaternion();
		std::vector<Ogre::Vector3> MeshNormals;
		{
			Ogre::VertexData* vertex_data = submesh->useSharedVertices ? submesh->parent->sharedVertexData : submesh->vertexData;
			const Ogre::VertexElement* normalElem = vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_NORMAL);
			if(normalElem == NULL)
			{
				return false;
			}
			Ogre::HardwareVertexBufferSharedPtr vbuf = vertex_data->vertexBufferBinding->getBuffer(normalElem->getSource());
			unsigned char* vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

			float* pReal;

			MeshNormals.resize(vertex_data->vertexCount);

			for (size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
			{
				normalElem->baseVertexPointerToElement(vertex, &pReal);
				MeshNormals[j] = Rotation*Ogre::Vector3(pReal[0],pReal[1],pReal[2]);
			}

			vbuf->unlock();
		}

		// Get vertex UV coordinates
		std::vector<Ogre::Vector2> MeshTextureCoords;
		{
			Ogre::VertexData* vertex_data = submesh->useSharedVertices ? submesh->parent->sharedVertexData : submesh->vertexData;
			// Get last set of texture coordinates
			int i = 0;
			const Ogre::VertexElement* texcoordElem;
			const Ogre::VertexElement* pCurrentElement = NULL;
			do
			{
				texcoordElem = pCurrentElement;
				pCurrentElement = vertex_data->vertexDeclaration->findElementBySemantic(Ogre::VES_TEXTURE_COORDINATES, i++);
			} while (pCurrentElement);
			this->coordSet = i-2;
			if (!texcoordElem)
				return false;
			Ogre::HardwareVertexBufferSharedPtr vbuf = vertex_data->vertexBufferBinding->getBuffer(texcoordElem->getSource());
			unsigned char* vertex = static_cast<unsigned char*>(vbuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY));

			float* pReal;

			MeshTextureCoords.resize(vertex_data->vertexCount);

			for (size_t j = 0; j < vertex_data->vertexCount; ++j, vertex += vbuf->getVertexSize())
			{
				texcoordElem->baseVertexPointerToElement(vertex, &pReal);
				MeshTextureCoords[j] = Ogre::Vector2(pReal[0], pReal[1]);
			}

			vbuf->unlock();
		}

		Ogre::IndexData* index_data = submesh->indexData;

		size_t numTris = index_data->indexCount / 3;
		Ogre::HardwareIndexBufferSharedPtr ibuf = index_data->indexBuffer;

		bool use32bitindexes = (ibuf->getType() == Ogre::HardwareIndexBuffer::IT_32BIT);

		unsigned int Indices[3];
		void* pBuffer = ibuf->lock(Ogre::HardwareBuffer::HBL_READ_ONLY);

		// Calculate the lightmap texture size
		if (this->pixelsPerUnit && this->texture.isNull())
		{
			Ogre::Real SurfaceArea = 0;
			for ( size_t k = 0; k < numTris*3; k+=3)
			{
				for (int i=0; i<3; ++i)
				{
					if (use32bitindexes)
						Indices[i] = ((unsigned int*)pBuffer)[k+i];
					else
						Indices[i] = ((unsigned short*)pBuffer)[k+i];
				}
				SurfaceArea += getTriangleArea(MeshVertices[Indices[0]], MeshVertices[Indices[1]], MeshVertices[Indices[2]]);
			}
			Ogre::Real TexSize = Ogre::Math::Sqrt(SurfaceArea)*this->pixelsPerUnit;

			int iTexSize = 1;
			while (iTexSize < TexSize)
				iTexSize *= 2;

			this->texSize = iTexSize;
		}

		// Create the texture with the new size
		createTexture();

		// Fill in the lightmap
		for ( size_t k = 0; k < numTris*3; k+=3)
		{
			for (int i=0; i<3; ++i)
			{
				if (use32bitindexes)
					Indices[i] = ((unsigned int*)pBuffer)[k+i];
				else
					Indices[i] = ((unsigned short*)pBuffer)[k+i];
			}
			lightTriangle(MeshVertices[Indices[0]], MeshVertices[Indices[1]], MeshVertices[Indices[2]],
				MeshNormals[Indices[0]], MeshNormals[Indices[1]], MeshNormals[Indices[2]],
				MeshTextureCoords[Indices[0]], MeshTextureCoords[Indices[1]], MeshTextureCoords[Indices[2]]);
		}

		ibuf->unlock();

		fillInvalidPixels();
		this->lightMap->blur(1.0);

		return true;
	}
	//---------------------------------------------------------
	void LightMap::createTexture()
	{
		if (!this->texture.isNull())
			return;
		this->lightMap.reset(new cimg_library::CImg<unsigned char>(this->texSize, this->texSize, 1, 2, 0));
		if (Ogre::TextureManager::getSingleton().resourceExists(this->lightMapName))
			Ogre::TextureManager::getSingleton().remove(this->lightMapName);
		// Create the texture
		this->texture = Ogre::TextureManager::getSingleton().createManual(
			this->lightMapName, // name
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
			Ogre::TEX_TYPE_2D,      // type
			this->texSize, this->texSize,         // width & height
			-1,                // number of mipmaps
			Ogre::PF_L8,     // pixel format
			Ogre::TU_DEFAULT,
			this);
	}
	//---------------------------------------------------------
	void LightMap::assignMaterial()
	{
		if (!this->material.isNull())
			return;
		if (Ogre::MaterialManager::getSingleton().resourceExists(this->lightMapName))
			Ogre::MaterialManager::getSingleton().remove(this->lightMapName);
		if (this->debugLightmaps)
		{
			this->material = Ogre::MaterialManager::getSingleton().create(this->lightMapName, Ogre::StringUtil::BLANK, true);
		}
		else
		{
			Ogre::MaterialPtr PrevMaterial = this->subEntity->getMaterial();
			this->material = PrevMaterial->clone(this->lightMapName);
			this->material->setReceiveShadows(true);
		}
		Ogre::Pass* pPass = this->material->getTechnique(0)->getPass(0);
		pPass->setLightingEnabled(false);
		Ogre::TextureUnitState* pTextureUnitState = pPass->createTextureUnitState(this->texture->getName(), this->coordSet);
		pTextureUnitState->setColourOperation(Ogre::LBO_MODULATE);
		pTextureUnitState->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
		this->subEntity->setMaterialName(this->lightMapName);
	}
	//---------------------------------------------------------
	void LightMap::fillInvalidPixels()
	{
		int i, j;
		int x, y;
		std::vector<std::pair<int, int> >::iterator itSearchPattern;
		for (i=0; i<this->texSize; ++i)
		{
			for (j=0; j<this->texSize; ++j)
			{
				// Invalid pixel found
				if ((*this->lightMap)(i, j, 0, 1) == 0)
				{
					for (itSearchPattern = this->searchPattern.begin(); itSearchPattern != this->searchPattern.end(); ++itSearchPattern)
					{
						x = i+itSearchPattern->first;
						y = j+itSearchPattern->second;
						if (x < 0 || x >= this->texSize)
							continue;
						if (y < 0 || y >= this->texSize)
							continue;
						// If search pixel is valid assign it to the invalid pixel and stop searching
						if ((*this->lightMap)(x, y, 0, 1) == 1)
						{
							(*this->lightMap)(i, j) = (*this->lightMap)(x, y);
							break;
						}
					}
				}
			}
		}
	}
	//---------------------------------------------------------
	void LightMap::buildSearchPattern()
	{
		this->searchPattern.clear();
		const int iSize = 5;
		int i, j;
		for (i=-iSize; i<=iSize; ++i)
		{
			for (j=-iSize; j<=iSize; ++j)
			{
				if (i==0 && j==0)
					continue;
				this->searchPattern.push_back(std::make_pair(i, j));
			}
		}
		sort(this->searchPattern.begin(), this->searchPattern.end(), SortCoordsByDistance());
	}
	//---------------------------------------------------------
	void LightMap::lightTriangle(const Ogre::Vector3 &P1, const Ogre::Vector3 &P2, const Ogre::Vector3 &P3,
		const Ogre::Vector3 &N1, const Ogre::Vector3 &N2, const Ogre::Vector3 &N3,
		const Ogre::Vector2 &T1, const Ogre::Vector2 &T2, const Ogre::Vector2 &T3)
	{
		Ogre::Vector2 TMin = T1, TMax = T1;
		TMin.makeFloor(T2);
		TMin.makeFloor(T3);
		TMax.makeCeil(T2);
		TMax.makeCeil(T3);
		int iMinX = getPixelCoordinate(TMin.x);
		int iMinY = getPixelCoordinate(TMin.y);
		int iMaxX = getPixelCoordinate(TMax.x);
		int iMaxY = getPixelCoordinate(TMax.y);
		int i, j;
		Ogre::Vector2 TextureCoord;
		Ogre::Vector3 BarycentricCoords;
		Ogre::Vector3 Pos;
		Ogre::Vector3 Normal;
		for (i=iMinX; i<=iMaxX; ++i)
		{
			for (j=iMinY; j<=iMaxY; ++j)
			{
				TextureCoord.x = getTextureCoordinate(i);
				TextureCoord.y = getTextureCoordinate(j);
				BarycentricCoords = getBarycentricCoordinates(T1, T2, T3, TextureCoord);
				Pos = BarycentricCoords.x * P1 + BarycentricCoords.y * P2 + BarycentricCoords.z * P3;
				Normal = BarycentricCoords.x * N1 + BarycentricCoords.y * N2 + BarycentricCoords.z * N3;
				Normal.normalise();
				if ((*this->lightMap)(i, j, 0, 1) == 1 || BarycentricCoords.x < 0 || BarycentricCoords.y < 0 || BarycentricCoords.z < 0)
					continue;
				(*this->lightMap)(i, j) = getLightIntensity(Pos, Normal);
				(*this->lightMap)(i, j, 0, 1) = 1;
			}
		}

	}
	//---------------------------------------------------------
	/*
	This is the only function which you should need to modify. Basically given the position coordinate
	and the surface normal at that point, you should return the light intensity value as a number between
	0 and 255. In this example I use the PhysX library to cast a ray in a fixed direction to see if it
	intersects with any other objects in the scene, if it does then this point is in the shade.
	*/
	Ogre::uint8 LightMap::getLightIntensity(const Ogre::Vector3 &Position, const Ogre::Vector3 &Normal)
	{
		const Ogre::Real Tolerance = (Ogre::Real)(1e-3);
		const Ogre::Real Distance = 10000;
		const Ogre::uint8 AmbientValue = 100;
		const Ogre::uint8 MaxValue = 255;

		Ogre::Vector3 LightDirection(0.4f, -1.f, -0.8f);
		LightDirection.normalise();

		Ogre::Real Intensity = -LightDirection.dotProduct(Normal);
		if (Intensity < 0)
			return AmbientValue;

		Ogre::uint8 LightValue = (Ogre::uint8)(AmbientValue+Intensity*(MaxValue-AmbientValue));

		Ogre::Vector3 Origin = Position-Distance*LightDirection;
		Ogre::Vector3 hitPos;
		float closestDistance = 0.f;
		Ogre::MovableObject* target = NULL;
		bool bHit = CollisionTools::getSingleton().raycastFromPoint(Origin, LightDirection, hitPos, target, closestDistance);
		if (bHit)
		{
			return AmbientValue;
		}
		else
		{
			bHit = CollisionTools::getSingleton().raycastFromPoint(Position, -LightDirection, hitPos, target, closestDistance);
			if (bHit)
				return AmbientValue;
			else
				return LightValue;
		}

		return LightValue;
	}
}