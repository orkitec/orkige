//#define cimg_OS 0
#define cimg_use_png
#include <engine_util/CImg.h>      // Open source image library (http://cimg.sourceforge.net/)

#include <engine_module/EnginePrerequisites.h>
#include <core_tinyxml/tinyxml.h>
#include <engine_util/StringUtil.h>
#include <conio.h>
#include <Ogre.h>

#include "FileUtils.h"


//---------------------------------------------------------
int GetIndexFromFilename(const char* szFilename)
{
	Ogre::String sFilenameXML(szFilename);
		
	// get fontindex from filename, e.g. "Font_3.xml" or "Font_2"
	int pos1 = sFilenameXML.find_last_of("_");
	int pos2 = sFilenameXML.find_last_of(".");
	if (pos2 == -1)
	{
		pos2 = sFilenameXML.size();
	}
	oAssertDesc(pos1 > 0 && pos1 > 0, "GetIndexFromFilename: unexpected filename format");
	oAssertDesc(pos1 < pos2, "GetIndexFromFilename: unexpected filename format");
	std::string sFontIndex = sFilenameXML.substr(pos1+1, pos2-pos1-1);
	int fontIndex = Orkige::StringUtil::Converter::parseInt(sFontIndex, -1);
	oAssertDesc(fontIndex >= 0, "GetIndexFromFilename: can't get font index");

	return fontIndex;
}
//---------------------------------------------------------
int ShowImage(const char* szFilenamePNG)
{
	// TODO this might start photoshop, which has a long loading time, alternatively use CImg lib
	// TODO for a non-interactive mode just return here

	std::stringstream command;
	command << "start " << szFilenamePNG;
	
	std::cout << "cmd: " << command.str().c_str() << std::endl;
	int returnValue = system(command.str().c_str());
	std::cout << "cmd returned: " << returnValue << std::endl;

	if (returnValue != 0)
	{
		std::cerr << "Error showing image with associated program or no program associated with png files" << std::endl;
		return -1;
	}

	return 0;
}
//---------------------------------------------------------
void AutocropImage(cimg_library::CImg<unsigned char>& image)
{
	// autocrop works from CImg version 1.30, we use 1.29
	//const unsigned char color = 0; 
	//bitmapFontImage.autocrop(color, "xy");


	//cimg_library::CImg<unsigned char> color = image.get_shared_channels(0, 2);
	cimg_library::CImg<unsigned char> alpha = image.get_shared_channel(3);
	
	int cropR = 0;
	int cropB = 0;

	int cropL = image.dimx();
	int cropU = image.dimy();

	cimg_forY(alpha, y)
	{
		cimg_forX(alpha, x)
		{
			unsigned char* p_alpha = alpha.ptr(0, y);
			
			if (p_alpha[x] != 0)
			{
				// this is not optimized but we are not time critical here
				if (x > cropR) 
					cropR = x;
				if (y > cropB) 
					cropB = y;
				if (x < cropL) 
					cropL = x;
				if (y < cropU) 
					cropU = y;
			}
		}
	}

	std::cout << "Image cropped " << cropR << " x " << cropB << std::endl;
	oAssertDesc(cropL >= 0 && cropU >= 0, "AutocropImage: crop values L/B invalid, check image content");
	oAssertDesc(cropR < image.dimx() && cropB < image.dimy(), "AutocropImage: crop values R/U invalid, check image content");

	if (cropL == 0 || cropU == 0 || cropR == image.dimx() || cropB == image.dimy())
	{
		MessageBox(NULL, "Warning: generated font image has no padding pixels on at least one border.", "Atlas Generator", MB_OK | MB_ICONASTERISK);
	}

	// do not crop at the left and right border to not mismatch with the uv-offset, add little space depending on font size
	cropR += cropL;
	cropB += cropU;
	oAssertDesc(cropR < image.dimx() && cropB < image.dimy(), "AutocropImage: crop values invalid, check image content");
	
	image.crop(0, 0, cropR, cropB, false);
}
//---------------------------------------------------------
char xtod(char c) 
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return c = 0;        // not Hex digit
}
//---------------------------------------------------------
int HextoDec(const char *hex)
{
	if (*hex == 0) 
		return 0;
	return HextoDec(hex-1)*16 + xtod(*hex) ; 
}
//---------------------------------------------------------
int xstrtoi(const char *hex)      // hex string to integer
{
	return HextoDec(hex+strlen(hex)-1);
}
//---------------------------------------------------------
struct HexCharStruct
{
	char c;
	HexCharStruct(char _c) : c(_c) { }
};
//---------------------------------------------------------
inline std::ostream& operator<<(std::ostream& o, const HexCharStruct& hs)
{
	return (o << std::setw(4) << std::setfill('0') << std::hex << (int)hs.c);
}
//---------------------------------------------------------
inline HexCharStruct hex(char _c)
{
	return HexCharStruct(_c);
}
//---------------------------------------------------------
using namespace Orkige;

