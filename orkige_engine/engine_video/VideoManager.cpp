/********************************************************************
	created:	Tuesday 2011/02/15 at 11:51
	filename: 	VideoManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_video/VideoManager.h"
#include "engine_video/VideoSoundHandler.h"
#include "engine_graphic/Engine.h"
#include "engine_util/StringUtil.h"
#include "engine_input/InputManager.h"

#ifdef ORKIGE_IPHONE
#include <iphone/iPhoneInputManager.h>
static bool g_StopCalledFromInsideVideoManager = false;
#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>
#import <MediaPlayer/MPMoviePlayerViewController.h>

@interface CustomMoviePlayerViewController : UIViewController 
{		
	MPMoviePlayerController *mp;
	NSURL  *movieURL;
}

- (id)initWithPath:(NSString *)moviePath;
- (void)readyPlayer;
 
@end


//#pragma mark -
//#pragma mark Compiler Directives & Static Variables

@implementation CustomMoviePlayerViewController

/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (id)initWithPath:(NSString *)moviePath
{
	// Initialize and create movie URL
	if (self = [super init])
	{
		movieURL = [NSURL fileURLWithPath:moviePath];    
		[movieURL retain];
	}
	return self;
}

/*---------------------------------------------------------------------------
 * For 3.2 and 4.x devices
 * For 3.1.x devices see moviePreloadDidFinish:
 *--------------------------------------------------------------------------*/
- (void) moviePlayerLoadStateChanged:(NSNotification*)notification 
{
	// Unless state is unknown, start playback
	if ([mp loadState] != MPMovieLoadStateUnknown)
	{
		// Remove observer
		[[NSNotificationCenter 	defaultCenter] 
		 removeObserver:self
		 name:MPMoviePlayerLoadStateDidChangeNotification 
		 object:nil];
		
		// When tapping movie, status bar will appear, it shows up
		// in portrait mode by default. Set orientation to landscape
		[[UIApplication sharedApplication] setStatusBarOrientation:UIInterfaceOrientationLandscapeRight animated:NO];
		
		// Rotate the view for landscape playback
		[[self view] setBounds:CGRectMake(0, 0, Orkige::Engine::getSingleton().getViewort()->getActualWidth(), Orkige::Engine::getSingleton().getViewort()->getActualHeight())];
		[[self view] setCenter:CGPointMake(Orkige::Engine::getSingleton().getViewort()->getActualHeight()/2, Orkige::Engine::getSingleton().getViewort()->getActualWidth()/2)];
		[[self view] setTransform:CGAffineTransformMakeRotation(M_PI / 2)]; 
		
		// Set frame of movieplayer
		[[mp view] setFrame:CGRectMake(0, 0, Orkige::Engine::getSingleton().getViewort()->getActualWidth(), Orkige::Engine::getSingleton().getViewort()->getActualHeight())];
		
		// Add movie player as subview
		[[self view] addSubview:[mp view]];   
		
		// Play the movie
		[mp play];
	}
}

/*---------------------------------------------------------------------------
 * For 3.1.x devices
 * For 3.2 and 4.x see moviePlayerLoadStateChanged: 
 *--------------------------------------------------------------------------*/
- (void) moviePreloadDidFinish:(NSNotification*)notification 
{
	// Remove observer
	[[NSNotificationCenter 	defaultCenter] 
	 removeObserver:self
	 name:MPMoviePlayerContentPreloadDidFinishNotification
	 object:nil];
	
	[[self view] setUserInteractionEnabled:YES];
	// Play the movie
 	[mp play];
}

/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void) moviePlayBackDidFinish:(NSNotification*)notification 
{    
	[[UIApplication sharedApplication] setStatusBarHidden:YES];
	
 	// Remove observer
	[[NSNotificationCenter 	defaultCenter] 
	 removeObserver:self
	 name:MPMoviePlayerPlaybackDidFinishNotification 
	 object:nil];
	
	[self dismissModalViewControllerAnimated:YES];
	if(!g_StopCalledFromInsideVideoManager)
	{
		Orkige::VideoManager::getSingleton().stop();
	}
}

/*---------------------------------------------------------------------------
 *
 *--------------------------------------------------------------------------*/
