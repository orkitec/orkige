/********************************************************************
	created:	Tuesday 2010/08/31 at 13:58
	filename: 	SoundManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __SoundManager_h__31_8_2010__13_58_35__
#define __SoundManager_h__31_8_2010__13_58_35__

#include "engine_sound/SoundSource.h"
#include "engine_render/RenderMath.h"
#include "engine_render/RenderNode.h"

namespace Orkige
{
	//! @brief Sound Management
	//! @remarks simple registry of fully buffered OpenAL (Soft) SoundSources;
	//! there is no dynamic source sharing/reusing, streaming or priorities and
	//! only AL_MAX_SOURCES concurrent SoundSources can be active
	//! (the old OgreOggSound backend is gone together with its dependency)
	class ORKIGE_ENGINE_DLL SoundManager : public Singleton<SoundManager>, public Interface
	{
		OOBJECT(SoundManager,Interface);
		DECL_OSINGLETON(SoundManager)
		//--- Types -------------------------------------------------
	public:
	protected:
		typedef std::map<String, optr<SoundSource> > SoundRegistry;		//!< registry of sound sources with id's
		typedef std::map<String, float>	InterruptedSoundRegistry;		//!< interrupted sound ids and play offset
	private:
		//--- Variables ---------------------------------------------
	public:

	protected:
		bool isInitialized;
		SoundRegistry				sounds;				//!< created SoundSource collection
		InterruptedSoundRegistry	interruptedSounds;	//!< all currently interrupted sounds
#ifdef ORKIGE_OPENAL_SOUND
		ALCcontext*					context;			//!< OpenAL context
#endif //ORKIGE_OPENAL_SOUND
		//! optional sound listener: the node the "ears" sit on (usually the
		//! camera's node) - facade-typed since A1 (Docs/render-abstraction.md);
		//! forward = the node's -Z, up = its +Y
		optr<RenderNode>			listener;

	private:
		//--- Methods -----------------------------------------------
	public:
		//! create SoundManager with optional listener node
		SoundManager(optr<RenderNode> const & soundListener = optr<RenderNode>());
		//! destructor
		virtual ~SoundManager();
		//! set/replace the listener node (NULL keeps the listener static)
		inline void setListener(optr<RenderNode> const & soundListener);
		//! init sound system
		bool init();
		//! update sounds and listener position
		void update(float delta = 0.f);
		//! deinit sound system
		bool deinit();
		//! create a SoundSource
		SoundSourcePtr createSound(String const & id, String const & fileName, bool loop = false, Vec3 const & pos = Vec3::ZERO,  bool stream = false, bool preBuffer=false);
		//! @brief create a SoundSource from a raw PCM buffer (no sound file needed)
		//! @remarks the samples get copied, see SoundSource::initFromPCM
		//! @param pcmData raw interleaved PCM samples (8 bit unsigned or 16 bit signed)
		//! @param dataSize size of pcmData in bytes
		//! @param channels channel count: 1 (mono) or 2 (stereo)
		//! @param bitsPerSample bits per sample: 8 or 16
		//! @param sampleRate SampleRate in Hz e.g. 44100
		SoundSourcePtr createSoundFromPCM(String const & id, void const * pcmData, int dataSize, int channels, int bitsPerSample, int sampleRate, bool loop = false, Vec3 const & pos = Vec3::ZERO);
		//! destroy a SoundSource
		bool destroySound(String const & id);
		//! does sound with given id exist
		bool hasSound(String const & id);
		//! get SoundSource with given id
		SoundSourcePtr getSound(String const & id);
		//! play sound
		bool playSound(String const & id);
		//! stop sound
		bool stopSound(String const & id);
		//! is given sound playing
		bool isPlaying(String const & id);
		//! pause all sounds
		void pause();
		//! resume all sounds
		void resume();
		//! sound interruption begin (iPhone call etc.)
		void onInterruptBegin();
		//! sound interruption end
		void onInterruptEnd();
		bool isinitialised();
	protected:
		//! init openAl
		bool initOpenAl();
		//! deinit openAl
		bool deinitOpenAl();

	private:
	};
	//---------------------------------------------------------------
	inline void SoundManager::setListener(optr<RenderNode> const & soundListener)
	{
		this->listener = soundListener;
	}
	//---------------------------------------------------------------
}

#endif //__SoundManager_h__31_8_2010__13_58_35__