enum ANTIALIAS_MODE
{
	ANTIALIAS_AUTOMATIC,
	ANTIALIAS_AA,
	ANTIALIAS_AA_GRID,
	ANTIALIAS_1BPP,
	ANTIALIAS_1BPP_GRID
};
int generateFont(const char* szExecutablePath, const char* szAtlasPath, ANTIALIAS_MODE antiAliasMode)
{
	std::cout << "Anti-Alias mode: ";
	switch (antiAliasMode)
	{
		case ANTIALIAS_AUTOMATIC	: std::cout << "automatic"; break;
		case ANTIALIAS_AA			: std::cout << "aa"; break;
		case ANTIALIAS_AA_GRID		: std::cout << "aagrid"; break;
		case ANTIALIAS_1BPP			: std::cout << "1bpp"; break;
		case ANTIALIAS_1BPP_GRID	: std::cout << "1bppgrid"; break;
		default						: std::cout << "unknown"; break;
	}
	std::cout << std::endl;

	// specify font index of new font
	std::cout << "Font Index (0..99): ";
	int fontindex;
	std::cin >> fontindex;

	// size of bitmap font
	std::cout << "Bitmap Font Image Size (64-1024): ";
	int image_size;
	std::cin >> image_size;

	// TODO get the tool accept different values for width and height to save space on the texture atlas


	// font range
	std::string letterFileName;
	char start_letter = ' ';
	char end_letter = '~';
	if (MessageBox( NULL, "Do you want to specify a custom font range?", "Font Generator", MB_YESNO | MB_ICONQUESTION) == IDYES)
	{
		std::cout << "Start Letter (default: ' '): ";
		start_letter = getch();
		if (start_letter == 13)		// enter_key
		{
			start_letter = ' ';
		}
		std::cout << std::endl << "End Letter (default: '~'): ";
		end_letter = getch();
		if (end_letter == 13)		// enter key
		{
			end_letter = '~';
		}
		std::cout << std::endl << "Range specified" << std::endl;
	}
	else if(MessageBox( NULL, "Do you want to specify a file wich holds all wanted letters", "Font Generator", MB_YESNO | MB_ICONQUESTION) == IDYES)
	{
		letterFileName = CC::FileUtils::DialogBrowseFile("Select Letter Input File", "txt", "Letter text files (*.txt)\0*.txt\0");
		if (letterFileName.empty())
		{
			std::cerr << "Error Loading file!" << std::endl;
			system("pause");
			return -1;
		}
		std::cout << "Letter input file specified" << std::endl;
	}

	
	// basename xml and png file
	std::string sTempPath = CC::FileUtils::GetTempPath();
	std::string sFilenameBase = std::string(szAtlasPath) + "\\Font_" + Orkige::StringUtil::Converter::toString(fontindex);
	std::string sFilenameBaseTemp = sTempPath + "Font_" + Orkige::StringUtil::Converter::toString(fontindex) + "_temp";
	std::string sFilenameXMLTemp = sFilenameBaseTemp + ".xml";
	std::string sFilenamePNGTemp = sFilenameBaseTemp + ".png";

	CC::FileUtils::SetCurrentPath(szExecutablePath);

	// execute bmfontgen and create bitmap font image and description
	{
		std::stringstream command;
		command << "" << szExecutablePath << "\\bmfontgen.exe -v -fontdialog -bmsize " << image_size;
		if (letterFileName.empty())
			command << " -range " << hex(start_letter) << "-" << hex(end_letter);
		else
			command << " -source \"" << letterFileName << "\"";
		command << " -output " << sFilenameBaseTemp;
		
		// see http://blogs.msdn.com/b/garykac/archive/2006/08/30/732007.aspx
		switch (antiAliasMode)
		{
			case ANTIALIAS_AA			: command << " -trh aa"; break;
			case ANTIALIAS_AA_GRID		: command << " -trh aa-grid"; break;
			case ANTIALIAS_1BPP			: command << " -trh 1bpp"; break;
			case ANTIALIAS_1BPP_GRID	: command << " -trh 1bpp-grid"; break;
		}

		std::cout << "cmd: " << command.str() << std::endl;
		
		int returnValue = system(command.str().c_str());
		std::cout << "cmd returned: " << returnValue << std::endl;

		if (returnValue != 0)
		{
			std::cerr << "Error generating font!" << std::endl;
			system("pause");		
			return -1;
		}
	}

	CC::FileUtils::SetCurrentPath(szAtlasPath);


	// open font description file
	optr<TiXmlDocument>	document =  onew(new TiXmlDocument(sFilenameXMLTemp.c_str()));
	if(!document || document->Error())
	{
		std::cerr << "Error Loading file: " << sFilenameXMLTemp << std::endl << document->ErrorDesc() << std::endl;
		system("pause");
		return -1;
	}
	document->LoadFile(document->Value(), TIXML_ENCODING_UTF8);
	if(!document || document->Error())
	{
		std::cerr << "Error Loading file: " << sFilenameXMLTemp << std::endl << document->ErrorDesc() << std::endl;
		system("pause");
		return -1;
	}
	TiXmlElement* xmlRoot = document->RootElement();
	if(!xmlRoot || document->Error())
	{
		std::cerr << "Error finding XML-Root in file: " << sFilenameXMLTemp << std::endl << document->ErrorDesc() << std::endl;
		system("pause");
		return -1;
	}

	// check if font did fit into the specified image size 
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

		// e.g. "test-0.png"
		String const & textureFilename = elementType->Attribute("name");
		sFilenamePNGTemp = textureFilename;
	}

	// let the tool finish write ops while showing this message
	MessageBox(NULL, "Font was created. Image is shown after this message.", "Font Generator", MB_OK | MB_ICONASTERISK);
	
	// show results with system associated program
	ShowImage(sFilenamePNGTemp.c_str());


	std::cout << "Temporary files:" << std::endl << sFilenameXMLTemp << std::endl << sFilenamePNGTemp << std::endl;
	
	std::string sFilenameXML = sFilenameBase + ".xml";
	std::string sFilenamePNG = sFilenameBase + ".png";
		
	// accept results and cleanup
	if(MessageBox(NULL, "Do you accept the result and want to add it to the texture atlas?", "Font Generator", MB_YESNO | MB_ICONQUESTION) == IDYES)
	{
		CopyFile(sFilenameXMLTemp.c_str(), sFilenameXML.c_str(), FALSE);
		CopyFile(sFilenamePNGTemp.c_str(), sFilenamePNG.c_str(), FALSE);
		remove(sFilenameXMLTemp.c_str());
		remove(sFilenamePNGTemp.c_str());
	
		remove("memleaks.log");
		remove("memory.log");

		// autocrop images
		cimg_library::CImg<unsigned char> bitmapFontImage;
		bitmapFontImage.load_png(sFilenamePNG.c_str());
		AutocropImage(bitmapFontImage);
		bitmapFontImage.save_png(sFilenamePNG.c_str());
		
		std::cout << "Final Files:" << std::endl << sFilenameXML << std::endl << sFilenamePNG << std::endl << "Font successfully created." << std::endl;
	}

	std::cout << std::endl;
	system("pause");
	
	return 0;
}
//---------------------------------------------------------
typedef std::map<int, std::pair<int,int> > FontOffsets;

