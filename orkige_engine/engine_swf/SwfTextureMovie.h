//created 2008/09/21
#ifndef __OGRESWFTEXTURE_H__
#define __OGRESWFTEXTURE_H__

#include "engine_swf/SwfApiDefs.h"
#include "engine_swf/SwfBaseMovie.h"

namespace Orkige
{
	class SwfTextureMovie : public SwfBaseMovie
	{
		// Attributes --------------------------------------------------------------
	public:
	protected:
		float widthConst;
		float heightConst;

		//things for rendering to the rendertexture
		Ogre::Camera*        camera;
		Ogre::RenderTexture* renderTexture;
		Ogre::Viewport*      viewport;
	private:
		// Methods -----------------------------------------------------------------
	public:
		SwfTextureMovie(const Ogre::String & file, const Ogre::String & name, unsigned int width, unsigned int height);
		~SwfTextureMovie(void);

		void setResolution(unsigned int width, unsigned int height);

	private:
		void update(Ogre::Real timeSinceLastFrame);

		//target calls from GameSWF (redirected by SwfMovieManager)
		void begin_display(float red, float green, float blue);
		void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset);

		//end
	};
};
#endif //__OGRESWFTEXTURE_H__