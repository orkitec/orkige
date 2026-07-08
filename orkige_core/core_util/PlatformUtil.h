/**************************************************************
        created:	2010/09/17 at 13:25
        filename: 	PlatformUtil.h
        author:		steffen.roemer
        notice:		This source file is part of orkige (orkitec Game engine)
                                For the latest info, see http://www.orkitec.com/
        copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __PlatformUtil_h__17_9_2010__13_25_23__
#define __PlatformUtil_h__17_9_2010__13_25_23__

#include "core_util/String.h"
#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
        //! platform specific utilities
        namespace PlatformUtil
        {
                //! retrieve base path of current running app
                ORKIGE_CORE_DLL String const & getBaseDirectory();
                //! retrieve Documents path
                ORKIGE_CORE_DLL String const & getDocumentsDirectory();
                //! retrieve Resource path
                ORKIGE_CORE_DLL String const & getResourceDirectory();
                //! retrieve path to application support/data directory
                ORKIGE_CORE_DLL String const & getSupportDirectory(String applicationName);

#ifdef __ANDROID__
                //! set path to android Apk
                void setApkPath(String const & path);
                //! get path to android Apk file
                String const & getApkPath();
                //! set path to android (cache) files on sdcard
                void setFilesPath(String const & path);
#endif //__ANDROID__
                enum ORKIGE_PLATFORM {
                        PLATFORM_MACOS,
                        PLATFORM_IPHONE,
                        PLATFORM_IPHONE4,
                        PLATFORM_IPAD,
                        PLATFORM_ANDROID,
                        PLATFORM_LINUX,
                        PLATFORM_WIN32,
                };
                ORKIGE_CORE_DLL const ORKIGE_PLATFORM getPlatform();

        };
        //---------------------------------------------------------
}

#endif //__PlatformUtil_h__17_9_2010__13_25_23__