- (void) readyPlayer: (bool) loop showVideoControls: (bool) showui
{
 	mp =  [[MPMoviePlayerController alloc] initWithContentURL:movieURL];
	
	if ([mp respondsToSelector:@selector(loadState)]) 
	{
		// Set movie player layout
		if(showui)
		{
			[mp setControlStyle:MPMovieControlStyleFullscreen];
		}
		else 
		{
			[mp setControlStyle:MPMovieControlStyleNone];
		}

		if(loop)
		{
			mp.repeatMode = MPMovieRepeatModeOne;
		}

		
		[mp setFullscreen:YES];
		
		// May help to reduce latency
		[mp prepareToPlay];
		
		// Register that the load state changed (movie is ready)
		[[NSNotificationCenter defaultCenter] addObserver:self 
												 selector:@selector(moviePlayerLoadStateChanged:) 
													 name:MPMoviePlayerLoadStateDidChangeNotification 
												   object:nil];
	}  
	else
	{
		// Register to receive a notification when the movie is in memory and ready to play.
		[[NSNotificationCenter defaultCenter] addObserver:self 
												 selector:@selector(moviePreloadDidFinish:) 
													 name:MPMoviePlayerContentPreloadDidFinishNotification 
												   object:nil];
	}
	
	// Register to receive a notification when the movie has finished playing. 
	[[NSNotificationCenter defaultCenter] addObserver:self 
											 selector:@selector(moviePlayBackDidFinish:) 
												 name:MPMoviePlayerPlaybackDidFinishNotification 
											   object:nil];
}

/*---------------------------------------------------------------------------
 *
 *--------------------------------------------------------------------------*/
- (void) finishPlayer
{
	if(mp.playbackState == MPMoviePlaybackStatePlaying)
	{
		[mp stop];
	}
}
- (BOOL)canBecomeFirstResponder {
    NSLog(@"canBecomeFirstResponder");
    return NO;
 }
/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void) loadView
{
	[self setView:[[[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]] autorelease]];
	//[[self view] setBackgroundColor:[UIColor blackColor]];
	[[self view] setUserInteractionEnabled:YES];
	//[self becomeFirstResponder];
}

/*---------------------------------------------------------------------------
 *  
 *--------------------------------------------------------------------------*/
- (void)dealloc 
{
	[self finishPlayer];
	[mp release];
  [movieURL release];
	[super dealloc];
}

@end

@interface TestViewController : UIViewController
{
	CustomMoviePlayerViewController *moviePlayer;
}

@end

@implementation TestViewController

/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void)loadMoviePlayer:(NSString*)path loopVideo: (bool) loop showVideoControls: (bool) showui
{  
	// Create custom movie player   
	moviePlayer = [[[CustomMoviePlayerViewController alloc] initWithPath:path] autorelease];
	
	// Show the movie player as modal
 	[self presentModalViewController:moviePlayer animated:NO];
	
	// Prep and play the movie
	[moviePlayer readyPlayer:loop showVideoControls:showui];    
}

/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void)stopMoviePlayer
{
	//[[self view] resignFirstResponder];
	[moviePlayer finishPlayer]; 
}
/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void)loadView
{
	// Setup the view
	[self setView:[[[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]] autorelease]];
	//[[self view] setBackgroundColor:[UIColor grayColor]];
	[[self view] setUserInteractionEnabled:YES];
	//[[self view] becomeFirstResponder];
}

/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void)dealloc 
{
	[super dealloc];
}
- (BOOL)canBecomeFirstResponder {
    NSLog(@"canBecomeFirstResponder");
    return NO;
}
@end

@implementation UIView (FindFirstResponder)
- (UIView *)findFirstResponder
{
    if (self.isFirstResponder) 
	{        
        return self;     
    }
	
    for (UIView *subView in self.subviews) 
	{
        UIView *firstResponder = [subView findFirstResponder];
		
        if (firstResponder != nil) 
		{
			return firstResponder;
        }
    }
	
    return nil;
}
@end

#endif //ORKIGE_IPHONE
namespace Orkige
{
#ifdef ORKIGE_IPHONE
	class VideoPlayerIphone
	{
	public:
		TestViewController *vc;
		UIView* view;
		VideoPlayerIphone()
		{
			vc = 0;
			view = InputManager::getSingleton().getInputDelegate();
			oAssert(view);
		}
		~VideoPlayerIphone()
		{
			this->stop();
			view = nil;
		}
		void play(String const & movie, bool loop, bool showui)
		{
			//view.userInteractionEnabled = NO;
			vc = [[TestViewController alloc] init];
			[view addSubview:vc.view];
			static String s;
			s = Orkige::PlatformUtil::getResourceDirectory() + movie;
			NSString * path = (NSString*)CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)s.c_str(), s.size(), kCFStringEncodingUTF8,false);
			

			[vc loadMoviePlayer:path loopVideo:loop showVideoControls:showui];
			
		}
		
		void stop()
		{
			//view.userInteractionEnabled = YES;
			//
			if(vc)
			{
				[vc.view removeFromSuperview];
				//[oldFirstResponder becomeFirstResponder];
				[vc stopMoviePlayer];
				[vc release];
				vc = 0;
			}
			
		}
		
	};
