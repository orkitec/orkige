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
		OFUNC_OVERL1(void,read,bool &)
		OFUNC_OVERL1(void,read,char &)
		OFUNC_OVERL1(void,read,signed char &)
		OFUNC_OVERL1(void,read,unsigned char &)
		OFUNC_OVERL1(void,read,wchar_t &)
		OFUNC_OVERL1(void,read,short &)
		OFUNC_OVERL1(void,read,unsigned short &)
		OFUNC_OVERL1(void,read,int &)
		OFUNC_OVERL1(void,read,unsigned int &)
		OFUNC_OVERL1(void,read,long &)
		OFUNC_OVERL1(void,read,unsigned long &)
		OFUNC_OVERL1(void,read,float &)
		OFUNC_OVERL1(void,read,double &)
		OFUNC_OVERL1(void,read,String &)
#ifndef ORKIGE_NDS
		OFUNC_OVERL1(void,read,ISerializeable &)
#endif
		OFUNC_OVERL1(void,read,optr<bool> &)
		OFUNC_OVERL1(void,read,optr<char> &)
		OFUNC_OVERL1(void,read,optr<signed char> &)
		OFUNC_OVERL1(void,read,optr<unsigned char> &)
		OFUNC_OVERL1(void,read,optr<wchar_t> &)
		OFUNC_OVERL1(void,read,optr<short> &)
		OFUNC_OVERL1(void,read,optr<unsigned short> &)
		OFUNC_OVERL1(void,read,optr<int> &)
		OFUNC_OVERL1(void,read,optr<unsigned int> &)
		OFUNC_OVERL1(void,read,optr<long> &)
		OFUNC_OVERL1(void,read,optr<unsigned long> &)
		OFUNC_OVERL1(void,read,optr<float> &)
		OFUNC_OVERL1(void,read,optr<double> &)
		OFUNC_OVERL1(void,read,optr<String> &)
		OFUNC_OVERL1(void,read,optr<ISerializeable> &)

		OFUNC_OVERL1(void,write,bool const &)
		OFUNC_OVERL1(void,write,char const &)
		OFUNC_OVERL1(void,write,signed char const &)
		OFUNC_OVERL1(void,write,unsigned char const &)
		OFUNC_OVERL1(void,write,wchar_t const &)
		OFUNC_OVERL1(void,write,short const &)
		OFUNC_OVERL1(void,write,unsigned short const &)
		OFUNC_OVERL1(void,write,int const &)
		OFUNC_OVERL1(void,write,unsigned int const &)
		OFUNC_OVERL1(void,write,long const &)
		OFUNC_OVERL1(void,write,unsigned long const &)
		OFUNC_OVERL1(void,write,float const &)
		OFUNC_OVERL1(void,write,double const &)
		OFUNC_OVERL1(void,write,String const &)
#ifndef ORKIGE_NDS
		OFUNC_OVERL1(void,write,ISerializeable &)
#endif

		OFUNC_OVERL1(void,write,optr<bool> const &)
		OFUNC_OVERL1(void,write,optr<char> const &)
		OFUNC_OVERL1(void,write,optr<signed char> const &)
		OFUNC_OVERL1(void,write,optr<unsigned char> const &)
		OFUNC_OVERL1(void,write,optr<wchar_t> const &)
		OFUNC_OVERL1(void,write,optr<short> const &)
		OFUNC_OVERL1(void,write,optr<unsigned short> const &)
		OFUNC_OVERL1(void,write,optr<int> const &)
		OFUNC_OVERL1(void,write,optr<unsigned int> const &)
		OFUNC_OVERL1(void,write,optr<long> const &)
		OFUNC_OVERL1(void,write,optr<unsigned long> const &)
		OFUNC_OVERL1(void,write,optr<float> const &)
		OFUNC_OVERL1(void,write,optr<double> const &)
		OFUNC_OVERL1(void,write,optr<String> const &)
		OFUNC_OVERL1(void,write,optr<ISerializeable> const &)

		OFUNC(isReading)
		OFUNC(isWriting)

		OFUNC(startReading)
		OFUNC(stopReading)
		OFUNC(startWriting)
		OFUNC(stopWriting)
	OOBJECT_END
}
