/********************************************************************
	created:	Tuesday 2010/08/10 at 16:27
	filename: 	LogConfig.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "core_util/StringUtil.h"
#include "core_util/String.h"
#include "core_debug/LogConfig.h"
#include "core_debug/LogManager.h"
#include <boost/scoped_ptr.hpp>
#include "core_tinyxml/tinyxml.h"
#include "core_util/PlatformUtil.h"

#ifdef __ANDROID__
#include <zzip/zzip.h>
#endif

namespace Orkige
{
#ifdef __ANDROID__	 
	Orkige::String LogConfig_getZzipErrorDescription(zzip_error_t zzipError) 
	{
		Orkige::String errorMsg;
		switch (zzipError)
		{
		case ZZIP_NO_ERROR:
			break;
		case ZZIP_OUTOFMEM:
			errorMsg = "Out of memory.";
			break;            
		case ZZIP_DIR_OPEN:
		case ZZIP_DIR_STAT: 
		case ZZIP_DIR_SEEK:
		case ZZIP_DIR_READ:
			errorMsg = "Unable to read zip file.";
			break;            
		case ZZIP_UNSUPP_COMPR:
			errorMsg = "Unsupported compression format.";
			break;            
		case ZZIP_CORRUPTED:
			errorMsg = "Corrupted archive.";
			break;            
		default:
			errorMsg = "Unknown error.";
			break;            
		};

		return errorMsg;
	}

	void LogConfig_checkZzipError(int zzipError, const Orkige::String& operation)
	{
		if (zzipError != ZZIP_NO_ERROR)
		{
			Orkige::String errorMsg = LogConfig_getZzipErrorDescription(static_cast<zzip_error_t>(zzipError));

			OutputDebugStringA((errorMsg + " [Operation: " + operation + "]").c_str());
		}
	}
#endif
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	bool LogConfig::readConfig(const char* configFileName)
	{
		boost::scoped_ptr<TiXmlDocument> config(new TiXmlDocument(configFileName));
#ifdef __ANDROID__	
		{
			OutputDebugStringA(("Loading Archive: " + PlatformUtil::getApkPath()).c_str());
			zzip_error_t zzipError;

			ZZIP_DIR* zipArchive = zzip_dir_open(PlatformUtil::getApkPath().c_str(), &zzipError);
			LogConfig_checkZzipError(zzipError, "opening archive");
			// print names
			ZZIP_DIRENT zzipEntry;
			while (zzip_dir_read(zipArchive, &zzipEntry))
			{
				OutputDebugStringA(zzipEntry.d_name);
			}
			Orkige::String _configFileName = Orkige::String("assets/") + configFileName;
			OutputDebugStringA(("Get frile from Apk: " + _configFileName).c_str());
			ZZIP_FILE* zzipFile = zzip_file_open(zipArchive, _configFileName.c_str(), ZZIP_ONLYZIP | ZZIP_CASELESS);
			LogConfig_checkZzipError(zzipError, "open file");
#define MAX_FILE_SIZE (64 * 1024)
			unsigned char buffer[MAX_FILE_SIZE];
			zzip_ssize_t length = zzip_file_read(zzipFile, (char*)buffer, MAX_FILE_SIZE);
			LogConfig_checkZzipError(zzipError, "read file");
			zzip_file_close(zzipFile);
			LogConfig_checkZzipError(zzipError, "close file");
			zzip_dir_close(zipArchive);
			LogConfig_checkZzipError(zzipError, "close archive");
			config->LoadFromMemory((char*)buffer, length);
			OutputDebugStringA("Log Loaded");
		}
#else
		config->LoadFile();
#endif

		if(config->Error())
		{
			oPopup((String("Could not Load DebugConfig! File: ") + configFileName).c_str());
			return false;
		}
		oInfo("Parsing LogConfig: " << configFileName << "!");
		TiXmlHandle docHandle( config.get() );
		TiXmlHandle logHandle = docHandle.FirstChildElement("LogConfig");

		TiXmlElement* logFileElement = logHandle.FirstChildElement("LogFile").Element();

		//If <LogFile> Tag exists an has a name start logging to this file
		if(logFileElement)
		{
			const char* logFileName = logFileElement->Attribute("name");
			if(logFileName)
			{
				LogManager::getSingleton().startFileLog(logFileName);
			}
			if(StringUtil::StringToBool(logFileElement->Attribute("fileNames")))
			{
				LogManager::getSingleton().enableFileNames();
			}

		}


		TiXmlHandle elementHandle = logHandle.FirstChildElement("DebugChannels").FirstChildElement();

		TiXmlElement *debugchannel = elementHandle.Element();

		// Loop debugchannels and add them
		// TODO: make this stuff case insensitive!

		while(debugchannel)
		{
			char* tag = const_cast<char*>(debugchannel->Value());
			bool enabled = StringUtil::StringToBool( const_cast<char*>(debugchannel->Attribute("enabled")) );
			int level = StringUtil::StringToInt(const_cast<char*>(debugchannel->Attribute("level")));

			LogManager::getSingleton().addChannel(tag,level,enabled);
			//check if to enable filtering of Logchannels
			if ( StringUtil::StringCompare(tag,"global") )
				LogManager::getSingleton().enableLogFiltering( StringUtil::StringToBool( debugchannel->Attribute("filterLog") ) );

			debugchannel = debugchannel->NextSiblingElement();
		}
		oInfo("LogConfig: <" << configFileName << "> parsed!");
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}

