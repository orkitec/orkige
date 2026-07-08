/********************************************************************
	created:	Monday 2010/08/09 at 18:47
	filename: 	TypeInfo.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_base/TypeInfo.h"
#include "core_base/Meta.h"
#include <memory>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	//hand-written export (TypeInfo has no OOBJECT block); the backend-neutral
	//OUSERTYPE vocabulary keeps this free of backend #ifdefs
	void TypeInfo::OrkigeMetaExport(const char * currentOrkigeModuleName)
	{
		OUSERTYPE("TypeInfo", TypeInfo)
		OCONSTRUCTOR1(String)
		OFUNCCR(getId)
		OFUNCCR(getName)
		OFUNC(isEqual)
		OFUNC(isNotEqual)
	}
}
