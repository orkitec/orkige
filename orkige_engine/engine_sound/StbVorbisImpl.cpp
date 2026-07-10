/********************************************************************
	created:	Friday 2026/07/10 at 21:00
	filename: 	StbVorbisImpl.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The ONE translation unit that compiles the single-file Vorbis decoder.
	Nothing else in the tree includes it, so the decoder stays out of every
	header, the neutral umbrella and the precompiled header. MusicStream and
	the tests reach Vorbis only through the MusicDecode seam declared in
	MusicStream.h.
*********************************************************************/
#ifdef ORKIGE_OPENAL_SOUND

#include "engine_sound/MusicStream.h"

// the decoder's own API (no pushdata/pulldata file helpers needed - we feed it
// the resident compressed bytes through the memory API only)
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API

// the vendored single-file decoder is pure C and trips a couple of the tree's
// warnings-as-behaviour flags; silence them locally without editing the file
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wtautological-compare"
#	pragma clang diagnostic ignored "-Wunused-function"
#	pragma clang diagnostic ignored "-Wunused-but-set-variable"
#	pragma clang diagnostic ignored "-Wcast-qual"
#	pragma clang diagnostic ignored "-Wcomma"
#elif defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#	pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#include "stb_vorbis.c"

#if defined(__clang__)
#	pragma clang diagnostic pop
#elif defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif

namespace Orkige
{
	namespace MusicDecode
	{
		//----------------------------------------------------
		void* open(unsigned char const * data, int size, Info * info)
		{
			if (data == NULL || size <= 0)
			{
				return NULL;
			}
			int error = 0;
			stb_vorbis * decoder =
				stb_vorbis_open_memory(data, size, &error, NULL);
			if (decoder == NULL)
			{
				return NULL;
			}
			if (info != NULL)
			{
				const stb_vorbis_info raw = stb_vorbis_get_info(decoder);
				info->channels = raw.channels;
				info->sampleRate = static_cast<int>(raw.sample_rate);
				info->durationSeconds =
					stb_vorbis_stream_length_in_seconds(decoder);
			}
			return decoder;
		}
		//----------------------------------------------------
		int read(void * handle, short * out, int maxFrames, int channels)
		{
			if (handle == NULL || out == NULL || maxFrames <= 0)
			{
				return 0;
			}
			stb_vorbis * decoder = static_cast<stb_vorbis *>(handle);
			// the interleaved reader takes a total-shorts capacity and returns
			// the number of frames (samples per channel) it produced
			return stb_vorbis_get_samples_short_interleaved(
				decoder, channels, out, maxFrames * channels);
		}
		//----------------------------------------------------
		void seekStart(void * handle)
		{
			if (handle != NULL)
			{
				stb_vorbis_seek_start(static_cast<stb_vorbis *>(handle));
			}
		}
		//----------------------------------------------------
		float length(void * handle)
		{
			if (handle == NULL)
			{
				return 0.0f;
			}
			return stb_vorbis_stream_length_in_seconds(
				static_cast<stb_vorbis *>(handle));
		}
		//----------------------------------------------------
		void close(void * handle)
		{
			if (handle != NULL)
			{
				stb_vorbis_close(static_cast<stb_vorbis *>(handle));
			}
		}
	}
}

#endif //ORKIGE_OPENAL_SOUND
