/********************************************************************
        created:	Tuesday 2010/08/10 at 16:12
        filename: 	LogManager.cpp
        author:		steffen.roemer
        notice:		This source file is part of orkige (orkitec Game engine)
                                For the latest info, see http://www.orkitec.com/
        copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_debug/LogManager.h"
#include "core_util/StringUtil.h"
#include "core_debug/LogChannel.h"
#include "core_debug/LogConfig.h"
#include <string>
#include <sstream>
#include <tinyxml2.h>

#include <time.h>//is this standard c?
#include <stdio.h>

namespace Orkige
{
        IMPL_OSINGLETON_GETCREATE(LogManager);
        //---------------------------------------------------------
        //--- public: ---------------------------------------------
        //---------------------------------------------------------
        LogManager::LogManager()
        {
                this->fileLog=false;
                this->filterLog=false;
                this->fileNameLog=false;
                oInfo("\t...LogManager created at: " << this->getDate() << "...");
        }
        //---------------------------------------------------------------
        LogManager::~LogManager()
        {
                oInfo("\t...LogManager destroyed!...");
        }
        //---------------------------------------------------------------
        bool LogManager::loadConfig(const char* configFileName)
        {
                config = optr<LogConfig>(new LogConfig());
                return config->readConfig(configFileName);
        }
        //---------------------------------------------------------------
        void LogManager::write(const char * tag,int level,String const & message, Priority priorityLevel, const char* fileName,int lineNumber)
        {
                ///if filterlog is enabled only print the enabled channels (through config.xml)
                ///if disabled log everything!
                if (this->filterLog)
                {
                        std::vector<optr<LogChannel> >::iterator iter = this->channels.begin();
                        std::vector<optr<LogChannel> >::iterator end = this->channels.end();
                        for (; iter != end; ++iter)
                        {

                                if((*iter)->isEnabled() && (*iter)->getTag()==tag && (*iter)->getLevel()>=level)
                                {
                                        this->write(message,priorityLevel,fileName,lineNumber);
                                }
                                //else
                                //	oDoubleInfo("No channel for %s",(*iter)->getTag());
                        }
                }
                else
                {
                        this->write(message,priorityLevel,fileName,lineNumber);
                }
        }
        //---------------------------------------------------------------
        void LogManager::write(String const & message, Priority priorityLevel, const char* fileName,int lineNumber)
        {
                switch(priorityLevel)
                {
                case LOGWARNING:
                        oTrace("WARNING:  "	+	message, fileName, lineNumber);
                        break;
                case LOGNOTIFY:
                        oTrace("NOTIFY:  "	+	message, fileName, lineNumber);
                        break;
                case LOGERROR:
                        oTrace("ERROR:  "	+	message, fileName, lineNumber);
                        oAssert(!"ORKIGE ERROR!");
                        break;
                case LOGDEBUG:
                        oTrace("DEBUG:  "	+	message, fileName, lineNumber);
                        break;
                default:
                        oAssert( (priorityLevel==0) || (priorityLevel==1) || (priorityLevel==2) || (priorityLevel==3) );
                        break;
                }
                //if logging to file is enabled write!
                if( this->fileLog )
                {
                        this->logMessage(message.c_str(),priorityLevel,fileName,lineNumber);
                }
        }
        //---------------------------------------------------------------
        void LogManager::appendFileLine(const char * tag, int level, const char * message,
                const char * fileName, int lineNumber)
        {
                if(!this->fileLog)
                {
                        return;
                }
#ifdef ORKIGE_XML_LOG
                // map the log-table level onto the legacy XML priority buckets
                Priority priority = LOGDEBUG;
                switch(level)
                {
                case 0: priority = LOGERROR; break;     //!< LL_ERROR
                case 1: priority = LOGWARNING; break;   //!< LL_WARN
                case 2: priority = LOGNOTIFY; break;    //!< LL_INFO
                default:        priority = LOGDEBUG; break;     //!< LL_DEBUG
                }
                this->logMessage(message, priority, fileName, lineNumber);
#else
                if(this->fileNameLog)
                {
                        this->logFile << fileName << "(" << lineNumber << "): ";
                }
                // a compact, level+tag-prefixed line (matches the stderr shape)
                const char * levelName = "debug";
                switch(level)
                {
                case 0: levelName = "error"; break;
                case 1: levelName = "warn"; break;
                case 2: levelName = "info"; break;
                default:        break;
                }
                this->logFile << "[" << levelName << "][" << (tag ? tag : "") << "] "
                        << message << std::endl;
                this->logFile.flush();
#endif
        }
        //---------------------------------------------------------------
        void LogManager::addChannel(const char * tag,int level,bool enabled)
        {
                //QUESTION: should disabled channels be added?
                bool add = true;
                std::vector<optr<LogChannel> >::iterator iter = this->channels.begin();
                std::vector<optr<LogChannel> >::iterator end = this->channels.end();
                for (; iter != end; ++iter)
                {
                        if((*iter)->getTag()==tag)
                        {
                                oInfo("Not added! Channel <" << tag  << "> already exists! Check your config!");
                                add = false;
                                break;
                        }
                }
                if(add)
                {
                        this->channels.push_back(onew(new LogChannel(tag,level,enabled)));
                        oInfo("DebugChannel <" << tag << "> added!");
                }
        }
        //---------------------------------------------------------------
        void LogManager::enableLogFiltering(bool lf)
        {
                this->filterLog=lf;
                if(this->filterLog)
                {
                        oInfo("Log Filtering is now enabled!");
                }
                else
                {
                        oInfo("Log Filtering is now disabled!");
                }
        }
        //---------------------------------------------------------------
        void LogManager::enableFileNames(bool lf)
        {
                this->fileNameLog=lf;
                if(this->fileNameLog)
                {
                        oInfo("Logging FileNames is now enabled!");
                }
                else
                {
                        oInfo("Logging FileNames is now disabled!");
                }
        }
        //---------------------------------------------------------------
        bool LogManager::startFileLog(const char* logFileName)
        {
#ifdef ORKIGE_XML_LOG
                this->logFile = onew(new tinyxml2::XMLDocument(logFileName));
                oInfo("trying to init logfile: " << logFileName);
                this->logFile->SaveFile();

                if(this->logFile->Error())
                {
                        oPopup("Could not create/write LogFile!");

                        return false;
                }
                this->fileLog=true;

                //create xml logfile structure
                this->elements.push_back(onew(new tinyxml2::XMLElement("NOTIFY") ) );
                this->elements.push_back(onew(new tinyxml2::XMLElement("WARNING") ) );
                this->elements.push_back(onew(new tinyxml2::XMLElement("ERROR") ) );
                this->elements.push_back(onew(new tinyxml2::XMLElement("DEBUG") ) );
                this->elements.push_back(onew(new tinyxml2::XMLElement("LogMessages") ) );//root node

                this->elements[4]->InsertEndChild(*this->elements[0]);
                this->elements[4]->InsertEndChild(*this->elements[1]);
                this->elements[4]->InsertEndChild(*this->elements[2]);
                this->elements[4]->InsertEndChild(*this->elements[3]);

                this->elements[4]->SetAttribute("Time",this->getDate());
                this->logFile->InsertEndChild((*this->elements[4]));
                this->logFile->SaveFile();
#else
                this->logFile.open(logFileName);
                this->fileLog=true;
#endif
                return true;
        }
        //---------------------------------------------------------------
        void LogManager::logMessage(const char* message, Priority priorityLevel,const char* fileName,int lineNumber)
        {
#ifdef ORKIGE_XML_LOG
                tinyxml2::XMLElement xFileName("file");
                tinyxml2::XMLElement xMessage("message");
                xMessage.SetAttribute("value", message);
                xMessage.SetAttribute("at", this->getDate());

                if(this->fileNameLog)
                {
                        xFileName.SetAttribute("name", fileName);
                        xFileName.SetAttribute("line", lineNumber);
                        xFileName.InsertEndChild(xMessage);
                }

                tinyxml2::XMLHandle docHandle( this->logFile.get() );
                tinyxml2::XMLHandle rootHandle = docHandle.FirstChildElement("LogMessages").FirstChildElement(this->elements[priorityLevel]->Value() );
                tinyxml2::XMLElement *root = rootHandle.Element();

                if(this->fileNameLog)
                {
                        root->InsertEndChild(xFileName);
                }
                else
                        root->InsertEndChild(xMessage);

                //save file after every message not only in destructor because of crashes
                this->logFile->SaveFile();
#else
                if(this->fileNameLog)
                {
                        this->logFile << fileName << "(" << lineNumber << "): ";
                }
                this->logFile << message << std::endl;

                // Flush stcmdream to ensure it is written (incase of a crash, we need log to be up to date)
                this->logFile.flush();
#endif
        }
        //---------------------------------------------------------------
        const char* LogManager::getDate()
        {
                //DEPRECATED AND UGLY CTIME SEEMS TO GIVE A '\n' NOT '\0' TERMINATED STRING
                time_t tp;
                time(&tp);
                char* c = ctime(&tp);//deprecated but is ctime_s available on linux?
                char* ptr = strrchr(c, '\n');//this is slow but '\n' termnated string puts bad look in tinyxml
                if(!oIsNull(ptr))
                        *ptr='\0';//go away ugly \n ! :P
                return c;
        }
        //---------------------------------------------------------
        //--- protected: ------------------------------------------
        //---------------------------------------------------------

        //---------------------------------------------------------
        //--- private: --------------------------------------------
        //---------------------------------------------------------

        OINTERFACE_IMPL(LogManager)
                OENUM_START(Priority)
                        OENUM_VALUE(LOGNOTIFY)
                        OENUM_VALUE(LOGWARNING)
                        OENUM_VALUE(LOGERROR)
                        OENUM_VALUE(LOGDEBUG)
                OENUM_END
                OFUNC_REN(py_write, write)
        OOBJECT_END


}


