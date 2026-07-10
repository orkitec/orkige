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

		//! @brief begin an in-memory write session (no file IO): the archive
		//! builds the same "XMLArchive" element tree startWriting does, but
		//! stopWritingMemory returns the serialized text instead of writing a
		//! file. The unit prefab child overrides / baselines are captured in
		//! (a component's serialized state as a standalone string).
		bool startWritingMemory();
		//! finish an in-memory write session, returning the serialized XML text
		//! (empty on error); the archive is reset afterwards
		String stopWritingMemory();
		//! @brief begin an in-memory read session over a string produced by
		//! stopWritingMemory; the cursor is positioned like startReading (at the
		//! first value element). stopReading tears it down. False on parse error.
		bool startReadingMemory(String const & xml);

		//! @brief is the read cursor positioned on a value element (false past
		//! the last one). Lets a reader walk a variable block defensively.
		bool hasMoreElements() const { return this->currentElement != nullptr; }
		//! @brief advance the read cursor past the current element WITHOUT
		//! decoding it (the whole subtree of a nested element is stepped over) -
		//! the read-only counterpart to read() for skipping a block whose
		//! payload the caller does not need.
		void skipElement();

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
		//! read a String element plus its (optional) named side attribute
		virtual void readAttributed(String & t, String const & attributeName,
			String & attributeValue);
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
		//! write a String element carrying an extra named attribute (skipped
		//! when the attribute value is empty)
		virtual void writeAttributed(String const & t,
			String const & attributeName, String const & attributeValue);
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
