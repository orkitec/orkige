/**************************************************************
	created:	2010/08/17 at 12:06
	filename: 	IArchive.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_serialization/IArchive.h"
#include "core_serialization/ISerializeable.h"

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
	OABSTRACT_IMPL(IArchive)
		//scripting backends cannot bind the primitive out-parameter overloads
		//(non-const lvalue references to value types); only the ISerializeable
		//overloads are exported - the boost::python-era per-primitive list
		//died with that backend
		OFUNC_OVERL1(void,read,ISerializeable &)
		OFUNC_OVERL1(void,write,ISerializeable &)

		OFUNC(isReading)
		OFUNC(isWriting)

		OFUNC(startReading)
		OFUNC(stopReading)
		OFUNC(startWriting)
		OFUNC(stopWriting)
	OOBJECT_END
}
