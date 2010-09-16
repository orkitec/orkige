/********************************************************************
	created:	Monday 2010/09/06 at 16:23
	filename: 	SoundError.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_sound/SoundError.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundError::SoundError(String const & msg, SoundError::SoundErrorCode code) : std::runtime_error(msg), errorCode(code) 
	{

	}
	//---------------------------------------------------------
	const char* SoundError::getErrorDesc(SoundError::SoundErrorCode errorCode)
	{
		switch(errorCode)
		{
		case AL_INVALID_VALUE:
			return "AL_INVALID_VALUE";
			break;
		case AL_INVALID_ENUM:
			return "AL_INVALID_ENUM";
			break;
		case AL_INVALID_NAME:
			return "AL_INVALID_NAME";
			break;
		case AL_INVALID_OPERATION:
			return "AL_INVALID_OPERATION";
			break;
		case AL_OUT_OF_MEMORY:
			return "AL_OUT_OF_MEMORY";
			break;
		default:
			return "UNKNOWN_AL_ERROR";
			break;
		}
	}
	//---------------------------------------------------------
	void SoundError::call(bool condition, String const & message, SoundError::SoundErrorCode code)
	{
		if(!condition)
		{
			SoundError exception(message, code);
			if(code != AL_INVALID)
			{
				oDebugMsg("sound",0,"OpenAL ErrorCode: " << SoundError::getErrorDesc(code));
			}
			oDebugMsg("sound",0,message);
			throw exception;
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}