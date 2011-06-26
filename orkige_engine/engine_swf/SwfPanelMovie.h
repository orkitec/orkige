/**************************************************************
	created:	2011/06/26 at 20:45
	filename: 	SwfPanelMovie.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfPanelMovie_h__26_6_2011__20_45_18__
#define __SwfPanelMovie_h__26_6_2011__20_45_18__

#include "engine_swf/SwfApiDefs.h"
#include "engine_swf/SwfBaseMovie.h"

namespace Orkige
{
	class SwfPanelMovie  : public SwfBaseMovie
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		SwfPanelMovie(const Ogre::String & file, const Ogre::String & name);
		~SwfPanelMovie(void);
	protected:
	private:
		//target calls from GameSWF (redirected by SwfMovieManager)
		void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset);
		//end

		void _setBounds(Ogre::ManualObject * obj, const Ogre::AxisAlignedBox& bounds, bool pad = true);
	};
	//---------------------------------------------------------
}

#endif //__SwfPanelMovie_h__26_6_2011__20_45_18__
