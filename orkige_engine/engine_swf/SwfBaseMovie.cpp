/**************************************************************
	created:	2011/06/25 at 4:13
	filename: 	SwfBaseMovie.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_swf/SwfBaseMovie.h"

#include <gameswf/gameswf_root.h>
#include <gameswf/gameswf_movie_def.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <Ogre.h>

#include "engine_swf/SwfMovieManager.h"

namespace Orkige
{
	SwfBaseMovie::SwfBaseMovie(const Ogre::String & file, const Ogre::String & name, SwfMovieType type, Ogre::Real initialSorting)
		: updateTransforms(true), updateMeshes(true), movieDefinition(0), movieInterface(0), initialSorting(initialSorting)
	{
		//add "this" Movie to the SwfMovieManager, to keep track of it
		SwfMovieManager::getSingleton().addMovie(type,this);

		this->type = type;
		this->file = file;
		this->name = name;
		this->numColorMaterials = 0;
		this->numImageMaterials = 0;
		this->numMeshes = 0;
		this->sorting = this->initialSorting;

		this->sceneManager = SwfMovieManager::getSingleton().getSceneManager();

		this->renderNode = this->sceneManager->getRootSceneNode()->createChildSceneNode();

		int	movie_version = 0;
		gameswf::get_movie_info(file.c_str(), &movie_version, &this->width, &this->height, NULL, NULL, NULL);
		this->ratio = (float)this->width / (float)this->height;

		this->rootColorMaterial = Ogre::MaterialPtr(Ogre::MaterialManager::getSingleton().getByName("Orkige/colorMaterial_Camera"));
		this->rootImageMaterial = Ogre::MaterialPtr(Ogre::MaterialManager::getSingleton().getByName("Orkige/imageMaterial_Camera"));
	}

	// -------------------------------------------------------------------------------
	SwfBaseMovie::~SwfBaseMovie()
	{
		//remove "this" SwfTextureMovie from the SwfMovieManager
		SwfMovieManager::getSingleton().removeMovie(this->type, this);

		//destroy the rendernode and all things attached to it
		this->renderNode->removeAndDestroyAllChildren();
		this->sceneManager->destroySceneNode(this->renderNode->getName());

		//free the texture list
		for(unsigned int i=0; i<this->textureList.size(); i++)
			Ogre::TextureManager::getSingleton().remove(this->textureList[i]->getName());

		//free the color material list
		for(unsigned int i=0; i<this->colorMaterialList.size(); i++)
			Ogre::MaterialManager::getSingleton().remove(this->colorMaterialList[i]->getName());

		//free the image material list
		for(unsigned int i=0; i<this->imageMaterialList.size(); i++)
			Ogre::MaterialManager::getSingleton().remove(this->imageMaterialList[i]->getName());

		//destroy the gameswf movie
		/*
		if (this->movieDefinition) this->movieDefinition->drop_ref();
		if (this->movieInterface) this->movieInterface->drop_ref();*/

