/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_sound/SoundManager.h"
#include "engine_sound/SoundError.h"
#include <core_util/foreach.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Orkige
{
	namespace
	{
		//! the env name the whole engine (and its child processes) reads
		char const * const kAudioBackendEnv = "ORKIGE_AUDIO_BACKEND";
		//! what kind of run this process is (@see SoundManager::setAutomatedRun)
		bool gAutomatedRun = false;

		//! set an environment variable on this process (and everything it
		//! spawns from here on)
		void publishEnv(char const * name, char const * value)
		{
#ifdef _WIN32
			_putenv_s(name, value);
#else
			setenv(name, value, 1 /*overwrite*/);
#endif
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundManager::SoundManager(optr<RenderNode> const & soundListener) : isInitialized(false)
	, listener(soundListener)
	, masterVolume(1.f)
	{
		oInfo("...SoundManager created!...");
	}
	//---------------------------------------------------------
	SoundManager::~SoundManager()
	{
		// the runtimes own the manager as a stack local and never call
		// deinit(), so this is the only teardown at shutdown: the voices go
		// first, then the device they live on
		if (this->isInitialized)
		{
			this->deinit();
		}
		oInfo("...SoundManager destroyed!...");
	}
	//---------------------------------------------------------
	IMPL_OSINGLETON(SoundManager)
	//---------------------------------------------------------
	bool SoundManager::init()
	{
		try
		{
			this->isInitialized = this->initAudioDevice();
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
			// the listener node's world pose: forward = -Z, up = +Y (the
			// same convention cameras attach with)
			const Vec3 pos = this->listener->getWorldPosition();
			const Quat pose = this->listener->getWorldOrientation();
			const Vec3 dir = pose * Vec3::NEGATIVE_UNIT_Z;
			const Vec3 up = pose * Vec3::UNIT_Y;
			AudioBackend::engineSetListener(pos.x, pos.y, pos.z,
				dir.x, dir.y, dir.z, up.x, up.y, up.z);
		}

		// streamed music: refill each track's ring on the main thread (the
		// 4x0.5s cushion tolerates the occasional long frame)
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
			// sources registered while audio was down own no voice
			if(src->isInitialized())
			{
				src->deinit();
			}
		}
		this->sounds.clear();
		// streamed tracks free their voice + decoder in their destructor
		this->music.clear();
		if (this->isInitialized)
		{
			this->isInitialized = false;
			return this->deinitAudioDevice();
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
			// a voice only exists while the device is up; an uninitialized
			// manager still REGISTERS the source (headless tests exercise the
			// gain model this way) - it just stays silent
			if(this->isInitialized)
			{
				// a sound file that cannot be read or (for a procedural
				// `.osfx`) cannot be parsed leaves a SILENT source instead of
				// unwinding into game code: the same shape as running with no
				// audio device, so one bad asset costs its own sound and
				// nothing else. SoundSource::init already logged the reason.
				try
				{
					sound->init();
				}
				catch(SoundError const & e)
				{
					oDebugError("sound", 0, "sound '" << id << "' from '"
						<< fileName << "' stays silent: " << e.what());
				}
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
		// the voice + decoding only happen while the device is up; an
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
		// ONE call scales the whole mix - the graph's own volume; reapplied
		// by initAudioDevice after an interruption reinit
		if(this->isInitialized)
		{
			AudioBackend::engineSetMasterVolume(this->masterVolume);
		}
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
	void SoundManager::setAutomatedRun(bool automatedRun)
	{
		gAutomatedRun = automatedRun;
		// A CHILD process makes its own decision, and it cannot see this
		// process's boolean - so an automated run states its choice in the
		// environment it hands down. That is what keeps the player an
		// automated editor spawns as silent as the editor itself, without
		// every caller having to remember to pass the flag on.
		if(automatedRun && std::getenv(kAudioBackendEnv) == NULL)
		{
			publishEnv(kAudioBackendEnv, "null");
		}
	}
	//---------------------------------------------------------
	bool SoundManager::isAutomatedRun()
	{
		return gAutomatedRun;
	}
	//---------------------------------------------------------
	bool SoundManager::resolveSilentDevice(char const * backendSetting,
		bool automatedRun)
	{
		if(backendSetting != NULL && backendSetting[0] != '\0')
		{
			// an EXPLICIT setting wins, either way: a developer who wants to
			// hear a scripted run says so and gets the machine's device
			return std::strcmp(backendSetting, "null") == 0 ||
				std::strcmp(backendSetting, "silent") == 0 ||
				std::strcmp(backendSetting, "off") == 0;
		}
		return automatedRun;
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
		// streamed tracks release their voice too (it dies with the device);
		// the decoder + resident bytes are kept for the restore below
		foreach(MusicRegistry::value_type const & vt, music)
		{
			vt.second->suspend();
		}

		//close the device
		this->deinitAudioDevice();
	}
	//---------------------------------------------------------
	void SoundManager::onInterruptEnd()
	{
		//reopen the device
		this->initAudioDevice();

		//rebuild the voices
		foreach(SoundRegistry::value_type const & vt, sounds)
		{
			// a source whose file went away stays silent instead of
			// unwinding into the caller, exactly like createSound
			try
			{
				vt.second->init();
			}
			catch(SoundError const & e)
			{
				oDebugError("sound", 0, "sound '" << vt.first
					<< "' stays silent after the interruption: " << e.what());
			}
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
	bool SoundManager::initAudioDevice()
	{
		// the platform's own default output; the audio backend picks the
		// platform API itself and, on iOS, sets up and activates the audio
		// session before it touches the hardware. An automated run opens the
		// SILENT device instead - silence belongs to the RUN.
		const bool silent = resolveSilentDevice(std::getenv(kAudioBackendEnv),
			gAutomatedRun);
		if(!AudioBackend::engineInit(silent))
		{
			return false;
		}
		oDebugMsg("sound", 0, "audio: opened the "
			<< (silent ? "SILENT device (this run makes no sound)"
				: "machine's own output device"));
		// a fresh graph starts at volume 1 - reapply the mixer's master
		// volume (matters on the interruption reinit path)
		AudioBackend::engineSetMasterVolume(this->masterVolume);
		return true;
	}
	//---------------------------------------------------------
	bool SoundManager::deinitAudioDevice()
	{
		AudioBackend::engineUninit();
		return true;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SoundManager)
	OOBJECT_END
}
