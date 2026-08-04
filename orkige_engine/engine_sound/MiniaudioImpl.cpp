/********************************************************************
	created:	Tuesday 2026/08/04 at 09:00
	filename: 	MiniaudioImpl.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The ONE translation unit that compiles the single-file audio library
	behind AudioBackend. Nothing else in the tree includes it, so it stays
	out of every header, the neutral umbrella and the precompiled header -
	the same containment StbVorbisImpl.cpp keeps around the Vorbis decoder.
*********************************************************************/

// the library is included FIRST: on Windows its implementation pulls the
// platform headers in, and those want to be the first thing a translation
// unit sees.
//
// The feature set is trimmed to what this engine actually asks for. The
// engine DECODES ITS OWN AUDIO (wav, caf, ogg vorbis, and the synthesized
// procedural effects) and hands the library finished PCM, so its file
// decoders, its encoders, its waveform generators and the resource manager
// that would drive them are all switched off - one decode path, not two.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wunused-function"
#	pragma clang diagnostic ignored "-Wunused-but-set-variable"
#	pragma clang diagnostic ignored "-Wcast-qual"
#	pragma clang diagnostic ignored "-Wcomma"
#	pragma clang diagnostic ignored "-Wtautological-compare"
#elif defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#	pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#include <miniaudio.h>

#if defined(__clang__)
#	pragma clang diagnostic pop
#elif defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif

#include "engine_sound/AudioBackend.h"

#include <atomic>
#include <cstring>
#include <new>

namespace Orkige
{
	namespace AudioBackend
	{
		namespace
		{
			//! the process-wide graph: one device, one mixer, one listener
			ma_context		gContext;
			ma_engine		gEngine;
			bool			gContextReady = false;
			bool			gEngineReady = false;

