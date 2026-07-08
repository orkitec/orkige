/********************************************************************
	created:	Monday 2010/08/09 at 18:43
	filename: 	Object.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_base/Object.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Object::~Object() 
	{

	}
	//---------------------------------------------------------
	void Object::save(optr<IArchive> const & ar)
	{
		ObjectAttributeHolder::save(ar);
		ar << this->objectId;
	}
	//---------------------------------------------------------
	void Object::load(optr<IArchive> const & ar)
	{
		ObjectAttributeHolder::load(ar);
		ar >> this->objectId;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

	IMPLEMENT_ATTRIBUTEHOLDER(String, Object)
		//#ifndef ORKIGE_IPHONE
		/*#ifndef ORKIGE_NDS*/
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, int)
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, long)
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, uint)
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, float)
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, double)
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, bool)
		IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(String, Object, String)
		//#endif
		/*#endif*/

		OOBJECT_IMPL(Object)
		OCONSTRUCTOR0()
		OCONSTRUCTOR1(String)
		OFUNCCR(getObjectID)
		//setAttribute/getAttribute are template overload sets - not exposable
		//generically (the luabind-era per-type OTPLFUNCDEFARGS exports died
		//with that backend); attribute access from scripts goes through the
		//AttributeWrapper usertypes
	OOBJECT_END
}


