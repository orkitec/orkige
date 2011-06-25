/**************************************************************
	created:	2011/06/25 at 4:14
	filename: 	SwfBitmapInfo.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfBitmapInfo_h__25_6_2011__4_14_16__
#define __SwfBitmapInfo_h__25_6_2011__4_14_16__

#include "engine_swf/SwfApiDefs.h"

namespace Orkige
{
	// bitmap_info_ogre declaration
	struct SwfBitmapInfo : public gameswf::bitmap_info
	{
		//--- Types -------------------------------------------

		//--- Variables ---------------------------------------
		unsigned int	textureId;		
		int				width;	
		int				height;	

		//--- Methods -----------------------------------------
		// Make a placeholder bitmap_info.  Must be filled in later before
		// using.
		SwfBitmapInfo();
		// Initialize this bitmap_info to an alpha image
		// containing the specified data (1 byte per texel).
		//
		// !! Munges *data in order to create mipmaps !!
		SwfBitmapInfo(int width, int height, Uint8* data);
		SwfBitmapInfo(image::rgb* im);
		// Version of the constructor that takes an image with alpha.
		SwfBitmapInfo(image::rgba* im);

		virtual int get_width() const;
		virtual int get_height() const;
	};
	//---------------------------------------------------------
}

#endif //__SwfBitmapInfo_h__25_6_2011__4_14_16__
