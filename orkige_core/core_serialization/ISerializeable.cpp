/**************************************************************
	created:	2010/08/17 at 12:07
	filename: 	ISerializeable.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_serialization/ISerializeable.h"
#include "core_serialization/IArchive.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ISerializeable::~ISerializeable()
	{

	}
	//---------------------------------------------------------
	bool ISerializeable::createBeforeLoad()	
	{
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	ISerializeable::ISerializeable()
	{
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

	OABSTRACT_IMPL(ISerializeable)
		OFUNC(save)
		OFUNC(load)
	OOBJECT_END
}
