/**************************************************************
	created:	2011/06/25 at 4:23
	filename: 	SwfFileHelper.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfFileHelper_h__25_6_2011__4_23_53__
#define __SwfFileHelper_h__25_6_2011__4_23_53__

#include "engine_swf/SwfApiDefs.h"

namespace Orkige
{
	// Return the number of bytes actually read.  EOF or an error would
	// cause that to not be equal to "bytes".
	/*static */int ogre_read_func(void* dst, int bytes, void* appdata);
	// Return the number of bytes actually written.
	/*static */int ogre_write_func(const void* src, int bytes, void* appdata);

	// Return 0 on success, or TU_FILE_SEEK_ERROR on failure.
	/*static */int ogre_seek_func(int pos, void *appdata);

	// Return 0 on success, TU_FILE_SEEK_ERROR on failure.
	/*static */int ogre_seek_to_end_func(void *appdata);

	// Return the file position, or -1 on failure.
	/*static */int ogre_tell_func(const void *appdata);

	// Return true if we're at EOF.
	/*static */bool ogre_get_eof_func(void *appdata);

	// Return 0 on success, or TU_FILE_CLOSE_ERROR on failure.
	/*static */int ogre_close_func(void *appdata);

	// Callback function.  This opens files for the gameswf library.
	/*static */tu_file*	/**/ogre_file_opener(const char* url);
};

#endif //__SwfFileHelper_h__25_6_2011__4_23_53__
