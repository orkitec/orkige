/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "engine_sound/SoundManager.h"
#include <core_util/foreach.h>

extern "C" int OpenAL_LoadLibrary( void );

namespace Orkige
{
#ifdef ORKIGE_IPHONE
	void SoundManagerInterruptionListener(	void *	inClientData, UInt32 inInterruptionState)
	{
		SoundManager *THIS = (SoundManager*)inClientData;
		oAssert(THIS);
		oInfo("Sound interruption!");
		if (inInterruptionState == kAudioSessionBeginInterruption)
		{
			THIS->onInterruptBegin();
		}
		else if (inInterruptionState == kAudioSessionEndInterruption)
		{
			THIS->onInterruptEnd();
		}
	}
#endif
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundManager::SoundManager(Ogre::Camera* soundListener) : listener(soundListener)
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
#ifdef ORKIGE_IPHONE
		// setup our audio session
		OSStatus result = AudioSessionInitialize(NULL, NULL, SoundManagerInterruptionListener, this);
		if (result) 
		{
			oDebugMsg("sound", 0, "Error initializing audio session! " << result);
			return false;
		}
		else 
		{
			UInt32 category = kAudioSessionCategory_AmbientSound;
			result = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(category), &category);
			if (result) 
			{
				oDebugMsg("sound", 0, "Error setting audio session category! " << result);
				return false;
			}
			else 
			{
				result = AudioSessionSetActive(true);
				if (result) 
				{
					oDebugMsg("sound", 0, "Error setting audio session active! " << result);
					return false;
				}
			}
		}
#else
#ifdef WIN32
		OpenAL_LoadLibrary();
#endif
#endif
		
		return this->initOpenAl();		
	}
	//---------------------------------------------------------
	void SoundManager::update()
	{
		if(this->listener)
		{
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
		return this->deinitOpenAl();
	}
	//---------------------------------------------------------
	woptr<SoundSource> SoundManager::createSound(String const & id, String const & fileName, bool loop, Ogre::Vector3 const & pos)
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
	woptr<SoundSource> SoundManager::getSound(String const & id)
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
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		bool success = it->second->play();
		return success;
	}
	//---------------------------------------------------------
	bool SoundManager::stopSound(String  const & id)
	{
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		bool success = it->second->stop();
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
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			vt.second->pause();
		}
	}
	//---------------------------------------------------------
	void SoundManager::resume()
	{
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
#ifdef ORKIGE_IPHONE
		//reinit audio
		OSStatus result = AudioSessionSetActive(true);
		if (result)
		{
			oDebugMsg("sound", 0, "Error setting audio session active! " << result);
		}
#endif
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
		// clear any errors
		alGetError();

		// Initialize our OpenAL environment
		ALenum			error;
		ALCcontext		*context = NULL;
		ALCdevice		*device = NULL;
		// try to init directsound device
#ifdef WIN32
		device = alcOpenDevice("DirectSound3D");
#endif
		// Create a new OpenAL Device
		// Pass NULL to specify the system’s default output device
		if(!device)
		{
			device = alcOpenDevice(NULL);
		}
		// still no working device try to init software device
		if(!device)
		{
			device = alcOpenDevice("Generic Software");
		}
		if(!device)
		{
			oDebugMsg("sound",0,"Error creating ALCdevice");
			return false;
		}

		// Create a new OpenAL Context
		// The new context will render to the OpenAL Device just created 
		context = alcCreateContext(device, 0);
		if (!context)
		{
			oDebugMsg("sound",0,"Error creating ALCcontext");
			return false;
		}

		// Make the new context the Current OpenAL Context
		alcMakeContextCurrent(context);

		if((error = alGetError()) != AL_NO_ERROR) 
		{
			oDebugMsg("sound",0,"Error initializing OpenAL!");
			return false;
		}

		return true;
	}
	//---------------------------------------------------------	
	bool SoundManager::deinitOpenAl()
	{
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
		//Release context
		alcDestroyContext(context);
		//Close device
		alcCloseDevice(device);

		return true;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SoundManager)
	OOBJECT_END
}