bool ConvertTXTtoOGUI(const char* szFilenameTXT, FontOffsets& fontOffsets, std::ofstream& fileOgui)
{
	// open txt file readonly	
	std::ifstream infile;
	infile.open(szFilenameTXT);
	if (infile)
	{
		fileOgui << std::endl;
		fileOgui << "[Sprites]" << std::endl;

		// read content 
		std::string strTXT;
		std::string strOgui;
		while (!infile.eof())
		{
			getline(infile, strTXT);
			if (!strTXT.empty())
			{	
				// parse line
				std::string sFont;
				Ogre::StringVector tokens = Ogre::StringUtil::split(strTXT, " ");
				oAssertDesc(tokens.size() == 6, "ConvertTXTtoOGUI: parse error");
				sFont = tokens[0];
				int x = Ogre::StringConverter::parseInt(tokens[2]);
				int y = Ogre::StringConverter::parseInt(tokens[3]); 
				oAssertDesc(!sFont.empty() && x >= 0 && y >= 0, "ConvertTXTtoOGUI: invalid values");

				// backup font offset
				if (Ogre::StringUtil::startsWith(sFont.c_str(), "Font_", false))
				{
					int fontIndex = GetIndexFromFilename(sFont.c_str());
					//fontOffsets.insert(fontIndex, std::pair<int, int>(x, y) );
					fontOffsets[fontIndex] = std::pair<int, int>(x, y);
				}

				// conversion
				strOgui = Ogre::StringUtil::replaceAll(strTXT, " =", "");				
				fileOgui << strOgui << std::endl;
			}
		}
		
		infile.close();
	}
	else
	{
		std::cerr << "ERROR: can't open file " << szFilenameTXT << std::endl;
		system("pause");		
		return false;
	}

	remove(szFilenameTXT);

	return true;
}
//---------------------------------------------------------
bool ConvertXMLtoOGUI(const char* szFilenameXML, FontOffsets& fontOffsets, std::ofstream& fileOgui)
{
	int fontIndex = GetIndexFromFilename(szFilenameXML);
	std::cout << "Processing font: " << fontIndex << std::endl;

	// write font section to config
	fileOgui << std::endl;
	fileOgui << "[Font."<< fontIndex << "]" << std::endl;

	fileOgui << "offset " << fontOffsets[fontIndex].first << " " << fontOffsets[fontIndex].second << std::endl;

	// parse xml font desc file
	optr<TiXmlDocument>	document = onew(new TiXmlDocument(szFilenameXML));
	document->LoadFile(document->Value(), TIXML_ENCODING_UTF8);
	TiXmlElement* xmlRoot = document->RootElement();

	std::map<char, int> glyphHexValues;
	
	Orkige::String sourceFontName = xmlRoot->Attribute("face");
	Orkige::String sourceFontSize = xmlRoot->Attribute("size");
	std::cout << "SourceFont " << sourceFontName << " "<< sourceFontSize << std::endl;

	for (TiXmlElement* elementType = xmlRoot->FirstChildElement(); elementType; elementType = elementType->NextSiblingElement())
	{
		String const & elementTypeName = elementType->Value();
		
		if (elementTypeName == "glyphs")
		{
			fileOgui << "lineheight " << xmlRoot->Attribute("height") << std::endl;
			fileOgui << "baseline " << xmlRoot->Attribute("base") << std::endl;

			std::vector<int> glyphs;
			
			for (TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
			{
				int glyph = xstrtoi(element->Attribute("code"));
				
				glyphHexValues[element->Attribute("ch")[0]] = glyph;
				String origin = element->Attribute("origin");
				String size = element->Attribute("size");
				std::string sizeX = size.substr(0, size.find("x"));
				std::string sizeY = size.substr(size.find("x")+1, size.length());
				std::string originX = origin.substr(0, origin.find(","));
				std::string originY = origin.substr(origin.find(",")+1, origin.length());
					
				if (glyph == 32)
				{
					int spacelength = Orkige::StringUtil::Converter::fromString<int>(sizeX) + Orkige::StringUtil::Converter::fromString<int>(element->Attribute("aw"));
					fileOgui << "spacelength " << spacelength << std::endl;
					fileOgui << "monowidth " << spacelength*2.5 << std::endl;
				}

				glyphs.push_back(glyph);
				fileOgui << "glyph_" << glyph
					<< " " << originX
					<< " " << originY
					<< " " << sizeX
					<< " " << sizeY
					<< " " << element->Attribute("aw")
					<< std::endl;
			}

			oAssertDesc(glyphs.size() > 0, "ConvertXMLtoOGUI: no glyphs found");
			fileOgui << "range " << glyphs[0] << " " << glyphs[glyphs.size()-1] << std::endl;
		}
		if (elementTypeName == "kernpairs")
		{
			for (TiXmlElement* element = elementType->FirstChildElement(); element; element = element->NextSiblingElement())
			{
				int left = glyphHexValues[element->Attribute("left")[0]];
				int right = glyphHexValues[element->Attribute("right")[0]];

				fileOgui << "kerning_" << left 
					<< " " << right
					<< " " << element->Attribute("adjust") 
					<< std::endl;
			}

			//fileOgui << "kerning " << ? << std::endl;
		}
	}

	return true;
}
//---------------------------------------------------------
int generateTextureAtlas(const char* szExecutablePath, const char* szAtlasPath, bool useBorder)
{
	// place to store ogui and texture atlas
	std::string sAtlasPath(szAtlasPath);
	int pos = sAtlasPath.find_last_of("\\");
	oAssertDesc(pos != String::npos, "Unexpected path format");
	std::string sAtlasName = sAtlasPath.substr(pos+1, sAtlasPath.length());
	std::string sOutputPath = sAtlasPath.substr(0, pos);

	// hide platform dependent string after "__"
	std::string sAtlasNameShort(sAtlasName);
	pos = sAtlasNameShort.find("__");
	if (pos != String::npos)
	{
		sAtlasNameShort = sAtlasNameShort.substr(0, pos);
	}
	pos = sAtlasNameShort.find_last_of("\\");
	oAssertDesc(pos == String::npos, "Atlas name contains path");

	std::cout << "Atlas Name: " << sAtlasName << std::endl;
	std::cout << "Atlas Name Short: " << sAtlasNameShort << std::endl;
	std::cout << "Output path: " << std::endl << sOutputPath << std::endl;
	
	//std::string sFilenameOGUI = DialogBrowseFile("Select Ogre Texture Atlas File", "ogui", "orkige gui Files (*.ogui)\0*.ogui\0");
	//std::string sFilenamePNG = DialogBrowseFile("Select Texture File", "texture", "textures (*.png)\0*.png\0");
	std::string outputImage = sAtlasNameShort + ".png";
	std::string outputMap   = sAtlasNameShort + ".txt";
	std::string sFilenameOGUI = sOutputPath + "\\" + sAtlasNameShort + ".ogui"; 
	std::string sFilenamePNG = sOutputPath + "\\" + outputImage;  
	std::string sFilenameTXT = sOutputPath + "\\" + outputMap;
	
	std::cout << "Output ogui: " << std::endl << sFilenameOGUI << std::endl;
	std::cout << "Output png: " << std::endl << sFilenamePNG << std::endl;

	// clean from previous run
	remove(sFilenameOGUI.c_str());
	remove(sFilenamePNG.c_str());
	
	
	// execute sprite sheet packer and create bitmap font image and description
	std::stringstream command;
	command << "" << szExecutablePath << "\\sspack.exe";
	command << " /image:" << outputImage << " /map:" << outputMap << " /sqr /pow2 ";
	//command << "/r ";
	if (useBorder)
	{
		command << "/pad:2 ";
	}
	else
	{
		command << "/pad:0 ";
	}
	command << sAtlasName << "\\*.png";
	
	// attention: the tool doesn't accept absolute path and then complains with a "No Images to pack." message
	// so far the only solution is to temporarily copy the executable to the output folder...
	//SetCurrentPath(szExecutablePath);
	CC::FileUtils::SetCurrentPath(sOutputPath.c_str());
	std::string sFilenameExe(szExecutablePath); sFilenameExe += "\\sspack.exe";
	std::string sFilenameExeTemp = sOutputPath + "\\sspack.exe";
	CopyFile(sFilenameExe.c_str(), sFilenameExeTemp.c_str(), FALSE);

	// execute tool
	std::cout << "cmd: " << command.str() << std::endl << "Please wait..." << std::endl;
	int returnValue = system(command.str().c_str());
	std::cout << "cmd returned: " << returnValue << std::endl;

	// delete the temporary executable
	remove(sFilenameExeTemp.c_str());

	if (returnValue != 0)
	{
		std::cerr << "Error layouting texture atlas!" << std::endl;
		system("pause");
		return -1;
	}
	else
	{		
		// get image dimensions and write white pixel
		int width = 0;
		int height = 0;
		{
			cimg_library::CImg<unsigned char> bitmapFontImage;
			bitmapFontImage.load_png(sFilenamePNG.c_str());
			
			width = bitmapFontImage.dimx();
			height = bitmapFontImage.dimy();

			std::cout << "Atlas texture size: " << width << " x " << height << std::endl;

			// TODO show a warning when image size increased

			// TODO do we need a white pixel?
			
			// write whitepixel
			const unsigned char white[4] = { 255, 255, 255, 255 };
			bitmapFontImage.draw_rectangle(width-2, height-2, width, height, white);
			bitmapFontImage.save_png(sFilenamePNG.c_str());
		}

		// open ogui file
		std::ofstream fileOgui;
		fileOgui.open(sFilenameOGUI.c_str());

		// write header
		fileOgui << "[Texture]" << std::endl;
		fileOgui << "file " << sAtlasNameShort << ".png" << std::endl;
		fileOgui << "whitepixel " << width-2 << " " << height-2 << std::endl;


		// list all xml files in atlas folder
		std::vector<std::string> files;
		CC::FileUtils::GetFilesInDirectory(sAtlasPath.c_str(), "*.xml", files);

		// parse map file and append it to ogui file, store font offset from texture atlas
		FontOffsets fontOffsets;
		bool ok = ConvertTXTtoOGUI(outputMap.c_str(), fontOffsets, fileOgui);

		// loop all font xml files and append to ogui file, apply font offset
		std::vector<std::string>::iterator it = files.begin();
		for ( ; it != files.end(); it++)
		{
			std::stringstream sFilenameXML;
			sFilenameXML << szAtlasPath << "\\" << (*it).c_str();

			ok &= ConvertXMLtoOGUI(sFilenameXML.str().c_str(), fontOffsets, fileOgui);
		}

		fileOgui.close();

		
		if (ok) 
		{
			// success
			MessageBox(NULL, "Atlas texture and Ogre Atlas description file successfully created. \nImage is shown after this message.", "Atlas Generator", MB_OK | MB_ICONASTERISK);
		}
		else
		{
			// something went wrong
			if (MessageBox(NULL, "ERROR: There was an error creating the Atlas texture. \nDo you want to delete the intermediate files?", "Atlas Generator", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				remove(sFilenameOGUI.c_str());
				remove(sFilenamePNG.c_str());
			}
		}

		// let the tool finish flushing data before reading these, so show after message box
		ShowImage(sFilenamePNG.c_str());
	}

	return 0;
}
//---------------------------------------------------------
int main(int argc, char **argv)
{
	std::cout << "Orkige font and texture atlas converter" << std::endl << std::endl;

	OLOAD_MODULE_STATIC(orkige_core);

	std::string sExecutablePath;
	std::string sAtlasPath;	
	
	bool createFont = false;
	bool useBorder = true;
	ANTIALIAS_MODE antiAliasMode = ANTIALIAS_AUTOMATIC;

	// parse command line parameters
	//bool createFont = (cimg_library::cimg::option("-createFont", argc, argv, false) != 0);
	if (argc == 0)
	{
		std::cout << "  usage: [-createFont] [-noBorder] [-aa|-aagrid|-1bpp|-1bppgrid] <path>" << std::endl;
		std::cout << "  <path>		is the path to the textures, within path all characters after two underscores (__) are ignored" << std::endl;
		std::cout << "  -aa			antialias on" << std::endl;
		std::cout << "  -aagrid		antialias on with grid fit" << std::endl;
		std::cout << "  -1bpp		antialias on with 1 bit per pixel" << std::endl;
		std::cout << "  -1bppgrid	antialias on with 1 bit per pixel with grid fit" << std::endl;
	}
	for (int i = 1; i < argc; ++i)
	{
		std::string s(argv[i]);
		if (s == "-createFont")
		{
			createFont = true;
		}
		else if (s == "-noBorder")
		{
			useBorder = false;
		}
		else if (s == "-aa")
		{
			antiAliasMode = ANTIALIAS_AA;
		}
		else if (s == "-aagrid")
		{
			antiAliasMode = ANTIALIAS_AA_GRID;
		}
		else if (s == "-1bpp")
		{
			antiAliasMode = ANTIALIAS_1BPP;
		}
		else if (s == "-1bppgrid")
		{
			antiAliasMode = ANTIALIAS_1BPP_GRID;
		}
		else
		{
			sAtlasPath = s;
		}
	}


	// backup startup path to get location for external tools
	//sExecutablePath = GetCurrentPath();
	sExecutablePath = argv[0];	

	std::cout << "path to executable:" << std::endl << sExecutablePath << std::endl;
	
	if (sExecutablePath.empty())
	{
		std::cerr << "ERROR: can't detect path to executable" << std::endl;
		system("pause");
		return -1;
	}
	int p = sExecutablePath.find_last_of("\\");
	oAssertDesc(p > 0, "ERROR: unexpected executable path");
	sExecutablePath = sExecutablePath.substr(0, p);


	if (sAtlasPath.empty())
	{	
		sAtlasPath = CC::FileUtils::DialogBrowseFolder("Browse for texture atlas folder");
	}

	std::cout << "path to texture altas:" << std::endl << sAtlasPath << std::endl;
		
	if (!sAtlasPath.empty())
	{
		// check for existence
		if (SetCurrentDirectory(sAtlasPath.c_str()) == 0)
		{
			std::cerr << "ERROR: texture atlas path doesn't exist" << std::endl;
			system("pause");
			return -1;
		}

		if (createFont)
		{
			generateFont(sExecutablePath.c_str(), sAtlasPath.c_str(), antiAliasMode);
		}	
		generateTextureAtlas(sExecutablePath.c_str(), sAtlasPath.c_str(), useBorder);
	}

	system("pause");

	return 0;
}
