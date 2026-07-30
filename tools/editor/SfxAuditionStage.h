/********************************************************************
	created:	Wednesday 2026/07/29 at 12:00
	filename: 	SfxAuditionStage.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SfxAuditionStage_h__29_7_2026__12_00_00__
#define __SfxAuditionStage_h__29_7_2026__12_00_00__

//! @file SfxAuditionStage.h
//! @brief the editor's HEAR-IT affordance for procedural sounds: render a
//! `.osfx` description and play it, right now, without a play session
//! @remarks The sibling of MeshPreviewStage / AnimationPreviewStage - a small
//! owned stage the Inspector drives so a designer tunes a number and hears the
//! result immediately. It reuses the ENGINE's audio path rather than inventing
//! an editor-local one: the pure synthesizer renders PCM (core_util/SfxSynth)
//! and `SoundManager::createSoundFromPCM` uploads it, so what the editor plays
//! is what the game will play.
//!
//! The editor has no audio otherwise, so the device is opened LAZILY on the
//! first audition and stays for the session - an editor that never auditions
//! never touches the sound hardware. Without a device the stage refuses
//! honestly (available() is false, message() says why) instead of pretending.

#include <core_util/SfxDesc.h>
#include <core_util/String.h>

#include <memory>
#include <string>

namespace Orkige
{
	class SoundManager;
}

namespace OrkigeEditor
{
	//! @brief plays a procedural sound description on demand (@see the file doc)
	class SfxAuditionStage
	{
	public:
		SfxAuditionStage();
		~SfxAuditionStage();

		SfxAuditionStage(SfxAuditionStage const &) = delete;
		SfxAuditionStage & operator=(SfxAuditionStage const &) = delete;

		//! @brief render @p desc and play it (replacing whatever was playing).
		//! @return false when no audio device could be opened - message()
		//! carries the honest reason and nothing was played.
		bool audition(Orkige::SfxDesc const & desc);
		//! stop the auditioned sound (safe when nothing plays)
		void stop();
		//! is the auditioned sound still running?
		bool isPlaying() const;
		//! @brief did the audio device come up? (false BEFORE the first
		//! audition attempt too - the device opens lazily)
		bool available() const;
		//! the last outcome, ready to show in the panel ("" before the first try)
		std::string const & message() const;
		//! the rendered length of the last audition in seconds (0 = none yet)
		float lastDurationSec() const;
		//! the byte size of the last rendered audition (0 = none yet)
		int lastByteSize() const;
		//! release the device (called at editor teardown)
		void shutdown();

	private:
		//! open the audio device once, on first use
		bool ensureAudio();

		std::unique_ptr<Orkige::SoundManager> mSound;
		bool			mTriedInit = false;		//!< the lazy open already ran
		bool			mAvailable = false;		//!< a device came up
		std::string		mMessage;				//!< the last outcome
		float			mLastDuration = 0.0f;
		int				mLastBytes = 0;
		unsigned int	mPlayCounter = 0;		//!< a fresh source id per audition
		std::string		mCurrentId;				//!< the source playing now ("" = none)
	};
}

#endif //__SfxAuditionStage_h__29_7_2026__12_00_00__
