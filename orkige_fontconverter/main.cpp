#define cimg_OS 0
#define cimg_use_png
#include "engine_util/CImg.h"      // Open source image library (http://cimg.sourceforge.net/)
#include <engine_module/EnginePrerequisites.h>
#include <ios>
#include <iostream>
#include <core_tinyxml/tinyxml.h>
#include <core_util/optr.h>
#include <engine_util/StringUtil.h>
#include <conio.h>
#include <direct.h>
#include <stdlib.h>
#include <stdio.h>
#include <Ogre.h>

using namespace Orkige;
int main(int argc, char **argv)
{
	String path;
	char* buffer;

	// Get the current working directory: 
	if( (buffer = _getcwd( NULL, 0 )) == NULL )
		perror( "_getcwd error" );
	else
	{
		path = buffer;
		#include "core_debug/DisableMemoryManager.h"
		free(buffer);
		#include "core_debug/EnableMemoryManager.h"
	}
	std::cout << path << std::endl;

	OPENFILENAME ofn;
	char szFileName[MAX_PATH] = "";

	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn); // SEE NOTE BELOW
	ofn.hwndOwner = 0;
	ofn.lpstrFilter = "orkige gui Files (*.ogui)\0*.ogui\0";
	ofn.lpstrFile = szFileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY;
	ofn.lpstrDefExt = "ogui";

	std::string filename;

	if(GetOpenFileName(&ofn))
	{
		filename = szFileName;
		if(filename.empty())
		{
			std::cerr << "Error Loading file!" << std::endl;
			system("pause");
			return -1;
		}
	}
	else
	{
		std::cerr << "Error Loading file!" << std::endl;
		system("pause");
		return -1;
	}

	filename = filename.substr(filename.find_last_of("\\")+1, filename.length());
	std::cout << "Filename: " << filename << std::endl;

	std::cout << "image size:";
	int image_size;
	std::cin >> image_size;

	int returnValue = system(("" + path +"\\bmfontgen.exe -fontdialog -bmsize "+ StringUtil::Converter::toString(image_size) + " -output " + filename + ".temp").c_str());
	std::cout << "bmfontgen.exe returned: " << returnValue << std::endl;

	Ogre::ConfigFile oguifile;
	oguifile.loadDirect(filename, " ", true);

	optr<TiXmlDocument>	document =  onew(new TiXmlDocument((filename + ".temp.xml").c_str()));
	if(!document || document->Error())
	{
		std::cerr << "Error Loading file: "<<filename<<std::endl <<document->ErrorDesc() <<std::endl;
		system("pause");
		return -1;
	}
	document->LoadFile(document->Value(), TIXML_ENCODING_UTF8);
	if(!document || document->Error())
	{
		std::cerr << "Error Loading file: "<<filename<<std::endl <<document->ErrorDesc()<<std::endl;
		system("pause");
		return -1;
	}
	TiXmlElement* xmlRoot = document->RootElement();
	if(!xmlRoot || document->Error())
	{
		std::cerr << "Error finding XML-Root in file: "<<filename<<std::endl <<document->ErrorDesc()<<std::endl;
		system("pause");
		return -1;
	}

	TiXmlElement* bitmapsElement = xmlRoot->FirstChildElement("bitmaps");

	for(TiXmlElement* elementType = bitmapsElement->FirstChildElement(); elementType; elementType = elementType->NextSiblingElement())
	{
		String const & elementTypeName = elementType->Attribute("id");

		if(elementTypeName == "1")
		{
			std::cerr << "Error: image size to small for given font and fontsize!" << std::endl;
			system("pause");
			return -1;
		}
	}

	std::cout << "Font Index: ";
	int fontindex;
	std::cin >> fontindex;

	std::ofstream outputOguiFile;
	outputOguiFile.open((filename + "_convert").c_str());

	Ogre::ConfigFile::SectionIterator it = oguifile.getSectionIterator();
	while(it.hasMoreElements())
	{
		String const & section = it.peekNextKey();
		Ogre::ConfigFile::SettingsMultiMap* settings = it.peekNextValue();
		if(section.empty())
		{
			for(Ogre::ConfigFile::SettingsMultiMap::iterator jit = settings->begin(), jitend = settings->end(); jit != jitend; jit++)
			{
				outputOguiFile << jit->first << " "<< jit->second << std::endl;
			}
		}
		else
		{
			if(section != ("Font." + Orkige::StringUtil::Converter::toString(fontindex)))
			{
				outputOguiFile << std::endl << "[" << section << "]" << std::endl;
				for(Ogre::ConfigFile::SettingsMultiMap::iterator jit = settings->begin(), jitend = settings->end(); jit != jitend; jit++)
				{
					outputOguiFile << jit->first << " "<< jit->second << std::endl;
				}
			}
		}
		it.moveNext();
	}

	outputOguiFile << std::endl;
	outputOguiFile << "[Font."<< fontindex << "]" << std::endl;

	std::cout << "offsetx:";
	int offsetx;
	std::cin >> offsetx;
	outputOguiFile << "offset " << offsetx;
	std::cout << "offsety:";
	int offsety;
	std::cin >> offsety;
	outputOguiFile << " " << offsety  << std::endl;

	document =  onew(new TiXmlDocument((filename + ".temp.xml").c_str()));
	document->LoadFile(document->Value(), TIXML_ENCODING_UTF8);
	xmlRoot = document->RootElement();
	for(TiXmlElement* elementType = xmlRoot->FirstChildElement(); elementType; elementType = elementType->NextSiblingElement())
	{
		String const & elementTypeName = elementType->Value();

		if(elementTypeName == "glyphs")
		{
			outputOguiFile << "lineheight " << xmlRoot->Attribute("height") << std::endl;
			outputOguiFile << "baseline " << xmlRoot->Attribute("base") << std::endl;

			std::vector<int> glyphs;
			
			for(TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
			{
				int glyph = int(element->Attribute("ch")[0]);
				
				String origin = element->Attribute("origin");
				String size = element->Attribute("size");

				if(glyph == 32)
				{
					int spacelength = Orkige::StringUtil::Converter::fromString<int>(size.substr(0,size.find("x"))) + Orkige::StringUtil::Converter::fromString<int>(element->Attribute("aw"));
					outputOguiFile << "spacelength " << spacelength << std::endl;
					outputOguiFile << "monowidth " << spacelength*2.5 << std::endl;
				}

				glyphs.push_back(glyph);
				outputOguiFile << "glyph_" <<  glyph
					<< " " << origin.substr(0,origin.find(","))
					<< " " << origin.substr(origin.find(",")+1,origin.length())
					<< " " << size.substr(0,size.find("x"))
					<< " " << size.substr(size.find("x")+1,size.length())
					<< " " << element->Attribute("aw")
					<< std::endl;
			}
			outputOguiFile << "range " << glyphs[0] << " " << glyphs[glyphs.size()-1] << std::endl;
		}
		if(elementTypeName == "kernpairs")
		{
			for(TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
			{
				int left = int(element->Attribute("left")[0]);
				int right = int(element->Attribute("right")[0]);

				outputOguiFile << "kerning_" << left 
					<< " " << right
					<< " " << element->Attribute("adjust") << std::endl;
			}
		}
	}

	cimg_library::CImg<unsigned char> textureAtlasImage;
	textureAtlasImage.load_png(oguifile.getSetting("file", "Texture").c_str());

	cimg_library::CImg<unsigned char> bitmapFontImage;
	bitmapFontImage.load_png(oguifile.getSetting("file", "Texture").c_str());

	std::cout << "done!" << std::endl;

	outputOguiFile.close();
	system("pause");

	return 0;
}