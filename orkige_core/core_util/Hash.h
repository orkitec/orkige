/**************************************************************
	created:	2010/08/19 at 23:20
	filename: 	Hash.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __Hash_h__19_8_2010__23_20_06__
#define __Hash_h__19_8_2010__23_20_06__

#include <functional>
#include "core_util/String.h"

namespace Orkige
{
	//! create a hash from a String
	static const std::hash<String> BoostHashFromString = std::hash<String>();
}

#endif //__Hash_h__19_8_2010__23_20_06__
	