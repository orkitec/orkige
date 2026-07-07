/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

// OpenAL Soft port notes:
// - the OgreOggSound backend (ORKIGE_OGGSOUNDMANAGER) is gone together with
//   its vendored dependency; streaming/ogg support returns in a later phase
// - the iOS AudioSession interruption wiring (AudioSessionInitialize & co)
//   was removed from the iOS SDK long ago; AVAudioSession based handling
//   returns with the mobile phase and should call onInterruptBegin/End
// - the Windows OpenAL_LoadLibrary loader shim is obsolete: openal-soft is
//   linked directly on every platform

#include "engine_sound/SoundManager.h"
#include <core_util/foreach.h>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundManager::SoundManager(Ogre::Camera* soundListener) : isInitialized(false)
#ifdef ORKIGE_OPENAL_SOUND
	, context(0)
#endif //ORKIGE_OPENAL_SOUND
	, listener(soundListener)
	{
		oInfo("...SoundManager created!...");
	}
	//---------------------------------------------------------
	SoundManager::~SoundManager()
	{
		oInfo("...SoundManager destroyed!...");
	}
	//---------------------------------------------------------
	IMPL_OSINGLETON(SoundManager)
	//---------------------------------------------------------
	bool SoundManager::init()
	{
		try
		{
			this->isInitialized = this->initOpenAl();
		}
		catch (...)
		{
			this->isInitialized = false;
		}
		return this->isInitialized;
	}
	//----------------------------------------------------
	bool SoundManager::isinitialised()
	{
		return this->isInitialized;
	}

	//---------------------------------------------------------
	void SoundManager::update(float delta)
	{
		OPROFILEFUNC();
		if (!this->isInitialized)
		{
			return;
		}

		if(this->listener)
		{
#ifdef ORKIGE_OPENAL_SOUND
			Ogre::Vector3 const & pos = this->listener->getDerivedPosition();
			alListener3f(AL_POSITION, pos.x, pos.y, pos.z);

			Ogre::Vector3 dir = this->listener->getDerivedDirection();
			Ogre::Vector3 up = this->listener->getDerivedUp();

			ALfloat orientation[6];
			orientation[0]= dir.x; // Forward.x
			orientation[1]= dir.y; // Forward.y
			orientation[2]= dir.z; // Forward.z

			orientation[3]= up.x; // Up.x
			orientation[4]= up.y; // Up.y
			orientation[5]= up.z; // Up.z

			alListenerfv(AL_ORIENTATION, orientation);
#endif //ORKIGE_OPENAL_SOUND
		}
	}
	//---------------------------------------------------------
	bool SoundManager::deinit()
	{
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			optr<SoundSource> src = vt.second;
			if(src->isPlaying())
			{
				src->stop();
			}
			src->deinit();
		}
		this->sounds.clear();
		if (this->isInitialized)
		{
			return this->deinitOpenAl();
		}
		else
		{
			return true;
		}

	}
	//---------------------------------------------------------
	SoundSourcePtr SoundManager::createSound(String const & id, String const & fileName, bool loop, Ogre::Vector3 const & pos, bool stream, bool preBuffer)
	{
		SoundRegistry::const_iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{

			optr<SoundSource> sound = onew(new SoundSource(id, fileName, loop, pos));
			sound->init();
			this->sounds[id] = sound;
			return sound;
		}
		return it->second;
	}
	//---------------------------------------------------------
	SoundSourcePtr SoundManager::createSoundFromPCM(String const & id, void const * pcmData, int dataSize, int channels, int bitsPerSample, int sampleRate, bool loop, Ogre::Vector3 const & pos)
	{
		SoundRegistry::const_iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			optr<SoundSource> sound = onew(new SoundSource(id, StringUtil::BLANK, loop, pos));
			sound->initFromPCM(pcmData, dataSize, channels, bitsPerSample, sampleRate);
			this->sounds[id] = sound;
			return sound;
		}
		return it->second;
	}
	//---------------------------------------------------------
	bool SoundManager::destroySound(String const & id)
	{
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			return false;
		}
		this->sounds.erase(it);
		return true;
	}
	//---------------------------------------------------------
	bool SoundManager::hasSound(String const & id)
	{
		if(this->sounds.find(id) == this->sounds.end())
		{
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	SoundSourcePtr SoundManager::getSound(String const & id)
	{
		SoundRegistry::const_iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			return oNULL(SoundSource);
		}
		return it->second;
	}
	//---------------------------------------------------------
	bool SoundManager::playSound(String  const & id)
	{
		if (!this->isInitialized)
		{
			return false;
		}
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		it->second->play();
		bool success = it->second->isPlaying();
		return success;
	}
	//---------------------------------------------------------
	bool SoundManager::stopSound(String  const & id)
	{
		if (!this->isInitialized)
		{
			return false;
		}
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		it->second->stop();
		bool success = !it->second->isPlaying();
		return success;
	}
	//---------------------------------------------------------
	bool SoundManager::isPlaying(String const & id)
	{
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		bool playing = it->second->isPlaying();
		return playing;
	}
	//---------------------------------------------------------
	void SoundManager::pause()
	{
		if (!this->isInitialized)
		{
			return;
		}
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			vt.second->pause();
		}
	}
	//---------------------------------------------------------
	void SoundManager::resume()
	{
		if (!this->isInitialized)
		{
			return;
		}
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			vt.second->resume();
		}
	}
	//---------------------------------------------------------
	void SoundManager::onInterruptBegin()
	{
		//backup playing sounds indexes and deinit sources
		this->interruptedSounds.clear();
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			optr<SoundSource> src = vt.second;
			if(src->isPlaying())
			{
				src->pause();
				this->interruptedSounds[vt.first] = src->getPlayPosition();
				src->stop();
			}
			src->deinit();
		}

		//deinit al
		this->deinitOpenAl();
	}
	//---------------------------------------------------------
	void SoundManager::onInterruptEnd()
	{
		//reinit al
		this->initOpenAl();

		//reinit sources
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			vt.second->init();
		}

		//resume interrupted sounds
		foreach(InterruptedSoundRegistry::value_type const & vt, interruptedSounds)
		{
			optr<SoundSource> src = this->sounds[vt.first];
			src->setPlayPosition(vt.second);
			src->play();
		}
		this->interruptedSounds.clear();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool SoundManager::initOpenAl()
	{
#ifdef ORKIGE_OPENAL_SOUND
		// clear any errors
		alGetError();

		// Create a new OpenAL Device
		// Pass NULL to open the system's default output device; OpenAL Soft
		// picks the platform backend itself (the old "DirectSound3D" /
		// "Generic Software" device names died with the Creative runtime)
		ALCdevice* device = alcOpenDevice(NULL);
		if(!device)
		{
			oDebugMsg("sound",0,"Error creating ALCdevice");
			return false;
		}

		// Create a new OpenAL Context
		// The new context will render to the OpenAL Device just created
		this->context = alcCreateContext(device, 0);
		if (!this->context)
		{
			oDebugMsg("sound",0,"Error creating ALCcontext");
			alcCloseDevice(device);
			return false;
		}

		// Make the new context the Current OpenAL Context
		alcMakeContextCurrent(this->context);

		ALenum error;
		if((error = alGetError()) != AL_NO_ERROR)
		{
			oDebugMsg("sound",0,"Error initializing OpenAL!");
			return false;
		}
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	bool SoundManager::deinitOpenAl()
	{
#ifdef ORKIGE_OPENAL_SOUND
		ALCcontext	*context = NULL;
		ALCdevice	*device = NULL;

		//Get active context (there can only be one)
		context = alcGetCurrentContext();
		if(context == NULL)
		{
			oDebugMsg("sound", 0, "");
			return false;
		}
		//Get device for active context
		device = alcGetContextsDevice(context);
		if(device == NULL)
		{
			return false;
		}
		//A context must not be current when it gets destroyed
		alcMakeContextCurrent(NULL);
		//Release context
		alcDestroyContext(context);
		//Close device
		alcCloseDevice(device);
		this->context = NULL;
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SoundManager)
	OOBJECT_END
}
