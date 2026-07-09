/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundSource.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#include "engine_sound/SoundSource.h"
#include "engine_sound/SoundError.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundSource::SoundSource(String const & id, String const & file, bool loop, Ogre::Vector3 const & pos)
		: Object(id), data(NULL), position(pos), fileName(file), looped(loop), initialized(false)
		, baseGain(1.f), group("sfx"), groupVolume(1.f)
	{
#ifdef ORKIGE_OPENAL_SOUND
		this->source = 0;
		this->buffer = 0;
		this->format = 0;
		this->size = 0;
		this->freq = 0;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	SoundSource::~SoundSource()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(this->initialized)
			this->deinit();

		if(this->data)
		{
			free(this->data);
			this->data = NULL;
		}
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::init(bool reloadData, bool alwaysFreeData)
	{
#ifdef ORKIGE_OPENAL_SOUND
		oAssertDesc(!this->initialized, "Already initialized you have to call deinit first!");

		ALenum  error = AL_NO_ERROR;
		try
		{
			alGenBuffers(1, &this->buffer);
			error = alGetError();
			SoundError::call(error == AL_NO_ERROR, "Error generating buffer for: " + this->fileName + "!", error);

			// Create some OpenAL Source Objects
			alGenSources(1, &this->source);
			error = alGetError();
			SoundError::call(error == AL_NO_ERROR, "Error generating source for: " + this->fileName + "!", error);

			if(this->data && reloadData)
			{
				free(this->data);
				this->data = NULL;
			}
			if(this->data == NULL)
			{
				this->data = SoundUtil::loadSoundData(this->fileName, &size, &format, &freq);
			}
			// honest failure instead of an assert: loadSoundData returns
			// NULL for unreadable files AND for .caf on non-Apple platforms
			// (the decoder is AudioToolbox-backed, see SoundPlatform.h) -
			// throw the established SoundError so the cleanup below runs
			SoundError::call(this->data != NULL,
				"Unsupported or unreadable sound file: " + this->fileName + "!", AL_INVALID_VALUE);
			SoundUtil::alBufferDataPlatform(this->buffer, format, data, size, freq);

			error = alGetError();
			SoundError::call(error == AL_NO_ERROR, "Error loading sound: " + this->fileName + "!", error);

			// alBufferData copied the samples into the al buffer so the data
			// can be freed right after the upload (with alBufferDataStatic it
			// had to stay alive as long as the buffer)
			if(alwaysFreeData)
			{
				free(this->data);
				this->data = NULL;
			}
		}
		catch(SoundError const & e)
		{
			if(this->buffer != 0)
			{
				if (alIsBuffer(this->buffer) == AL_TRUE)
				{
					alDeleteBuffers(1, &this->buffer);
					error = alGetError();
					SoundError::call(error == AL_NO_ERROR, "Failed to delete Buffer", error);
				}
			}

			// Prevent the ~Sound() destructor from double-freeing.
			this->buffer = 0;
			// Propagate.
			throw (e);
		}

		// Turn Looping ON or OFF
		alSourcei(this->source, AL_LOOPING, this->looped ? AL_TRUE : AL_FALSE);

		// Set Source Position
		float sourcePosAL[] = {this->position.x, this->position.y, this->position.z};
		alSourcefv(this->source, AL_POSITION, sourcePosAL);

		//@TODO: make pitch/distance model configurable with SoundSource set and get functions
		// AL_GAIN carries the mixer model (baseGain * groupVolume) - note
		// AL_MAX_GAIN is pinned to 1.0 here, so gains above 1 would clamp
		// SILENTLY: the whole volume vocabulary stays in 0..1 by design
		alSourcef (this->source, AL_PITCH,				1.f);
		alSourcef (this->source, AL_GAIN,				this->getEffectiveGain());
		alSourcef (this->source, AL_MAX_GAIN,			1.f);
		alSourcef (this->source, AL_MIN_GAIN,			0.f);
		alSourcef (this->source, AL_MAX_DISTANCE,		3400.f);
		alSourcef (this->source, AL_ROLLOFF_FACTOR,		1.f);
		alSourcef (this->source, AL_REFERENCE_DISTANCE,	150.f);
		alSourcef (this->source, AL_CONE_OUTER_GAIN,	0.f);
		alSourcef (this->source, AL_CONE_INNER_ANGLE,	360.f);
		alSourcef (this->source, AL_CONE_OUTER_ANGLE,	360.f);
		alSourcei (this->source, AL_SOURCE_RELATIVE,	AL_FALSE);

		// attach OpenAL Buffer to OpenAL Source
		alSourcei(this->source, AL_BUFFER, this->buffer);

		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error attaching buffer to source: " << this->fileName << "!");
			return false;
		}

		this->initialized = true;
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::initFromPCM(void const * pcmData, int dataSize, int channels, int bitsPerSample, int sampleRate)
	{
#ifdef ORKIGE_OPENAL_SOUND
		oAssertDesc(!this->initialized, "Already initialized you have to call deinit first!");
		oAssertDesc(pcmData != NULL && dataSize > 0, "No PCM data given!");
		oAssertDesc(channels == 1 || channels == 2, "Only mono and stereo PCM supported!");
		oAssertDesc(bitsPerSample == 8 || bitsPerSample == 16, "Only 8 and 16 bit PCM supported!");

		if(channels == 1)
		{
			this->format = (bitsPerSample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
		}
		else
		{
			this->format = (bitsPerSample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
		}
		this->size = static_cast<ALsizei>(dataSize);
		this->freq = static_cast<ALsizei>(sampleRate);

		// keep an own copy like the file loaders do so init() can reupload it
		// e.g. after an interruption
		if(this->data)
		{
			free(this->data);
			this->data = NULL;
		}
		this->data = malloc(dataSize);
		memcpy(this->data, pcmData, dataSize);

		// data is already loaded here so init() only creates the al objects
		// and uploads it
		return this->init();
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::deinit(bool freeData)
	{
#ifdef ORKIGE_OPENAL_SOUND
		oAssert(this->initialized);
		ALenum  error = AL_NO_ERROR;

		bool ret = true;

		//stop source
		if(this->isPlaying())
			ret = this->stop();

		// Delete the Sources
		alDeleteSources(1, &this->source);

		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error deleting source: " << this->fileName << "!");
			ret = false;
		}

		// Delete the Buffers
		alDeleteBuffers(1, &this->buffer);

		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error deleting buffer: " << this->fileName << "!");
			ret = false;
		}

		if(this->data && freeData)
		{
			free(this->data);
			this->data = NULL;
		}
		this->initialized = false;
		return ret;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::play()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(this->isPlaying())
			return false;

		ALenum error = AL_NO_ERROR;

		// Begin playing our source file
		alSourcePlay(this->source);
		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error starting source for " << this->fileName << "!");
			return false;
		}

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::stop()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->isPlaying())
			return false;

		ALenum error = AL_NO_ERROR;

		alSourceStop(this->source);
		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error stopping source for " << this->fileName << "!");
			return false;
		}

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::pause()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(this->isPaused())
			return false;

		ALenum error = AL_NO_ERROR;

		alSourcePause(this->source);
		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error pausing source for " << this->fileName << "!");
			return false;
		}

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::resume()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->isPaused())
			return false;

		if(!this->play())
			return false;

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::isPlaying()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->initialized)
			return false;

		ALenum state;
		alGetSourcei(this->source, AL_SOURCE_STATE, &state);

		if(state != AL_PLAYING)
			return false;

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::isPaused()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->initialized)
			return false;

		ALenum state;
		alGetSourcei(this->source, AL_SOURCE_STATE, &state);

		if(state != AL_PAUSED)
			return false;

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	float SoundSource::getPlayPosition()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->isPlaying())
			return 0.f;

		float pos;
		alGetSourcef(this->source, AL_SEC_OFFSET,  &pos);
		return pos;
#else //ORKIGE_OPENAL_SOUND
		return 0.f;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	void SoundSource::setPlayPosition(float pos)
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->initialized)
			return;

		alSourcef(this->source, AL_SEC_OFFSET, pos);
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundSource::setPosition(Ogre::Vector3 const & pos)
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(!this->initialized)
			return false;

		this->position = pos;
		float sourcePosAL[] = {this->position.x, this->position.y, this->position.z};
		alSourcefv(this->source, AL_POSITION, sourcePosAL);

		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
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
	bool SoundSource::isInitialized() const
	{
		return this->initialized;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SoundSource::applyGain()
	{
#ifdef ORKIGE_OPENAL_SOUND
		if(this->initialized)
		{
			alSourcef(this->source, AL_GAIN, this->getEffectiveGain());
		}
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------


	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(SoundSource)
	OOBJECT_END
}
