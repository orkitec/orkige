/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	FileUtils.h
	author:		MorrK
	
	purpose:	
***************************************************************/
#ifndef __FileUtils_h__19_7_2010__23_44_36__
#define __FileUtils_h__19_7_2010__23_44_36__


#include <core_base/Object.h>



namespace CC
{
	class FileUtils // : Object
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		FileUtils();
		virtual ~FileUtils();

		static std::string GetResourceDirectory();
		static std::string GetCurrentPath();
		static void SetCurrentPath(const char* szPath);
		static std::string FileUtils::DialogBrowseFolder(const char* szTitle);
		//static Ogre::String DialogBrowseFile(Ogre::String const & sTitle, Ogre::String const & sFileType, Ogre::String const & sFileTypeDesc);
		static std::string DialogBrowseFile(const char* szTitle, const char* szFileType, const char* szFileTypeDesc);
		static void GetFilesInDirectory(const char* szPath, const char* szPattern, std::vector<std::string>& files);
		static std::string GetTempPath();

	protected:
	private:

	};
	//---------------------------------------------------------
}

#endif //__FileUtils_h__19_7_2010__23_44_36__