/*
-----------------------------------------------------------------------------
This source file is part of Orkige.
A Flash movie rendering addon to the Ogre3D engine

Copyright (c) 2008 Steffen Römer aka. MorrK

Orkige was forked from ogreSWF
ogreSWF: Copyright (c) 2000-2006 Wolfgang Steiner (AUT) aka. stoneCold

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place - Suite 330, Boston, MA 02111-1307, USA, or go to
http://www.gnu.org/copyleft/lesser.txt.
-----------------------------------------------------------------------------
*/
//created 2008/09/21
#ifndef __OGRESWFPANEL_H__
#define __OGRESWFPANEL_H__

#include "engine_swf/SwfApiDefs.h"
#include "engine_swf/SwfBaseMovie.h"

namespace Orkige
{
	class PanelMovie  : public SwfBaseMovie
	{
		// Attributes --------------------------------------------------------------
	public:
	protected:
	private:
		// Methods -----------------------------------------------------------------
	public:
		PanelMovie(const Ogre::String & file, const Ogre::String & name);
		~PanelMovie(void);

	private:
		//target calls from GameSWF (redirected by SwfMovieManager)
		void draw_mesh_strip(const void* coords, int vertex_count, float transform[16], float xOffset, float yOffset);
		//end

		void _setBounds(Ogre::ManualObject * obj, const Ogre::AxisAlignedBox& bounds, bool pad = true);

	};

};
#endif //__OGRESWFPANEL_H__