#endif //ORKIGE_IPHONE
	void VideoManagerLog(String message)
	{
		oInfo("VideoManager: " << message);
	}
	//---------------------------------------------------------
	IMPL_OSINGLETON(VideoManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VideoManager::VideoManager(int num_worker_threads) 
#if defined(ORKIGE_THEORAVIDEOMANAGER) || defined(ORKIGE_IPHONE)
	: 
#endif
#ifdef ORKIGE_THEORAVIDEOMANAGER
	OgreVideoManager(num_worker_threads), clip(NULL), videoLayer(NULL), videoPanel(NULL)
#endif
#ifdef ORKIGE_IPHONE
#	ifdef ORKIGE_THEORAVIDEOMANAGER
		, 
#	endif
		iphoneClip(NULL)
#endif
	{
	}
	//---------------------------------------------------------
	VideoManager::~VideoManager()
	{
	}
	//---------------------------------------------------------
	void VideoManager::init()
	{
#ifdef ORKIGE_THEORAVIDEOMANAGER
		Ogre::ExternalTextureSourceManager::getSingleton().setExternalTextureSource("ogg_video", this);
		Ogre::Root::getSingleton().addFrameListener(this);

		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();

		// create overlay layers for everything
		this->videoLayer = om.create("VideoPanelLayer");

		this->videoPanel = Ogre::OverlayManager::getSingleton().createOverlayElement("Panel", "VideoPanel");
		this->videoPanel->setMaterialName("VideoTextureMaterial");
		this->videoPanel->setMetricsMode(Ogre::GMM_RELATIVE);
		this->videoPanel->setWidth(1.f);
		this->videoPanel->setHeight(1.f);
		this->videoPanel->setHorizontalAlignment(Ogre::GHA_LEFT);
		this->videoPanel->setVerticalAlignment(Ogre::GVA_TOP);
		this->videoPanel->setLeft(0);
		this->videoPanel->setTop(0);

		this->videoLayer->setZOrder(100);
		this->videoLayer->add2D(static_cast<Ogre::OverlayContainer*>(this->videoPanel));
		this->videoLayer->hide();
		this->videoPanel->hide();
		this->soundFactory = new VideoSoundHandlerFactory();
		this->setAudioInterfaceFactory(this->soundFactory);
		this->setLogFunction(VideoManagerLog);
#endif
	}
	//---------------------------------------------------------
	void VideoManager::deinit()
	{
		this->stop();
#ifdef ORKIGE_THEORAVIDEOMANAGER
		Ogre::Root::getSingleton().removeFrameListener(this);
		delete this->soundFactory;
		this->soundFactory = NULL;
#endif
	}
	//---------------------------------------------------------
	bool VideoManager::play(String const & fileName, bool loop, bool showui)
	{
#ifdef ORKIGE_THEORAVIDEOMANAGER
		if(this->clip)
			this->stop();
#endif
#ifdef ORKIGE_IPHONE
		if(this->iphoneClip)
		{
			this->stop();
		}
		if(StringUtil::hasEnding(fileName, ".mp4") || StringUtil::hasEnding(fileName, ".mov") || StringUtil::hasEnding(fileName, ".m2v") || StringUtil::hasEnding(fileName, ".m4v"))
		{
			this->iphoneClip = new VideoPlayerIphone();
			iphoneClip->play(fileName, loop, showui);
			return true;
		}
#else
		if(StringUtil::hasEnding(fileName, ".mp4") || StringUtil::hasEnding(fileName, ".mov") || StringUtil::hasEnding(fileName, ".m2v") || StringUtil::hasEnding(fileName, ".m4v"))
		{
			return false;
		}
#endif
#ifdef ORKIGE_THEORAVIDEOMANAGER
		this->setInputName(fileName);
		this->createDefinedTexture("VideoTextureMaterial");
		this->clip = this->getVideoClipByName(fileName);
		this->clip->setAutoRestart(loop);
		this->videoLayer->show();
		this->videoPanel->show();
#endif
		return true;
	}
	//---------------------------------------------------------
	bool VideoManager::stop()
	{
#ifdef ORKIGE_IPHONE
		if(this->iphoneClip)
		{
			g_StopCalledFromInsideVideoManager = true;
			this->iphoneClip->stop();
			delete this->iphoneClip;
			this->iphoneClip = NULL;
			g_StopCalledFromInsideVideoManager = false;
			return true;
		}
#endif
#ifdef ORKIGE_THEORAVIDEOMANAGER
		if(this->clip)
		{
			this->clip->stop();
			String clipName = this->clip->getName();
			this->destroyVideoClip(this->clip);
			this->clip = NULL;
			this->videoLayer->hide();
			this->videoPanel->hide();
			Ogre::TextureManager::getSingleton().remove(clipName);
			return true;
		}
#endif
		return false;
	}
	//---------------------------------------------------------
	bool VideoManager::isPlaying()
	{
#ifdef ORKIGE_IPHONE
		if(this->iphoneClip)
		{
			return true;
		}
#endif
#ifdef ORKIGE_THEORAVIDEOMANAGER
		if(this->clip)
		{
			bool playing = !this->clip->isDone();
			return playing;
		}
#endif
		return false;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}