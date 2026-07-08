/********************************************************************
	created:	Monday 2010/08/09 at 18:51
	filename: 	Value.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_base/Value.h"

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

	OOBJECT_TEMPLATE_IMPL(Value,int)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,unsigned int)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,unsigned long)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,long)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,bool)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,float)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,double)
		OVAR(value)
	OOBJECT_END
	OOBJECT_TEMPLATE_IMPL(Value,String)
		OVAR(value)
	OOBJECT_END
}
