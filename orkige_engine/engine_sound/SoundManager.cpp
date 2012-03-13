/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_sound/SoundManager.h"
#include <core_util/foreach.h>
#include <engine_graphic/Engine.h>

#ifdef ORKIGE_IPHONE
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/ExtendedAudioFile.h>
#endif

#ifdef ORKIGE_OPENAL_SOUND
extern "C" int OpenAL_LoadLibrary( void );
#endif //ORKIGE_OPENAL_SOUND

namespace Orkige
{
#ifdef ORKIGE_IPHONE
	void SoundManagerInterruptionListener(void * inClientData, UInt32 inInterruptionState)
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
#ifdef ORKIGE_OPENAL_SOUND
	, context(0)
#endif //ORKIGE_OPENAL_SOUND
	{
#ifdef ORKIGE_OGGSOUNDMANAGER
		this->ms_Singleton = this->singleton;
#endif
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
#if defined(WIN32) && defined(ORKIGE_OPENAL_SOUND)
		//int openAlLibraryLoaded = OpenAL_LoadLibrary();
		//oAssert(openAlLibraryLoaded);
#endif
#endif
#ifdef ORKIGE_OGGSOUNDMANAGER
#if OGREOGGSOUND_STATIC
		this->plugin = new OgreOggSound::OgreOggSoundPlugin();
		Ogre::Root::getSingleton().installPlugin(this->plugin);
#endif
#endif

		
		try
		{
			this->isInitialized = true;
			this->isInitialized = this->initOpenAl();	
		}
		catch (...)
		{
			this->isInitialized = false;
		}
		//this->isInitialized= false;
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

#ifdef ORKIGE_OGGSOUNDMANAGER
		OgreOggSound::OgreOggSoundManager::update(delta);
		if(this->listener)
		{
			if(!this->getListener()->isAttached())
			{
				this->getListener()->setPosition(this->listener->getRealPosition());
			}
		}
#else
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
#endif
	}
	//---------------------------------------------------------
	bool SoundManager::deinit()
	{
#ifdef ORKIGE_OGGSOUNDMANAGER
#if OGREOGGSOUND_STATIC
		this->plugin = new OgreOggSound::OgreOggSoundPlugin();
		Ogre::Root::getSingleton().uninstallPlugin(this->plugin);
		delete this->plugin;
#endif
#else
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
#endif
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
#ifdef ORKIGE_OGGSOUNDMANAGER
		SoundSourcePtr sound = OgreOggSound::OgreOggSoundManager::createSound(id, fileName, stream, loop, preBuffer, Engine::getSingleton().getSceneManager());
		sound->setPosition(pos);
		return sound;
#else
		SoundRegistry::const_iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{

			optr<SoundSource> sound = onew(new SoundSource(id, fileName, loop, pos));
			sound->init();
			this->sounds[id] = sound;
			return sound;
		}
		return it->second;
#endif
	}
	//---------------------------------------------------------
#ifndef ORKIGE_OGGSOUNDMANAGER
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
#endif
	//---------------------------------------------------------
	bool SoundManager::playSound(String  const & id)
	{
		if (!this->isInitialized)
		{
			return false;
		}
#ifdef ORKIGE_OGGSOUNDMANAGER
		SoundSourcePtr sound = this->getSound(id);
		if(sound)
		{
			if(!sound->isPlaying())
			{
				sound->play();
				return sound->isPlaying();
			}
		}
		return false;
#else
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		it->second->play();
		bool success = it->second->isPlaying();
		return success;
#endif
	}
	//---------------------------------------------------------
	bool SoundManager::stopSound(String  const & id)
	{	
		if (!this->isInitialized)
		{
			return false;
		}
#ifdef ORKIGE_OGGSOUNDMANAGER
		SoundSourcePtr sound = this->getSound(id);
		if(sound)
		{
			if(sound->isPlaying())
			{
				sound->stop();
				return true;
			}
		}
		return false;
#else
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		it->second->stop();
		bool success = !it->second->isPlaying();
		return success;
#endif
	}
	//---------------------------------------------------------
	bool SoundManager::isPlaying(String const & id)
	{
#ifdef ORKIGE_OGGSOUNDMANAGER
		SoundSourcePtr sound = this->getSound(id);
		if(sound)
		{
			return sound->isPlaying();
		}
		return false;
#else
		SoundRegistry::iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			oDebugMsg("sound",0,"Sound not found: " << id <<"!");
			return false;
		}
		bool playing = it->second->isPlaying();
		return playing;
#endif
	}
	//---------------------------------------------------------
	void SoundManager::pause()
	{
		if (!this->isInitialized)
		{
			return;
		}
#ifdef ORKIGE_OGGSOUNDMANAGER
		this->pauseAllSounds();
#else
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			vt.second->pause();
		}
