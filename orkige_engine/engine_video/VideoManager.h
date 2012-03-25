/********************************************************************
	created:	Tuesday 2011/02/15 at 11:09
	filename: 	VideoManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __VideoManager_h__15_2_2011__11_09_25__
#define __VideoManager_h__15_2_2011__11_09_25__
#include "engine_module/EnginePrerequisites.h"
#ifdef ORKIGE_THEORAVIDEOMANAGER
#	include <OgreVideoManager.h>
#	include <TheoraAudioInterface.h>
#	include <TheoraTimer.h>
#endif
#include <core_event/GlobalEventManager.h>
namespace Orkige
{
#ifdef ORKIGE_THEORAVIDEOMANAGER
	class VideoSoundHandlerFactory;
#endif
	//! simplified video interface around OgreVideoManager and TheoraVideoManager to play fullscreen videos
	class ORKIGE_ENGINE_DLL VideoManager : 
#ifdef ORKIGE_THEORAVIDEOMANAGER
		public Ogre::OgreVideoManager,
#endif
		public Singleton<VideoManager>
	{
		DECL_OSINGLETON(VideoManager)
		//--- Types -------------------------------------------------
	public:
		/** \addtogroup EngineEvents
		 *  @{ */
		//! Called when a video is started (on iOS after preloading)
		DECL_EVENTTYPE(VideoStartedEvent);
		/** @} End of "addtogroup EngineEvents"*/
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		class VideoPlayerIphone* iphoneClip;
#ifdef ORKIGE_THEORAVIDEOMANAGER
		TheoraVideoClip* clip;
		Ogre::OverlayElement* videoPanel;
		Ogre::Overlay* videoLayer;
		VideoSoundHandlerFactory* soundFactory;
#endif
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
		bool play(String const & fileName, bool loop = false, bool showui = true);
		//! stop currently playinf video
		bool stop();
		//! is currently a video playing
		bool isPlaying();
		//! force manager to update the video and the screen
		void forceUpdate(float delta);
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__VideoManager_h__15_2_2011__11_09_25__