/********************************************************************
	created:	Tuesday 2011/02/15 at 11:52
	filename: 	VideoSoundHandler.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __VideoSoundHandler_h__15_2_2011__11_52_17__
#define __VideoSoundHandler_h__15_2_2011__11_52_17__

#include <engine_sound/SoundManager.h>
#include <engine_video/VideoManager.h>

namespace Orkige
{
	//! handle ogg video sound playback trough openal
	class VideoSoundHandler : public TheoraAudioInterface, TheoraTimer
	{
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		int mMaxBuffSize;
		int mBuffSize;
		short *mTempBuffer;
		float mTimeOffset;

		struct OpenAL_Buffer
		{
			ALuint id;
			int nSamples;
		}mBuffers[1000];
		std::queue<OpenAL_Buffer> mBufferQueue;

		ALuint mSource;
		int mNumProcessedSamples,mNumPlayedSamples;
		//--- Methods -----------------------------------------------
	public:
		VideoSoundHandler(TheoraVideoClip* owner,int nChannels,int freq);
		virtual ~VideoSoundHandler();
		void insertData(float** data,int nSamples);
		void destroy();

		void update(float time_increase);

		void pause();
		void play();
		void seek(float time);
	protected:
	private:
	};
	//---------------------------------------------------------------
	class VideoSoundHandlerFactory : public TheoraAudioInterfaceFactory
	{
		VideoSoundHandler* handler;
	public:
		VideoSoundHandlerFactory();
		~VideoSoundHandlerFactory();
		VideoSoundHandler* createInstance(TheoraVideoClip* owner,int nChannels,int freq);
	};
}

#endif //__VideoSoundHandler_h__15_2_2011__11_52_17__