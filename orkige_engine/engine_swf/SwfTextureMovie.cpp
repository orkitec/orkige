/**************************************************************
	created:	2011/06/26 at 20:48
	filename: 	SwfTextureMovie.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_swf/SwfTextureMovie.h"
#include "engine_swf/SwfMovieManager.h"

namespace Orkige
{
	SwfTextureMovie::SwfTextureMovie(const Ogre::String & file, const Ogre::String & name, unsigned int width, unsigned int height) 
		: SwfBaseMovie(file,name,SwfBaseMovie::TEXTURE, -100)
	{
		this->widthConst  = 200.0 / (float)this->width;
		this->heightConst = 200.0 / (float)this->height;

		//create the rendertexture to display the swf
		this->renderTexture = Ogre::TextureManager::getSingleton().createManual(this->name, "General", Ogre::TEX_TYPE_2D, width, height, 1, 0, Ogre::PF_R8G8B8, Ogre::TU_RENDERTARGET)->getBuffer(0,0)->getRenderTarget(0);
		this->renderTexture->setAutoUpdated(false);

		//create the camera and adjust
		this->camera = this->sceneManager->createCamera(name);
		this->camera->setPosition(Ogre::Vector3::ZERO);
		this->camera->lookAt(Ogre::Vector3::NEGATIVE_UNIT_Z);
		this->camera->setNearClipDistance(0.001f);

		//add a viewport
		this->viewport = this->renderTexture->addViewport(this->camera, 0, 0, 0, 1, 1);
		this->viewport->setClearEveryFrame(true);
		this->viewport->setBackgroundColour(Ogre::ColourValue(1,0,1));
		this->viewport->setOverlaysEnabled(false);

		this->createMovie();
	}
	
	// -------------------------------------------------------------------------------
	SwfTextureMovie::~SwfTextureMovie()
	{
		//destroy the camera
		this->sceneManager->destroyCamera(this->camera);
	}

	// -------------------------------------------------------------------------------
	void SwfTextureMovie::update(Ogre::Real timeSinceLastFrame)
	{
		SwfBaseMovie::update(timeSinceLastFrame);

		this->renderNode->setVisible(true);
		this->renderTexture->update();
		this->renderNode->setVisible(false);
	}
	
	// -------------------------------------------------------------------------------
	void SwfTextureMovie::setResolution(unsigned int width, unsigned int height)
	{
		//remove the viewport
		this->renderTexture->removeAllViewports();
		Ogre::TextureManager::getSingleton().remove(this->name);

		this->renderTexture = Ogre::TextureManager::getSingleton().createManual(this->name, "General", Ogre::TEX_TYPE_2D, width, height, 1, 0, Ogre::PF_R8G8B8, Ogre::TU_RENDERTARGET)->getBuffer(0,0)->getRenderTarget(0);
		this->renderTexture->setAutoUpdated(false);

		//add a viewport
		this->viewport = this->renderTexture->addViewport(this->camera, 0, 0, 0, 1, 1);
		this->viewport->setClearEveryFrame(true);
		this->viewport->setBackgroundColour(Ogre::ColourValue(1,0,1));
		this->viewport->setOverlaysEnabled(false);
	}

	// -------------------------------------------------------------------------------
	void SwfTextureMovie::begin_display(float red, float green, float blue)
	{
		SwfBaseMovie::begin_display(red, green, blue);

		this->viewport->setBackgroundColour(Ogre::ColourValue(red, green, blue));
	}

	// -------------------------------------------------------------------------------
	void SwfTextureMovie::draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset)
	{
		transform[12] -= xOffset;
		transform[13] -= yOffset;


		short* mCoords = (short*) coords;

		//Prepare the transformation matrix
		float xS = +transform[0]  * this->widthConst;
		float yS = +transform[5]  * this->heightConst;

		float xT = +transform[12] * this->widthConst;
		float yT = -transform[13] * this->heightConst;

		float xC = -transform[4]  * this->widthConst;
		float yC = -transform[1]  * this->heightConst;

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

			manualObject->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY);
			manualObject->setVisibilityFlags(2);

			Ogre::SceneNode* objectNode = this->renderNode->createChildSceneNode();
			objectNode->attachObject(manualObject);

			//store node & object
			this->meshList.push_back(manualObject);
			this->nodeList.push_back(objectNode);
		}

		this->numMeshes++;

		this->sorting += 0.01f;
	}
}