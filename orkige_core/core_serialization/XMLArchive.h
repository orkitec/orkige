/**************************************************************
	created:	2010/08/17 at 12:10
	filename: 	XMLArchive.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __XMLArchive_h__17_8_2010__12_10_18__
#define __XMLArchive_h__17_8_2010__12_10_18__

#include "core_serialization/IArchive.h"

namespace tinyxml2
{
	class XMLDocument;
	class XMLElement;
}

namespace Orkige
{
	//! IArchive that writes and reades to xml
	class ORKIGE_CORE_DLL XMLArchive : public IArchive
	{
		OOBJECT(XMLArchive,IArchive)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		optr<tinyxml2::XMLDocument>		file;
		tinyxml2::XMLElement*			currentElement;
		Orkige::String					fileName;
		//--- Methods -----------------------------------------
	public:
		XMLArchive();
		virtual ~XMLArchive();
		virtual bool startReading(String const & fileName);
		virtual bool stopReading();

		virtual bool startWriting(String const & fileName);
		virtual bool stopWriting();

		virtual void read(bool & t);
		virtual void read(char & t);
		virtual void read(signed char & t);
		virtual void read(unsigned char & t);
		virtual void read(wchar_t & t);
		virtual void read(short & t);
		virtual void read(unsigned short & t);
		virtual void read(int & t);
		virtual void read(unsigned int & t);
		virtual void read(long & t);
		virtual void read(unsigned long & t);
		virtual void read(float & t);
		virtual void read(double & t);
		virtual void read(String & t);
		virtual void read(ISerializeable & t);
		//! read (existing) but don't create
		virtual void readx(ISerializeable & t);

		virtual void read(optr<bool> & t);
		virtual void read(optr<char> & t);
		virtual void read(optr<signed char> & t);
		virtual void read(optr<unsigned char> & t);
		virtual void read(optr<wchar_t> & t);
		virtual void read(optr<short> & t);
		virtual void read(optr<unsigned short> & t);
		virtual void read(optr<int> & t);
		virtual void read(optr<unsigned int> & t);
		virtual void read(optr<long> & t);
		virtual void read(optr<unsigned long> & t);
		virtual void read(optr<float> & t);
		virtual void read(optr<double> & t);
		virtual void read(optr<String> & t);
		virtual void read(optr<ISerializeable> & t);

		virtual void write(bool const & t);
		virtual void write(char const & t);
		virtual void write(signed char const & t);
		virtual void write(unsigned char const & t);
		virtual void write(wchar_t const & t);
		virtual void write(short const & t);
		virtual void write(unsigned short const & t);
		virtual void write(int const & t);
		virtual void write(unsigned int const & t);
		virtual void write(long const & t);
		virtual void write(unsigned long const & t);
		virtual void write(float const & t);
		virtual void write(double const & t);
		virtual void write(String const & t);
		virtual void write(ISerializeable & t);

		virtual void write(optr<bool> const & t);
		virtual void write(optr<char> const & t);
		virtual void write(optr<signed char> const & t);
		virtual void write(optr<unsigned char> const & t);
		virtual void write(optr<wchar_t> const & t);
		virtual void write(optr<short> const & t);
		virtual void write(optr<unsigned short> const & t);
		virtual void write(optr<int> const & t);
		virtual void write(optr<unsigned int> const & t);
		virtual void write(optr<long> const & t);
		virtual void write(optr<unsigned long> const & t);
		virtual void write(optr<float> const & t);
		virtual void write(optr<double> const & t);
		virtual void write(optr<String> const & t);
		virtual void write(optr<ISerializeable> const & t);
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__XMLArchive_h__17_8_2010__12_10_18__
