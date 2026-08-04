/********************************************************************
	created:	Monday 2010/09/06 at 16:23
	filename: 	SoundError.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
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
		case SE_BAD_DATA:
			return "SE_BAD_DATA";
		case SE_UNREADABLE:
			return "SE_UNREADABLE";
		case SE_DEVICE:
			return "SE_DEVICE";
		default:
			return "SE_UNKNOWN";
		}
	}
	//---------------------------------------------------------
	void SoundError::call(bool condition, String const & message, SoundError::SoundErrorCode code)
	{
		if(!condition)
		{
			SoundError exception(message, code);
			if(code != SE_UNKNOWN)
			{
				oDebugMsg("sound",0,"Sound error: " << SoundError::getErrorDesc(code));
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
