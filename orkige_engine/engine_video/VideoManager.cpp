/********************************************************************
	created:	Tuesday 2011/02/15 at 11:51
	filename: 	VideoManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_video/VideoManager.h"
#include "engine_video/VideoSoundHandler.h"
#include "engine_graphic/Engine.h"

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
		[[self view] setBounds:CGRectMake(0, 0, 480, 320)];
		[[self view] setCenter:CGPointMake(160, 240)];
		[[self view] setTransform:CGAffineTransformMakeRotation(M_PI / 2)]; 
		
		// Set frame of movieplayer
		[[mp view] setFrame:CGRectMake(0, 0, 480, 320)];
		
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
}

/*---------------------------------------------------------------------------
 *
 *--------------------------------------------------------------------------*/
- (void) readyPlayer
{
 	mp =  [[MPMoviePlayerController alloc] initWithContentURL:movieURL];
	
	if ([mp respondsToSelector:@selector(loadState)]) 
	{
		// Set movie player layout
		[mp setControlStyle:MPMovieControlStyleFullscreen];
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
- (void) loadView
{
	[self setView:[[[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]] autorelease]];
	[[self view] setBackgroundColor:[UIColor blackColor]];
}

/*---------------------------------------------------------------------------
 *  
 *--------------------------------------------------------------------------*/
- (void)dealloc 
{
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
- (void)loadMoviePlayer
{  
	// Play movie from the bundle
	NSString *path = [[NSBundle mainBundle] pathForResource:@"data/Video/Movie-1" ofType:@"mp4" inDirectory:nil];
	
	// Create custom movie player   
	moviePlayer = [[[CustomMoviePlayerViewController alloc] initWithPath:path] autorelease];
	
	// Show the movie player as modal
 	[self presentModalViewController:moviePlayer animated:YES];
	
	// Prep and play the movie
	[moviePlayer readyPlayer];    
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
}

/*---------------------------------------------------------------------------
 * 
 *--------------------------------------------------------------------------*/
- (void)dealloc 
{
	[super dealloc];
}

@end

namespace Orkige
{
	class VideoPlayerIphoneImpl
	{
	public:
		TestViewController *vc;
		void play(String const & movie)
		{
			static UIView* view = nil;
			if(view == nil)
			{
				if([[[UIDevice currentDevice] systemVersion] floatValue] >= 4.0)
				{
					Engine::getSingleton().getRenderWindow()->getCustomAttribute( "VIEW", &view );
				}
			}
			oAssert(view);

			vc = [[TestViewController alloc] init];
			[view addSubview:vc.view];
			[vc loadMoviePlayer];
			
		}
		
	};
	void VideoManagerLog(String message)
	{
		oInfo("VideoManager: " << message);
	}
	//---------------------------------------------------------
	IMPL_OSINGLETON(VideoManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VideoManager::VideoManager(int num_worker_threads) : OgreVideoManager(num_worker_threads), clip(NULL), videoLayer(NULL), videoPanel(NULL)
	{
	}
	//---------------------------------------------------------
	VideoManager::~VideoManager()
	{
	}
	//---------------------------------------------------------
	void VideoManager::init()
	{
		//iphoneVideoPlayer = new VideoPlayerIphoneImpl();
		
		Ogre::ExternalTextureSourceManager::getSingleton().setExternalTextureSource("ogg_video",this);
		Ogre::Root::getSingleton().addFrameListener(this);

		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();

		// create overlay layers for everything
		this->videoLayer = om.create("VideoPanelLayer");

		this->videoPanel = Ogre::OverlayManager::getSingleton().createOverlayElement("Panel","VideoPanel");
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
		this->setDefaultNumPrecachedFrames(60);
	}
	//---------------------------------------------------------
	void VideoManager::deinit()
	{
		this->stop();
		Ogre::Root::getSingleton().removeFrameListener(this);
		delete this->soundFactory;
		this->soundFactory = NULL;
	}
	//---------------------------------------------------------
	bool VideoManager::play(String const & fileName, bool loop)
	{
		//iphoneVideoPlayer->play(fileName);
		
		if(this->clip)
			this->stop();

		this->setInputName(fileName);
		this->createDefinedTexture("VideoTextureMaterial");
		this->clip = this->getVideoClipByName(fileName);
		this->clip->setAutoRestart(loop);
		this->videoLayer->show();
		this->videoPanel->show();
		return false;
	}
	//---------------------------------------------------------
	bool VideoManager::stop()
	{
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
		return false;
	}
	//---------------------------------------------------------
	bool VideoManager::isPlaying()
	{
		if(this->clip)
		{
			bool playing = !this->clip->isDone();
			return playing;
		}
		return false;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}