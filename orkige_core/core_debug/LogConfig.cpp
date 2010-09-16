/********************************************************************
	created:	Tuesday 2010/08/10 at 16:27
	filename: 	LogConfig.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "core_util/StringUtil.h"
#include "core_util/String.h"
#include "core_debug/LogConfig.h"
#include "core_debug/LogManager.h"
#include <boost/scoped_ptr.hpp>
#include "core_tinyxml/tinyxml.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	bool LogConfig::readConfig(const char* configFileName)
	{
		boost::scoped_ptr<TiXmlDocument> config(new TiXmlDocument(configFileName));
		config->LoadFile();

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

