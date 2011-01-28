//#define cimg_OS 0
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
#include <windows.h>

struct HexCharStruct
{
	char c;
	HexCharStruct(char _c) : c(_c) { }
};

inline std::ostream& operator<<(std::ostream& o, const HexCharStruct& hs)
{
	return (o << std::setw(4) << std::setfill('0') << std::hex << (int)hs.c);
}

inline HexCharStruct hex(char _c)
{
	return HexCharStruct(_c);
}

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

	LPCTSTR Caption = "Overwrite existing ogui files?";
	String Text = "if you press yes the selected ogui file will be overwritten if you press no the following file(s) will be created:\n" + filename + ".new.ogui\n *.new.png";

	bool overwriteFiles = false;
	int messageboxReturn = MessageBox( NULL, Text.c_str(), Caption,	MB_YESNO | MB_ICONQUESTION);
	if(messageboxReturn == IDYES)
	{
		overwriteFiles = true;
	}

	std::cout << "Filename: " << filename << std::endl;

	std::cout << "Bitmap Font Image Size (64-1024): ";
	int image_size;
	std::cin >> image_size;

	char start_letter=' ';
	char end_letter='~';
	if(MessageBox( NULL, "Do you want to specify a custom font range?", "Font Range",	MB_YESNO | MB_ICONQUESTION) == IDYES)
	{
		std::cout << "Start Letter (default: ' '): ";
		start_letter = getch();
		if(start_letter == 13)
		{
			start_letter=' ';
		}
		std::cout << std::endl << "End Letter (default: '~'): ";
		end_letter = getch();
		if(end_letter == 13)
		{
			end_letter='~';
		}
		std::cout << std::endl;
	}

	std::stringstream bmfontGenCommand;
	bmfontGenCommand << "" << path << "\\bmfontgen.exe -fontdialog -bmsize " << image_size << " -range "<<hex(start_letter)<<"-"<<hex(end_letter)<<" -output " << filename << ".temp";
	std::cout << bmfontGenCommand.str() << std::endl;
	int returnValue = system(bmfontGenCommand.str().c_str());
	std::cout << "bmfontgen.exe returned: " << returnValue << std::endl;

	if(returnValue != 0)
	{
		system("pause");
		return -1;
	}
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
	if(overwriteFiles)
	{
		outputOguiFile.open(filename .c_str());
	}
	else
	{
		outputOguiFile.open((filename + ".new.ogui").c_str());
	}
	

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
					if(overwriteFiles)
					{
						outputOguiFile << jit->first << " "<< jit->second << std::endl;
					}
					else
					{
						if(section == "Texture" && jit->first == "file")
						{
							outputOguiFile << jit->first << " "<< jit->second << ".new.png" << std::endl;
						}
						else
						{
							outputOguiFile << jit->first << " "<< jit->second << std::endl;
						}
					}
				}
			}
		}
		it.moveNext();
	}

	outputOguiFile << std::endl;
	outputOguiFile << "[Font."<< fontindex << "]" << std::endl;

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

	String textureAtlasImageFilename = oguifile.getSetting("file", "Texture");
	cimg_library::CImg<unsigned char> textureAtlasImage;
	textureAtlasImage.load_png(textureAtlasImageFilename.c_str());

	cimg_library::CImg<unsigned char> textureAtlasImageOrig;
	textureAtlasImageOrig.load_png(textureAtlasImageFilename.c_str());

	String bitmapFontImageFilename = filename + ".temp-0.png";
	cimg_library::CImg<unsigned char> bitmapFontImage;
	bitmapFontImage.load_png(bitmapFontImageFilename.c_str());

	int offsetx = 0;
	int offsety = 0;

	float opacity = 1.f;
	cimg_library::CImgDisplay main_disp(textureAtlasImage,"Click where to draw the font!"), draw_disp(bitmapFontImage,"Bitmap Font");
	while(!main_disp.is_closed)
	{
		main_disp.wait();

		if (main_disp.button && main_disp.mouse_y>=0) {
			offsetx = main_disp.mouse_x;
			offsety = main_disp.mouse_y;

			textureAtlasImage.draw_image(textureAtlasImageOrig,0,0);
			{
				// RGBA over RGBA (@see ftp://ftp.alvyray.com/Acrobat/4_Comp.pdf)
				cimg_library::CImg<unsigned char> dst_color = textureAtlasImage.get_shared_channels(0, 2);
				cimg_library::CImg<unsigned char> src_color = bitmapFontImage.get_shared_channels(0, 2);
				cimg_library::CImg<unsigned char> dst_alpha = textureAtlasImage.get_shared_channel(3);
				cimg_library::CImg<unsigned char> src_alpha = bitmapFontImage.get_shared_channel(3);

				cimg_forV(dst_color, v)
				{
					unsigned int src_y, src_x;
					cimg_for_inY(dst_color, offsety, offsety + src_color.height-1, dst_y)
					{
						src_y = dst_y - offsety;
						unsigned char* p_dst_color = dst_color.ptr(0, dst_y, 0, v);		
						unsigned char* p_src_color = src_color.ptr(0, src_y, 0, v);		
						unsigned char* p_dst_alpha = dst_alpha.ptr(0, dst_y);
						unsigned char* p_src_alpha = src_alpha.ptr(0, src_y);
						cimg_for_inX(dst_color, offsetx, offsetx + src_color.width-1, dst_x)
						{
							src_x = dst_x - offsetx;
							float c1 = p_src_color[src_x] / 255.f;
							float c2 = p_dst_color[dst_x] / 255.f;
							float a1 = p_src_alpha[src_x] / 255.f * opacity;
							float a2 = p_dst_alpha[dst_x] / 255.f;
							p_dst_color[dst_x] = (unsigned char) (255.0f * ((c1 * a1 + c2 * (1 - a1) * a2) / (a1 + a2 - a1 * a2)));	
							//p_dst_color[dst_x] = (unsigned char) (255.0f * ((c1 * a1 + c2 * (1 - a1))));	
						}
					}
				}
				{
					unsigned int src_y, src_x;
					cimg_for_inY(dst_alpha, offsety, offsety + src_alpha.height-1, dst_y)
					{
						src_y = dst_y - offsety;
						unsigned char* p_dst_alpha = dst_alpha.ptr(0, dst_y);
						unsigned char* p_src_alpha = src_alpha.ptr(0, src_y);
						cimg_for_inX(dst_alpha, offsetx, offsetx + src_alpha.width-1, dst_x)
						{
							src_x = dst_x - offsetx;
							float a1 = p_src_alpha[src_x] / 255.f * opacity;;
							float a2 = p_dst_alpha[dst_x] / 255.f;
							p_dst_alpha[dst_x] = (unsigned char) (255.0f * (a1 + a2 - a1 * a2));
							//p_dst_alpha[dst_x] = (unsigned char) (255.0f * std::min(1.0f, a1 + a2));
						}
					}
				}
			}
		}

		
		textureAtlasImage.display(main_disp);
	}
	if(!draw_disp.is_closed)
		draw_disp.close();

	outputOguiFile << "offset " << offsetx << " " << offsety  << std::endl;

	if(overwriteFiles)
	{
		textureAtlasImage.save_png((textureAtlasImageFilename).c_str());
	}
	else
	{
		textureAtlasImage.save_png((textureAtlasImageFilename + ".new.png").c_str());
	}

	outputOguiFile.close();
	if(MessageBox( NULL, "Should temporary files generated while building the font be deleted?", "Remove Temp Files?",	MB_YESNO | MB_ICONQUESTION) == IDYES)
	{
		remove((filename + ".temp.xml").c_str());
		remove(bitmapFontImageFilename.c_str());
		remove("memleaks.log");
		remove("memory.log");
	}

	std::stringstream quitMessage;
	quitMessage << "A font with id [Font." << fontindex << "]";
	quitMessage << " was succesfully created in " << filename;
	quitMessage << " and placed in " << textureAtlasImageFilename << (overwriteFiles ? "" : ".new.png") << "!";
	MessageBox( NULL, quitMessage.str().c_str(), "Font succesfully created!",	MB_OK | MB_ICONINFORMATION);

	return 0;
}