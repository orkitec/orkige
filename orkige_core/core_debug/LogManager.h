/********************************************************************
        created:	Tuesday 2010/08/10 at 16:04
        filename: 	LogManager.h
        author:		steffen.roemer
        notice:		This source file is part of orkige (orkitec Game engine)
                                For the latest info, see http://www.orkitec.com/
        copyright:	(c) 2009-2011 orkitec

        purpose:	this is the main debuglog libraray include file
*********************************************************************/
#ifndef __LogManager_h__10_8_2010__16_04_17__
#define __LogManager_h__10_8_2010__16_04_17__

#include "core_base/Meta.h"
#include "core_debug/DebugMacros.h"
#include "core_util/optr.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"
#include <fstream>
#include <vector>


namespace Orkige
{
        class LogConfig;
        class LogChannel;

        /** \addtogroup Debug
        *  @{ */
        //! manager for logging
        class ORKIGE_CORE_DLL LogManager : public Singleton<LogManager>
        {
        public:
                /** @brief get the TypeInfo of this class. */
                static Orkige::TypeInfo const & getClassTypeInfo();
                /** @brief get the TypeInfo of this class instance. */
                virtual Orkige::TypeInfo const & getTypeInfo();
                /** @brief get the TypeInfo of this class instance. */
                virtual Orkige::TypeInfo const & getTypeInfo() const;

                /** Export and init ClassName meta information */
                static void OrkigeMetaExport(const char * currentOrkigeModuleName);
                DECL_OSINGLETON(LogManager)
                //--- Types -------------------------------------------------
        public:
                //! logging priorities
                enum Priority
                {
                        LOGNOTIFY,
                        LOGWARNING,
                        LOGERROR,
                        LOGDEBUG
                };
        protected:
        private:
                //--- Variables ---------------------------------------------
        public:
        protected:
        private:
                bool fileLog;
                bool filterLog;
                bool fileNameLog;
                std::vector<optr<LogChannel> > channels;

                optr<LogConfig> config;
#ifdef ORKIGE_XML_LOG
                optr<tinyxml2::XMLDocument> logFile;
                std::vector<optr<tinyxml2::XMLElement> > elements;
#else
                std::ofstream logFile;
#endif
                //--- Methods -----------------------------------------------
        public:
                //! constructor
                LogManager();
                //! destructor
                virtual ~LogManager();
                //! write a logmessage with level and tag
                void write(const char * tag, int level,String const & message, Priority priorityLevel, const char* fileName,int lineNumber);
                //! write a logmessage
                void write(String const & message, Priority priorityLevel, const char* fileName,int lineNumber);
                //! Add a LogChannel objects
                void addChannel(const char * tag,int level,bool enabled);
                //! Enables or disables filtering of oDebugMsg disabled per default
                void enableLogFiltering(bool lf=true);
                //! Enables or disables filtering of oDebugMsg disabled per default
                void enableFileNames(bool lf=true);
                //! initializes FileLogging
                bool startFileLog(const char* logFileName);
                //! loag log configfile
                bool loadConfig(const char* configFileName);
                //! get current date
                const char* getDate();
        protected:
                //! log to xml file
                void logMessage(const char* message, Priority priorityLevel, const char* fileName,int lineNumber);
                //! python utility function
                inline void py_write(const char * tag,int level,String const & message, Priority priorityLevel, const char* fileName,int lineNumber);
        private:
        };
        /** @} End of "addtogroup Debug"*/
        //---------------------------------------------------------------
        inline void LogManager::py_write(const char * tag,int level,String const & message, Priority priorityLevel, const char* fileName,int lineNumber)
        {
                this->write(tag, level, message, priorityLevel, fileName, lineNumber);
        }
        //---------------------------------------------------------------
}

#endif //__LogManager_h__10_8_2010__16_04_17__
