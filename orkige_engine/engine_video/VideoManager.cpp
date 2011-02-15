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

namespace Orkige
{
	void VideoManagerLog(String message)
	{
		oDebugMsg("engine", 0 ,"VideoManager: " << message);
	}
	//---------------------------------------------------------
	IMPL_OSINGLETON(VideoManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VideoManager::VideoManager() : clip(NULL), videoLayer(NULL), videoPanel(NULL)
	{
	}
	//---------------------------------------------------------
	VideoManager::~VideoManager()
	{
	}
	//---------------------------------------------------------
	void VideoManager::init()
	{
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
			this->destroyVideoClip(this->clip);
			this->clip = NULL;
			this->videoLayer->hide();
			this->videoPanel->hide();
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