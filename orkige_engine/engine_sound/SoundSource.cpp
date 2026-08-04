/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundSource.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "engine_sound/SoundSource.h"
#include "engine_sound/SoundError.h"
#include "core_util/SoundVariation.h"

#include <algorithm>
#include <random>
#include <cstdlib>
#include <cstring>

namespace
{
	//! @brief a shared uniform sample source for per-play pitch/volume variation.
	//! A fixed seed keeps a run reproducible (the value still varies play-to-play
	//! within a run); the pure variation math is unit-tested separately with
	//! fixed samples, so this only supplies the randomness.
	float nextVariationSample()
	{
		static std::mt19937 engine(0x0A6C1235u);
		static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
		return dist(engine);
	}

	//--- the distance model every positional source plays under -------------
	//! full volume out to here
	const float kReferenceDistance	= 150.f;
	//! attenuation stops falling past here
	const float kMaxDistance		= 3400.f;
	//! how steeply it falls in between (1 = the plain inverse curve)
	const float kRolloffFactor		= 1.f;
}

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundSource::SoundSource(String const & id, String const & file, bool loop, Ogre::Vector3 const & pos)
		: Object(id), voice(NULL), size(0), data(NULL), position(pos)
		, fileName(file), looped(loop), initialized(false), paused(false)
		, baseGain(1.f), group("sfx"), groupVolume(1.f)
		, pitchVariation(0.f), gainVariation(0.f), currentPitch(1.f)
	{
		this->format.channels = 0;
		this->format.bitsPerSample = 0;
		this->format.sampleRate = 0;
	}
	//---------------------------------------------------------
	SoundSource::~SoundSource()
	{
		if(this->initialized)
			this->deinit();

		if(this->data)
		{
			free(this->data);
			this->data = NULL;
		}
	}
	//---------------------------------------------------------
	bool SoundSource::init(bool reloadData, bool alwaysFreeData)
	{
		oAssertDesc(!this->initialized, "Already initialized you have to call deinit first!");

		if(this->data && reloadData)
		{
			free(this->data);
			this->data = NULL;
		}
		if(this->data == NULL)
		{
			this->data = SoundUtil::loadSoundData(this->fileName, &this->size,
				&this->format);
		}
		// honest failure instead of an assert: loadSoundData returns NULL for
		// unreadable files AND for .caf on non-Apple platforms (the decoder is
		// AudioToolbox-backed, see SoundData.h) - throw the established
		// SoundError so the caller's cleanup runs
		SoundError::call(this->data != NULL,
			"Unsupported or unreadable sound file: " + this->fileName + "!",
			SoundError::SE_UNREADABLE);

		this->voice = AudioBackend::voiceCreate(this->data, this->size,
			this->format, this->looped);
		SoundError::call(this->voice != NULL,
			"Error creating a voice for: " + this->fileName + "!",
			SoundError::SE_DEVICE);

		// the samples were copied into the voice, so the block can be freed
		// right after (a source that wants to survive an interruption keeps it)
		if(alwaysFreeData)
		{
			free(this->data);
			this->data = NULL;
		}

		AudioBackend::voiceSetPosition(this->voice, this->position.x,
			this->position.y, this->position.z);
		AudioBackend::voiceSetAttenuation(this->voice, kReferenceDistance,
			kMaxDistance, kRolloffFactor);
		AudioBackend::voiceSetPitch(this->voice, 1.f);
		AudioBackend::voiceSetVolume(this->voice, this->getEffectiveGain());

		this->paused = false;
		this->initialized = true;
		return true;
	}
	//---------------------------------------------------------
	bool SoundSource::initFromPCM(void const * pcmData, int dataSize, int channels, int bitsPerSample, int sampleRate)
	{
		oAssertDesc(!this->initialized, "Already initialized you have to call deinit first!");
		oAssertDesc(pcmData != NULL && dataSize > 0, "No PCM data given!");
		oAssertDesc(channels == 1 || channels == 2, "Only mono and stereo PCM supported!");
		oAssertDesc(bitsPerSample == 8 || bitsPerSample == 16, "Only 8 and 16 bit PCM supported!");

		this->format.channels = channels;
		this->format.bitsPerSample = bitsPerSample;
		this->format.sampleRate = sampleRate;
		this->size = dataSize;

		// keep an own copy like the file loaders do so init() can rebuild the
		// voice e.g. after an interruption
		if(this->data)
		{
			free(this->data);
			this->data = NULL;
		}
		this->data = malloc(dataSize);
		if(this->data == NULL)
		{
			return false;
		}
		memcpy(this->data, pcmData, dataSize);

		// data is already loaded here so init() only creates the voice
		return this->init();
	}
	//---------------------------------------------------------
	bool SoundSource::deinit(bool freeData)
	{
		oAssert(this->initialized);

		AudioBackend::voiceDestroy(this->voice);
		this->voice = NULL;

		if(this->data && freeData)
		{
			free(this->data);
			this->data = NULL;
		}
		this->paused = false;
		this->initialized = false;
		return true;
	}
	//---------------------------------------------------------
	bool SoundSource::play()
	{
		// per-play randomization: the pitch the source WOULD play at is kept in a
		// member so it stays queryable headlessly; a zero range leaves it at 1.0,
		// so a source that never opts in behaves exactly as before.
		this->currentPitch = variedPitch(this->pitchVariation,
			this->pitchVariation != 0.f ? nextVariationSample() : 0.5f);

		if(this->isPlaying())
			return false;

		// push this play's varied pitch/gain onto the voice before it starts
		if(this->initialized)
		{
			const float playGain = variedGain(this->getEffectiveGain(),
				this->gainVariation,
				this->gainVariation != 0.f ? nextVariationSample() : 0.5f);
			AudioBackend::voiceSetPitch(this->voice, this->currentPitch);
			AudioBackend::voiceSetVolume(this->voice, playGain);
		}

		if(!AudioBackend::voiceStart(this->voice))
		{
			oDebugMsg("sound",0,"Error starting source for " << this->fileName << "!");
			return false;
		}
		this->paused = false;
		return true;
	}
	//---------------------------------------------------------
	bool SoundSource::stop()
	{
		if(!this->isPlaying())
			return false;

		if(!AudioBackend::voiceStop(this->voice))
		{
			oDebugMsg("sound",0,"Error stopping source for " << this->fileName << "!");
			return false;
		}
		this->paused = false;
		return true;
	}
	//---------------------------------------------------------
	bool SoundSource::pause()
	{
		if(this->isPaused())
			return false;

		if(!AudioBackend::voiceSuspend(this->voice))
		{
			oDebugMsg("sound",0,"Error pausing source for " << this->fileName << "!");
			return false;
		}
		// a paused source keeps its playhead: resume() picks it up there
		this->paused = this->initialized;
		return true;
	}
	//---------------------------------------------------------
	bool SoundSource::resume()
	{
		if(!this->isPaused())
			return false;

		if(!this->play())
			return false;

		return true;
	}
	//---------------------------------------------------------
	bool SoundSource::isPlaying()
	{
		if(!this->initialized)
			return false;

		return AudioBackend::voiceIsPlaying(this->voice);
	}
	//---------------------------------------------------------
	bool SoundSource::isPaused()
	{
		if(!this->initialized)
			return false;

		return this->paused && !AudioBackend::voiceIsPlaying(this->voice);
	}
	//---------------------------------------------------------
	float SoundSource::getPlayPosition()
	{
		if(!this->isPlaying())
			return 0.f;

		return AudioBackend::voiceGetCursorSeconds(this->voice);
	}
	//---------------------------------------------------------
	void SoundSource::setPlayPosition(float pos)
	{
		if(!this->initialized)
			return;

		AudioBackend::voiceSeekSeconds(this->voice, pos);
	}
	//---------------------------------------------------------
	bool SoundSource::setPosition(Ogre::Vector3 const & pos)
	{
		if(!this->initialized)
			return false;

		this->position = pos;
		AudioBackend::voiceSetPosition(this->voice, this->position.x,
			this->position.y, this->position.z);

		return true;
	}
	//---------------------------------------------------------
	Ogre::Vector3 const & SoundSource::getPosition()
	{
		return this->position;
	}
	//---------------------------------------------------------
	void SoundSource::setBaseGain(float gain)
	{
		this->baseGain = std::clamp(gain, 0.f, 1.f);
		this->applyGain();
	}
	//---------------------------------------------------------
	float SoundSource::getBaseGain() const
	{
		return this->baseGain;
	}
	//---------------------------------------------------------
	void SoundSource::setGroup(String const & groupName)
	{
		this->group = groupName;
	}
	//---------------------------------------------------------
	String const & SoundSource::getGroup() const
	{
		return this->group;
	}
	//---------------------------------------------------------
	void SoundSource::setGroupVolume(float volume)
	{
		this->groupVolume = std::clamp(volume, 0.f, 1.f);
		this->applyGain();
	}
	//---------------------------------------------------------
	float SoundSource::getEffectiveGain() const
	{
		return this->baseGain * this->groupVolume;
	}
	//---------------------------------------------------------
	void SoundSource::setPitchVariation(float range)
	{
		this->pitchVariation = range < 0.f ? -range : range;
	}
	//---------------------------------------------------------
	float SoundSource::getPitchVariation() const
	{
		return this->pitchVariation;
	}
	//---------------------------------------------------------
	void SoundSource::setVolumeVariation(float range)
	{
		this->gainVariation = range < 0.f ? -range : range;
	}
	//---------------------------------------------------------
	float SoundSource::getVolumeVariation() const
	{
		return this->gainVariation;
	}
	//---------------------------------------------------------
	float SoundSource::getCurrentPitch() const
	{
		return this->currentPitch;
	}
	//---------------------------------------------------------
	float SoundSource::queryPitch() const
	{
		if(this->initialized)
		{
			return AudioBackend::voiceGetPitch(this->voice);
		}
		return 0.f;
	}
	//---------------------------------------------------------
	int SoundSource::queryBufferBytes() const
	{
		if(this->initialized)
		{
			const int frames = AudioBackend::voiceFrameCount(this->voice);
			const int bytesPerFrame = this->format.channels *
				(this->format.bitsPerSample / 8);
			return frames * bytesPerFrame;
		}
		return 0;
	}
	//---------------------------------------------------------
	int SoundSource::queryBufferSampleRate() const
	{
		if(this->initialized)
		{
			return AudioBackend::voiceSampleRate(this->voice);
		}
		return 0;
	}
	//---------------------------------------------------------
	bool SoundSource::isInitialized() const
	{
		return this->initialized;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SoundSource::applyGain()
	{
		if(this->initialized)
		{
			AudioBackend::voiceSetVolume(this->voice, this->getEffectiveGain());
		}
	}
	//---------------------------------------------------------


	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(SoundSource)
	OOBJECT_END
}
