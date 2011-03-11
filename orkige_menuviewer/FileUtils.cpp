/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	FileUtils.cpp
	author:		MorrK
	
	purpose:	
***************************************************************/

#include "FileUtils.h"

// for browser file dialog
#include <direct.h>
//#include <stdlib.h>
//#include <stdio.h>


namespace CC
{
	using namespace Orkige;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FileUtils::FileUtils() 
	{
	}
	//---------------------------------------------------------
	FileUtils::~FileUtils()
	{
	}

	std::string FileUtils::GetCurrentPath()
	{
		std::string path;

		// get the current working directory
		char szPath[MAX_PATH];
		if (_getcwd(szPath, sizeof(szPath)) == NULL)
		{
			oAssertDesc(false, "Warning: can't get current directory");
		}
		else
		{
			path = szPath;
		}

		return path;
	}

	void FileUtils::SetCurrentPath(const char* szPath)
	{
		// set the current working directory
		if (_chdir(szPath) != 0)
		{
			oAssertDesc(false, "Warning: can't set current directory");
		}
	}

	//Ogre::String FileUtils::DialogBrowseFile(Ogre::String const & sTitle, Ogre::String const & sFileType, Ogre::String const & sFileTypeDesc)
	std::string FileUtils::DialogBrowseFile(const char* szTitle, const char* szFileType, const char* szFileTypeDesc)
	{
		// backup
		std::string sCurrentPath = FileUtils::GetCurrentPath();

		char szFilename[MAX_PATH] = "";

		OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = 0;
		ofn.lpstrFilter = szFileTypeDesc; // e.g. "orkige gui Files (*.ogui)\0*.ogui\0";
		ofn.lpstrFile = szFilename;
		ofn.nMaxFile = sizeof(szFilename);
		ofn.Flags = OFN_EXPLORER; // | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = szFileType; // e.g. "ogui";
		ofn.lpstrInitialDir = NULL;

		std::string sFilename;
		if (GetOpenFileName(&ofn))
		{
			sFilename = szFilename;
		}

		// restore
		FileUtils::SetCurrentPath(sCurrentPath.c_str());

		return sFilename;
	}

	std::string FileUtils::GetResourceDirectory()
	{
		//Orkige::String sRootPath(Orkige::PlatformUtil::getResourceDirectory());

		// get path to executable
		char szPath[MAX_PATH];
		GetModuleFileName(NULL, szPath, sizeof(szPath));		
		Orkige::String sPath(szPath);
		
		Orkige::String sPathTest;
		while (sPath.length() > 2)	// "C:"
		{
			int pos = sPath.find_last_of("\\");
			sPath = sPath.substr(0, pos);

			sPathTest = sPath + "\\Export\\data";
			if (SetCurrentDirectory(sPathTest.c_str()) != 0)
			{
				break;
			}
		}
		
		return sPath + "\\Export";
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}