/********************************************************************
	created:	Monday 2010/08/23 at 19:31
	filename: 	LightMap.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __LightMap_h__23_8_2010__19_31_36__
#define __LightMap_h__23_8_2010__19_31_36__

#include "engine_module/EnginePrerequisites.h"

namespace cimg_library
{
	template<typename T> struct CImg;
};

namespace Orkige
{
	//! create a lightmap from Ogre::SubEntity
	class LightMap : public Ogre::ManualResourceLoader
	{
		//--- Types -------------------------------------------------
	public:
		//! coord sorting
		struct SortCoordsByDistance
		{
			//! sorting op
			bool operator()(std::pair<int, int> const & left, std::pair<int, int> const & right)
			{
				return (left.first*left.first + left.second*left.second) < (right.first*right.first + right.second*right.second);
			}
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		Ogre::TexturePtr texture;
		Ogre::MaterialPtr material;
		Ogre::SubEntity* subEntity;
		optr<cimg_library::CImg<unsigned char> > lightMap;
		String lightMapName;
		int texSize;
		int coordSet;
		static int StaticLightMapCounter;
		Ogre::Real pixelsPerUnit;
		std::vector<std::pair<int, int> > searchPattern;
		bool debugLightmaps;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		LightMap(Ogre::SubEntity* subEntity, Ogre::Real pixelsPerUnit = 0, int texSize = 512, bool debugLightmaps = false);
		//! destructor
		~LightMap(void);
		//! @see Ogre::ManualResourceLoader::loadResource
		void loadResource(Ogre::Resource *resource);
		//! get LightMap name
		String getName();
		//! reset count
		static void resetLightMapCounter();
	protected:
	private:
		void lightTriangle(const Ogre::Vector3 &P1, const Ogre::Vector3 &P2, const Ogre::Vector3 &P3,
			const Ogre::Vector3 &N1, const Ogre::Vector3 &N2, const Ogre::Vector3 &N3,
			const Ogre::Vector2 &T1, const Ogre::Vector2 &T2, const Ogre::Vector2 &T3);
		Ogre::uint8 getLightIntensity(const Ogre::Vector3 &position, const Ogre::Vector3 &normal);
		bool calculateLightMap();
		void assignMaterial();
		void createTexture();
		void fillInvalidPixels();
		void buildSearchPattern();

		//! Convert between texture coordinates given as Ogre::Reals and pixel coordinates given as integers
		inline int getPixelCoordinate(Ogre::Real textureCoord);
		inline Ogre::Real getTextureCoordinate(int pixelCoord);

		//! Calculate coordinates of P in terms of P1, P2 and P3
		//! P = x*P1 + y*P2 + z*P3
		//! If any of P.x, P.y or P.z are negative then P is outside of the triangle
		Ogre::Vector3 getBarycentricCoordinates(const Ogre::Vector2 &P1, const Ogre::Vector2 &P2, const Ogre::Vector2 &P3, const Ogre::Vector2 &P);
		//! Get the surface area of a triangle
		Ogre::Real getTriangleArea(const Ogre::Vector3 &P1, const Ogre::Vector3 &P2, const Ogre::Vector3 &P3);
	};
	//---------------------------------------------------------------
	int LightMap::getPixelCoordinate(Ogre::Real textureCoord)
	{
		int pixel = (int)(textureCoord*this->texSize);
		if (pixel < 0)
			pixel = 0;
		if (pixel >= this->texSize)
			pixel = this->texSize-1;
		return pixel;
	}
	//---------------------------------------------------------------
	Ogre::Real LightMap::getTextureCoordinate(int pixelCoord)
	{
		return (Ogre::Real(pixelCoord)+0.5f)/Ogre::Real(this->texSize);
	}
	//---------------------------------------------------------------
	//! create LightMap for Entity and all of its SubEntity
	class EntityLightMap
	{
	public:
		//! contsruct LightMap for all subentities of this entity
		EntityLightMap(Ogre::Entity* entity, Ogre::Real pixelsPerUnit = 0, int texSize = 512, bool debugLightmaps = false)
		{
			int i, numSubEntities = entity->getNumSubEntities();
			LightMap::resetLightMapCounter();
			for (i=0; i<numSubEntities; ++i)
			{
				optr<LightMap> lightMap(new LightMap(entity->getSubEntity(i), pixelsPerUnit, texSize, debugLightmaps));
				this->lightMaps.push_back(lightMap);
			}
		}

	private:
		std::vector<optr<LightMap> > lightMaps;
	};
}

#endif //__LightMap_h__23_8_2010__19_31_36__