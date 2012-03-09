/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundSource.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef ORKIGE_OGGSOUNDMANAGER
#include "engine_sound/SoundSource.h"
#include "engine_sound/SoundError.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundSource::SoundSource(String const & id, String const & file, bool loop, Ogre::Vector3 const & pos)
		: Object(id), fileName(file), looped(loop), position(pos), initialized(false), data(NULL)
	{

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
			if(alwaysFreeData)
			{
				free(this->data);
				this->data = NULL;
			}
			oAssert(this->data);
			SoundUtil::alBufferDataPlatform(this->buffer, format, data, size, freq);

			error = alGetError();
			SoundError::call(error == AL_NO_ERROR, "Error loading sound: " + this->fileName + "!", error);
		}
		catch(SoundError const & e)
		{
			if(this->buffer != NULL) 
			{
				if (alIsBuffer(this->buffer) == AL_TRUE)
				{
					alDeleteBuffers(1, &this->buffer);
					error = alGetError();
					SoundError::call(error == AL_NO_ERROR, "Failed to delete Buffer", error);
				}
			}

			// Prevent the ~Sound() destructor from double-freeing.
			this->buffer = NULL;
			// Propagate.
			throw (e);
		}

		// Turn Looping ON or OFF
		alSourcei(this->source, AL_LOOPING, this->looped ? AL_TRUE : AL_FALSE);

		// Set Source Position
		float sourcePosAL[] = {this->position.x, this->position.y, this->position.z};
		alSourcefv(this->source, AL_POSITION, sourcePosAL);

		//@TODO: make this configurable with SoundSource set and get functions
		alSourcef (this->source, AL_PITCH,				1.f);
		alSourcef (this->source, AL_GAIN,				1.f);
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
	//--- protected: ------------------------------------------
	//---------------------------------------------------------


	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(SoundSource)
	OOBJECT_END
}
#endif //ORKIGE_OGGSOUNDMANAGER
