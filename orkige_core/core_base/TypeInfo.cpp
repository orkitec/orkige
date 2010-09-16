/********************************************************************
	created:	Monday 2010/08/09 at 18:47
	filename: 	TypeInfo.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "core_base/TypeInfo.h"
#include "core_base/Meta.h"
#include <boost/shared_ptr.hpp>

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
#ifdef ORKIGE_NOSCRIPT
	void TypeInfo::OrkigeMetaExport(const char * currentOrkigeModuleName) 
	{
		typedef TypeInfo ExposedClassType;
	}
#elif ORKIGE_LUA
	bp::scope  TypeInfo::OrkigeMetaExport(const char * currentOrkigeModuleName) 
	{
		typedef TypeInfo ExposedClassType;
		bp::class_< TypeInfo , boost::shared_ptr<TypeInfo> > py_class( "TypeInfo");
		OCONSTRUCTOR1(String)
			OFUNCCR(getId)
			OFUNCCR(getName)
			OFUNC(isEqual)
			OFUNC(isNotEqual)
			OOBJECT_END
#else
	void TypeInfo::OrkigeMetaExport(const char * currentOrkigeModuleName) 
	{
		typedef TypeInfo ExposedClassType;
		bp::class_< TypeInfo , boost::noncopyable, boost::shared_ptr<TypeInfo>> py_class( "TypeInfo" , bp::no_init );
		OCONSTRUCTOR1(String)
			OFUNCCR(getId)
			OFUNCCR(getName)
			OFUNC(isEqual)
			OFUNC(isNotEqual)
	}
#endif
}
