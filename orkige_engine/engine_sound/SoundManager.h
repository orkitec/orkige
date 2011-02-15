/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __SoundManager_h__31_8_2010__13_58_35__
#define __SoundManager_h__31_8_2010__13_58_35__

#include "engine_sound/SoundSource.h"

namespace Orkige
{
	//! @brief Basic Sound Management
	//! @remarks currently no dynamic source sharing/reusing, streaming, priorities, etc and only AL_MAX_SOURCES concurrent SoundSources can be active
	class SoundManager : public Singleton<SoundManager>, public Interface
#ifdef ORKIGE_OGGSOUNDMANAGER
		, public OgreOggSound::OgreOggSoundManager
#endif
	{
		OOBJECT(SoundManager,Interface);
		DECL_OSINGLETON(SoundManager)
		//--- Types -------------------------------------------------
	public:
	protected:
#ifdef ORKIGE_OGGSOUNDMANAGER
#if OGREOGGSOUND_STATIC
		OgreOggSound::OgreOggSoundPlugin* plugin;
#endif
#else
		typedef std::map<String, optr<SoundSource> > SoundRegistry;		//!< registry of sound sources with id's
		typedef std::map<String, float>	InterruptedSoundRegistry;		//!< interrupted sound ids and play offset
#endif
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
#ifndef ORKIGE_OGGSOUNDMANAGER
		SoundRegistry				sounds;				//!< created SoundSource collection
		InterruptedSoundRegistry	interruptedSounds;	//!< all currently interrupted sounds
#endif
		Ogre::Camera*				listener;			//!< optional sound listener

	private:
		//--- Methods -----------------------------------------------
	public:
		//! create SoundManager with optional listener
		SoundManager(Ogre::Camera* soundListener = NULL);
		//! destructor
		virtual ~SoundManager();
		//! init sound system
		bool init();
		//! update sounds and listener position
		void update(float delta = 0.f);
		//! deinit sound system
		bool deinit();
		//! create a SoundSource
		SoundSourcePtr createSound(String const & id, String const & fileName, bool loop = false, Ogre::Vector3 const & pos = Ogre::Vector3::ZERO,  bool stream = false, bool preBuffer=false);
#ifndef ORKIGE_OGGSOUNDMANAGER
		//! destroy a SoundSource
		bool destroySound(String const & id);
		//! does sound with given id exist
		bool hasSound(String const & id);
		//! get SoundSource with given id
		SoundSourcePtr getSound(String const & id);
#endif
		//! play sound
		bool playSound(String const & id);
		//! stop sound
		bool stopSound(String const & id);
		//! is given sound playing
		bool isPlaying(String const & id);
		//! pause all sounds
		void pause();
		//! resume all sounds
		void resume();
		//! sound interruption begin (iPhone call etc.)
		void onInterruptBegin();
		//! sound interruption end
		void onInterruptEnd();
	protected:
		//! init openAl
		bool initOpenAl();
		//! deinit openAl
		bool deinitOpenAl();
	private:
	};
	//---------------------------------------------------------------
}

#endif //__SoundManager_h__31_8_2010__13_58_35__