/*
		delete this->movieDefinition;
		delete this->movieInterface;*/

	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::createMovie()
	{
		//create the gameswf movie
		this->movieDefinition = /*gameswf::create_movie(file.c_str());*/SwfMovieManager::getSingleton().getSingleton().getPlayer()->create_movie(this->file.c_str());
		this->movieInterface = this->movieDefinition->create_instance();
		this->movieInterface->set_play_state(gameswf::character::PLAY);
		//to initialise 'everything'
		this->update(0);
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::update( Ogre::Real timeSinceLastFrame)
	{
		if(!this->movieDefinition || !this->movieInterface)
			return;

		this->movieInterface->set_display_viewport(0, 0, this->movieDefinition->get_width_pixels(), this->movieDefinition->get_height_pixels());

		this->movieInterface->advance(timeSinceLastFrame);
		this->movieInterface->display();
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::stream_bitmap(int width, int height, char* data, unsigned int &index)
	{
		index = this->textureList.size();

		Ogre::TexturePtr ptex = Ogre::TextureManager::getSingleton().createManual(
			this->name + "_Tex_" + Ogre::StringConverter::toString(this->textureList.size()), 
			"General", 
			Ogre::TEX_TYPE_2D, 
			width, 
			height, 
			1, 
			0, 
			Ogre::PF_R8G8B8, 
			Ogre::TU_DYNAMIC_WRITE_ONLY
			);

		Ogre::HardwarePixelBufferSharedPtr buffer = ptex->getBuffer(0,0);
		buffer->blitFromMemory(Ogre::PixelBox( width, height, 1, Ogre::PF_BYTE_RGB, data ));

		this->textureList.push_back(ptex);
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::stream_bitmapAlpha(int width, int height, char* data, unsigned int &index)
	{
		index = this->textureList.size();

		Ogre::TexturePtr ptex = Ogre::TextureManager::getSingleton().createManual(
			this->name + "_Tex_" + Ogre::StringConverter::toString(this->textureList.size()), 
			"General", 
			Ogre::TEX_TYPE_2D, 
			width, 
			height, 
			1, 
			0, 
			Ogre::PF_R8G8B8A8, 
			Ogre::TU_DYNAMIC_WRITE_ONLY
			);

		Ogre::HardwarePixelBufferSharedPtr buffer = ptex->getBuffer(0,0);
		buffer->blitFromMemory(Ogre::PixelBox( width, height, 1, Ogre::PF_BYTE_RGBA, data ));

		this->textureList.push_back(ptex);

		////DEBUGGING CODE -> START
		buffer->lock(Ogre::HardwareBuffer::HBL_READ_ONLY );
		const Ogre::PixelBox &readrefpb = buffer->getCurrentLock();   
		unsigned char *readrefdata = static_cast<unsigned char*>(readrefpb.data);      
		buffer->unlock();

		Ogre::Image img;
		img = img.loadDynamicImage (readrefdata, ptex->getWidth(),
			ptex->getHeight(), ptex->getFormat());   
		img.save("C:/image"+Ogre::StringConverter::toString(index)+".png"); 
		////DEBUGGING CODE -> END
	}
	// -------------------------------------------------------------------------------
	void SwfBaseMovie::fill_style_color(float red, float green, float blue, float alpha)
	{
		//If an free Object is available, use it
		if(this->numColorMaterials < this->colorMaterialList.size())
		{
			this->currentMaterial = this->colorMaterialList[this->numColorMaterials];

			Ogre::Pass* pass = this->currentMaterial->getBestTechnique()->getPass(0);
			Ogre::GpuProgramParametersSharedPtr params = pass->getFragmentProgramParameters();
			params->setNamedConstant("color", Ogre::ColourValue(red, green, blue, alpha));
		}
		//if no more free Object is in the list, create a new one
		else
		{
			Ogre::MaterialPtr material = this->rootColorMaterial->clone(this->name + "_ColorMat_" + Ogre::StringConverter::toString(this->numColorMaterials), true, "Swf_Renderer");
			material->compile(false);
			material->getBestTechnique()->setLightingEnabled(true);
			Ogre::Pass* pass = material->getBestTechnique()->getPass(0);
			Ogre::GpuProgramParametersSharedPtr params = pass->getFragmentProgramParameters();
			/*params->setAutoAddParamName(true);*/
			params->setNamedConstant("color", Ogre::ColourValue(red, green, blue, alpha));

			this->colorMaterialList.push_back(material);

			this->currentMaterial = material;
		}

		this->numColorMaterials++;
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::fill_style_bitmap(int index)
	{
		//If an free Object is available, use it
		if(this->numImageMaterials < this->imageMaterialList.size())
		{
			this->currentMaterial = this->imageMaterialList[this->numImageMaterials];

			Ogre::Pass* pass = this->currentMaterial->getBestTechnique()->getPass(0);
			pass->getTextureUnitState(0)->setTextureName(this->textureList[index]->getName());
		}
		//if no more free Object is in the list, create a new one
		else
		{
			Ogre::MaterialPtr material = this->rootImageMaterial->clone(this->name + "_ImageMat_" + Ogre::StringConverter::toString(this->numImageMaterials), true, "Swf_Renderer");
			material->compile(false);
			Ogre::Pass* pass = material->getBestTechnique()->getPass(0);
			Ogre::TexturePtr texture = this->textureList[index];
			pass->createTextureUnitState(texture->getName());

			if(texture->hasAlpha())
			{
				pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
				pass->setDepthWriteEnabled(false);
			}

			this->imageMaterialList.push_back(material);

			this->currentMaterial = material;
		}

		this->numImageMaterials++;
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::draw_line_strip()
	{
		assert( 0 &&"SwfBaseMovie::draw_line_strip Needs to be implemented");
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::begin_display(float red, float green, float blue)
	{
		this->numColorMaterials = 0;
		this->numImageMaterials = 0;
		this->numMeshes = 0;
		this->sorting = this->initialSorting;
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::end_display()
	{
		this->sorting = this->initialSorting;
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::createTriangleStrip(Ogre::ManualObject* manualObject, short* coords, int vertex_count)
	{
		manualObject->estimateVertexCount(vertex_count);
		manualObject->setDynamic(false);

		manualObject->begin(this->currentMaterial->getName(), Ogre::RenderOperation::OT_TRIANGLE_STRIP);

		//Precalculations for UVs
		Ogre::Real xMin = coords[0];
		Ogre::Real xMax = coords[0];

		Ogre::Real yMin = coords[1];
		Ogre::Real yMax = coords[1];

		//find the highest and the lowest x/y positions (for UV mapping)
		for(int i=0; i<vertex_count*2; i+=2)
		{
			if(coords[i] < xMin)
				xMin = coords[i];
			if(coords[i] > xMax)
				xMax = coords[i];

			if(coords[i+1] < yMin)
				yMin = coords[i+1];
			if(coords[i+1] > yMax)
				yMax = coords[i+1];
		}

		Ogre::Real xRange = xMax - xMin;
		Ogre::Real yRange = yMax - yMin;

		//Fill in the new vertices and UVs
		for(int i=0; i<vertex_count*2; i+=2)
		{
			Ogre::Real x = coords[i]   / 2000.0;
			Ogre::Real y = -coords[i+1]/ 2000.0;
			Ogre::Real z = 0;

			//draw the vertex
			manualObject->position( x, y, z );

			//calculate the texture coordinates
			Ogre::Real u = (coords[i]   - xMin) / xRange;
			Ogre::Real v = (coords[i+1] - yMin) / yRange;
			manualObject->textureCoord(u, v);
		}
		//finished the TRIANGLE_STRIP
		manualObject->end();
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::updateFreeMesh(short* coords, int vertex_count, float xS, float yS, float xT, float yT, float xC, float yC)
	{
		Ogre::SceneNode* currentNode = this->nodeList[this->numMeshes];

		if(this->updateMeshes)
		{
			Ogre::ManualObject* manualObject = this->meshList[this->numMeshes];
			//Free the previous vertexbuffer
			manualObject->clear();
			this->createTriangleStrip(manualObject, coords, vertex_count);
		}
		if(this->updateTransforms)
		{
			Ogre::Pass* pass = this->currentMaterial->getBestTechnique()->getPass(0);
			Ogre::GpuProgramParametersSharedPtr params = pass->getVertexProgramParameters();

			Ogre::Matrix4 trans = Ogre::Matrix4(
				xS, yC, 0, 0,
				xC, yS, 0, 0,
				0,  0,  1, 0,
				xT, yT, 0, 1
				);

			/*params->setAutoAddParamName(true);*/
			params->setNamedConstant("transform", trans);

			currentNode->_updateBounds();
		}

		currentNode->setPosition(0, 0, this->sorting);
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::setMouseSettings(float x, float y, int mouseMask)
	{
		int absX = x * this->movieDefinition->get_width_pixels();
		int absY = y * this->movieDefinition->get_height_pixels();

		this->movieInterface->notify_mouse_state(absX, absY, mouseMask);
	}

	// -------------------------------------------------------------------------------
	Ogre::String SwfBaseMovie::getVariable(Ogre::String name)
	{
		if(!this->movieDefinition || !this->movieInterface)
			return "";

		return this->movieInterface->get_variable(name.c_str());
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::setVariable(Ogre::String name, Ogre::String data)
	{
		this->movieInterface->set_variable(name.c_str(), data.c_str());
	}

	// -------------------------------------------------------------------------------
	Ogre::String SwfBaseMovie::callFunction(Ogre::String name)
	{
		return this->movieInterface->call_method(name.c_str(),"");
	}

	// -------------------------------------------------------------------------------
	int SwfBaseMovie::getCurrentFrame()
	{
		return this->movieInterface->get_current_frame();
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::setCurrentFrame(int number)
	{
		this->movieInterface->goto_frame(number);
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::setUpdateProperties(bool updateTransformations, bool updateMeshes)
	{
		this->updateTransforms = updateTransformations;
		this->updateMeshes = updateMeshes;
	}

	// -------------------------------------------------------------------------------
	float SwfBaseMovie::getMovieRatio()
	{
		return this->ratio;
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::setVisible(bool visible)
	{
		this->renderNode->setVisible(visible);
	}

	// -------------------------------------------------------------------------------
	//DEBUG
	Ogre::SceneNode* SwfBaseMovie::_getSceneNode()
	{
		return this->renderNode;
	}

	// -------------------------------------------------------------------------------
	int SwfBaseMovie::getMovieWidth(void)
	{
		return this->movieDefinition->get_width_pixels();
	}

	// -------------------------------------------------------------------------------
	int SwfBaseMovie::getMovieHeight(void)
	{
		return this->movieDefinition->get_height_pixels();
	}

	// -------------------------------------------------------------------------------
	void SwfBaseMovie::OnMovieCallback(const char* command, const char* args)
	{
		std::stringstream sstr;
		sstr << "Unhandled Movie Callback: " << command << " on Movie: " << this->name << " File: " << this->file <<".";  
		Ogre::LogManager::getSingleton().logMessage(Ogre::LML_TRIVIAL, sstr.str());
	}
};