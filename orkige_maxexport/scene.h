////////////////////////////////////////////////////////////////////////////////
// scene.h
// Author     : Doug Perkowski
// Start Date : March 27th, 2008
// Copyright  : Copyright (c) 2002-2010 OC3 Entertainment, Inc.
////////////////////////////////////////////////////////////////////////////////
/*********************************************************************************
*                                                                                *
*   This program is free software; you can redistribute it and/or modify         *
*   it under the terms of the GNU Lesser General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or            *
*   (at your option) any later version.                                          *
*                                                                                *
**********************************************************************************/
#ifndef _SCENE_H
#define _SCENE_H


#include "paramList.h"
#include "tinyxml.h"

namespace OrkigeMaxExporter
{
	/***** structures to store scene info *****/
	enum OrkigeLightType
	{
		OGRE_LIGHT_POINT,
		OGRE_LIGHT_DIRECTIONAL,
		OGRE_LIGHT_SPOT,
		OGRE_LIGHT_RADPOINT
	};
	
	typedef struct OrkigePoint3Tag
	{
		float x, y, z;
		OrkigePoint3Tag()
		{
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
		}
		OrkigePoint3Tag(float X, float Y, float Z)
		{
			x = X;
			y = Y;
			z = Z;
		}
	} OrkigePoint3;

	typedef struct OrkigePoint4Tag
	{
		float w, x, y, z;
		OrkigePoint4Tag()
		{
			w = 0.0f;
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
		}
		OrkigePoint4Tag(float W, float X, float Y, float Z)
		{
			w = W;
			x = X;
			y = Y;
			z = Z;
		}
	} OrkigePoint4;

	typedef struct OrkigeTransformTag
	{
		OrkigePoint3 pos;
		OrkigePoint4 rot;
		OrkigePoint3 scale;
	} OrkigeTransform;

	typedef struct OrkigeNodeTag
	{
		OrkigeTransform trans;
		bool isTarget;
		std::string name;
		int id;
	} OrkigeNode;

	typedef struct OrkigeLightTag
	{
		OrkigeNode node;
		OrkigeLightType type;
		OrkigePoint3 diffuseColour;
		OrkigePoint3 specularColour;
		// Point light parameters
		float attenuation_range, attenuation_constant, attenuation_linear, attenuation_quadratic;
		// Spotlight parameters
		float range_inner, range_outer, range_falloff;
		OrkigeLightTag()
		{
			type = OGRE_LIGHT_POINT;
			attenuation_range = 0.0f;
			attenuation_constant = 0.0f;
			attenuation_linear = 0.0f;
			attenuation_quadratic = 0.0f;
			range_inner = 0.0f;
			range_outer = 0.0f;
			range_falloff = 0.0f;
		}
	} OrkigeLight;

	typedef struct OrkigeCameraTag
	{
		OrkigeNode node;
		float fov, clipNear, clipFar;
		
	} OrkigeCamera;

	typedef struct OrkigeMeshTag
	{
		OrkigeNode node;
		std::string meshFile;
	} OrkigeMesh;

	/***** Class OrkigeScene *****/
	class OrkigeScene
	{
	public:
		//constructor
		OrkigeScene();
		//destructor
		~OrkigeScene();
		// Clears structures in between exports
		void clear();
		bool writeSceneFile(ParamList &params);
		void addLight(OrkigeLight& light);
		void addMesh(OrkigeMesh& node);
		void addCamera(OrkigeCamera& camera);
		int getNumMeshes() {return m_meshes.size();}
		void clearMeshes() {m_meshes.clear();}
	protected:
		std::vector<OrkigeLight> m_lights;
		std::vector<OrkigeCamera> m_cameras;
		std::vector<OrkigeMesh> m_meshes;
		int id_counter;
		TiXmlElement* writeNodeData(TiXmlElement* parent, const OrkigeNode &node);
		std::string getLightTypeString(OrkigeLightType type);

	};

}; // end of namespace

#endif
