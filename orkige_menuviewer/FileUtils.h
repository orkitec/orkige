/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	FileUtils.h
	author:		MorrK
	
	purpose:	
***************************************************************/
#ifndef __FileUtils_h__19_7_2010__23_44_36__
#define __FileUtils_h__19_7_2010__23_44_36__


// TODO 
//#include <core_game/GameState.h>
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
		//static Ogre::String DialogBrowseFile(Ogre::String const & sTitle, Ogre::String const & sFileType, Ogre::String const & sFileTypeDesc);
		static std::string DialogBrowseFile(const char* szTitle, const char* szFileType, const char* szFileTypeDesc);
	protected:
	private:

	};
	//---------------------------------------------------------
}

#endif //__FileUtils_h__19_7_2010__23_44_36__