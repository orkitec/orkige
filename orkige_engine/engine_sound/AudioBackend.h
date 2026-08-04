/********************************************************************
	created:	Tuesday 2026/08/04 at 09:00
	filename: 	AudioBackend.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __AudioBackend_h__4_8_2026__09_00_00__
#define __AudioBackend_h__4_8_2026__09_00_00__

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//! @brief the audio backend seam: the ONE door to the output device, the
	//! mixing graph and the voices sitting on it.
	//! @remarks the single-file audio library is compiled in exactly ONE
	//! translation unit (MiniaudioImpl.cpp) and every handle here is opaque,
	//! so it never reaches a header, the neutral umbrella or the precompiled
	//! header - the same shape MusicDecode keeps around the Vorbis decoder.
	//!
	//! There is ONE graph per process: the engine object lives in the backend,
	//! not in a handle, because the mixer, the listener and the master volume
	//! are process-wide facts. SoundManager is a Singleton and owns its
	//! lifetime (@see SoundManager::init / deinit).
	namespace AudioBackend
	{
		//! how a decoded (or synthesized) block of samples is laid out
		struct PcmFormat
		{
			int	channels;		//!< 1 (mono) or 2 (stereo)
			int	bitsPerSample;	//!< 8 (unsigned) or 16 (signed)
			int	sampleRate;		//!< Hz, e.g. 44100
		};

		//--- the device + the mixing graph ---------------------------
		//! @brief open the output device and build the mixing graph.
		//! @param silent open the SILENT device instead of the machine's -
		//! it consumes samples in real time, so playheads advance and streams
		//! drain exactly as they would audibly, with nothing reaching the
		//! speakers. Automated runs ask for it (@see SoundManager::init).
		//! @remarks calling this while a graph is already open replaces it.
		bool	engineInit(bool silent);
		//! close the graph and the device; safe to call when nothing is open
		void	engineUninit();
		//! is a device open right now
		bool	engineReady();
		//! the master volume 0..1 - one knob over the whole graph
		void	engineSetMasterVolume(float volume);
		//! @brief place the "ears": world position plus the forward and up
		//! axes of the listener's pose (forward = the node's -Z, up = its +Y)
		void	engineSetListener(float posX, float posY, float posZ,
			float forwardX, float forwardY, float forwardZ,
			float upX, float upY, float upZ);

		//--- a fully buffered voice ----------------------------------
		//! one clip held whole in memory and played by the mixer
		typedef void* Voice;

		//! @brief create a voice over a block of PCM. The samples are COPIED
		//! into the voice, so the caller may free its own block right after.
		//! @returns NULL when no device is open or the format is unsupported
		//! @remarks a MONO voice is spatialized (its world position drives
		//! distance attenuation and panning); a stereo one is not - a
		//! pre-mixed stereo clip plays as authored.
		Voice	voiceCreate(void const * pcm, int byteSize,
			PcmFormat const & format, bool loop);
		//! release a voice (safe with NULL)
		void	voiceDestroy(Voice voice);
		//! start (or continue) playback
		bool	voiceStart(Voice voice);
		//! stop playback AND rewind to the first sample
		bool	voiceStop(Voice voice);
		//! stop playback and keep the playhead where it is
		bool	voiceSuspend(Voice voice);
		//! is this voice being mixed right now
		bool	voiceIsPlaying(Voice voice);
		//! the voice's own volume 0..1 (the mixer multiplies the master in)
		void	voiceSetVolume(Voice voice, float volume);
		//! playback rate multiplier (1 = as recorded)
		void	voiceSetPitch(Voice voice, float pitch);
		//! @see AudioBackend::voiceSetPitch - the value the mixer holds
		float	voiceGetPitch(Voice voice);
		//! the voice's world position (ignored while not spatialized)
		void	voiceSetPosition(Voice voice, float x, float y, float z);
		//! @brief is this voice placed in the world (mono) or played as the
		//! finished mix it already is (stereo)
		bool	voiceIsSpatialized(Voice voice);
		//! @brief the distance model: full volume within refDistance, rolling
		//! off with the inverse-distance curve and clamped at maxDistance
		void	voiceSetAttenuation(Voice voice, float refDistance,
			float maxDistance, float rolloff);
		//! the playhead in seconds
		float	voiceGetCursorSeconds(Voice voice);
		//! move the playhead
		void	voiceSeekSeconds(Voice voice, float seconds);
		//! frames the mixer reads behind this voice (0 when there is none)
		int		voiceFrameCount(Voice voice);
		//! the sample rate the mixer reads this voice at (0 when there is none)
		int		voiceSampleRate(Voice voice);

		//--- a streamed voice fed from the MAIN thread ---------------
		//! @brief a voice whose samples arrive a little at a time through a
		//! lock-free ring: the producer is the main thread (one writer), the
		//! consumer is the mixer (one reader).
		//! @remarks an underrun plays silence and keeps going - a long frame
		//! costs a gap, never the track. A stream that has been marked drained
		//! ENDS once the ring empties, so a non-looping track stops by itself.
		typedef void* Stream;

		//! @brief create a streamed voice with a ring of ringFrames frames.
		//! The samples are always interleaved 16-bit. Not spatialized: a
		//! streamed track is a 2D mix, never a point in the world.
		//! @returns NULL when no device is open
		Stream	streamCreate(int channels, int sampleRate, int ringFrames);
		//! release a stream (safe with NULL)
		void	streamDestroy(Stream stream);
		//! frames the ring can accept right now
		int		streamWritableFrames(Stream stream);
		//! @brief hand interleaved 16-bit frames to the ring
		//! @returns the frames it took (less than frameCount when it filled up)
		int		streamWrite(Stream stream, short const * frames, int frameCount);
		//! no more frames will ever arrive - end the voice once the ring empties
		void	streamMarkDrained(Stream stream);
		//! @brief forget everything queued and un-drain the stream (the
		//! rewind path: the producer seeks its decoder and refills)
		void	streamReset(Stream stream);
		//! start (or continue) playback
		bool	streamStart(Stream stream);
		//! stop playback, keeping whatever the ring still holds
		bool	streamSuspend(Stream stream);
		//! is this stream being mixed right now
		bool	streamIsPlaying(Stream stream);
		//! the stream's own volume 0..1
		void	streamSetVolume(Stream stream, float volume);
		//! @brief frames the mixer has actually consumed since the last reset -
		//! the playhead base (silence played through an underrun is not counted)
		long long streamConsumedFrames(Stream stream);
	}
	//---------------------------------------------------------------
}

#endif //__AudioBackend_h__4_8_2026__09_00_00__
