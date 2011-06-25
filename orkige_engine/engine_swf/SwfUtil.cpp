/**************************************************************
	created:	2011/06/25 at 3:58
	filename: 	SwfUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_swf/SwfMovieManager.h"
#include <gameswf/gameswf_stream.h>
#include <base/tu_file.h>
#include <base/zlib_adapter.h>

#include <Ogre.h>

#include "engine_swf/SwfUtil.h"
#include "engine_swf/SwfFileHelper.h"

void	gameswf::get_movie_info(
					   const char* filename,
					   int* version,
					   int* width,
					   int* height,
					   float* frames_per_second,
					   int* frame_count,
					   int* tag_count
					   )
					   // Attempt to read the header of the given .swf movie file.
					   // Put extracted info in the given vars.
					   // Sets *version to 0 if info can't be extracted.
{
	tu_file* in = Orkige::ogre_file_opener(filename);

	if (in == NULL || in->get_error() != TU_FILE_NO_ERROR)
	{
		std::stringstream sstr;
		sstr << "error: get_movie_info(): can't open "<< filename;
		Orkige::log_callback(true,sstr.str().c_str());
		if (version) *version = 0;
		delete in;
		return;
	}

	Uint32	file_start_pos = in->get_position();
	Uint32	header = in->read_le32();
	Uint32	file_length = in->read_le32();
	Uint32	file_end_pos = file_start_pos + file_length;

	int	local_version = (header >> 24) & 255;
	if ((header & 0x0FFFFFF) != 0x00535746
		&& (header & 0x0FFFFFF) != 0x00535743)
	{
		// ERROR
		std::stringstream sstr;
		sstr << "error: get_movie_info(): file '"<< filename<<"' does not start with a SWF header!\n";
		Orkige::log_callback(true,sstr.str().c_str());
		if (version) *version = 0;
		delete in;
		return;
	}
	bool	compressed = (header & 255) == 'C';

	tu_file*	original_in = NULL;
	if (compressed)
	{
#if TU_CONFIG_LINK_TO_ZLIB == 0
		log_error("get_movie_info(): can't read zipped SWF data; TU_CONFIG_LINK_TO_ZLIB is 0!\n");
		return;
#endif
		original_in = in;

		// Uncompress the input as we read it.
		in = zlib_adapter::make_inflater(original_in);

		// Subtract the size of the 8-byte header, since
		// it's not included in the compressed
		// stream length.
		file_end_pos = file_length - 8;
	}

	stream	str(in);

	rect	frame_size;
	frame_size.read(&str);

	float	local_frame_rate = str.read_u16() / 256.0f;
	int	local_frame_count = str.read_u16();

	if (version) *version = local_version;
	if (width) *width = int(frame_size.width() / 20.0f + 0.5f);
	if (height) *height = int(frame_size.height() / 20.0f + 0.5f);
	if (frames_per_second) *frames_per_second = local_frame_rate;
	if (frame_count) *frame_count = local_frame_count;

	if (tag_count)
	{
		// Count tags.
		int local_tag_count = 0;
		while ((Uint32) str.get_position() < file_end_pos)
		{
			int tag_type = str.open_tag();
			UNUSED(tag_type);
			str.close_tag();
			local_tag_count++;
		}
		*tag_count = local_tag_count;
	}

	delete in;
	delete original_in;
}
// -------------------------------------------------------------------------------
namespace Orkige
{
	// -------------------------------------------------------------------------------
	// Process a log message.
	/*static */void	message_log(const char* message)
	{
		Ogre::LogManager::getSingleton().logMessage(Ogre::LML_NORMAL,message);
	}

	// -------------------------------------------------------------------------------
	// Process a error message.
	/*static */void	message_error(const char* message)
	{
		Ogre::LogManager::getSingleton().logMessage(Ogre::LML_CRITICAL,message);
	}

	// -------------------------------------------------------------------------------
	// Error callback for handling gameswf messages.
	/*static */void	log_callback(bool error, const char* message)
	{
		if (error)
		{
			// Log, and also print to stderr.
			message_error(message);
		}
		else
		{
			message_log(message);
		}
	}

	// -------------------------------------------------------------------------------
	// For handling notification callbacks from ActionScript.
	/*static */void	fs_callback(gameswf::character* movie, const char* command, const char* args)
	{
		//message_log("fs_callback: '");
		//message_log(command);
		//message_log("' '");
		//message_log(args);
		//message_log("'\n");

		SwfMovieManager::getSingleton().OnMovieCallback(movie, command, args);
	}
}
