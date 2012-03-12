/********************************************************************
        created:	Tuesday 2012/02/21 at 20:21
        filename: 	BigZipArchiveFactory.cpp
        author:		steffen.roemer
        notice:		This source file is part of orkige (orkitec Game engine)
                                For the latest info, see http://www.orkitec.com/
        copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_filesystem/BigZipArchiveFactory.h"
#include "engine_filesystem/BigZipArchive.h"

namespace Orkige
{
        IMPL_OSINGLETON(BigZipArchiveFactory);
        //---------------------------------------------------------
        //--- public: ---------------------------------------------
        //---------------------------------------------------------
        BigZipArchiveFactory::BigZipArchiveFactory(Ogre::String const & _fileName, Ogre::String const & _pathPrefix) : fileName(_fileName), pathPrefix(_pathPrefix)
        {
                this->zipFile = onew(new Ogre::ZipArchive(fileName, "Zip"));
                oDebugMsg("steffen", 0, "Loading BigZip: " << this->fileName);
                this->zipFile->load();
                this->fileInfo = this->zipFile->listFileInfo();
                for(Ogre::FileInfoList::iterator it = this->fileInfo->begin(), itend = this->fileInfo->end(); it != itend; it++)
                {
                        oDebugMsg("steffen", 0, "Found File: " << it->filename);
                        oDebugMsg("steffen", 0, " - Path: " << it->path);
                }
        }
        //---------------------------------------------------------
        BigZipArchiveFactory::~BigZipArchiveFactory()
        {
        }
        //-----------------------------------------------------------------------
        const Ogre::String& BigZipArchiveFactory::getType(void) const
        {
                static String name = "BigZip";
                return name;
        }
        //-----------------------------------------------------------------------
        Ogre::Archive * BigZipArchiveFactory::createInstance( const Ogre::String& name )
        {
                Ogre::String _name = name;
                if(name.substr(0, 2) == "./")
                {
                        _name = name.substr(2, name.length()-1);
                }
                if(name[name.length()-1] != '/' && name[name.length()-1] != '\\')
                {
                        _name += "/";
                }
                return OGRE_NEW BigZipArchive(_name, "BigZip");
        }
        //-----------------------------------------------------------------------
        Ogre::DataStreamPtr BigZipArchiveFactory::open(const Ogre::String& filename, bool readOnly) const
        {
                Ogre::DataStreamPtr dstr = this->zipFile->open(this->pathPrefix + filename, readOnly);
                return dstr;
        }
        //-----------------------------------------------------------------------
        bool BigZipArchiveFactory::exists(const Ogre::String& filename)
        {
                return this->zipFile->exists(this->pathPrefix + filename);
        }
        //-----------------------------------------------------------------------
        Ogre::FileInfoListPtr BigZipArchiveFactory::getFileInfoList(const Ogre::String& path)
        {
                String searchPattern = this->pathPrefix + path;
                if(searchPattern[searchPattern.length()-1] != '/' && searchPattern[searchPattern.length()-1] != '\\')
                {
                        searchPattern += "/";
                }
                return this->zipFile->listFileInfo(/*searchPattern + "*"*/true, false);
        }
        //-----------------------------------------------------------------------
        Ogre::ZipArchive const & BigZipArchiveFactory::getZipFile()
        {
                return (*this->zipFile);
        }
        //---------------------------------------------------------
        //--- protected: ------------------------------------------
        //---------------------------------------------------------

        //---------------------------------------------------------
        //--- private: --------------------------------------------
        //---------------------------------------------------------
}
