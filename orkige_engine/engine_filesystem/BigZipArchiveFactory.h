/********************************************************************
	created:	Tuesday 2012/02/21 at 20:16
	filename: 	BigZipArchiveFactory.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __BigZipArchiveFactory_h__21_2_2012__20_16_58__
#define __BigZipArchiveFactory_h__21_2_2012__20_16_58__

#include "engine_module/EnginePrerequisites.h"
#include <OgreArchiveFactory.h>
#include <OgreZip.h>

namespace Orkige
{
	class ORKIGE_ENGINE_DLL BigZipArchiveFactory : public Ogre::ArchiveFactory, public Singleton<BigZipArchiveFactory>
	{
		DECL_OSINGLETON(BigZipArchiveFactory);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		//! OGRE 14 made ZipArchive private - instances only come from ZipArchiveFactory
		Ogre::ZipArchiveFactory zipArchiveFactory;
		optr<Ogre::Archive> zipFile;
		Ogre::FileInfoListPtr fileInfo;
		Ogre::String fileName;
		Ogre::String pathPrefix;
	private:
		//--- Methods -----------------------------------------------
	public:
		BigZipArchiveFactory(Ogre::String const & fileName, Ogre::String const & pathPrefix);
		virtual ~BigZipArchiveFactory();
		/// @copydoc FactoryObj::getType
		const Ogre::String& getType(void) const override;
		/// @copydoc FactoryObj::createInstance
		Ogre::Archive *createInstance( const Ogre::String& name, bool readOnly ) override;
		/// @copydoc FactoryObj::destroyInstance
		void destroyInstance( Ogre::Archive* arch) override { OGRE_DELETE arch; }

		inline Ogre::String getPathPrefix();
		//! @copydoc Archive::open
		Ogre::DataStreamPtr open(const Ogre::String& filename, bool readOnly = true) const;
		//! @copydoc Archive::exists
		bool exists(const Ogre::String& filename);

		//! @copydoc Archive::findFileInfo
		Ogre::FileInfoListPtr getFileInfoList(const Ogre::String& path);
		/// get big zipfile
		Ogre::Archive const & getZipFile();
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline Ogre::String BigZipArchiveFactory::getPathPrefix()
	{
		return this->pathPrefix;
	}
}

#endif //__BigZipArchiveFactory_h__21_2_2012__20_16_58__
