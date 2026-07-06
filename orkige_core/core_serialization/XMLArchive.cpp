/**************************************************************
	created:	2010/08/17 at 12:11
	filename: 	XMLArchive.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include <tinyxml2.h>

#include "core_serialization/XMLArchive.h"
#include "core_serialization/ISerializeable.h"

#include <sstream>
#include <typeinfo>

namespace Orkige
{
	//---------------------------------------------------------
	//! convert a c-string attribute value to the given ValueType (replacement for the old lexical_cast)
	template<typename ValueType>
	static ValueType XMLArchiveFromString(const char * str)
	{
		std::istringstream stream(str);
		ValueType value;
		stream >> value;
		if(stream.fail())
			throw std::bad_cast();
		return value;
	}
	//---------------------------------------------------------
	template<>
	String XMLArchiveFromString<String>(const char * str)
	{
		return String(str);
	}
	//---------------------------------------------------------
	template<>
	wchar_t XMLArchiveFromString<wchar_t>(const char * str)
	{
		if(!str || !str[0])
			throw std::bad_cast();
		return (wchar_t)str[0];
	}
	//---------------------------------------------------------
	//! convert a value to a String (replacement for the old lexical_cast<String>)
	template<typename ValueType>
	static String XMLArchiveToString(ValueType const & value)
	{
		std::ostringstream stream;
		stream << value;
		return stream.str();
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	XMLArchive::XMLArchive()
	{
		this->readMode = false;
		this->writeMode = false;
		this->currentElement = NULL;
	}
	//---------------------------------------------------------
	XMLArchive::~XMLArchive()
	{
	}
	//---------------------------------------------------------
	bool XMLArchive::startWriting(String const & fileName)
	{
		oAssert(!writeMode);
		oAssert(!readMode);
		oAssert(!fileName.empty());
		this->fileName = fileName;
		this->readMode = false;
		this->writeMode = true;
		this->file = onew(new tinyxml2::XMLDocument());

		if(!this->file || this->file->Error())
		{
			oDebugMsg("serialize",0,"Errot Loading file: "<<fileName);
			oDebugMsg("serialize",0,this->file->GetErrorStr1());
			return false;
		}
		tinyxml2::XMLDeclaration* decl = this->file->NewDeclaration("1.0, UTF-8, yes");
		this->file->InsertEndChild(decl);
		this->file->SaveFile(this->fileName.c_str());
		if(this->file->Error())
		{
			oDebugMsg("serialize",0,this->file->GetErrorStr1());
			return false;
		}
		this->currentElement = this->file->NewElement("XMLArchive");
		this->currentElement->SetAttribute("Version", (int)0);
		return true;
	}
	//---------------------------------------------------------
	bool XMLArchive::stopWriting()
	{
		oAssert(writeMode);
		oAssert(!readMode);
		this->file->InsertEndChild(this->currentElement);
		this->file->SaveFile(this->fileName.c_str());
		if(this->file->Error())
		{
			oDebugMsg("serialize",0,this->file->GetErrorStr1());
			return false;
		}
		this->readMode = false;
		this->writeMode = false;
		this->currentElement = NULL;
		this->file.reset();
		return true;
	}
	//---------------------------------------------------------
	bool XMLArchive::startReading(String const & fileName)
	{
		oAssert(!writeMode);
		oAssert(!readMode);
		oAssert(!fileName.empty());
		this->readMode = true;
		this->writeMode = false;
		this->fileName = fileName;
		this->file = onew(new tinyxml2::XMLDocument());
		this->file->LoadFile(fileName.c_str());
		if(!this->file || this->file->Error())
		{
			oDebugMsg("serialize",0,"Errot Loading file: "<<fileName);
			oDebugMsg("serialize",0,this->file->GetErrorStr1());
			return false;
		}
		this->file->LoadFile(this->file->Value());
		if(!this->file || this->file->Error())
		{
			oDebugMsg("serialize",0,"Errot Loading file: "<<fileName);
			oDebugMsg("serialize",0,this->file->GetErrorStr1());
			return false;
		}
		this->currentElement = this->file->RootElement();
		oAssert(this->currentElement);
		const char * attr_version = this->currentElement->Attribute("Version");
		oAssert(attr_version);
		int version =  XMLArchiveFromString<int>(attr_version);
		this->currentElement = this->currentElement->FirstChildElement();
		return true;
	}
	//---------------------------------------------------------
	bool XMLArchive::stopReading()
	{
		this->currentElement = NULL;
		this->file.reset();
		this->readMode = false;
		this->writeMode = false;
		return true;
	}
	//---------------------------------------------------------
	//--WRITING------------------------------------------------
#if defined(ORKIGE_NDS) || defined(__ANDROID__)
	void XMLArchiveReadElementWCT(XMLArchive * ar, tinyxml2::XMLElement* element, wchar_t &t)
	{
		oAssert(ar->isReading());
		oAssert(!ar->isWriting());
		oAssert(element);
		const char * attr_type	= element->Value();
		const char * attr_value = element->Attribute("value");
		oAssert(attr_type);
		oAssert(attr_value);
		String temp = XMLArchiveFromString<String>(attr_value);
		std::basic_string<wchar_t> widestr = std::basic_string<wchar_t>(temp.begin(), temp.end());
		const wchar_t* widecstr = widestr.c_str();
		t = *widecstr;
		tinyxml2::XMLNode* node = element->NextSibling();
		if(node)
			element = static_cast<tinyxml2::XMLElement*>(node);
	}
#endif
	//---------------------------------------------------------
	template<typename ValueType>
	void XMLArchiveReadElement(XMLArchive * ar, tinyxml2::XMLElement* element, ValueType &t)
	{
		oAssert(ar->isReading());
		oAssert(!ar->isWriting());
		oAssert(element);
		const char * attr_type	= element->Value();
		const char * attr_value = element->Attribute("value");
		oAssert(attr_type);
		oAssert(attr_value);
		t = XMLArchiveFromString<ValueType>(attr_value);
		tinyxml2::XMLNode* node = element->NextSibling();
		if(node)
			element = static_cast<tinyxml2::XMLElement*>(node);
	}
	//---------------------------------------------------------
	template<typename ValueType>
	void XMLArchiveReadPtrElement(XMLArchive * ar, std::map<unsigned int,void*> & registry, tinyxml2::XMLElement* element, optr<ValueType> & t)
	{
		oAssert(ar->isReading());
		oAssert(!ar->isWriting());
		oAssert(element);
		const char * attr_type		= element->Value();
		const char * attr_ref		= element->Attribute("ref");
		oAssert(attr_type);
		if(attr_ref)
		{
			int ref_id = XMLArchiveFromString<int>(attr_ref);
			oAssert(ar->isRegistered(ref_id));
			t = std::static_pointer_cast<ValueType>(oBadPointer(registry[ref_id])); 
			oAssert(t.get());
		}
		else
		{
			const char * attr_ref_id	= element->Attribute("ref_id");
			oAssert(attr_ref_id);
			int ref_id = XMLArchiveFromString<int>(attr_ref_id);
			tinyxml2::XMLElement* oldElement = element;
			element = element->FirstChildElement();
			t = onew(new ValueType());//me doez teh magic =)
			registry[ref_id] = t.get();
			ar->read(*t);
			element = oldElement;	
		}
	}
	//---------------------------------------------------------
	void XMLArchive::read(bool & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(char & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(signed char & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(unsigned char & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(wchar_t & t)
	{
#if defined(ORKIGE_NDS) || defined(__ANDROID__)
		XMLArchiveReadElementWCT(this, this->currentElement, t);
#else
		XMLArchiveReadElement(this, this->currentElement, t);
#endif
	}
	//---------------------------------------------------------
	void XMLArchive::read(short & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(unsigned short & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(int & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(unsigned int & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(long & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(unsigned long & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(float & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(double & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(String & t)
	{
		XMLArchiveReadElement(this, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(ISerializeable & t)
	{
		oAssert(this->isReading());
		oAssert(!this->isWriting());
		oAssert(this->currentElement);
		const char * attr_type	= this->currentElement->Value();
		const char * create	= this->currentElement->Attribute("create");
		oAssert(attr_type);
		oAssert(create);
		bool createBeforeLoad = XMLArchiveFromString<bool>(create);
		tinyxml2::XMLElement* oldElement = this->currentElement;
		this->currentElement = this->currentElement->FirstChildElement();

		if(createBeforeLoad)
		{
			String type = String(attr_type) + "()";
			//t = bp::extract<ISerializeable>(PythonInterpreter::getSingleton().eval(type));
			Interface* tempCreatedObject = TypeManager::getSingleton().create(attr_type);
			t = (*static_cast<ISerializeable*>(tempCreatedObject));
			delete tempCreatedObject;
		}

		t.load(oBadPointer(this));
		tinyxml2::XMLNode* node = oldElement->NextSibling();
		if(node)
			this->currentElement = static_cast<tinyxml2::XMLElement*>(node);
	}
	//---------------------------------------------------------
	void XMLArchive::readx(ISerializeable & t)
	{
		oAssert(this->isReading());
		oAssert(!this->isWriting());
		oAssert(this->currentElement);
		const char * attr_type	= this->currentElement->Value();
		const char * create	= this->currentElement->Attribute("create");
		oAssert(attr_type);
		oAssert(create);
		bool createBeforeLoad = XMLArchiveFromString<bool>(create);
		tinyxml2::XMLElement* oldElement = this->currentElement;
		this->currentElement = this->currentElement->FirstChildElement();

		t.load(oBadPointer(this));
		tinyxml2::XMLNode* node = oldElement->NextSibling();
		if(node)
			this->currentElement = static_cast<tinyxml2::XMLElement*>(node);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<bool> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement,t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<char> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<signed char> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<unsigned char> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<wchar_t> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<short> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<unsigned short> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<int> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<unsigned int> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<long> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<unsigned long> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<float> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<double> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<String> & t)
	{
		XMLArchiveReadPtrElement(this, this->registryIntOptr, this->currentElement, t);
	}
	//---------------------------------------------------------
	void XMLArchive::read(optr<ISerializeable> & t)
	{
		oAssert(this->isReading());
		oAssert(!this->isWriting());
		oAssert(this->currentElement);
		const char * attr_type		= this->currentElement->Value();
		const char * attr_ref		= this->currentElement->Attribute("ref");
		oAssert(attr_type);
		if(attr_ref)
		{
			int ref_id = XMLArchiveFromString<int>(attr_ref);
			t = std::static_pointer_cast<ISerializeable>(oBadPointer(this->registryIntOptr[ref_id]));
		}
		else
		{
			const char * attr_ref_id	= this->currentElement->Attribute("ref_id");
			const char * create	= this->currentElement->Attribute("create");
			oAssert(attr_ref_id);
			oAssert(create);
			bool createBeforeLoad = XMLArchiveFromString<bool>(create);
			int ref_id = XMLArchiveFromString<int>(attr_ref_id);
			tinyxml2::XMLElement* oldElement = this->currentElement;
			this->currentElement = this->currentElement->FirstChildElement();
			
			if(createBeforeLoad)
			{
				String type = String(attr_type) + "()";
				//t = PythonInterpreter::getSingleton().eval<optr<ISerializeable>>(type);
				t= onew(static_cast<ISerializeable*>(TypeManager::getSingleton().create(attr_type)));
			}

			this->registryIntOptr[ref_id] = t.get();
			this->readx(*t);
			this->currentElement= oldElement;	
		}
		tinyxml2::XMLNode* node = this->currentElement->NextSibling();
		if(node)
			this->currentElement = static_cast<tinyxml2::XMLElement*>(node);
	}
	//--WRITING------------------------------------------------
	//---------------------------------------------------------
	template<typename ValueType>
	void XMLArchiveWriteElement(XMLArchive * ar, tinyxml2::XMLElement* currentElement, ValueType const & value, String const & type)
	{
		oAssert(ar);
		oAssert(!ar->isReading());
		oAssert(ar->isWriting());
		oAssert(!type.empty());
		oAssert(currentElement);
		tinyxml2::XMLElement* element = currentElement->GetDocument()->NewElement(type.c_str());
		element->SetAttribute("value",(XMLArchiveToString(value)).c_str());
		currentElement->InsertEndChild(element);
	}
	//---------------------------------------------------------
	template<typename ValueType>
	void XMLArchiveWritePtrElement(XMLArchive * ar,std::map<void*,unsigned int> & registry, tinyxml2::XMLElement* currentElement, ValueType const & value, String const & type)
	{
		oAssert(ar);
		oAssert(!ar->isReading());
		oAssert(ar->isWriting());
		oAssert(!type.empty());
		oAssert(currentElement);
		if(ar->isRegistered(value))
		{
			tinyxml2::XMLElement* element = currentElement->GetDocument()->NewElement(type.c_str());
			element->SetAttribute("ref",registry[value.get()]);
			currentElement->InsertEndChild(element);
		}
		else
		{
			unsigned int ref_id = (int)registry.size();
			registry[value.get()] = ref_id;
			tinyxml2::XMLElement* oldElement = currentElement;
			currentElement = currentElement->GetDocument()->NewElement(type.c_str());
			currentElement->SetAttribute("ref_id",ref_id);
			ar->write(*value);
			oldElement->InsertEndChild(currentElement);
			currentElement = oldElement;
		}
	}
	//---------------------------------------------------------
	void XMLArchive::write(bool const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "bool");
	}
	//---------------------------------------------------------
	void XMLArchive::write(char const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "char");
	}
	//---------------------------------------------------------
	void XMLArchive::write(signed char const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "signed_char");
	}
	//---------------------------------------------------------
	void XMLArchive::write(unsigned char const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "unsigned_char");
	}
	//---------------------------------------------------------
	void XMLArchive::write(wchar_t const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, (char)t, "wchar_t");
	}
	//---------------------------------------------------------
	void XMLArchive::write(short const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "short");
	}
	//---------------------------------------------------------
	void XMLArchive::write(unsigned short const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "unsigned_short");
	}
	//---------------------------------------------------------
	void XMLArchive::write(int const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "int");
	}
	//---------------------------------------------------------
	void XMLArchive::write(unsigned int const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "unsigned_int");
	}
	//---------------------------------------------------------
	void XMLArchive::write(long const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "long");
	}
	//---------------------------------------------------------
	void XMLArchive::write(unsigned long const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "unsigned_long");
	}
	//---------------------------------------------------------
	void XMLArchive::write(float const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "float");
	}
	//---------------------------------------------------------
	void XMLArchive::write(double const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "double");
	}
	//---------------------------------------------------------
	void XMLArchive::write(String const & t)
	{
		XMLArchiveWriteElement(this, this->currentElement, t, "String");
	}
	//---------------------------------------------------------
	void XMLArchive::write(ISerializeable & t)
	{
		oAssert(writeMode);
		oAssert(!readMode);
		tinyxml2::XMLElement* oldElement = this->currentElement;
		const char * className = t.getTypeInfo().getName().c_str();
		oAssert(className);
		bool createBeforeLoad = t.createBeforeLoad();
		this->currentElement = this->file->NewElement(className);
		this->currentElement->SetAttribute("create",(XMLArchiveToString(createBeforeLoad)).c_str());
		t.save(oBadPointer(this));
		oldElement->InsertEndChild(this->currentElement);
		this->currentElement = oldElement;
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<bool> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "bool");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<char> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "char");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<signed char> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "signed_char");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<unsigned char> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "unsigned_char");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<wchar_t> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "wchar_t");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<short> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "short");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<unsigned short> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "unsigned_short");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<int> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "int");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<unsigned int> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "unsigned_int");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<long> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "long");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<unsigned long> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "unsigned long");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<float> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "float");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<double> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "double");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<String> const & t)
	{
		XMLArchiveWritePtrElement(this, this->registryOptrInt, this->currentElement, t, "String");
	}
	//---------------------------------------------------------
	void XMLArchive::write(optr<ISerializeable> const & t)
	{
		oAssert(!this->isReading());
		oAssert(this->isWriting());
		oAssert(this->currentElement);
		if(this->isRegistered(t))
		{
			const char * className = t->getTypeInfo().getName().c_str();
			oAssert(className);

			tinyxml2::XMLElement* element = this->file->NewElement(className);
			element->SetAttribute("ref",this->registryOptrInt[t.get()]);
			this->currentElement->InsertEndChild(element);
		}
		else
		{
			unsigned int ref_id = (int)this->registryOptrInt.size();
			this->registryOptrInt[t.get()] = ref_id;
			tinyxml2::XMLElement* oldElement = this->currentElement;
			const char * className = t->getTypeInfo().getName().c_str();
			oAssert(className);
			this->currentElement = this->file->NewElement(className);
			this->currentElement->SetAttribute("ref_id",ref_id);
			bool createBeforeLoad = t->createBeforeLoad();
			this->currentElement->SetAttribute("create",(XMLArchiveToString(createBeforeLoad)).c_str());
			this->write(*t);
			oldElement->InsertEndChild(this->currentElement);
			this->currentElement = oldElement;
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(XMLArchive)
		OCONSTRUCTOR0()
	OOBJECT_END
}
