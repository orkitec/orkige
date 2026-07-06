/**************************************************************
	created:	2010/09/11 at 23:59
	filename: 	String.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __String_h__11_9_2010__23_59_35__
#define __String_h__11_9_2010__23_59_35__

#include <string>
#include <list>
#include <vector>
#include <ctype.h>

namespace Orkige
{
	typedef std::string String;					//!< Orkige String implementation
	typedef std::list<String> StringList;		//!< list of Orkige Strings
	typedef std::vector<String> StringVector;	//!< vector of Orkige Strings
	//---------------------------------------------------------
}

#endif //__String_h__11_9_2010__23_59_35__

