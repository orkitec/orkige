/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	FileUtils.cpp
	author:		MorrK
	
	purpose:	
***************************************************************/

#include "FileUtils.h"

#include <direct.h>		// for browser file dialog
#include <Shlobj.h>		// for browser folder dialog

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
	//---------------------------------------------------------
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
	//---------------------------------------------------------
	void FileUtils::SetCurrentPath(const char* szPath)
	{
		// set the current working directory
		if (_chdir(szPath) != 0)
		{
			oAssertDesc(false, "Warning: can't set current directory");
		}
	}
	//---------------------------------------------------------
	std::string FileUtils::DialogBrowseFolder(const char* szTitle)
	{
		// code from http://vctipsplusplus.wordpress.com/2008/07/15/using-shbrowseforfolder/

		//std::cout << szTitle << std::endl;

		BROWSEINFO bi; 
		ZeroMemory(&bi, sizeof(bi)); 
		TCHAR szDisplayName[MAX_PATH]; 
		szDisplayName[0] = '\0';  

		bi.hwndOwner = NULL; 
		bi.pidlRoot = NULL; 
		bi.pszDisplayName = szDisplayName; 
		bi.lpszTitle = szTitle; 
		bi.ulFlags = BIF_RETURNONLYFSDIRS;
		bi.lParam = NULL; 
		bi.iImage = 0;

		LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
		TCHAR szPathName[MAX_PATH]; 
		szPathName[0] = '\0';
		if (pidl != NULL)
		{
			BOOL bRet = SHGetPathFromIDList(pidl, szPathName);
			if (bRet == FALSE)
		 {
			 szPathName[0] = '\0';
		 }
		}

		if (strlen(szPathName) == 0)
		{
			//std::cout << "Browse Dialog user aborted" << std::endl;
		}

		return std::string(szPathName);
	}
	//---------------------------------------------------------

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
	//---------------------------------------------------------
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
		oAssertDesc(sPath.length() > 2, "Warning: resource directory not found");
		
		return sPath + "\\Export";
	}
	//---------------------------------------------------------
	void FileUtils::GetFilesInDirectory(const char* szPath, const char* szPattern, std::vector<std::string>& files)
	{
		// code from http://www.dreamincode.net/code/snippet546.htm
		// and http://www.c-plusplus.de/forum/39396

		WIN32_FIND_DATA findFileData; 
		HANDLE hFind = INVALID_HANDLE_VALUE; 
		
		std::stringstream sPathPattern;
		sPathPattern << szPath << "\\" << szPattern << "\0";
		
		char szPathPattern[MAX_PATH];
		strcpy(szPathPattern, sPathPattern.str().c_str());

		// the first two results used to be "." and "..", but not todays
		hFind = FindFirstFile(szPathPattern, &findFileData);

		if (hFind == INVALID_HANDLE_VALUE)
		{
			std::cout << "Warning: invalid path or not files found: \n" << szPathPattern << std::endl;
		}
		else
		{
			do
			{
				if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{ 
					// directory
				}
				else
				{
					//std::cout << "file: " << findFileData.cFileName << std::endl;
					files.push_back(findFileData.cFileName);
				}
			}
			while (FindNextFile(hFind, &findFileData) != 0);		

			// sort, we are nice
			std::sort(files.begin(), files.end());
		}
		FindClose(hFind);


		/*	
		// TODO use ogre
		use Ogre::FileSystemArchive::find()
		
		StringVectorPtr vec(OGRE_NEW_T(StringVector, MEMCATEGORY_GENERAL)(), SPFM_DELETE_T);

		Ogre::ArchiveManager::ArchiveMapIterator it = Ogre::ArchiveManager::getSingleton().getArchiveIterator();
		for (Ogre::ArchiveManager::ArchiveMapIterator i = it.begin(); i != it.end(); i++)
		{
			Ogre::Archive* archive = (*it);

			StringVectorPtr lst = archive->find(pattern, (*i)->recursive, dirs);
				vec->insert(vec->end(), lst->begin(), lst->end());        
		}

		// TODO use boost
		boost::path p("");
		cout << p << " is a directory containing:\n";

		typedef vector<path> vec;
		vec v;

		copy(directory_iterator(p), directory_iterator(), back_inserter(v));

		sort(v.begin(), v.end());

		for (vec::const_iterator it (v.begin()); it != v.end(); ++it)
		{
			cout << "   " << *it << '\n';
		}	

		vector<string> listFiles = boost::filesystem::getFiles("\directory", ...);
		*/
	}
	//---------------------------------------------------------
	std::string FileUtils::GetTempPath()
	{
		/*
		TCHAR buf[MAX_PATH];
		if (GetTempPath(MAX_PATH, buf))
		{
			return std::string(buf);
		}
		return std::string();
		*/

		// hack: because of the external tool crashes with long temp path names, we use a short fixed one
		const char* szTempPath = "C:\\Temp";
		if (SetCurrentDirectory(szTempPath) == 0)
		{
			if (!CreateDirectory(szTempPath, NULL))
			{
				std::cerr << "ERROR: can't create temp directory" << szTempPath << std::endl;
			}
		}
		return std::string(szTempPath) + "\\";
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}