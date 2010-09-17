/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	IphoneUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __IphoneUtil_h__19_8_2010__23_21_56__
#define __IphoneUtil_h__19_8_2010__23_21_56__
#include "core_util/String.h"

namespace Orkige
{
	//! iPhone utilities
	namespace IPhoneUtil
	{
#ifdef __APPLE__
		//! retrieve iPhone data path of current running app
		String GetIPhoneDataPath();
		//! retrieve iPhone Documents path
		String GetIPhoneDocumentsDirectory();
#endif
	}
}

#endif //__IphoneUtil_h__19_8_2010__23_21_56__
