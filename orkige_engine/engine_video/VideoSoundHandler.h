/********************************************************************
	created:	Tuesday 2011/02/15 at 11:52
	filename: 	VideoSoundHandler.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __VideoSoundHandler_h__15_2_2011__11_52_17__
#define __VideoSoundHandler_h__15_2_2011__11_52_17__
#ifdef ORKIGE_THEORAVIDEOMANAGER
#include <engine_sound/SoundManager.h>
#include <engine_video/VideoManager.h>

namespace Orkige
{
	//! handle ogg video sound playback trough openal
	class ORKIGE_ENGINE_DLL VideoSoundHandler : public TheoraAudioInterface, TheoraTimer
	{
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		int maxBuffSize;
		int buffSize;
		short *tempBuffer;
		float timeOffset;

		struct OpenAL_Buffer
		{
			ALuint id;
			int samples;
		} buffers[1000];

		std::queue<OpenAL_Buffer> bufferQueue;

		ALuint source;
		int numProcessedSamples;
		int numPlayedSamples;
		//--- Methods -----------------------------------------------
	public:
		VideoSoundHandler(TheoraVideoClip* owner, int channels, int freq);
		virtual ~VideoSoundHandler();
		void insertData(float** data, int samples);
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
#endif //ORKIGE_THEORAVIDEOMANAGER
#endif //__VideoSoundHandler_h__15_2_2011__11_52_17__