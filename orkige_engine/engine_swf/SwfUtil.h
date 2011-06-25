/**************************************************************
	created:	2011/06/25 at 3:53
	filename: 	SwfUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SwfUtil_h__25_6_2011__3_53_21__
#define __SwfUtil_h__25_6_2011__3_53_21__

#include "engine_swf/SwfApiDefs.h"

namespace Orkige
{
	// Attempt to read the header of the given .swf movie file.
	// Put extracted info in the given vars.
	// Sets *version to 0 if info can't be extracted.
	//void	gameswf::get_movie_info(const char* filename, int* version, int* width, int* height, 
	//	float* frames_per_second, int* frame_count, int* tag_count )

	// Process a log message.
	/*static */void	message_log(const char* message);

	// Process a log message.
	/*static */void	message_error(const char* message);

	// Error callback for handling gameswf messages.
	/*static */void	log_callback(bool error, const char* message);

	// For handling notification callbacks from ActionScript.
	/*static */void	fs_callback(gameswf::character* movie, const char* command, const char* args);
};

#endif //__SwfUtil_h__25_6_2011__3_53_21__
