#include <stdio.h>
#ifdef WIN32
#include <windows.h>
#endif
#include <iostream>
#include "Application.h"
#include <core_debug/Profile.h>
//#include "ConsoleCommands.h"

namespace boost
{
	void throw_exception(class std::exception const &)
	{

	}
}

#ifndef ORKIGE_BROWSERPLUGIN
#ifdef ORKIGE_EXTERN_LOG
void __orkige_debug_msg(std::string const & msg)
{
	OutputDebugStringA(msg.c_str());
}
#endif
#endif
#ifndef ORKIGE_IPHONE

#ifdef WIN32
INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, INT)
#else
int main(int argc, char **argv)
#endif
{
	OLOAD_MODULE_STATIC(orkige_core);
	OLOAD_MODULE_STATIC(orkige_engine);
//	OLOAD_MODULE_STATIC(ChaosCreatures);

	CC::Application app;

	if(!app.init(Ogre::String(lpCmdLine)))
		return -1;

//	KS::initConsoleCommands();

	while (app.run())
	{
	}

	app.deinit();

	Orkige::ProfileManager::getSingleton().debugOutput();

	return 0;
}

#else
#import <UIKit/UIKit.h>
#define USE_CADISPLAYLINK 1
int main(int argc, char **argv)
{
	NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
	int retVal = UIApplicationMain(argc, argv, @"UIApplication", @"AppDelegate");
	[pool release];
	return retVal;
}

//copy from ogre3d samplebrowser
@interface AppDelegate : NSObject <UIApplicationDelegate>
{
	NSTimer *timer;
	CC::Application app;

	// Use of the CADisplayLink class is the preferred method for controlling your animation timing.
	// CADisplayLink will link to the main display and fire every vsync when added to a given run-loop.
	// The NSTimer class is used only as fallback when running on a pre 3.1 device where CADisplayLink
	// isn't available.
	id displayLink;
	NSDate* date;
	NSTimeInterval lastFrameTime;
	bool isDisplayLinkSupported;
	bool isActive;

}

- (void)go;

@property (retain) NSTimer *timer;
@property (nonatomic) NSTimeInterval lastFrameTime;

@end

@implementation AppDelegate


@synthesize timer;
@dynamic lastFrameTime;

- (NSTimeInterval)mLastFrameTime
{
	return lastFrameTime;
}

- (void)setLastFrameTime:(NSTimeInterval)frameInterval
{
	// Frame interval defines how many display frames must pass between each time the
	// display link fires. The display link will only fire 30 times a second when the
	// frame internal is two on a display that refreshes 60 times a second. The default
	// frame interval setting of one will fire 60 times a second when the display refreshes
	// at 60 times a second. A frame interval setting of less than one results in undefined
	// behavior.
	if (frameInterval >= 1)
	{
		lastFrameTime = frameInterval;
	}
}

- (void)go 
{

	NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

	if (isDisplayLinkSupported)
	{
		// CADisplayLink is API new to iPhone SDK 3.1. Compiling against earlier versions will result in a warning, but can be dismissed
		// if the system version runtime check for CADisplayLink exists in -initWithCoder:. The runtime check ensures this code will
		// not be called in system versions earlier than 3.1.
		date = [[NSDate alloc] init];
		lastFrameTime = -[date timeIntervalSinceNow];

		displayLink = [NSClassFromString(@"CADisplayLink") displayLinkWithTarget:self selector:@selector(stepOneFrame:)];
		[displayLink setFrameInterval:lastFrameTime];
		[displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
	}
	else
	{
		timer = [NSTimer scheduledTimerWithTimeInterval:(NSTimeInterval)(1.0f / 60.0f) * lastFrameTime target:self selector:@selector(stepOneFrame:) userInfo:nil repeats:YES];
	}

	[pool release];
	
	if(!app.init())
		return;
	
	KS::initConsoleCommands();

	isActive = true;
}


- (void)applicationDidFinishLaunching:(UIApplication *)application 
{
	// Hide the status bar
	[[UIApplication sharedApplication] setStatusBarHidden:YES];

	isDisplayLinkSupported = false;
	lastFrameTime = 1;
	displayLink = nil;
	timer = nil;
	isActive = false;

	OLOAD_MODULE_STATIC(orkige_core);
	OLOAD_MODULE_STATIC(orkige_engine);
	OLOAD_MODULE_STATIC(ChaosCreatures);
	


	
	// A system version of 3.1 or greater is required to use CADisplayLink. The NSTimer
	// class is used as fallback when it isn't available.
#if USE_CADISPLAYLINK
	NSString *reqSysVer = @"3.1";
	NSString *currSysVer = [[UIDevice currentDevice] systemVersion];
	if ([currSysVer compare:reqSysVer options:NSNumericSearch] != NSOrderedAscending)
		isDisplayLinkSupported = TRUE;
#endif

	[self go];
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
	isActive = true;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
	isActive = false;
}

- (void)stepOneFrame:(id)sender
{
	if(isActive)
	{
		if (isDisplayLinkSupported)
		{
			// NSTimerInterval is a simple typedef for double
			NSTimeInterval currentFrameTime = -[date timeIntervalSinceNow];
			NSTimeInterval differenceInSeconds = currentFrameTime - lastFrameTime;
			lastFrameTime = currentFrameTime;

			app.run();
		}
		else
		{	
			float t = (float)[timer timeInterval];
			app.run();
		}
	}
}

- (void)applicationWillTerminate:(UIApplication *)application 
{
	app.deinit();
	Orkige::ProfileManager::getSingleton().debugOutput();
}

- (void)dealloc 
{
	if (isDisplayLinkSupported)
	{
		[date release];
		date = nil;

		[displayLink invalidate];
		displayLink = nil;
	}
	else
	{
		[timer invalidate];
		timer = nil;
	}

	[super dealloc];
}


@end
#endif