/********************************************************************
	created:	Tuesday 2011/02/15 at 11:52
	filename: 	VideoSoundHandler.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifdef ORKIGE_THEORAVIDEOMANAGER
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
	VideoSoundHandler::VideoSoundHandler(TheoraVideoClip* owner, int channels, int freq) : TheoraAudioInterface(owner, channels, freq)
	{
		this->maxBuffSize=freq*mNumChannels*2;
		this->buffSize=0;
		this->numProcessedSamples=0;
		this->timeOffset=0;

		this->tempBuffer=new short[this->maxBuffSize];
		alGenSources(1,&this->source);
		owner->setTimer(this);
		this->numPlayedSamples=0;
	}
	//---------------------------------------------------------
	VideoSoundHandler::~VideoSoundHandler()
	{
		// todo: delete buffers and source
		if (this->tempBuffer) delete[] this->tempBuffer;
	}
	//---------------------------------------------------------
	void VideoSoundHandler::destroy()
	{
		// todo
	}
	//---------------------------------------------------------
	void VideoSoundHandler::insertData(float** data,int samples)
	{
		for (int i=0;i<samples;i++)
		{
			if (this->buffSize < this->maxBuffSize)
			{
				this->tempBuffer[this->buffSize++]=float2short(data[0][i]);
				if (mNumChannels == 2)
					this->tempBuffer[this->buffSize++]=float2short(data[1][i]);
			}
			if (this->buffSize == mFreq*mNumChannels/4)
			{	
				OpenAL_Buffer buff;
				alGenBuffers(1,&buff.id);
				ALuint format = (mNumChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
				alBufferData(buff.id,format,this->tempBuffer,this->buffSize*2,mFreq);
				alSourceQueueBuffers(this->source, 1, &buff.id);
				buff.samples=this->buffSize/mNumChannels;
				this->numProcessedSamples+=this->buffSize/mNumChannels;
				this->bufferQueue.push(buff);

				this->buffSize=0;

				int state;
				alGetSourcei(this->source,AL_SOURCE_STATE,&state);
				if (state != AL_PLAYING)
				{
					//alSourcef(mSource,AL_PITCH,0.5); // debug
					alSourcef(this->source,AL_SAMPLE_OFFSET,this->numProcessedSamples-mFreq/4);
					alSourcePlay(this->source);

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

		alGetSourcei(this->source,AL_BUFFERS_PROCESSED,&nProcessed);
		for (i=0;i<nProcessed;i++)
		{
			buff=this->bufferQueue.front();
			this->bufferQueue.pop();
			this->numPlayedSamples+=buff.samples;
			alSourceUnqueueBuffers(this->source,1,&buff.id);
			alDeleteBuffers(1,&buff.id);
		}

		// control playback and return time position
		alGetSourcei(this->source,AL_SOURCE_STATE,&state);
		if (state == AL_PLAYING)
		{
			alGetSourcef(this->source,AL_SEC_OFFSET,&mTime);
			mTime+=(float) this->numPlayedSamples/mFreq;
			this->timeOffset=0;
		}
		else
		{
			mTime=(float) this->numProcessedSamples/mFreq+this->timeOffset;
			this->timeOffset+=time_increase;
		}

		float duration=mClip->getDuration();
		if (mTime > duration) mTime=duration;
	}
	//---------------------------------------------------------
	void VideoSoundHandler::pause()
	{
		alSourcePause(this->source);
		TheoraTimer::pause();
	}
	//---------------------------------------------------------
	void VideoSoundHandler::play()
	{
		alSourcePlay(this->source);
		TheoraTimer::play();
	}
	//---------------------------------------------------------
	void VideoSoundHandler::seek(float time)
	{
		OpenAL_Buffer buff;

		alSourceStop(this->source);
		while (!this->bufferQueue.empty())
		{
			buff=this->bufferQueue.front();
			this->bufferQueue.pop();
			alSourceUnqueueBuffers(this->source,1,&buff.id);
			alDeleteBuffers(1,&buff.id);
		}
		//		int nProcessed;
		//		alGetSourcei(mSource,AL_BUFFERS_PROCESSED,&nProcessed);
		//		if (nProcessed != 0)
		//			nProcessed=nProcessed;
		this->buffSize=0;
		this->timeOffset=0;

		this->numPlayedSamples=this->numProcessedSamples=time*mFreq;
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
	VideoSoundHandler* VideoSoundHandlerFactory::createInstance(TheoraVideoClip* owner,int channels, int freq)
	{
		if(this->handler)
		{
			delete this->handler;
			this->handler = NULL;
		}

		this->handler = new VideoSoundHandler(owner, channels, freq);
		return this->handler;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
#endif //ORKIGE_THEORAVIDEOMANAGER