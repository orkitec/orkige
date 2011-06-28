/**************************************************************
	created:	2011/06/26 at 19:11
	filename: 	SwfHUDMovie.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfHUDMovie_h__26_6_2011__19_11_11__
#define __SwfHUDMovie_h__26_6_2011__19_11_11__

#include "engine_swf/SwfApiDefs.h"
#include "engine_swf/SwfBaseMovie.h"

/*#include <OgreAxisAlignedBox.h>*/
#include <OgreCamera.h>

namespace Orkige
{
	class SwfHudMovie : public SwfBaseMovie
	{
		friend class SwfMovieManager;
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		//viewport movie position
		float left;
		float top;

		//viewport movie scale
		float scaleX;
		float scaleY;

		float widthConst;
		float heightConst;

		Ogre::Camera* currentCamera;
	private:
		//--- Methods -----------------------------------------
	public:
		SwfHudMovie(const Ogre::String & file, const Ogre::String & name, Ogre::Camera* cam);
		~SwfHudMovie(void);

		void setPosition(Ogre::Real x, Ogre::Real y);
		void setScale(Ogre::Real x, Ogre::Real y);

		//the cam at witch the gui is targeted
		inline void setCurrentCamera(Ogre::Camera* cam);
		virtual void setMouseSettings(float x, float y, int mouseMask);
	protected:
	private:
		void update(Ogre::Real timeSinceLastFrame);

		//target calls from GameSWF (redirected by SwfMovieManager)
		void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset);
		//end

		/*Ogre::AxisAlignedBox infiniteBox;*/
	};
	//---------------------------------------------------------
	void SwfHudMovie::setCurrentCamera(Ogre::Camera* cam)
	{
		this->currentCamera = cam;
	}
}

#endif //__SwfHUDMovie_h__26_6_2011__19_11_11__
