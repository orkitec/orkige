/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// OpenAL Soft port notes:
// - the OgreOggSound backend (ORKIGE_OGGSOUNDMANAGER) is gone together with
//   its vendored dependency; streaming/ogg support is future work
// - the iOS AudioSession interruption wiring (AudioSessionInitialize & co)
//   was removed from the iOS SDK long ago; AVAudioSession based handling
//   returns with the mobile phase and should call onInterruptBegin/End
// - the Windows OpenAL_LoadLibrary loader shim is obsolete: openal-soft is
//   linked directly on every platform

#include "engine_sound/SoundManager.h"
#include <core_util/foreach.h>

#include <algorithm>
#include <cstdlib>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundManager::SoundManager(optr<RenderNode> const & soundListener) : isInitialized(false)
#ifdef ORKIGE_OPENAL_SOUND
	, context(0)
#endif //ORKIGE_OPENAL_SOUND
	, listener(soundListener)
	, masterVolume(1.f)
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
		OPROFILE("sound.update");
		if (!this->isInitialized)
		{
			return;
		}

		if(this->listener)
		{
#ifdef ORKIGE_OPENAL_SOUND
			// the listener node's world pose: forward = -Z, up = +Y (the
			// same convention cameras attach with)
			const Vec3 pos = this->listener->getWorldPosition();
			alListener3f(AL_POSITION, pos.x, pos.y, pos.z);

			const Quat pose = this->listener->getWorldOrientation();
			Vec3 dir = pose * Vec3::NEGATIVE_UNIT_Z;
			Vec3 up = pose * Vec3::UNIT_Y;

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

		// streamed music: refill each track's ring on the main thread (the
		// 4x0.5s AL cushion tolerates the occasional long frame)
		this->updateMusic();
	}
	//---------------------------------------------------------
	void SoundManager::updateMusic()
	{
		if (!this->isInitialized)
		{
			return;
		}
		foreach(MusicRegistry::value_type const & vt, music)
		{
			vt.second->update();
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
			// sources registered while OpenAL was down own no AL objects
			if(src->isInitialized())
			{
				src->deinit();
			}
		}
		this->sounds.clear();
		// streamed tracks free their AL objects + decoder in their destructor
		this->music.clear();
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
	SoundSourcePtr SoundManager::createSound(String const & id, String const & fileName, bool loop, Vec3 const & pos, bool stream, bool preBuffer)
	{
		SoundRegistry::const_iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{

			optr<SoundSource> sound = onew(new SoundSource(id, fileName, loop, pos));
			// AL objects only exist while OpenAL is up; an uninitialized
			// manager still REGISTERS the source (headless tests exercise the
			// gain model this way) - it just stays silent
			if(this->isInitialized)
			{
				sound->init();
			}
			// a new source starts in its default group ("sfx") - push that
			// group's current volume
			sound->setGroupVolume(this->getGroupVolume(sound->getGroup()));
			this->sounds[id] = sound;
			return sound;
		}
		return it->second;
	}
	//---------------------------------------------------------
	SoundSourcePtr SoundManager::createSoundFromPCM(String const & id, void const * pcmData, int dataSize, int channels, int bitsPerSample, int sampleRate, bool loop, Vec3 const & pos)
	{
		SoundRegistry::const_iterator it = this->sounds.find(id);
		if(it == this->sounds.end())
		{
			optr<SoundSource> sound = onew(new SoundSource(id, StringUtil::BLANK, loop, pos));
			if(this->isInitialized)
			{
				sound->initFromPCM(pcmData, dataSize, channels, bitsPerSample, sampleRate);
			}
			sound->setGroupVolume(this->getGroupVolume(sound->getGroup()));
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
	//--- streamed music --------------------------------------
	//---------------------------------------------------------
	bool SoundManager::playMusic(String const & id, String const & fileName, bool loop)
	{
		MusicRegistry::const_iterator it = this->music.find(id);
		if(it != this->music.end())
		{
			// an already-registered track: keep it playing (idempotent)
			return it->second->isPlaying();
		}
		optr<MusicStream> stream = onew(new MusicStream(id, fileName, loop));
		// AL objects + decoding only happen while OpenAL is up; an
		// uninitialized manager still REGISTERS the track (the gain model stays
		// queryable headlessly), exactly like createSound does for SoundSource
		bool started = false;
		if(this->isInitialized)
		{
			if(stream->open())
			{
				started = stream->play();
			}
			else
			{
				oDebugMsg("sound",0,"Music failed to open: " << fileName << "!");
			}
		}
		// a new track sits in the "music" group - push that group's volume
		stream->setGroupVolume(this->getGroupVolume(stream->getGroup()));
		this->music[id] = stream;
		return started;
	}
	//---------------------------------------------------------
	bool SoundManager::stopMusic(String const & id)
	{
		MusicRegistry::iterator it = this->music.find(id);
		if(it == this->music.end())
		{
			return false;
		}
		if(it->second->isPrimed())
		{
			it->second->stop();
		}
		this->music.erase(it);
		return true;
	}
	//---------------------------------------------------------
	void SoundManager::stopAllMusic()
	{
		foreach(MusicRegistry::value_type const & vt, music)
		{
			if(vt.second->isPrimed())
			{
				vt.second->stop();
			}
		}
		this->music.clear();
	}
	//---------------------------------------------------------
	bool SoundManager::isMusicPlaying(String const & id) const
	{
		MusicRegistry::const_iterator it = this->music.find(id);
		if(it == this->music.end())
		{
			return false;
		}
		return it->second->isPlaying();
	}
	//---------------------------------------------------------
	void SoundManager::setMusicVolume(String const & id, float baseGain)
	{
		MusicRegistry::const_iterator it = this->music.find(id);
		if(it != this->music.end())
		{
			it->second->setBaseGain(baseGain);
		}
	}
	//---------------------------------------------------------
	MusicStreamPtr SoundManager::getMusic(String const & id) const
	{
		MusicRegistry::const_iterator it = this->music.find(id);
		if(it == this->music.end())
		{
			return oNULL(MusicStream);
		}
		return it->second;
	}
	//---------------------------------------------------------
	std::vector<SoundManager::MusicTrackInfo> SoundManager::snapshotMusic() const
	{
		std::vector<MusicTrackInfo> out;
		out.reserve(this->music.size());
		foreach(MusicRegistry::value_type const & vt, music)
		{
			optr<MusicStream> const & stream = vt.second;
			MusicTrackInfo info;
			info.id = vt.first;
			info.file = stream->getFile();
			info.playing = stream->isPlaying();
			info.positionSec = stream->getPlayPosition();
			info.durationSec = stream->getDuration();
			info.baseGain = stream->getBaseGain();
			info.groupVolume = this->getGroupVolume(stream->getGroup());
			info.effectiveGain = stream->getEffectiveGain();
			info.loop = stream->isLoop();
			out.push_back(info);
		}
		return out;
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
		foreach(MusicRegistry::value_type const & vt, music)
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
		foreach(MusicRegistry::value_type const & vt, music)
		{
			vt.second->resume();
		}
	}
	//---------------------------------------------------------
	void SoundManager::setGroupVolume(String const & group, float volume)
	{
		volume = std::clamp(volume, 0.f, 1.f);
		this->groupVolumes[group] = volume;
		// recompute every source of the group (effective = base * group)
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			if(vt.second->getGroup() == group)
			{
				vt.second->setGroupVolume(volume);
			}
		}
		// streamed tracks share the same mixer model - the "music" group's
		// volume must reach them too, so sound.setGroupVolume("music", ...) and
		// tween.volume("music", ...) transparently control streamed volume
		foreach(MusicRegistry::value_type const & vt, music)
		{
			if(vt.second->getGroup() == group)
			{
				vt.second->setGroupVolume(volume);
			}
		}
	}
	//---------------------------------------------------------
	float SoundManager::getGroupVolume(String const & group) const
	{
		GroupVolumeMap::const_iterator it = this->groupVolumes.find(group);
		return it == this->groupVolumes.end() ? 1.f : it->second;
	}
	//---------------------------------------------------------
	void SoundManager::setMasterVolume(float volume)
	{
		this->masterVolume = std::clamp(volume, 0.f, 1.f);
#ifdef ORKIGE_OPENAL_SOUND
		// ONE call scales the whole mix - the listener gain; reapplied by
		// initOpenAl after interruption reinits
		if(this->isInitialized)
		{
			alListenerf(AL_GAIN, this->masterVolume);
		}
#endif //ORKIGE_OPENAL_SOUND
	}
	//---------------------------------------------------------
	float SoundManager::getMasterVolume() const
	{
		return this->masterVolume;
	}
	//---------------------------------------------------------
	void SoundManager::setSoundGroup(SoundSourcePtr const & sound, String const & group)
	{
		if(!sound)
		{
			return;
		}
		sound->setGroup(group);
		sound->setGroupVolume(this->getGroupVolume(group));
	}
	//---------------------------------------------------------
	void SoundManager::applySettings(std::map<String, String> const & settings)
	{
		static const String masterKey = "audio.master";
		static const String groupPrefix = "audio.group.";
		for(std::map<String, String>::const_iterator it = settings.begin(),
			itend = settings.end(); it != itend; ++it)
		{
			// honest parsing: a malformed value reads as 0 via strtof - the
			// keys are tool-written floats, not user-facing free text
			if(it->first == masterKey)
			{
				this->setMasterVolume(std::strtof(it->second.c_str(), NULL));
			}
			else if(it->first.compare(0, groupPrefix.size(), groupPrefix) == 0 &&
				it->first.size() > groupPrefix.size())
			{
				this->setGroupVolume(it->first.substr(groupPrefix.size()),
					std::strtof(it->second.c_str(), NULL));
			}
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
			if(src->isInitialized())
			{
				src->deinit();
			}
		}
		// streamed tracks release their AL objects too (they die with the
		// context); the decoder + resident bytes are kept for the restore below
		foreach(MusicRegistry::value_type const & vt, music)
		{
			vt.second->suspend();
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

		// recreate each streamed track's AL ring and resume the ones that were
		// playing when the interruption began
		foreach(MusicRegistry::value_type const & vt, music)
		{
			vt.second->restore();
		}
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
		// a fresh context starts at listener gain 1 - reapply the mixer's
		// master volume (matters on the interruption reinit path)
		alListenerf(AL_GAIN, this->masterVolume);
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
