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
// wav bytes, so streamed music stays mounted-pak/APK compatible
#include <OgreDataStream.h>
#include <OgreResourceGroupManager.h>

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//---------------------------------------------------------
	const int	MusicStream::kBufferCount	= 4;		// ~2s of cushion
	const float	MusicStream::kBufferSeconds	= 0.5f;
	const String MusicStream::kMusicGroup	= "music";
	//---------------------------------------------------------
	namespace
	{
		//! read a resource's whole byte content through the OGRE resource
		//! system (the same path LoadWavData uses), so music resolves inside
		//! mounted pak archives and the APK asset extraction
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
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	MusicStream::MusicStream(String const & id, String const & fileName, bool loop)
		: Object(id)
		, mStream(NULL)
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
		, mPaused(false)
		, mBaseGain(1.f)
		, mGroupVolume(1.f)
	{
	}
	//---------------------------------------------------------
	MusicStream::~MusicStream()
	{
		this->teardownAudio();
		if (this->mVorbis)
		{
			MusicDecode::close(this->mVorbis);
			this->mVorbis = NULL;
		}
	}
	//---------------------------------------------------------
	bool MusicStream::open()
	{
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
		// create the voice + prime its ring so play() can start it
		return this->primeAudio();
	}
	//---------------------------------------------------------
	bool MusicStream::openFromMemory(std::vector<unsigned char> encoded)
	{
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
		this->mOpen = true;
		return true;
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
		if (!this->mOpen || out == NULL || maxFrames <= 0)
		{
			return 0;
		}
		int framesWritten = 0;
		// keep filling until the buffer is full; on end of stream a looping
		// track seeks to the start and continues so no silent gap is handed on
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
	}
	//---------------------------------------------------------
	void MusicStream::update()
	{
		if (!this->mPrimed)
		{
			return;
		}
		this->fillRing();
	}
	//---------------------------------------------------------
	bool MusicStream::play()
	{
		if (!this->mPrimed)
		{
			return false;
		}
		if (this->isPlaying())
		{
			return true;
		}
		AudioBackend::streamStart(this->mStream);
		this->mPaused = false;
		return this->isPlaying();
	}
	//---------------------------------------------------------
	bool MusicStream::stop()
	{
		if (!this->mPrimed)
		{
			return false;
		}
		// a stop rewinds: the voice goes quiet, the ring is emptied and the
		// decoder goes back to the top, so a later play() starts from there
		AudioBackend::streamReset(this->mStream);
		MusicDecode::seekStart(this->mVorbis);
		this->mReachedEnd = false;
		this->mPaused = false;
		this->fillRing();
		return true;
	}
	//---------------------------------------------------------
	bool MusicStream::pause()
	{
		if (!this->mPrimed)
		{
			return false;
		}
		AudioBackend::streamSuspend(this->mStream);
		this->mPaused = true;
		return true;
	}
	//---------------------------------------------------------
	bool MusicStream::resume()
	{
		if (!this->mPrimed || !this->mPaused)
		{
			return false;
		}
		AudioBackend::streamStart(this->mStream);
		this->mPaused = false;
		return true;
	}
	//---------------------------------------------------------
	void MusicStream::suspend()
	{
		if (!this->mPrimed)
		{
			return;
		}
		this->mWasPlaying = this->isPlaying();
		// releases the voice; the decoder + resident bytes stay, so restore()
		// re-primes the ring from the decoder's current position
		this->teardownAudio();
	}
	//---------------------------------------------------------
	void MusicStream::restore()
	{
		if (this->mPrimed || !this->mOpen)
		{
			return;
		}
		if (this->primeAudio() && this->mWasPlaying)
		{
			this->play();
		}
		this->mWasPlaying = false;
	}
	//---------------------------------------------------------
	bool MusicStream::isPlaying() const
	{
		if (!this->mPrimed)
		{
			return false;
		}
		return AudioBackend::streamIsPlaying(this->mStream);
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
		if (!this->mPrimed)
		{
			return 0.f;
		}
		const double rate = (this->mDecodeRate > 0) ? this->mDecodeRate : 44100;
		double seconds = static_cast<double>(
			AudioBackend::streamConsumedFrames(this->mStream)) / rate;
		// a looping track reports its position within the track, not the
		// ever-growing total
		if (this->mLoop && this->mDuration > 0.f)
		{
			seconds = std::fmod(seconds, static_cast<double>(this->mDuration));
		}
		return static_cast<float>(seconds);
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
		if (!this->mOpen)
		{
			return false;
		}
		if (this->mPrimed)
		{
			return true;
		}
		// the ring carries kBufferCount chunks of cushion, which is what lets
		// a main-thread refill tolerate the occasional long frame
		this->mStream = AudioBackend::streamCreate(this->mChannels,
			this->mDecodeRate, this->bufferFrames() * kBufferCount);
		if (this->mStream == NULL)
		{
			oDebugMsg("sound", 0, "Error creating the music voice for "
				<< this->mFileName << "!");
			return false;
		}
		this->mPrimed = true;
		AudioBackend::streamSetVolume(this->mStream, this->getEffectiveGain());
		this->fillRing();
		return true;
	}
	//---------------------------------------------------------
	void MusicStream::fillRing()
	{
		if (!this->mPrimed)
		{
			return;
		}
		const int chunkFrames = this->bufferFrames();
		if (chunkFrames <= 0)
		{
			return;
		}
		if (static_cast<int>(this->mChunk.size()) < chunkFrames * this->mChannels)
		{
			this->mChunk.assign(static_cast<std::size_t>(chunkFrames) *
				this->mChannels, 0);
		}
		int room = AudioBackend::streamWritableFrames(this->mStream);
		while (room > 0)
		{
			const int want = (room < chunkFrames) ? room : chunkFrames;
			const int got = this->decodeChunk(&this->mChunk[0], want);
			if (got <= 0)
			{
				// a non-looping track is out of samples: the voice ends once
				// the mixer has played what is still in the ring
				AudioBackend::streamMarkDrained(this->mStream);
				break;
			}
			const int taken = AudioBackend::streamWrite(this->mStream,
				&this->mChunk[0], got);
			room -= taken;
			if (taken < got)
			{
				break;	// the ring filled up mid-chunk
			}
		}
	}
	//---------------------------------------------------------
	void MusicStream::applyGain()
	{
		if (this->mPrimed)
		{
			AudioBackend::streamSetVolume(this->mStream,
				this->getEffectiveGain());
		}
	}
	//---------------------------------------------------------
	void MusicStream::teardownAudio()
	{
		if (!this->mPrimed)
		{
			return;
		}
		AudioBackend::streamDestroy(this->mStream);
		this->mStream = NULL;
		this->mPrimed = false;
		this->mPaused = false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(MusicStream)
	OOBJECT_END
}