			//---------------------------------------------------------
			//! our PCM vocabulary -> the library's sample format
			bool sampleFormat(int bitsPerSample, ma_format & out)
			{
				if(bitsPerSample == 8)
				{
					out = ma_format_u8;
					return true;
				}
				if(bitsPerSample == 16)
				{
					out = ma_format_s16;
					return true;
				}
				return false;
			}
			//---------------------------------------------------------
			//! one fully buffered clip: the samples plus the voice reading them
			struct VoiceImpl
			{
				ma_audio_buffer	buffer;
				ma_sound		sound;
				bool			soundReady;
			};
			//---------------------------------------------------------
			//! @brief one streamed voice: a lock-free ring the MAIN thread
			//! fills and the mixer drains, wrapped as a data source.
			//! @remarks the wrapper (rather than handing the ring itself to
			//! the voice) buys the two facts a track needs and a bare ring
			//! cannot give: an END once a drained ring empties, so a
			//! non-looping track stops by itself, and a count of the frames
			//! the mixer really consumed, which is the playhead.
			struct StreamImpl
			{
				ma_data_source_base		base;
				ma_pcm_rb				ring;
				ma_sound				sound;
				ma_uint32				channels;
				ma_uint32				sampleRate;
				std::atomic<long long>	consumed;
				std::atomic<bool>		drained;
				bool					ringReady;
				bool					soundReady;
			};
			//---------------------------------------------------------
			ma_result streamOnRead(ma_data_source * dataSource, void * framesOut,
				ma_uint64 frameCount, ma_uint64 * framesRead)
			{
				StreamImpl * stream = static_cast<StreamImpl *>(dataSource);
				const ma_uint32 bytesPerFrame =
					ma_get_bytes_per_frame(ma_format_s16, stream->channels);
				ma_uint64 taken = 0;
				while(taken < frameCount)
				{
					ma_uint64 want = frameCount - taken;
					if(want > 0xFFFFFFFFu)
					{
						want = 0xFFFFFFFFu;
					}
					ma_uint32 mapped = static_cast<ma_uint32>(want);
					void * source = NULL;
					if(ma_pcm_rb_acquire_read(&stream->ring, &mapped, &source)
						!= MA_SUCCESS || mapped == 0)
					{
						break;
					}
					std::memcpy(static_cast<unsigned char *>(framesOut) +
						taken * bytesPerFrame, source,
						static_cast<std::size_t>(mapped) * bytesPerFrame);
					ma_pcm_rb_commit_read(&stream->ring, mapped);
					taken += mapped;
				}
				// only REAL frames move the playhead - silence played through
				// an underrun is a gap, not progress
				stream->consumed.fetch_add(static_cast<long long>(taken),
					std::memory_order_relaxed);
				*framesRead = taken;
				if(taken < frameCount)
				{
					if(stream->drained.load(std::memory_order_acquire))
					{
						// nothing more will ever arrive: end the voice
						return MA_AT_END;
					}
					// an underrun costs a gap of silence, never the track
					ma_silence_pcm_frames(
						static_cast<unsigned char *>(framesOut) +
						taken * bytesPerFrame, frameCount - taken,
						ma_format_s16, stream->channels);
					*framesRead = frameCount;
				}
				return MA_SUCCESS;
			}
			//---------------------------------------------------------
			ma_result streamOnGetDataFormat(ma_data_source * dataSource,
				ma_format * format, ma_uint32 * channels, ma_uint32 * sampleRate,
				ma_channel * channelMap, size_t channelMapCap)
			{
				StreamImpl * stream = static_cast<StreamImpl *>(dataSource);
				if(format != NULL)
				{
					*format = ma_format_s16;
				}
				if(channels != NULL)
				{
					*channels = stream->channels;
				}
				if(sampleRate != NULL)
				{
					*sampleRate = stream->sampleRate;
				}
				if(channelMap != NULL)
				{
					ma_channel_map_init_standard(ma_standard_channel_map_default,
						channelMap, channelMapCap, stream->channels);
				}
				return MA_SUCCESS;
			}
			//---------------------------------------------------------
			ma_data_source_vtable gStreamVTable =
			{
				streamOnRead,
				NULL,	// onSeek - a live stream has no random access
				streamOnGetDataFormat,
				NULL,	// onGetCursor - the consumed count is the playhead
				NULL,	// onGetLength - a stream has no length
				NULL,	// onSetLooping - looping is the decoder's job
				0
			};
			//---------------------------------------------------------
			VoiceImpl * asVoice(Voice voice)
			{
				return static_cast<VoiceImpl *>(voice);
			}
			//---------------------------------------------------------
			StreamImpl * asStream(Stream stream)
			{
				return static_cast<StreamImpl *>(stream);
			}
		}
		//---------------------------------------------------------
		//--- the device + the mixing graph ------------------------
		//---------------------------------------------------------
		bool engineInit(bool silent)
		{
			// a graph left open would keep its device thread running
			engineUninit();

			ma_context_config contextConfig = ma_context_config_init();
#if defined(MA_APPLE_MOBILE)
			// AMBIENT: a game's audio is not the user's primary media, so it
			// mixes with whatever they are already playing and honours the
			// ringer switch. This is the engine default; the library sets the
			// category and activates the session before it touches the audio
			// hardware, which is what keeps an unactivated session from
			// deadlocking the audio server on a simulator.
			contextConfig.coreaudio.sessionCategory =
				ma_ios_session_category_ambient;
#endif
			ma_result result;
			if(silent)
			{
				// the SILENT device: it consumes samples in real time, so
				// playheads advance and streams drain exactly as they would
				// audibly, with nothing reaching the speakers
				const ma_backend backends[] = { ma_backend_null };
				result = ma_context_init(backends, 1, &contextConfig, &gContext);
			}
			else
			{
				result = ma_context_init(NULL, 0, &contextConfig, &gContext);
			}
			if(result != MA_SUCCESS)
			{
				oDebugMsg("sound", 0, "no audio: the audio backend could not "
					"be opened - continuing without sound");
				return false;
			}
			gContextReady = true;

			ma_engine_config engineConfig = ma_engine_config_init();
			engineConfig.pContext = &gContext;
			if(ma_engine_init(&engineConfig, &gEngine) != MA_SUCCESS)
			{
				oDebugMsg("sound", 0, "no audio: the output device could not "
					"be opened - continuing without sound");
				ma_context_uninit(&gContext);
				gContextReady = false;
				return false;
			}
			gEngineReady = true;
			return true;
		}
		//---------------------------------------------------------
		void engineUninit()
		{
			if(gEngineReady)
			{
				ma_engine_uninit(&gEngine);
				gEngineReady = false;
			}
			if(gContextReady)
			{
				ma_context_uninit(&gContext);
				gContextReady = false;
			}
		}
		//---------------------------------------------------------
		bool engineReady()
		{
			return gEngineReady;
		}
		//---------------------------------------------------------
		void engineSetMasterVolume(float volume)
		{
			if(gEngineReady)
			{
				ma_engine_set_volume(&gEngine, volume);
			}
		}
		//---------------------------------------------------------
		void engineSetListener(float posX, float posY, float posZ,
			float forwardX, float forwardY, float forwardZ,
			float upX, float upY, float upZ)
		{
			if(!gEngineReady)
			{
				return;
			}
			ma_engine_listener_set_position(&gEngine, 0, posX, posY, posZ);
			ma_engine_listener_set_direction(&gEngine, 0,
				forwardX, forwardY, forwardZ);
			ma_engine_listener_set_world_up(&gEngine, 0, upX, upY, upZ);
		}
		//---------------------------------------------------------
		//--- a fully buffered voice -------------------------------
		//---------------------------------------------------------
		Voice voiceCreate(void const * pcm, int byteSize,
			PcmFormat const & format, bool loop)
		{
			ma_format sampleType = ma_format_unknown;
			if(!gEngineReady || pcm == NULL || byteSize <= 0 ||
				format.channels <= 0 || format.sampleRate <= 0 ||
				!sampleFormat(format.bitsPerSample, sampleType))
			{
				return NULL;
			}
			const ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(sampleType,
				static_cast<ma_uint32>(format.channels));
			if(bytesPerFrame == 0)
			{
				return NULL;
			}
			const ma_uint64 frames = static_cast<ma_uint64>(byteSize) /
				bytesPerFrame;
			if(frames == 0)
			{
				return NULL;
			}

			VoiceImpl * voice = new (std::nothrow) VoiceImpl();
			if(voice == NULL)
			{
				return NULL;
			}
			voice->soundReady = false;

			// the samples are COPIED into the voice, so the caller may free
			// its own block right after this returns
			ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
				sampleType, static_cast<ma_uint32>(format.channels), frames,
				pcm, NULL);
			bufferConfig.sampleRate = static_cast<ma_uint32>(format.sampleRate);
			if(ma_audio_buffer_init_copy(&bufferConfig, &voice->buffer)
				!= MA_SUCCESS)
			{
				delete voice;
				return NULL;
			}

