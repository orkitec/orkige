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
#include "core_util/PlatformUtil.h"
#include <tinyxml2.h>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	bool LogConfig::readConfig(const char* configFileName)
	{
		boost::scoped_ptr<tinyxml2::XMLDocument> config(new ::tinyxml2::XMLDocument(true));
		config->LoadFile(configFileName);

		if(config->Error())
		{
			oPopup((String("Could not Load DebugConfig! File: ") + configFileName).c_str());
			return false;
		}
		oInfo("Parsing LogConfig: " << configFileName << "!");
		tinyxml2::XMLElement* logHandle = config->FirstChildElement("LogConfig");

		tinyxml2::XMLElement* logFileElement = logHandle->FirstChildElement("LogFile");

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

		tinyxml2::XMLElement *debugchannel = logHandle->FirstChildElement("DebugChannels")->FirstChildElement();

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

