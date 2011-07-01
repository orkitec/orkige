/**************************************************************
	created:	2011/06/25 at 4:24
	filename: 	SwfFileHelper.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_swf/SwfFileHelper.h"

namespace Orkige
{
	// Return the number of bytes actually read.  EOF or an error would
	// cause that to not be equal to "bytes".
	/*static */int ogre_read_func(void* dst, int bytes, void* appdata) 
	{
		assert(appdata);
		assert(dst);
		Ogre::DataStream* stream = static_cast<Ogre::DataStream*>(appdata);
		assert(stream);
		return stream->read(dst,bytes);
	}

	// -------------------------------------------------------------------------------
	// Return the number of bytes actually written.
	/*static */int ogre_write_func(const void* src, int bytes, void* appdata)
	{
		assert(appdata);
		assert(src);
		Ogre::DataStream* stream = static_cast<Ogre::DataStream*>(appdata);
		assert(stream);
		assert(!"writing to ogre archives is not allowed!");
		return 0;
	}

	// -------------------------------------------------------------------------------
	// Return 0 on success, or TU_FILE_SEEK_ERROR on failure.
	/*static */int ogre_seek_func(int pos, void *appdata)
	{
		assert(appdata);
		Ogre::DataStream* stream = static_cast<Ogre::DataStream*>(appdata);
		assert(stream);
		stream->seek(pos);
		if (stream->eof())
		{
			// @@ TODO should set m_error to something relevant based on errno.
			return TU_FILE_SEEK_ERROR;
		}
		return 0;
	}

	// -------------------------------------------------------------------------------
	// Return 0 on success, TU_FILE_SEEK_ERROR on failure.
	/*static */int ogre_seek_to_end_func(void *appdata)
	{
		assert(appdata);
		Ogre::DataStream* stream = static_cast<Ogre::DataStream*>(appdata);
		assert(stream);
		stream->seek(stream->size());
		if (stream->eof())
		{
			// @@ TODO should set m_error to something relevant based on errno.
			return TU_FILE_SEEK_ERROR;
		}
		return 0;
	}

	// -------------------------------------------------------------------------------
	// Return the file position, or -1 on failure.
	/*static */int ogre_tell_func(const void *appdata)
	{
		assert(appdata);
		const Ogre::DataStream* stream = static_cast<const Ogre::DataStream*>(appdata);
		assert(stream);
		return stream->tell();
	}

	// -------------------------------------------------------------------------------
	// Return true if we're at EOF.
	/*static */bool ogre_get_eof_func(void *appdata)
	{
		assert(appdata);
		Ogre::DataStream* stream = static_cast<Ogre::DataStream*>(appdata);
		assert(stream);
		return stream->eof();
	}

	// -------------------------------------------------------------------------------
	// Return 0 on success, or TU_FILE_CLOSE_ERROR on failure.
	/*static */int ogre_close_func(void *appdata)
	{
		assert(appdata);
		Ogre::DataStream* stream = static_cast<Ogre::DataStream*>(appdata);
		assert(stream);
		stream->close();
		delete stream;
		/*
		if (stream->eof())
		{
		// @@ TODO should set m_error to something relevant based on errno.
		return TU_FILE_CLOSE_ERROR;
		}*/

		return 0;//Hm? is there any check we can do here?
	}

	// -------------------------------------------------------------------------------
	// Callback function.  This opens files for the gameswf library.
	/*static */tu_file*	ogre_file_opener(const char* url)
	{

		Ogre::DataStreamPtr fileStream = Ogre::ResourceGroupManager::getSingleton().openResource(url); // open the script file
		(*fileStream.useCountPointer())++;//hackyhacky increment stream refcount so it doesn't get destroyed in function scope we delete the pointer in ogre_close_func
		Ogre::DataStream* stream = fileStream.get();
		tu_file* file = new tu_file(stream,ogre_read_func,ogre_write_func,ogre_seek_func,ogre_seek_to_end_func,ogre_tell_func,ogre_get_eof_func,ogre_close_func);
		return file;

		/*
		FILE * ffile = fopen (url,"rb"); 
		tu_file* file = new tu_file(ffile, true); 
		return file; */
	}
};
