/**************************************************************
	created:	2010/08/16 at 23:12
	filename: 	IArchive.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __IArchive_h__16_8_2010__23_12_52__
#define __IArchive_h__16_8_2010__23_12_52__

#include "core_base/Meta.h"
#include "core_util/String.h"
#include "core_util/optr.h"
#include "core_base/Interface.h"
#include "core_serialization/StreamOperators.h"
#include "core_serialization/ISerializeable.h"

namespace Orkige
{
	class ISerializeable;
	//! virtual base archive type for serialization
	class ORKIGE_CORE_DLL IArchive : public Interface, public StreamOperators<IArchive>
	{
		OOBJECT(IArchive, Interface)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		bool readMode;									//!< marks if archive is in reading mode
		bool writeMode;									//!< marks if archive is in writing mode
		std::map<void*,unsigned int> registryOptrInt;	//!< registry of all saved pointers to prevent doubled saving
		std::map<unsigned int,void*> registryIntOptr;	//!< registry of all loaded pointers to prevent doubled loading
	private:
		//--- Methods -----------------------------------------
	public:
		//! start reading archive from given File/Path
		virtual bool startReading(String const & fileName)=0;
		//! stop reading archive
		virtual bool stopReading()=0;

		//! start writing archive to given File/Path
		virtual bool startWriting(String const & fileName)=0;
		//! stop writing archive
		virtual bool stopWriting()=0;

		//! read a bool value from archive
		virtual void read(bool & t)=0;
		//! read a char value from archive
		virtual void read(char & t)=0;
		//! read a signed char value from archive
		virtual void read(signed char & t)=0;
		//! read a unsigned char value from archive
		virtual void read(unsigned char & t)=0;
		//! read a wchar_t value from archive
		virtual void read(wchar_t & t)=0;
		//! read a short value from archive
		virtual void read(short & t)=0;
		//! read a unsigned short value from archive
		virtual void read(unsigned short & t)=0;
		//! read a int value from archive
		virtual void read(int & t)=0;
		//! read a unsigned int value from archive
		virtual void read(unsigned int & t)=0;
		//! read a long value from archive
		virtual void read(long & t)=0;
		//! read a unsigned long value from archive
		virtual void read(unsigned long & t)=0;
		//! read a float value from archive
		virtual void read(float & t)=0;
		//! read a double value from archive
		virtual void read(double & t)=0;
		//! read a String value from archive
		virtual void read(String & t)=0;
		//! @brief read a String value that MAY carry a named side attribute
		//! (see writeAttributed); attributeValue becomes "" when the value
		//! was written without one - which is exactly how legacy archives
		//! stay loadable. Backends without attribute support read the plain
		//! value.
		virtual void readAttributed(String & t, String const & attributeName,
			String & attributeValue)
		{
			(void)attributeName;
			attributeValue.clear();
			this->read(t);
		}
		//! read a ISerializeable object from archive
		virtual void read(ISerializeable & t)=0;
		//! read a ISerializeable derived from archive
		template<class Type> 
		void read(Type & obj,
			typename std::enable_if<std::is_base_of<ISerializeable,Type>::value>::type * = 0,
			typename std::enable_if<!is_optr<Type>::value>::type * = 0,
			typename std::enable_if<!std::is_convertible<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,ISerializeable>::value>::type * = 0)
		{
			oAssert(readMode);
			oAssert(!writeMode);
			ISerializeable & s(obj);
			this->read(s);
			Type * ptr = static_cast<Type*>(&s);
			oAssert(ptr);
			obj = *ptr;
		}
		//! read a enum value from archive
		template<class Type> 
		void read(Type & obj,
			typename std::enable_if<std::is_enum<Type>::value>::type * = 0,
			typename std::enable_if<!is_optr<Type>::value>::type * = 0,
			typename std::enable_if<!std::is_convertible<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,ISerializeable>::value>::type * = 0)
		{
			oAssert(readMode);
			oAssert(!writeMode);
			int i = static_cast<int>(obj);
			this->read(i);
			obj = static_cast<Type>(i);
		}
		//! read a bool pointer from archive
		virtual void read(optr<bool> & t)=0;
		//! read a char pointer from archive
		virtual void read(optr<char> & t)=0;
		//! read a signed char pointer from archive
		virtual void read(optr<signed char> & t)=0;
		//! read a unsigned char pointer from archive
		virtual void read(optr<unsigned char> & t)=0;
		//! read a wchar_t pointer from archive
		virtual void read(optr<wchar_t> & t)=0;
		//! read a short pointer from archive
		virtual void read(optr<short> & t)=0;
		//! read a unsigned short pointer from archive
		virtual void read(optr<unsigned short> & t)=0;
		//! read a int pointer from archive
		virtual void read(optr<int> & t)=0;
		//! read a unsigned int pointer from archive
		virtual void read(optr<unsigned int> & t)=0;
		//! read a long pointer from archive
		virtual void read(optr<long> & t)=0;
		//! read a unsigned long pointer from archive
		virtual void read(optr<unsigned long> & t)=0;
		//! read a float pointer from archive
		virtual void read(optr<float> & t)=0;
		//! read a double pointer from archive
		virtual void read(optr<double> & t)=0;
		//! read a String pointer from archive
		virtual void read(optr<String> & t)=0;
		//! read a ISerializeable pointer from archive
		virtual void read(optr<ISerializeable> & t)=0;
		//! read a ISerializeable derived pointer from archive
		template<typename Type> 
		void read(Type & obj,
			typename std::enable_if<is_optr<Type>::value>::type * = 0,
			typename std::enable_if<std::is_convertible<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,ISerializeable>::value>::type * = 0)
		{
			oAssert(readMode);
			oAssert(!writeMode);
			optr<ISerializeable> s;
			this->read(s);
			oAssert(s.get());
			obj = std::static_pointer_cast<typename Type::element_type>(s);
			oAssert(obj.get());
		}

		//! write a bool to archive
		virtual void write(bool const & t)=0;
		//! write a char to archive
		virtual void write(char const & t)=0;
		//! write a signed char to archive
		virtual void write(signed char const & t)=0;
		//! write a unsigned char to archive
		virtual void write(unsigned char const & t)=0;
		//! write a wchar_t to archive
		virtual void write(wchar_t const & t)=0;
		//! write a short to archive
		virtual void write(short const & t)=0;
		//! write a unsigned short to archive
		virtual void write(unsigned short const & t)=0;
		//! write a int to archive
		virtual void write(int const & t)=0;
		//! write a unsigned int to archive
		virtual void write(unsigned int const & t)=0;
		//! write a long to archive
		virtual void write(long const & t)=0;
		//! write a unsigned long to archive
		virtual void write(unsigned long const & t)=0;
		//! write a float to archive
		virtual void write(float const & t)=0;
		//! write a double to archive
		virtual void write(double const & t)=0;
		//! write a String to archive
		virtual void write(String const & t)=0;
		//! @brief write a String value carrying a named side attribute - the
		//! archive-positional-compatible way to attach optional metadata to a
		//! value (asset ids next to legacy resource names/paths): readers
		//! that don't know the attribute read the plain value, readAttributed
		//! sees "" for values written without it. An empty attributeValue
		//! writes the plain value. Backends without attribute support just
		//! write the value.
		virtual void writeAttributed(String const & t,
			String const & attributeName, String const & attributeValue)
		{
			(void)attributeName;
			(void)attributeValue;
			this->write(t);
		}
		//! write a ISerializeable to archive
		virtual void write(ISerializeable & t)=0;
		//! write a ISerializeable derived object to archive
		template<class Type> 
		void write(Type & obj,
			typename std::enable_if<std::is_base_of<ISerializeable,Type>::value>::type * = 0,
			typename std::enable_if<!is_optr<Type>::value>::type * = 0,
			typename std::enable_if<!std::is_convertible<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,ISerializeable>::value>::type * = 0)
		{
			oAssert(writeMode);
			oAssert(!readMode);
			ISerializeable & s(obj); 
			this->write(s);
		}
		//! write a enum to archive
		template<class Type> 
		void write(Type & obj,
			typename std::enable_if<std::is_enum<Type>::value>::type * = 0,
			typename std::enable_if<!is_optr<Type>::value>::type * = 0,
			typename std::enable_if<!std::is_convertible<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,ISerializeable>::value>::type * = 0)
		{
			oAssert(writeMode);
			oAssert(!readMode);
			int i = static_cast<int>(obj);
			this->write(i);
		}
		//! write a bool pointer to archive
		virtual void write(optr<bool> const & t)=0;
		//! write a char pointer to archive
		virtual void write(optr<char> const & t)=0;
		//! write a signed char pointer to archive
		virtual void write(optr<signed char> const & t)=0;
		//! write a unsigned char pointer to archive
		virtual void write(optr<unsigned char> const & t)=0;
		//! write a wchar_t pointer to archive
		virtual void write(optr<wchar_t> const & t)=0;
		//! write a short pointer to archive
		virtual void write(optr<short> const & t)=0;
		//! write a unsigned short pointer to archive
		virtual void write(optr<unsigned short> const & t)=0;
		//! write a int pointer to archive
		virtual void write(optr<int> const & t)=0;
		//! write a unsigned int pointer to archive
		virtual void write(optr<unsigned int> const & t)=0;
		//! write a long pointer to archive
		virtual void write(optr<long> const & t)=0;
		//! write a unsigned long pointer to archive
		virtual void write(optr<unsigned long> const & t)=0;
		//! write a float pointer to archive
		virtual void write(optr<float> const & t)=0;
		//! write a double pointer to archive
		virtual void write(optr<double> const & t)=0;
		//! write a String pointer to archive
		virtual void write(optr<String> const & t)=0;
		//! write a ISerializeable pointer to archive
		virtual void write(optr<ISerializeable> const & t)=0;
		//! write a ISerializeable derived pointer to archive
		template<class Type> 
		void write(Type & obj,
			typename std::enable_if<is_optr<Type>::value>::type * = 0,
			typename std::enable_if<std::is_convertible<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,optr<ISerializeable> >::value>::type * = 0,
			typename std::enable_if<!std::is_same<Type,ISerializeable>::value>::type * = 0)
		{
			oAssert(writeMode);
			oAssert(!readMode);
			optr<ISerializeable> s = std::static_pointer_cast<ISerializeable>(obj);
			oAssert(s.get());
			this->write(s);
		}

		//! is Archive currently reading?
		inline bool isReading();
		//! is Archive currently writing
		inline bool isWriting();
		//! is given pointer already registered?
		inline bool isRegistered(optr<void> const & ptr);
		//! is a pointer with given id registered?
		inline bool isRegistered(unsigned int ptrId);
	protected:
		inline IArchive();
	private:
	};
	//---------------------------------------------------------
	inline IArchive::IArchive()
	{

	}
	//---------------------------------------------------------
	inline bool IArchive::isReading()								
	{	
		return this->readMode;	
	}
	//---------------------------------------------------------
	inline bool IArchive::isWriting()								
	{	
		return this->writeMode;	
	}
	//---------------------------------------------------------
	inline bool IArchive::isRegistered(optr<void> const & ptr)	
	{	
		return this->registryOptrInt.find(ptr.get()) != this->registryOptrInt.end();		
	}
	//---------------------------------------------------------
	inline bool IArchive::isRegistered(unsigned int ptrId)		
	{	
		return this->registryIntOptr.find(ptrId) != this->registryIntOptr.end();	
	}
	//---------------------------------------------------------
}

#endif //__IArchive_h__16_8_2010__23_12_52__
