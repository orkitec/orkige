/**************************************************************
	created:	2026/07/11 at 15:00
	filename: 	AppLifecycle.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "core_game/AppLifecycle.h"

namespace Orkige
{
	//---------------------------------------------------------
	AppLifecycle::AppLifecycle()
		: mPhase(Phase::Foreground)
		, mSimPaused(false)
		, mAudioSuspended(false)
		, mRenderingStopped(false)
		, mTerminating(false)
	{
	}
	//---------------------------------------------------------
	AppLifecycle::Actions AppLifecycle::handle(Event event)
	{
		Actions actions;
		switch (event)
		{
		case Event::WillEnterBackground:
			// the crash-safe cleanup, done while still foregrounded (the OS has
			// not suspended us yet): flush the save, tell the game it is pausing,
			// pause the sim and suspend audio. Guarded so a duplicate WILL - or a
			// WILL after its own DID already folded the work in - is a no-op.
			if (!mSimPaused)
			{
				actions.flushSave = true;
				actions.notifyPause = true;
				actions.suspendAudio = true;
				actions.breadcrumb = "background";
				mSimPaused = true;
				mAudioSuspended = true;
			}
			break;
		case Event::DidEnterBackground:
			// defensive: some platforms deliver DID without a preceding WILL -
			// fold the background cleanup in so the contract still holds
			if (!mSimPaused)
			{
				actions.flushSave = true;
				actions.notifyPause = true;
				actions.suspendAudio = true;
				actions.breadcrumb = "background";
				mSimPaused = true;
				mAudioSuspended = true;
			}
			// now fully backgrounded: stop drawing (mobile GPU work in the
			// background is an OS kill)
			if (!mRenderingStopped)
			{
				mRenderingStopped = true;
			}
			mPhase = Phase::Background;
			break;
		case Event::WillEnterForeground:
			// about to return to the foreground: bring rendering and audio back
			// before the game runs again
			if (mRenderingStopped)
			{
				mRenderingStopped = false;
			}
			if (mAudioSuspended)
			{
				actions.resumeAudio = true;
				mAudioSuspended = false;
			}
			break;
		case Event::DidEnterForeground:
			// defensive: DID may arrive without a preceding WILL - resume
			// rendering/audio here too if they are still down
			if (mRenderingStopped)
			{
				mRenderingStopped = false;
			}
			if (mAudioSuspended)
			{
				actions.resumeAudio = true;
				mAudioSuspended = false;
			}
			// fully foreground: resume the sim (the engine default is
			// resume-running) and let the game react through onAppResume
			if (mSimPaused)
			{
				actions.notifyResume = true;
				actions.breadcrumb = "foreground";
				mSimPaused = false;
			}
			mPhase = Phase::Foreground;
			break;
		case Event::Terminating:
			// the OS is killing the app: one last save flush and a marker crumb
			actions.flushSave = true;
			actions.terminating = true;
			actions.breadcrumb = "terminating";
			mTerminating = true;
			break;
		case Event::LowMemory:
			// memory pressure: a cheap save flush guards against a kill that may
			// follow, plus a marker crumb for the trail
			actions.flushSave = true;
			actions.breadcrumb = "low_memory";
			break;
		}
		return actions;
	}
}