#endif
	}
	//---------------------------------------------------------
	void SoundManager::resume()
	{
		if (!this->isInitialized)
		{
			return;
		}
#ifdef ORKIGE_OGGSOUNDMANAGER
		this->resumeAllPausedSounds();
#else
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			vt.second->resume();
		}
#endif
	}
	//---------------------------------------------------------
	void SoundManager::onInterruptBegin()
	{
#ifdef ORKIGE_OGGSOUNDMANAGER
#	ifdef ORKIGE_IPHONE
		// Deactivate the current audio session
		AudioSessionSetActive(false);
#	endif //ORKIGE_IPHONE
		oAssert(this->context);
		// set the current context to NULL will 'shutdown' openAL
		alcMakeContextCurrent(NULL);
		ALenum err = alGetError();
		if (err != 0) 
		{
			oDebugMsg("sound", 0, "Error Calling alcMakeContextCurrent. Error: " << err);
		}
		// now suspend your context to 'pause' your sound world
		alcSuspendContext(this->context);
		err = alGetError();
		if (err != 0) 
		{
			oDebugMsg("sound", 0, "Error Calling alcSuspendContext. Error: " << err);
		}
#else
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
#endif
	}
	//---------------------------------------------------------
	void SoundManager::onInterruptEnd()
	{
#ifdef ORKIGE_OGGSOUNDMANAGER
#ifdef ORKIGE_IPHONE
		// Reset audio session
		UInt32 category = kAudioSessionCategory_AmbientSound;
		bool result = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(category), &category);
		//Sleep(1);
		if (result) 
		{
			oDebugMsg("sound", 0, "Error setting audio session category! " << result);
			return;
		}
		else 
		{
			// Reactivate the current audio session
			result = AudioSessionSetActive(true);
			//Sleep(1);
			if (result) 
			{
				oDebugMsg("sound", 0, "Error setting audio session active! " << result);
				return;
			}
		}
#endif
		oAssert(this->context);
		// Restore open al context
		alcMakeContextCurrent(this->context);
		//Sleep(1);
		ALenum err = alGetError();
		if (err != 0) 
		{
			oDebugMsg("sound", 0, "Error Calling alcMakeContextCurrent. Error: " << err);
		}
		// 'unpause' my context
		alcProcessContext(this->context);
		//Sleep(1);
		err = alGetError();
		if (err != 0) 
		{
			oDebugMsg("sound", 0, "Error Calling alcProcessContext. Error: " << err);
		}
#else
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
#endif
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool SoundManager::initOpenAl()
	{
#ifdef ORKIGE_OPENAL_SOUND
#ifdef ORKIGE_OGGSOUNDMANAGER
		String devicename;
#ifdef WIN32
		devicename = "Generic Software";
#endif
		if(!OgreOggSound::OgreOggSoundManager::init(devicename, 100, 64, Engine::getSingleton().getSceneManager()))
			return false;
		this->context = alcGetCurrentContext();
		oAssert(this->context);
#else
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
#endif
		return true;
#else //ORKIGE_OPENAL_SOUND
		return false;
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------	
	bool SoundManager::deinitOpenAl()
	{
#ifdef ORKIGE_OPENAL_SOUND
#ifndef ORKIGE_OGGSOUNDMANAGER
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
#endif
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
