/********************************************************************
	created:	Monday 2010/08/09 at 18:39
	filename: 	Interface.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "core_base/Interface.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Interface::~Interface() 
	{

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OINTERFACE_IMPL(Interface)
		OFUNCR_OVERL(Orkige::TypeInfo const &,getTypeInfo,bp::return_internal_reference<>())
	OOBJECT_END	
}
