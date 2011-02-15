/********************************************************************
	created:	Tuesday 2011/02/15 at 11:52
	filename: 	VideoSoundHandler.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_video/VideoSoundHandler.h"

namespace Orkige
{
	short float2short(float f)
	{
		if      (f >  1) f= 1;
		else if (f < -1) f=-1;
		return f*32767;
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VideoSoundHandler::VideoSoundHandler(TheoraVideoClip* owner,int nChannels,int freq) : TheoraAudioInterface(owner,nChannels,freq)
	{
		mMaxBuffSize=freq*mNumChannels*2;
		mBuffSize=0;
		mNumProcessedSamples=0;
		mTimeOffset=0;

		mTempBuffer=new short[mMaxBuffSize];
		alGenSources(1,&mSource);
		owner->setTimer(this);
		mNumPlayedSamples=0;
	}
	//---------------------------------------------------------
	VideoSoundHandler::~VideoSoundHandler()
	{
		// todo: delete buffers and source
		if (mTempBuffer) delete[] mTempBuffer;
	}
	//---------------------------------------------------------
	void VideoSoundHandler::destroy()
	{
		// todo
	}
	//---------------------------------------------------------
	void VideoSoundHandler::insertData(float** data,int nSamples)
	{
		for (int i=0;i<nSamples;i++)
		{
			if (mBuffSize < mMaxBuffSize)
			{
				mTempBuffer[mBuffSize++]=float2short(data[0][i]);
				if (mNumChannels == 2)
					mTempBuffer[mBuffSize++]=float2short(data[1][i]);
			}
			if (mBuffSize == mFreq*mNumChannels/4)
			{	
				OpenAL_Buffer buff;
				alGenBuffers(1,&buff.id);
				ALuint format = (mNumChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
				alBufferData(buff.id,format,mTempBuffer,mBuffSize*2,mFreq);
				alSourceQueueBuffers(mSource, 1, &buff.id);
				buff.nSamples=mBuffSize/mNumChannels;
				mNumProcessedSamples+=mBuffSize/mNumChannels;
				mBufferQueue.push(buff);

				mBuffSize=0;

				int state;
				alGetSourcei(mSource,AL_SOURCE_STATE,&state);
				if (state != AL_PLAYING)
				{
					//alSourcef(mSource,AL_PITCH,0.5); // debug
					alSourcef(mSource,AL_SAMPLE_OFFSET,mNumProcessedSamples-mFreq/4);
					alSourcePlay(mSource);

				}

			}
		}
	}
	//---------------------------------------------------------
	void VideoSoundHandler::update(float time_increase)
	{
		int i,state,nProcessed;
		OpenAL_Buffer buff;

		// process played buffers

		alGetSourcei(mSource,AL_BUFFERS_PROCESSED,&nProcessed);
		for (i=0;i<nProcessed;i++)
		{
			buff=mBufferQueue.front();
			mBufferQueue.pop();
			mNumPlayedSamples+=buff.nSamples;
			alSourceUnqueueBuffers(mSource,1,&buff.id);
			alDeleteBuffers(1,&buff.id);
		}

		// control playback and return time position
		alGetSourcei(mSource,AL_SOURCE_STATE,&state);
		if (state == AL_PLAYING)
		{
			alGetSourcef(mSource,AL_SEC_OFFSET,&mTime);
			mTime+=(float) mNumPlayedSamples/mFreq;
			mTimeOffset=0;
		}
		else
		{
			mTime=(float) mNumProcessedSamples/mFreq+mTimeOffset;
			mTimeOffset+=time_increase;
		}

		float duration=mClip->getDuration();
		if (mTime > duration) mTime=duration;
	}
	//---------------------------------------------------------
	void VideoSoundHandler::pause()
	{
		alSourcePause(mSource);
		TheoraTimer::pause();
	}
	//---------------------------------------------------------
	void VideoSoundHandler::play()
	{
		alSourcePlay(mSource);
		TheoraTimer::play();
	}
	//---------------------------------------------------------
	void VideoSoundHandler::seek(float time)
	{
		OpenAL_Buffer buff;

		alSourceStop(mSource);
		while (!mBufferQueue.empty())
		{
			buff=mBufferQueue.front();
			mBufferQueue.pop();
			alSourceUnqueueBuffers(mSource,1,&buff.id);
			alDeleteBuffers(1,&buff.id);
		}
		//		int nProcessed;
		//		alGetSourcei(mSource,AL_BUFFERS_PROCESSED,&nProcessed);
		//		if (nProcessed != 0)
		//			nProcessed=nProcessed;
		mBuffSize=0;
		mTimeOffset=0;

		mNumPlayedSamples=mNumProcessedSamples=time*mFreq;
	}
	//---------------------------------------------------------
	VideoSoundHandlerFactory::VideoSoundHandlerFactory() : handler(NULL)
	{

	}
	//---------------------------------------------------------
	VideoSoundHandlerFactory::~VideoSoundHandlerFactory()
	{
		if(this->handler)
		{
			delete this->handler;
			this->handler = NULL;
		}
	}
	//---------------------------------------------------------
	VideoSoundHandler* VideoSoundHandlerFactory::createInstance(TheoraVideoClip* owner,int nChannels,int freq)
	{
		if(this->handler)
		{
			delete this->handler;
			this->handler = NULL;
		}

		this->handler = new VideoSoundHandler(owner,nChannels,freq);
		return this->handler;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}