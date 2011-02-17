/********************************************************************
	created:	Tuesday 2011/02/15 at 11:09
	filename: 	VideoManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __VideoManager_h__15_2_2011__11_09_25__
#define __VideoManager_h__15_2_2011__11_09_25__
#include "engine_module/EnginePrerequisites.h"
#include <OgreVideoManager.h>
#include <TheoraAudioInterface.h>
#include <TheoraTimer.h>


namespace Orkige
{
	class VideoSoundHandlerFactory;
	//! simplified video interface around OgreVideoManager and TheoraVideoManager to play fullscreen videos
	class VideoManager : public Ogre::OgreVideoManager, public Singleton<VideoManager>
	{
		DECL_OSINGLETON(VideoManager)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		class VideoPlayerIphoneImpl* iphoneVideoPlayer;
		TheoraVideoClip* clip;
		Ogre::OverlayElement* videoPanel;
		Ogre::Overlay* videoLayer;
		VideoSoundHandlerFactory* soundFactory;
	private:
		//--- Methods -----------------------------------------------
	public:
		VideoManager(int num_worker_threads);
		virtual ~VideoManager();
		//! initialise video manager and needed textures
		void init();
		//! deinit videomanager and free ressources
		void deinit();
		//! play given video file
		bool play(String const & fileName, bool loop = false);
		//! stop currently playinf video
		bool stop();
		//! is currently a video playing
		bool isPlaying();
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__VideoManager_h__15_2_2011__11_09_25__