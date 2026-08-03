/**************************************************************
	created:	2026/08/03 at 09:00
	filename: 	AudioSessionApple.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The iOS audio-session bridge behind SoundManager. AVAudioSession is
// Foundation ObjC, so it lives in this .mm (the engine's other Apple shims -
// HapticBridgeApple.mm, SDLNativeWindow.mm - follow the same pattern);
// SoundManager stays plain C++ and calls the extern "C" entry point below.
//
// WHY IT EXISTS: OpenAL Soft does NOT manage the audio session - that is the
// application's job. Its CoreAudio backend initialises an AURemoteIO inside
// alcCreateContext, and on a session that was never activated the iOS 17+
// SIMULATOR leaves that call waiting on the audio server until the RPC
// watchdog fires and abort()s the process ("SetProperty: RPC timeout.
// Apparently deadlocked."). Real hardware tolerates the omission, which is why
// it read as a simulator-only fault; the same missing activation also explains
// the -12985 initialise failures reported when an app is backgrounded during
// load. Activating the session before OpenAL touches the hardware is the fix
// for both.
//
// iOS-only: added to the build only there (macOS manages no session).

#import <AVFoundation/AVFoundation.h>

//! @brief make this process's audio session usable before OpenAL opens a
//! device. @return true when the session is active - a false answer is the
//! caller's cue to stay off the CoreAudio backend rather than risk the
//! watchdog abort.
extern "C" bool orkige_activate_audio_session(char const ** outError)
{
	@autoreleasepool
	{
		AVAudioSession * session = [AVAudioSession sharedInstance];
		NSError * error = nil;
		// AMBIENT: a game's audio is not the user's primary media, so it mixes
		// with whatever they are already playing and honours the ringer
		// switch. This is the engine default; a game wanting to play through
		// silence sets its own category before the sound system starts.
		if(![session setCategory:AVAudioSessionCategoryAmbient error:&error])
		{
			if(outError != 0)
			{
				*outError = "could not set the audio session category";
			}
			return false;
		}
		if(![session setActive:YES error:&error])
		{
			if(outError != 0)
			{
				*outError = "could not activate the audio session";
			}
			return false;
		}
		return true;
	}
}
