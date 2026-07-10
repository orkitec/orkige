/********************************************************************
	created:	Friday 2026/07/10 at 21:00
	filename: 	MusicStream.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __MusicStream_h__10_7_2026__21_00_00__
#define __MusicStream_h__10_7_2026__21_00_00__

#include "engine_sound/SoundPlatform.h"
#include <core_base/Object.h>

#include <vector>

namespace Orkige
{
	//! @brief the compressed-audio decode seam: a thin, AL-free and
	//! renderer-free wrapper over the single-file Vorbis decoder.
	//! @remarks the ONE place the decoder is compiled is StbVorbisImpl.cpp;
	//! everything else (MusicStream, the unit tests) reaches Vorbis ONLY through
	//! these functions, so the decoder never leaks into headers, the neutral
	//! umbrella or the precompiled header. Handles are opaque.
	namespace MusicDecode
	{
		//! stream shape read off the decoder when it opens
		struct Info
		{
			int		channels;			//!< 1 (mono) or 2 (stereo)
			int		sampleRate;			//!< Hz, e.g. 44100
			float	durationSeconds;	//!< total length in seconds (0 if unknown)
		};
		//! @brief open a decoder over an in-memory compressed-audio blob.
		//! @returns an opaque handle, or NULL on failure; fills info when non-NULL.
		void*	open(unsigned char const * data, int size, Info * info);
		//! @brief decode up to maxFrames interleaved 16-bit frames into out.
		//! @returns the number of frames decoded (0 at end of stream)
		int		read(void * handle, short * out, int maxFrames, int channels);
		//! seek the decoder back to the first sample (looping)
		void	seekStart(void * handle);
		//! total stream length in seconds (0 when the decoder cannot report it)
		float	length(void * handle);
		//! free a decoder opened by MusicDecode::open
		void	close(void * handle);
	}

	//! @brief one streamed music track: the compressed audio is held resident
	//! and decoded a little at a time into a small ring of OpenAL buffers that
	//! is refilled on the main thread (SoundManager::update). Unlike SoundSource
	//! (one fully-buffered clip) this keeps memory flat for long tracks.
	//! @remarks looping is decoder-level (seek-to-start on end of stream), not
	//! native AL_LOOPING (which would loop a single queued buffer). The mixer
	//! model mirrors SoundSource: effective AL_GAIN = baseGain * groupVolume,
	//! master rides the AL listener (SoundManager::setMasterVolume). All streams
	//! sit in the "music" mixer group. The gain state is kept even while OpenAL
	//! is down, so the volume math stays queryable headlessly.
	class ORKIGE_ENGINE_DLL MusicStream : public Object
	{
		OOBJECT(MusicStream, Object)
		//--- Types -------------------------------------------------
	public:
		static const int	kBufferCount;		//!< AL queued-buffer ring size
		static const float	kBufferSeconds;		//!< seconds of PCM per ring buffer
		//! the mixer group every streamed track belongs to
		static const String	kMusicGroup;
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
#ifdef ORKIGE_OPENAL_SOUND
		ALuint			mSource;			//!< OpenAL streaming source
		std::vector<ALuint>	mBuffers;		//!< the ring of queued buffers
		ALenum			mFormat;			//!< MONO16 / STEREO16
		ALsizei			mSampleRate;		//!< stream sample rate (Hz)
#endif //ORKIGE_OPENAL_SOUND
		void*			mVorbis;			//!< decoder handle (MusicDecode)
		std::vector<unsigned char> mEncoded;	//!< resident compressed audio bytes
		int				mChannels;			//!< 1 or 2
		int				mDecodeRate;		//!< decoder sample rate (device-free)
		float			mDuration;			//!< track length in seconds
		String			mFileName;			//!< source file (resource-relative)
		String			mGroup;				//!< mixer group ("music")
		bool			mLoop;				//!< seek-to-start on end of stream
		bool			mOpen;				//!< decoder opened
		bool			mPrimed;			//!< AL objects created + ring filled
		bool			mReachedEnd;		//!< a non-looping stream drained
		bool			mWasPlaying;		//!< playing when suspend() ran (for restore())
		//--- effective gain = baseGain * groupVolume (0..1), like SoundSource
		float			mBaseGain;			//!< this track's own volume (default 1)
		float			mGroupVolume;		//!< the "music" group volume, pushed in
		//! frames unqueued (fully played out) so far - the base of the playhead
		long long		mPlayedFrames;
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief construct a track (nothing is decoded or opened yet)
		//! @param id unique registry id
		//! @param fileName the resource-relative audio file (e.g. "music/x.ogg")
		//! @param loop restart from the beginning on end of stream
		MusicStream(String const & id, String const & fileName, bool loop = true);
		//! destructor - frees the decoder and any AL objects
		virtual ~MusicStream();

		//! @brief read the file's bytes through the resource system and open the
		//! decoder; then, when OpenAL is up, create the AL objects and prime the
		//! ring. @returns false on a missing/unreadable/undecodable file.
		bool open();
		//! @brief open the decoder over an already-loaded compressed blob (no
		//! resource system, no OpenAL). The device-free entry the unit tests and
		//! open() share. @returns false when the bytes are not decodable.
		bool openFromMemory(std::vector<unsigned char> encoded);
		//! @brief MAIN-THREAD refill: recycle processed buffers (unqueue, decode,
		//! requeue) and recover a source AL auto-stopped on a brief underrun.
		void update();
		//! start playback (idempotent while already playing)
		bool play();
		//! stop playback and rewind the decoder to the start
		bool stop();
		//! pause playback (keeps the queue)
		bool pause();
		//! resume a paused stream
		bool resume();
		//! @brief release the AL objects for an audio-session interruption,
		//! remembering whether the track was playing; the decoder and the
		//! resident compressed bytes are kept for restore().
		void suspend();
		//! recreate the AL ring after an interruption and resume if it was playing
		void restore();
		//! is the source currently playing
		bool isPlaying() const;
		//! true once the decoder is open (device or not)
		bool isOpen() const;
		//! true once the AL ring is created and primed
		bool isPrimed() const;
		//! did a non-looping stream reach its end
		bool reachedEnd() const;

		//! @brief decode one ring-buffer's worth of interleaved 16-bit PCM into
		//! out (capacity kBufferSeconds * rate frames). On end of stream: loop ->
		//! seek to start and keep filling so no gap is queued; non-loop -> stop
		//! early and set reachedEnd. @returns frames written (0 only when a
		//! non-looping stream is fully drained). AL-free; the ring accounting in
		//! update() uploads the result.
		int decodeChunk(short * out, int maxFrames);
		//! frames one ring buffer holds (kBufferSeconds at the stream rate)
		int bufferFrames() const;

		//--- MIXER (effective = baseGain * groupVolume) ---------------
		//! this track's own volume, clamped 0..1
		void setBaseGain(float gain);
		//! @see MusicStream::setBaseGain
		float getBaseGain() const;
		//! the group-volume push channel (SoundManager calls it), clamped 0..1
		void setGroupVolume(float volume);
		//! the gain the track actually plays at (baseGain * groupVolume)
		float getEffectiveGain() const;
		//! the mixer group tag (always "music")
		String const & getGroup() const;

		//! the decoded playhead in seconds (looping tracks wrap at the duration)
		float getPlayPosition() const;
		//! the track length in seconds (0 until opened)
		float getDuration() const;
		//! is this a looping track
		bool isLoop() const;
		//! the source file (resource-relative)
		String const & getFile() const;
	protected:
		//! create the AL objects and fill + queue the ring (needs an AL context)
		bool primeAudio();
		//! decode into ONE AL buffer and upload it; false at end (non-loop)
		bool fillBuffer(ALuint buffer);
		//! push the effective gain onto the AL source (when primed)
		void applyGain();
		//! release the AL source + buffers (when primed)
		void teardownAudio();
	private:
	};
	//---------------------------------------------------------------
	typedef optr<MusicStream> MusicStreamPtr;
}

#endif //__MusicStream_h__10_7_2026__21_00_00__
