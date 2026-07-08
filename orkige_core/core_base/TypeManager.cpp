/********************************************************************
	created:	Monday 2010/08/09 at 18:49
	filename: 	TypeManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_base/TypeManager.h"
#include "core_util/optr.h"

namespace Orkige
{
	IMPL_OSINGLETON_GETCREATE(TypeManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TypeManager::TypeManager()
	{
		oInfo("...TypeManager created!...");
	}
	//---------------------------------------------------------
	TypeManager::~TypeManager()
	{
		oInfo("\t...TypeManager destroyed!...");
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}