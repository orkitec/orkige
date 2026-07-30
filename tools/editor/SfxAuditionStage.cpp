/**************************************************************
	created:	2026/07/29 at 12:00
	filename: 	SfxAuditionStage.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file SfxAuditionStage.cpp
//! @brief the editor's procedural-sound audition (@see SfxAuditionStage.h)

#include "SfxAuditionStage.h"

#include <core_util/SfxSynth.h>
#include <engine_sound/SoundManager.h>
#include <engine_sound/SoundSource.h>

#include <cstdio>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	SfxAuditionStage::SfxAuditionStage()
	{
	}
	//---------------------------------------------------------
	SfxAuditionStage::~SfxAuditionStage()
	{
		this->shutdown();
	}
	//---------------------------------------------------------
	bool SfxAuditionStage::ensureAudio()
	{
		if (this->mTriedInit)
		{
			return this->mAvailable;
		}
		this->mTriedInit = true;
		// no listener node: an auditioned sound is UI feedback, not a sound in
		// a scene - it plays at the listener's own position
		this->mSound.reset(new Orkige::SoundManager());
		if (!this->mSound->init())
		{
			this->mSound.reset();
			this->mAvailable = false;
			this->mMessage = "No audio device - this machine cannot preview "
				"sounds (the asset itself is fine).";
			return false;
		}
		this->mAvailable = true;
		return true;
	}
	//---------------------------------------------------------
	bool SfxAuditionStage::audition(Orkige::SfxDesc const & desc)
	{
		if (!this->ensureAudio())
		{
			return false;
		}

		// clamp verdicts are the caller's to show; the render sanitizes anyway
		Orkige::SfxDesc sane = desc;
		std::vector<Orkige::String> notes;
		Orkige::SfxSynth::sanitize(sane, &notes);
		const Orkige::SfxSynth::Pcm pcm = Orkige::SfxSynth::render(sane);
		this->mLastDuration = pcm.durationSec();
		this->mLastBytes = static_cast<int>(pcm.byteSize());
		if (pcm.samples.empty())
		{
			this->mMessage = "The sound rendered no samples.";
			return false;
		}

		this->stop();
		// a fresh registry id per audition: the manager keeps a source's
		// buffer for its lifetime, and a tuned description is new samples
		++this->mPlayCounter;
		this->mCurrentId = "editor.audition." +
			std::to_string(this->mPlayCounter);
		Orkige::SoundSourcePtr source = this->mSound->createSoundFromPCM(
			this->mCurrentId, pcm.samples.data(),
			static_cast<int>(pcm.byteSize()), pcm.channels,
			pcm.bitsPerSample, pcm.sampleRate);
		if (!source || !source->isInitialized())
		{
			this->mMessage = "The audio device refused the rendered samples.";
			this->mCurrentId.clear();
			return false;
		}
		this->mSound->playSound(this->mCurrentId);

		char summary[160];
		std::snprintf(summary, sizeof(summary),
			"Playing %.2fs, %d Hz mono (%d bytes)%s", pcm.durationSec(),
			pcm.sampleRate, this->mLastBytes,
			notes.empty() ? "" : " - values were clamped");
		this->mMessage = summary;
		return true;
	}
	//---------------------------------------------------------
	void SfxAuditionStage::stop()
	{
		if (!this->mSound || this->mCurrentId.empty())
		{
			return;
		}
		this->mSound->stopSound(this->mCurrentId);
		// the source (and its buffer) go with the id - an audition is a
		// one-shot, and the next one renders fresh samples anyway
		this->mSound->destroySound(this->mCurrentId);
		this->mCurrentId.clear();
	}
	//---------------------------------------------------------
	bool SfxAuditionStage::isPlaying() const
	{
		if (!this->mSound || this->mCurrentId.empty())
		{
			return false;
		}
		return this->mSound->isPlaying(this->mCurrentId);
	}
	//---------------------------------------------------------
	bool SfxAuditionStage::available() const
	{
		return this->mAvailable;
	}
	//---------------------------------------------------------
	std::string const & SfxAuditionStage::message() const
	{
		return this->mMessage;
	}
	//---------------------------------------------------------
	float SfxAuditionStage::lastDurationSec() const
	{
		return this->mLastDuration;
	}
	//---------------------------------------------------------
	int SfxAuditionStage::lastByteSize() const
	{
		return this->mLastBytes;
	}
	//---------------------------------------------------------
	void SfxAuditionStage::shutdown()
	{
		if (!this->mSound)
		{
			return;
		}
		this->stop();
		this->mSound->deinit();
		this->mSound.reset();
		this->mAvailable = false;
	}
}