			// a MONO clip is a point in the world; a stereo one is a finished
			// mix and plays as authored
			ma_uint32 flags = 0;
			if(format.channels != 1)
			{
				flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
			}
			if(ma_sound_init_from_data_source(&gEngine, &voice->buffer, flags,
				NULL, &voice->sound) != MA_SUCCESS)
			{
				ma_audio_buffer_uninit(&voice->buffer);
				delete voice;
				return NULL;
			}
			voice->soundReady = true;
			ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
			return voice;
		}
		//---------------------------------------------------------
		void voiceDestroy(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice == NULL)
			{
				return;
			}
			if(voice->soundReady)
			{
				// detaches the voice from the graph and waits for the mixer
				// to be done with it, so the samples below are free to go
				ma_sound_uninit(&voice->sound);
			}
			ma_audio_buffer_uninit(&voice->buffer);
			delete voice;
		}
		//---------------------------------------------------------
		bool voiceStart(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			return voice != NULL &&
				ma_sound_start(&voice->sound) == MA_SUCCESS;
		}
		//---------------------------------------------------------
		bool voiceStop(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice == NULL)
			{
				return false;
			}
			const bool stopped = ma_sound_stop(&voice->sound) == MA_SUCCESS;
			// a stop rewinds: the next start plays from the first sample
			ma_sound_seek_to_pcm_frame(&voice->sound, 0);
			return stopped;
		}
		//---------------------------------------------------------
		bool voiceSuspend(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			return voice != NULL &&
				ma_sound_stop(&voice->sound) == MA_SUCCESS;
		}
		//---------------------------------------------------------
		bool voiceIsPlaying(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			// a voice that ran off the end of its samples is finished, even
			// though the graph has not swept its node yet
			return voice != NULL &&
				ma_sound_is_playing(&voice->sound) == MA_TRUE &&
				ma_sound_at_end(&voice->sound) == MA_FALSE;
		}
		//---------------------------------------------------------
		void voiceSetVolume(Voice handle, float volume)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice != NULL)
			{
				ma_sound_set_volume(&voice->sound, volume);
			}
		}
		//---------------------------------------------------------
		void voiceSetPitch(Voice handle, float pitch)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice != NULL)
			{
				ma_sound_set_pitch(&voice->sound, pitch);
			}
		}
		//---------------------------------------------------------
		float voiceGetPitch(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			return voice != NULL ? ma_sound_get_pitch(&voice->sound) : 0.f;
		}
		//---------------------------------------------------------
		void voiceSetPosition(Voice handle, float x, float y, float z)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice != NULL)
			{
				ma_sound_set_position(&voice->sound, x, y, z);
			}
		}
		//---------------------------------------------------------
		bool voiceIsSpatialized(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			return voice != NULL &&
				ma_sound_is_spatialization_enabled(&voice->sound) == MA_TRUE;
		}
		//---------------------------------------------------------
		void voiceSetAttenuation(Voice handle, float refDistance,
			float maxDistance, float rolloff)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice == NULL)
			{
				return;
			}
			// the inverse-distance curve, clamped at both ends:
			// gain = ref / (ref + rolloff * (clamp(d, ref, max) - ref))
			ma_sound_set_attenuation_model(&voice->sound,
				ma_attenuation_model_inverse);
			ma_sound_set_min_distance(&voice->sound, refDistance);
			ma_sound_set_max_distance(&voice->sound, maxDistance);
			ma_sound_set_rolloff(&voice->sound, rolloff);
		}
		//---------------------------------------------------------
		float voiceGetCursorSeconds(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			float cursor = 0.f;
			if(voice != NULL &&
				ma_sound_get_cursor_in_seconds(&voice->sound, &cursor)
				!= MA_SUCCESS)
			{
				return 0.f;
			}
			return cursor;
		}
		//---------------------------------------------------------
		void voiceSeekSeconds(Voice handle, float seconds)
		{
			VoiceImpl * voice = asVoice(handle);
			if(voice != NULL)
			{
				ma_sound_seek_to_second(&voice->sound, seconds);
			}
		}
		//---------------------------------------------------------
		int voiceFrameCount(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			ma_uint64 length = 0;
			if(voice == NULL || ma_data_source_get_length_in_pcm_frames(
				&voice->buffer, &length) != MA_SUCCESS)
			{
				return 0;
			}
			return static_cast<int>(length);
		}
		//---------------------------------------------------------
		int voiceSampleRate(Voice handle)
		{
			VoiceImpl * voice = asVoice(handle);
			ma_format format = ma_format_unknown;
			ma_uint32 channels = 0;
			ma_uint32 rate = 0;
			if(voice == NULL || ma_data_source_get_data_format(&voice->buffer,
				&format, &channels, &rate, NULL, 0) != MA_SUCCESS)
			{
				return 0;
			}
			return static_cast<int>(rate);
		}
		//---------------------------------------------------------
		//--- a streamed voice fed from the MAIN thread ------------
		//---------------------------------------------------------
		Stream streamCreate(int channels, int sampleRate, int ringFrames)
		{
			if(!gEngineReady || channels <= 0 || sampleRate <= 0 ||
				ringFrames <= 0)
			{
				return NULL;
			}
			StreamImpl * stream = new (std::nothrow) StreamImpl();
			if(stream == NULL)
			{
				return NULL;
			}
			stream->channels = static_cast<ma_uint32>(channels);
			stream->sampleRate = static_cast<ma_uint32>(sampleRate);
			stream->consumed.store(0, std::memory_order_relaxed);
			stream->drained.store(false, std::memory_order_relaxed);
			stream->ringReady = false;
			stream->soundReady = false;

			ma_data_source_config sourceConfig = ma_data_source_config_init();
			sourceConfig.vtable = &gStreamVTable;
			if(ma_data_source_init(&sourceConfig, &stream->base) != MA_SUCCESS)
			{
				delete stream;
				return NULL;
			}
			if(ma_pcm_rb_init(ma_format_s16, stream->channels,
				static_cast<ma_uint32>(ringFrames), NULL, NULL, &stream->ring)
				!= MA_SUCCESS)
			{
				ma_data_source_uninit(&stream->base);
				delete stream;
				return NULL;
			}
			stream->ringReady = true;

			// a streamed track is a 2D mix, never a point in the world, and
			// it is never pitch-shifted
			const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION |
				MA_SOUND_FLAG_NO_PITCH;
			if(ma_sound_init_from_data_source(&gEngine, &stream->base, flags,
				NULL, &stream->sound) != MA_SUCCESS)
			{
				ma_pcm_rb_uninit(&stream->ring);
				ma_data_source_uninit(&stream->base);
				delete stream;
				return NULL;
			}
			stream->soundReady = true;
			return stream;
		}
		//---------------------------------------------------------
		void streamDestroy(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			if(stream == NULL)
			{
				return;
			}
			if(stream->soundReady)
			{
				ma_sound_uninit(&stream->sound);
			}
			if(stream->ringReady)
			{
				ma_pcm_rb_uninit(&stream->ring);
			}
			ma_data_source_uninit(&stream->base);
			delete stream;
		}
		//---------------------------------------------------------
		int streamWritableFrames(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			if(stream == NULL)
			{
				return 0;
			}
			return static_cast<int>(ma_pcm_rb_available_write(&stream->ring));
		}
		//---------------------------------------------------------
		int streamWrite(Stream handle, short const * frames, int frameCount)
		{
			StreamImpl * stream = asStream(handle);
			if(stream == NULL || frames == NULL || frameCount <= 0)
			{
				return 0;
			}
			const ma_uint32 bytesPerFrame =
				ma_get_bytes_per_frame(ma_format_s16, stream->channels);
			int written = 0;
			// the ring hands back a CONTIGUOUS span, so a write that spans
			// the wrap takes two passes
			while(written < frameCount)
			{
				ma_uint32 mapped = static_cast<ma_uint32>(frameCount - written);
				void * target = NULL;
				if(ma_pcm_rb_acquire_write(&stream->ring, &mapped, &target)
					!= MA_SUCCESS || mapped == 0)
				{
					break;
				}
				std::memcpy(target,
					reinterpret_cast<unsigned char const *>(frames) +
					static_cast<std::size_t>(written) * bytesPerFrame,
					static_cast<std::size_t>(mapped) * bytesPerFrame);
				ma_pcm_rb_commit_write(&stream->ring, mapped);
				written += static_cast<int>(mapped);
			}
			return written;
		}
		//---------------------------------------------------------
		void streamMarkDrained(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			if(stream != NULL)
			{
				stream->drained.store(true, std::memory_order_release);
			}
		}
		//---------------------------------------------------------
		void streamReset(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			if(stream == NULL)
			{
				return;
			}
			// a reset is a user-level boundary (a track was stopped), so the
			// voice is taken out of the graph before the ring is rewound
			if(stream->soundReady)
			{
				ma_sound_stop(&stream->sound);
			}
			ma_pcm_rb_reset(&stream->ring);
			stream->consumed.store(0, std::memory_order_relaxed);
			stream->drained.store(false, std::memory_order_release);
		}
		//---------------------------------------------------------
		bool streamStart(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			return stream != NULL &&
				ma_sound_start(&stream->sound) == MA_SUCCESS;
		}
		//---------------------------------------------------------
		bool streamSuspend(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			return stream != NULL &&
				ma_sound_stop(&stream->sound) == MA_SUCCESS;
		}
		//---------------------------------------------------------
		bool streamIsPlaying(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			return stream != NULL &&
				ma_sound_is_playing(&stream->sound) == MA_TRUE &&
				ma_sound_at_end(&stream->sound) == MA_FALSE;
		}
		//---------------------------------------------------------
		void streamSetVolume(Stream handle, float volume)
		{
			StreamImpl * stream = asStream(handle);
			if(stream != NULL)
			{
				ma_sound_set_volume(&stream->sound, volume);
			}
		}
		//---------------------------------------------------------
		long long streamConsumedFrames(Stream handle)
		{
			StreamImpl * stream = asStream(handle);
			return stream != NULL
				? stream->consumed.load(std::memory_order_relaxed) : 0;
		}
	}
	//---------------------------------------------------------------
}
