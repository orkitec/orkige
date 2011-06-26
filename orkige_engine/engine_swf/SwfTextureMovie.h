/**************************************************************
	created:	2011/06/26 at 20:49
	filename: 	SwfTextureMovie.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfTextureMovie_h__26_6_2011__20_49_10__
#define __SwfTextureMovie_h__26_6_2011__20_49_10__

#include "engine_swf/SwfApiDefs.h"
#include "engine_swf/SwfBaseMovie.h"

namespace Orkige
{
	class SwfTextureMovie : public SwfBaseMovie
	{
		//--- Types -------------------------------------------
	public:
	protected:
		float widthConst;
		float heightConst;

		//things for rendering to the rendertexture
		Ogre::Camera*        camera;
		Ogre::RenderTexture* renderTexture;
		Ogre::Viewport*      viewport;
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		SwfTextureMovie(const Ogre::String & file, const Ogre::String & name, unsigned int width, unsigned int height);
		~SwfTextureMovie(void);

		void setResolution(unsigned int width, unsigned int height);
	protected:
	private:
		void update(Ogre::Real timeSinceLastFrame);

		//target calls from GameSWF (redirected by SwfMovieManager)
		void begin_display(float red, float green, float blue);
		void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset);

		//end
	};
	//---------------------------------------------------------
}

#endif //__SwfTextureMovie_h__26_6_2011__20_49_10__
