/**************************************************************
	created:	2010/08/20 at 0:43
	filename: 	Timer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __Timer_h__20_8_2010__0_43_36__
#define __Timer_h__20_8_2010__0_43_36__

#include "core_base/Meta.h"

namespace Orkige
{
	//! time utilities
	namespace Timer
	{
		//! Must be called before getMilliseconds
		void ORKIGE_CORE_DLL initialise();

		//! Return the number of elapsed MS since initialise was called
#ifdef ORKIGE_NDS
		unsigned long ORKIGE_CORE_DLL getMilliseconds();
#else
		unsigned long ORKIGE_CORE_DLL getMilliseconds();
#endif

	}
}

#endif //__Timer_h__20_8_2010__0_43_36__
