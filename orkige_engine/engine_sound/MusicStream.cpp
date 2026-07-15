/********************************************************************
	created:	Friday 2026/07/10 at 21:00
	filename: 	MusicStream.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "engine_sound/MusicStream.h"
#include "engine_sound/SoundError.h"
// explicit (the neutral umbrella carries math only): the compressed bytes come
// through the resource system - Ogre::DataStream/ResourceGroupManager exist
// identically in classic OGRE 14 and Ogre-Next, exactly as LoadWavData reads
// wav bytes, so streamed music stays BigZip/APK compatible
#ifdef ORKIGE_OPENAL_SOUND
#include <OgreDataStream.h>
#include <OgreResourceGroupManager.h>
#endif

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//---------------------------------------------------------
	const int	MusicStream::kBufferCount	= 4;		// ~2s of cushion
	const float	MusicStream::kBufferSeconds	= 0.5f;
	const String MusicStream::kMusicGroup	= "music";
	//---------------------------------------------------------
#ifdef ORKIGE_OPENAL_SOUND
	namespace
	{
		//! read a resource's whole byte content through the OGRE resource
		//! system (the same path LoadWavData uses), so music resolves inside
		//! BigZip archives and the APK asset extraction
		bool readResourceBytes(String const & fileName,
			std::vector<unsigned char> & out)
		{
			Ogre::ResourceGroupManager * groupManager =
				Ogre::ResourceGroupManager::getSingletonPtr();
			if (groupManager == NULL)
			{
				return false;
			}
			try
			{
				const Ogre::String group =
					groupManager->findGroupContainingResource(fileName);
				Ogre::DataStreamPtr stream =
					groupManager->openResource(fileName, group);
				if (!stream)
				{
					return false;
				}
				const std::size_t size = stream->size();
				out.resize(size);
				if (size > 0)
				{
					stream->read(&out[0], size);
				}
				return true;
			}
			catch (...)
			{
				// findGroupContainingResource throws when the file is unknown -
				// an honest false, never a crash (mirrors the loaders' policy)
				return false;
			}
		}
	}
#endif //ORKIGE_OPENAL_SOUND
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	MusicStream::MusicStream(String const & id, String const & fileName, bool loop)
		: Object(id)
		, mVorbis(NULL)
		, mChannels(0)
		, mDecodeRate(0)
		, mDuration(0.f)
		, mFileName(fileName)
		, mGroup(kMusicGroup)
		, mLoop(loop)
		, mOpen(false)
		, mPrimed(false)
		, mReachedEnd(false)
		, mWasPlaying(false)
		, mBaseGain(1.f)
		, mGroupVolume(1.f)
		, mPlayedFrames(0)
	{
#ifdef ORKIGE_OPENAL_SOUND
		this->mSource = 0;
		this->mFormat = 0;
		this->mSampleRate = 0;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	MusicStream::~MusicStream()
	{
#ifdef ORKIGE_OPENAL_SOUND
		this->teardownAudio();
		if (this->mVorbis)
		{
			MusicDecode::close(this->mVorbis);
			this->mVorbis = NULL;
		}
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::open()
	{
#ifdef ORKIGE_OPENAL_SOUND
		std::vector<unsigned char> bytes;
		if (!readResourceBytes(this->mFileName, bytes) || bytes.empty())
		{
			oDebugMsg("sound", 0, "Music file not found or empty: "
				<< this->mFileName << "!");
			return false;
		}
		if (!this->openFromMemory(std::move(bytes)))
		{
			oDebugMsg("sound", 0, "Cannot decode music file: "
				<< this->mFileName << "!");
			return false;
		}
		// create the AL objects + prime the ring so play() can start it
		return this->primeAudio();
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::openFromMemory(std::vector<unsigned char> encoded)
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (this->mOpen)
		{
			return true;
		}
		if (encoded.empty())
		{
			return false;
		}
		this->mEncoded = std::move(encoded);
		MusicDecode::Info info = {0, 0, 0.f};
		this->mVorbis = MusicDecode::open(&this->mEncoded[0],
			static_cast<int>(this->mEncoded.size()), &info);
		if (this->mVorbis == NULL)
		{
			this->mEncoded.clear();
			return false;
		}
		this->mChannels = (info.channels == 2) ? 2 : 1;
		this->mDecodeRate = (info.sampleRate > 0) ? info.sampleRate : 44100;
		this->mDuration = info.durationSeconds;
		this->mReachedEnd = false;
		this->mPlayedFrames = 0;
		this->mFormat = (this->mChannels == 2)
			? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
		this->mSampleRate = static_cast<ALsizei>(this->mDecodeRate);
		this->mOpen = true;
		return true;
#else //ORKIGE_OPENAL_SOUND
		(void)encoded;
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	int MusicStream::bufferFrames() const
	{
		const int rate = (this->mDecodeRate > 0) ? this->mDecodeRate : 44100;
		return static_cast<int>(kBufferSeconds * rate);
	}
	//---------------------------------------------------------
	int MusicStream::decodeChunk(short * out, int maxFrames)
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mOpen || out == NULL || maxFrames <= 0)
		{
			return 0;
		}
		int framesWritten = 0;
		// keep filling until the buffer is full; on end of stream a looping
		// track seeks to the start and continues so no silent gap is queued
		while (framesWritten < maxFrames)
		{
			const int got = MusicDecode::read(this->mVorbis,
				out + framesWritten * this->mChannels,
				maxFrames - framesWritten, this->mChannels);
			if (got > 0)
			{
				framesWritten += got;
				continue;
			}
			// end of stream
			if (this->mLoop)
			{
				MusicDecode::seekStart(this->mVorbis);
				// guard against a zero-length/failed stream spinning forever
				const int retry = MusicDecode::read(this->mVorbis,
					out + framesWritten * this->mChannels,
					maxFrames - framesWritten, this->mChannels);
				if (retry <= 0)
				{
					this->mReachedEnd = true;
					break;
				}
				framesWritten += retry;
			}
			else
			{
				this->mReachedEnd = true;
				break;
			}
		}
		return framesWritten;
#else //ORKIGE_OPENAL_SOUND
		(void)out;
		(void)maxFrames;
		return 0;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	void MusicStream::update()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return;
		}
		ALint processed = 0;
		alGetSourcei(this->mSource, AL_BUFFERS_PROCESSED, &processed);
		while (processed-- > 0)
		{
			ALuint buffer = 0;
			alSourceUnqueueBuffers(this->mSource, 1, &buffer);
			// the buffer that just finished contributes to the playhead base
			ALint bits = 16;
			ALint channels = this->mChannels;
			ALint sizeBytes = 0;
			alGetBufferi(buffer, AL_SIZE, &sizeBytes);
			const int bytesPerFrame = (bits / 8) * channels;
			if (bytesPerFrame > 0)
			{
				this->mPlayedFrames += sizeBytes / bytesPerFrame;
			}
			// refill and requeue unless a non-looping stream has drained
			if (this->fillBuffer(buffer))
			{
				alSourceQueueBuffers(this->mSource, 1, &buffer);
			}
		}
		// a brief underrun can leave AL_STOPPED with buffers still queued -
		// restart so the track keeps playing through the hiccup
		if (!this->mReachedEnd)
		{
			ALint state = 0;
			alGetSourcei(this->mSource, AL_SOURCE_STATE, &state);
			ALint queued = 0;
			alGetSourcei(this->mSource, AL_BUFFERS_QUEUED, &queued);
			if (state != AL_PLAYING && state != AL_PAUSED && queued > 0)
			{
				alSourcePlay(this->mSource);
			}
		}
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::play()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return false;
		}
		if (this->isPlaying())
		{
			return true;
		}
		alSourcePlay(this->mSource);
		return this->isPlaying();
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::stop()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return false;
		}
		alSourceStop(this->mSource);
		// rewind the decoder so a later play() starts from the top
		MusicDecode::seekStart(this->mVorbis);
		this->mReachedEnd = false;
		this->mPlayedFrames = 0;
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::pause()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return false;
		}
		alSourcePause(this->mSource);
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::resume()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return false;
		}
		ALint state = 0;
		alGetSourcei(this->mSource, AL_SOURCE_STATE, &state);
		if (state == AL_PAUSED)
		{
			alSourcePlay(this->mSource);
			return true;
		}
		return false;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	void MusicStream::suspend()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return;
		}
		this->mWasPlaying = this->isPlaying();
		// releases the AL source + buffers; the decoder + resident bytes stay,
		// so restore() re-primes the ring from the decoder's current position
		this->teardownAudio();
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	void MusicStream::restore()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (this->mPrimed || !this->mOpen)
		{
			return;
		}
		if (this->primeAudio() && this->mWasPlaying)
		{
			this->play();
		}
		this->mWasPlaying = false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::isPlaying() const
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return false;
		}
		ALint state = 0;
		alGetSourcei(this->mSource, AL_SOURCE_STATE, &state);
		return state == AL_PLAYING;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::isOpen() const		{ return this->mOpen; }
	bool MusicStream::isPrimed() const		{ return this->mPrimed; }
	bool MusicStream::reachedEnd() const	{ return this->mReachedEnd; }
	//---------------------------------------------------------
	void MusicStream::setBaseGain(float gain)
	{
		this->mBaseGain = std::clamp(gain, 0.f, 1.f);
		this->applyGain();
	}
	//---------------------------------------------------------
	float MusicStream::getBaseGain() const
	{
		return this->mBaseGain;
	}
	//---------------------------------------------------------
	void MusicStream::setGroupVolume(float volume)
	{
		this->mGroupVolume = std::clamp(volume, 0.f, 1.f);
		this->applyGain();
	}
	//---------------------------------------------------------
	float MusicStream::getEffectiveGain() const
	{
		return this->mBaseGain * this->mGroupVolume;
	}
	//---------------------------------------------------------
	String const & MusicStream::getGroup() const
	{
		return this->mGroup;
	}
	//---------------------------------------------------------
	float MusicStream::getPlayPosition() const
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return 0.f;
		}
		ALint offset = 0;
		alGetSourcei(this->mSource, AL_SAMPLE_OFFSET, &offset);
		const double rate = (this->mDecodeRate > 0) ? this->mDecodeRate : 44100;
		double seconds = (static_cast<double>(this->mPlayedFrames) + offset)
			/ rate;
		// a looping track reports its position within the track, not the
		// ever-growing total
		if (this->mLoop && this->mDuration > 0.f)
		{
			seconds = std::fmod(seconds, static_cast<double>(this->mDuration));
		}
		return static_cast<float>(seconds);
#else //ORKIGE_OPENAL_SOUND
		return 0.f;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	float MusicStream::getDuration() const	{ return this->mDuration; }
	bool MusicStream::isLoop() const		{ return this->mLoop; }
	String const & MusicStream::getFile() const { return this->mFileName; }
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool MusicStream::primeAudio()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mOpen)
		{
			return false;
		}
		if (this->mPrimed)
		{
			return true;
		}
		alGetError();
		alGenSources(1, &this->mSource);
		if (alGetError() != AL_NO_ERROR)
		{
			oDebugMsg("sound", 0, "Error generating music source for "
				<< this->mFileName << "!");
			return false;
		}
		this->mBuffers.assign(kBufferCount, 0);
		alGenBuffers(kBufferCount, &this->mBuffers[0]);
		if (alGetError() != AL_NO_ERROR)
		{
			alDeleteSources(1, &this->mSource);
			this->mSource = 0;
			this->mBuffers.clear();
			oDebugMsg("sound", 0, "Error generating music buffers for "
				<< this->mFileName << "!");
			return false;
		}
		// music is a 2D stream: keep it listener-relative at the origin so the
		// 3D attenuation model never touches it
		alSourcei(this->mSource, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(this->mSource, AL_POSITION, 0.f, 0.f, 0.f);
		alSourcef(this->mSource, AL_ROLLOFF_FACTOR, 0.f);
		alSourcef(this->mSource, AL_MAX_GAIN, 1.f);
		alSourcef(this->mSource, AL_MIN_GAIN, 0.f);
		alSourcef(this->mSource, AL_GAIN, this->getEffectiveGain());

		// prime the ring: fill each buffer and queue the ones that got data
		std::vector<short> pcm(bufferFrames() * this->mChannels);
		std::vector<ALuint> toQueue;
		for (int i = 0; i < kBufferCount; ++i)
		{
			const int frames = this->decodeChunk(&pcm[0], bufferFrames());
			if (frames <= 0)
			{
				break;
			}
			alBufferData(this->mBuffers[i], this->mFormat, &pcm[0],
				static_cast<ALsizei>(frames * this->mChannels * sizeof(short)),
				this->mSampleRate);
			toQueue.push_back(this->mBuffers[i]);
		}
		if (!toQueue.empty())
		{
			alSourceQueueBuffers(this->mSource,
				static_cast<ALsizei>(toQueue.size()), &toQueue[0]);
		}
		this->mPrimed = true;
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool MusicStream::fillBuffer(ALuint buffer)
	{
#ifdef ORKIGE_OPENAL_SOUND
		std::vector<short> pcm(bufferFrames() * this->mChannels);
		const int frames = this->decodeChunk(&pcm[0], bufferFrames());
		if (frames <= 0)
		{
			return false;	// non-looping stream fully drained
		}
		alBufferData(buffer, this->mFormat, &pcm[0],
			static_cast<ALsizei>(frames * this->mChannels * sizeof(short)),
			this->mSampleRate);
		return true;
#else //ORKIGE_OPENAL_SOUND
		(void)buffer;
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	void MusicStream::applyGain()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (this->mPrimed)
		{
			alSourcef(this->mSource, AL_GAIN, this->getEffectiveGain());
		}
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	void MusicStream::teardownAudio()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if (!this->mPrimed)
		{
			return;
		}
		alSourceStop(this->mSource);
		// unqueue anything still queued before deleting the buffers
		ALint queued = 0;
		alGetSourcei(this->mSource, AL_BUFFERS_QUEUED, &queued);
		while (queued-- > 0)
		{
			ALuint buffer = 0;
			alSourceUnqueueBuffers(this->mSource, 1, &buffer);
		}
		alDeleteSources(1, &this->mSource);
		if (!this->mBuffers.empty())
		{
			alDeleteBuffers(static_cast<ALsizei>(this->mBuffers.size()),
				&this->mBuffers[0]);
		}
		this->mSource = 0;
		this->mBuffers.clear();
		this->mPrimed = false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(MusicStream)
	OOBJECT_END
}
