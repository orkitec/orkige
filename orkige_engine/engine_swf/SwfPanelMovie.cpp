/**************************************************************
	created:	2011/06/26 at 20:41
	filename: 	SwfPanelMovie.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_swf/SwfPanelMovie.h"
#include "engine_swf/SwfMovieManager.h"

namespace Orkige
{
	SwfPanelMovie::SwfPanelMovie(const Ogre::String & file, const Ogre::String & name)
		: SwfBaseMovie(file,name,SwfBaseMovie::PANEL,0)
	{

/*	materials currently doens't exist...
		this->rootColorMaterial = Ogre::MaterialPtr(Ogre::MaterialManager::getSingleton().getByName("Orkige/colorMaterial_Camera"));
		this->rootImageMaterial = Ogre::MaterialPtr(Ogre::MaterialManager::getSingleton().getByName("Orkige/imageMaterial_Camera"));*/


		this->createMovie();
	}

	// -------------------------------------------------------------------------------
	SwfPanelMovie::~SwfPanelMovie()
	{

	}

	// -------------------------------------------------------------------------------
	void SwfPanelMovie::draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset)
	{
		transform[12] -= xOffset;
		transform[13] -= yOffset;

		short* mCoords = (short*) coords;

		//Prepare the transformation matrix
		float xS = +transform[0];
		float yS = +transform[5];

		float xT = +transform[12];
		float yT = -transform[13];

		float xC = -transform[4];
		float yC = -transform[1];

		//If an Object is available, use it
		if(this->numMeshes < this->meshList.size())
		{
			this->updateFreeMesh(mCoords, vertex_count, xS, yS, xT, yT, xC, yC);
		}
		//if no more free Object is in the list, create a new one
		else
		{
			//create the new ManualObject
			Ogre::ManualObject* manualObject = this->sceneManager->createManualObject(this->name + "_PolygonData_" + Ogre::StringConverter::toString(this->numMeshes));
			this->createTriangleStrip(manualObject, mCoords, vertex_count);

			Ogre::SceneNode* objectNode = this->renderNode->createChildSceneNode();
			this->_setBounds(manualObject,Ogre::AxisAlignedBox(-this->movieDefinition->get_width_pixels() * 0.005, -this->movieDefinition->get_height_pixels() * 0.005, 0.0, this->movieDefinition->get_width_pixels() * 0.005, this->movieDefinition->get_height_pixels() * 0.005, 0.0));
			objectNode->attachObject(manualObject);

			//store node & object
			this->meshList.push_back(manualObject);
			this->nodeList.push_back(objectNode);
		}

		this->numMeshes++;

		this->sorting += 0.001f;
	}

	// -------------------------------------------------------------------------------
	void SwfPanelMovie::_setBounds(Ogre::ManualObject * obj, const Ogre::AxisAlignedBox& bounds, bool pad) 
	{ 
		Ogre::AxisAlignedBox AABB = bounds; 
		Ogre::Vector3 max = AABB.getMaximum(); 
		Ogre::Vector3 min = AABB.getMinimum(); 

		if (pad)	
		{ 
			// Pad out the AABB a little, helps with most bounds tests 
			Ogre::Vector3 scaler = (max - min) * Ogre::MeshManager::getSingleton().getBoundsPaddingFactor(); 
			AABB.setExtents(min  - scaler, max + scaler); 
		} 
		else 
		{ 
			AABB.setExtents(min, max); 
		} 
		obj->setBoundingBox( AABB ); 
	}
}
