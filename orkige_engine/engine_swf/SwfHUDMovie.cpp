#include "engine_swf/SwfHUDMovie.h"
#include "engine_swf/SwfMovieManager.h"

#include <gameswf/gameswf_root.h>
#include <gameswf/gameswf_movie_def.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <Ogre.h>

namespace Orkige
{
	// -------------------------------------------------------------------------------
	SwfHudMovie::SwfHudMovie(const Ogre::String & file, const Ogre::String & name, Ogre::Camera* cam)
		: SwfBaseMovie(file,name,SwfBaseMovie::HUD,-100), left(0), top(0), scaleX(1), scaleY(1)
	{
		this->widthConst  = 200.0 / (float)this->width * this->scaleX;
		this->heightConst = 200.0 / (float)this->height * this->scaleY;
		this->setCurrentCamera(cam);
		/*infiniteBox.setInfinite();*/
		//create the gameswf movie
		this->createMovie();
	}

	// -------------------------------------------------------------------------------
	SwfHudMovie::~SwfHudMovie()
	{

	}

	// -------------------------------------------------------------------------------
	void SwfHudMovie::update(Ogre::Real timeSinceLastFrame)
	{	

		if(this->currentCamera)
		{
			this->renderNode->setPosition(this->currentCamera->getRealPosition());
			this->renderNode->setOrientation(this->currentCamera->getRealOrientation());

		}

		this->renderNode->setVisible(true,true);
		/*
		//In theory this + the setting a infinite boundingbox on each manualobject would
		//free us from the need to set the renderNode to the given Camera position
		//but on my pc sometimes the sorting gets messed up when i move through the scene :(
		//maybe this can be fixed with a shader
		this->renderNode->setUseIdentityProjection(true);
		this->renderNode->setUseIdentityView(true);*/
		

		SwfBaseMovie::update(timeSinceLastFrame);
	}

	// -------------------------------------------------------------------------------
	void SwfHudMovie::setPosition(Ogre::Real x, Ogre::Real y)
	{
		this->left = x;
		this->top = y;

		//update the transform constants too
		this->widthConst  = 100.0 / (float)this->width * this->scaleX;
		this->heightConst = 100.0 / (float)this->height * this->scaleY;
	}

	// -------------------------------------------------------------------------------
	void SwfHudMovie::setScale(Ogre::Real x, Ogre::Real y)
	{
		this->scaleX = x;
		this->scaleY = y;

		//update the transform constants too
		this->widthConst  = 100.0 / (float)this->width * this->scaleX;
		this->heightConst = 100.0 / (float)this->height * this->scaleY;
	}

	// -------------------------------------------------------------------------------
	void SwfHudMovie::draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset)
	{
		short* mCoords = (short*) coords;

		//Prepare the transformation matrix
		float xS = +transform[0]  * this->widthConst;
		float yS = +transform[5]  * this->heightConst;

		float xT = +transform[12] * this->widthConst  + this->left * 2.0 - 1.0;
		float yT = -transform[13] * this->heightConst - this->top  * 2.0 + 1.0;

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


			/*manualObject->setBoundingBox(this->infiniteBox);*/
			manualObject->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY);
			
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
