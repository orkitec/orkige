/**************************************************************
	created:	2011/06/25 at 4:03
	filename: 	SwfBaseMovie.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfBaseMovie_h__25_6_2011__4_03_04__
#define __SwfBaseMovie_h__25_6_2011__4_03_04__

#include "engine_swf/SwfApiDefs.h"
#include <OgreMaterial.h>

namespace Orkige
{
	class SwfMovieManager;

	class SwfBaseMovie
	{
		//--- Types -------------------------------------------
		friend class SwfMovieManager;
	protected:
		enum SwfMovieType
		{
			HUD,
			PANEL,
			TEXTURE
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		SwfMovieType type;

		bool updateMeshes;
		bool updateTransforms;

		int width;
		int height;

		//movie aspect ratio
		float ratio;

		//name of the texture
		Ogre::String name;
		Ogre::String file;

		//counter variables
		unsigned int numColorMaterials;
		unsigned int numImageMaterials;
		unsigned int numMeshes;

		//things for rendering to the viewport
		Ogre::MaterialPtr    currentMaterial;
		Ogre::Real           sorting;
		const Ogre::Real     initialSorting;//< defines where sorting should start
		Ogre::SceneManager*  sceneManager;
		Ogre::SceneNode*     renderNode;

		//lists for storing the rendering data
		std::vector<Ogre::ManualObject*> meshList;
		//std::vector<ManualObject*> mLineList;
		std::vector<Ogre::MaterialPtr>   colorMaterialList;
		std::vector<Ogre::MaterialPtr>   imageMaterialList;
		std::vector<Ogre::TexturePtr>    textureList;
		std::vector<Ogre::SceneNode*>    nodeList;

		//all materials get cloned from this
		Ogre::MaterialPtr rootColorMaterial;
		Ogre::MaterialPtr rootImageMaterial;

		//GameSWF definitions
		gameswf::movie_definition*	movieDefinition;
		gameswf::root*				movieInterface;
	private:
		//--- Methods -----------------------------------------
	public:
		void setUpdateProperties(bool updateTransformations, bool updateMeshes);
		float getMovieRatio();

		void setVisible(bool visible);

		int getMovieWidth(void);
		int getMovieHeight(void);

		virtual void setMouseSettings(float x, float y, int mouseMask);
		Ogre::String getVariable(Ogre::String name);
		void setVariable(Ogre::String name, Ogre::String data);
		Ogre::String callFunction(Ogre::String name);
		int getCurrentFrame();
		void setCurrentFrame(int number);
	protected:
		SwfBaseMovie(const Ogre::String & file, const Ogre::String & name, SwfMovieType type, Ogre::Real initialSorting);
		virtual ~SwfBaseMovie();

		void createMovie();

		virtual void update(Ogre::Real timeSinceLastFrame);

		//helper for creating a Ogre::RenderOperation::OT_TRIANGLE_STRIP on given ManualObject 
		void createTriangleStrip(Ogre::ManualObject* manualObject, short* coords, int vertex_count);
		void updateFreeMesh(short* coords, int vertex_count, float xS, float yS, float xT, float yT, float xC, float yC);

		//before rendering translate the necessary bitmaps
		void stream_bitmap(int width, int height, char* data, unsigned int &index);
		void stream_bitmapAlpha(int width, int height, char* data, unsigned int &index);

		//DEBUG
		Ogre::SceneNode* _getSceneNode(void);

		//target calls from GameSWF (redirected by SwfMovieManager)
		virtual void begin_display(float red, float green, float blue);
		virtual void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset)=0;
		void draw_line_strip();
		void fill_style_color(float red, float green, float blue, float alpha);
		void fill_style_bitmap(int index);
		void end_display();
		//end

		// called when a flash callback occours on current movie
		virtual void OnMovieCallback(const char* command, const char* args);
	private:
	};
	//---------------------------------------------------------
}

#endif //__SwfBaseMovie_h__25_6_2011__4_03_04__
