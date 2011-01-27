#include <ios>
#include <iostream>
#include <core_tinyxml/tinyxml.h>
#include <core_util/optr.h>
#include <engine_util/StringUtil.h>
#include <conio.h>
using namespace Orkige;
int main(int argc, char **argv)
{
	if(argc != 2)
	{
		std::cout << "no font file given!" << std::endl;
		_getch();
		return -1;
	}
	String filename = argv[1];
	optr<TiXmlDocument>	document =  onew(new TiXmlDocument(filename.c_str()));
	if(!document || document->Error())
	{
		std::cerr << "Error Loading file: "<<filename<<std::endl <<document->ErrorDesc() <<std::endl;
		_getch();
		return -1;
	}
	document->LoadFile(document->Value(), TIXML_ENCODING_UTF8);
	if(!document || document->Error())
	{
		std::cerr << "Error Loading file: "<<filename<<std::endl <<document->ErrorDesc()<<std::endl;
		_getch();
		return -1;
	}
	TiXmlElement* xmlRoot = document->RootElement();
	if(!xmlRoot || document->Error())
	{
		std::cerr << "Error finding XML-Root in file: "<<filename<<std::endl <<document->ErrorDesc()<<std::endl;
		_getch();
		return -1;
	}

	for(TiXmlElement* elementType = xmlRoot->FirstChildElement(); elementType; elementType = elementType->NextSiblingElement())
	{
		String const & elementTypeName = elementType->Value();
		if(elementTypeName == "info")
		{
			std::cout << "spacing " << elementType->Attribute("spacing") << std::endl;
		}
		if(elementTypeName == "common")
		{
			std::cout << "lineheight " << elementType->Attribute("lineHeight") << std::endl;
			std::cout << "baseline " << elementType->Attribute("base") << std::endl;
		}
		if(elementTypeName == "chars")
		{
			int count = Orkige::StringUtil::Converter::fromString<int>(elementType->Attribute("count"));
			std::cout << "range " << 32 << " " << 32+count<< std::endl;
			for(TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
			{
				std::cout << "glyph_" << element->Attribute("id") 
					<< " " << element->Attribute("x") 
					<< " " << element->Attribute("y")
					<< " " << element->Attribute("width")
					<< " " << element->Attribute("height")
					<< " " << element->Attribute("xadvance") << std::endl;
			}

		}
	}
	std::cout << argv[1] << std::endl;
	std::cout << "offsetx:";
	int offsetx;
	std::cin >> offsetx;
	std::cout << "offsety:";
	int offsety;
	std::cin >> offsety;
	std::cout << "done!";
	_getch();
	return 0;
}