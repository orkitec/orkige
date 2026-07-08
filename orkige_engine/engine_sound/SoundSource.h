/********************************************************************
        created:	Tuesday 2010/08/31 at 13:58
        filename: 	SoundSource.h
        author:		steffen.roemer
        notice:		This source file is part of orkige (orkitec Game engine)
                                For the latest info, see http://www.orkitec.com/
        copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#ifndef __SoundSource_h__31_8_2010__13_58_48__
#define __SoundSource_h__31_8_2010__13_58_48__

#include "engine_sound/SoundPlatform.h"
#include <core_base/Object.h>

namespace Orkige
{
        //! simple SoundSource (3D for Mono sounds 2D for Stereo)
        class ORKIGE_ENGINE_DLL SoundSource : public Object
        {
                OOBJECT(SoundSource, Object)
                //--- Types -------------------------------------------------
        public:
        protected:
        private:
                //--- Variables ---------------------------------------------
        public:
        protected:
#ifdef ORKIGE_OPENAL_SOUND
                ALuint			source;			//!< openAl source
                ALuint			buffer;			//!< openAl buffer

                ALenum			format;			//!< Sound samples: format specifier e.g. AL_FORMA_*
                ALsizei			size;			//!< sound data size
                ALsizei			freq;			//!< sound SampleRate
#endif //ORKIGE_OPENAL_SOUND
                void*			data;			//!< the actual SoundFile data

                Ogre::Vector3	position;		//!< position in 3d space (only for mono sounds)
                String			fileName;		//!< filename of this SoundSource
                bool			looped;			//!< marks if source should be looping
                bool			initialized;	//!< true after SoundSource::init() was called
        private:
                //--- Methods -----------------------------------------------
        public:
                //! constructor for uninitialized SoundSource
                SoundSource(String const & id, String const & file, bool loop = false, Ogre::Vector3 const & pos = Ogre::Vector3::ZERO);
                //! destructor
                virtual ~SoundSource();
                //! @brief init source
                //! @param reloadData if true SoundSource::data gets reloaded if its already loaded
                //! @param alwaysFreeData useful if you don't want to reause the data and are on systems with low memmory
                bool init(bool reloadData = false, bool alwaysFreeData = false);
                //! @brief init source from a raw PCM buffer instead of a sound file
                //! @remarks the samples get copied so the SoundSource keeps its own
                //! data copy for re-init after interruptions (a later init() call
                //! reuploads it; don't combine with init(true) since there is no
                //! file to reload the data from)
                //! @param pcmData raw interleaved PCM samples (8 bit unsigned or 16 bit signed)
                //! @param dataSize size of pcmData in bytes
                //! @param channels channel count: 1 (mono) or 2 (stereo)
                //! @param bitsPerSample bits per sample: 8 or 16
                //! @param sampleRate SampleRate in Hz e.g. 44100
                bool initFromPCM(void const * pcmData, int dataSize, int channels, int bitsPerSample, int sampleRate);
                //! @brief deinit source
                //! freeData if true frees sound data (if you don't wan't to reuse it)
                bool deinit(bool freeData = false);
                //! play sound
                bool play();
                //! stop sound
                bool stop();
                //! pause sound
                bool pause();
                //! resume paused sound
                bool resume();
                //! is sound playing?
                bool isPlaying();
                //! is sound paused?
                bool isPaused();
                //! set sound 3D position
                bool setPosition(Ogre::Vector3 const & pos);
                //! get sound 3D position
                Ogre::Vector3 const & getPosition();
                //! get current offset in seconds for playing sound
                float getPlayPosition();
                //! set current offset in seconds for playing sound
                void setPlayPosition(float pos);

        protected:
        private:
        };
        //---------------------------------------------------------------
        typedef optr<SoundSource> SoundSourcePtr;
}

#endif //__SoundSource_h__31_8_2010__13_58_48__
