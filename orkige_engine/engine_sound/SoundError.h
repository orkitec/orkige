/********************************************************************
	created:	Monday 2010/09/06 at 16:30
	filename: 	SoundError.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SoundError_h__6_9_2010__16_30_32__
#define __SoundError_h__6_9_2010__16_30_32__

#include "engine_module/EnginePrerequisites.h"

#include <stdexcept>

namespace Orkige
{
	//! error that gets thrown on errors regarding Sounds
	class ORKIGE_ENGINE_DLL SoundError : public std::runtime_error
	{
		//-Types--------------------------------------------
	public:
		//! what went wrong - the whole vocabulary the sound tier can report
		enum SoundErrorCode
		{
			SE_UNKNOWN = -1,	//!< no code was given
			SE_BAD_DATA,		//!< the bytes are not the sound file they claim
			SE_UNREADABLE,		//!< the file is missing, unreadable or unsupported
			SE_DEVICE			//!< the audio device refused the request
		};
	protected:
	private:
		//-Variables----------------------------------------
	public:
	protected:
		SoundErrorCode errorCode;		//!< the sound error code
	private:
		//-Methods------------------------------------------
	public:
		//! construct SoundError with optional message and SoundErrorCode
		SoundError(String const & msg = "", SoundErrorCode code = SE_UNKNOWN);
		//! get the error code
		inline SoundErrorCode getErrorCode();
		//! convert error code to readable string
		static const char* getErrorDesc(SoundErrorCode errorCode);
		//! throw SoundError if condition is false
		static void call(bool condition, String const & message = "", SoundErrorCode code = SE_UNKNOWN);
	protected:
	private:
	};
	//----------------------------------------------------
	inline SoundError::SoundErrorCode SoundError::getErrorCode()
	{
		return this->errorCode;
	}
	//---------------------------------------------------------------
}

#endif //__SoundError_h__6_9_2010__16_30_32__
