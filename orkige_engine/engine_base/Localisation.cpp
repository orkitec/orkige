/********************************************************************
	created:	Thursday 2010/11/11 at 15:18
	filename: 	Localisation.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/


#include "engine_base/Localisation.h"
#include <core_util/PlatformUtil.h>
#include <boost/static_assert.hpp>
#include <boost/algorithm/string.hpp>
#ifdef __APPLE__
#import <Foundation/NSString.h>
#import <Foundation/NSPathUtilities.h>
#import <Foundation/NSBundle.h>
#import <Foundation/NSUserDefaults.h>
#endif

namespace Orkige
{
	IMPL_OSINGLETON(Localisation);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Localisation::Localisation(Orkige::String const & _currentLocale, Orkige::String const & validLocales, Orkige::String const & defaultLocale) : currentLocale(_currentLocale)
	{
		boost::split(this->supportedLocales, validLocales, boost::is_any_of(","));
		if(this->currentLocale.empty())
		{
#ifdef WIN32

			//@TODO: extend for all languages
			//http://msdn.microsoft.com/en-us/goglobal/bb964664.aspx
			LCID lcid = GetSystemDefaultLCID();

			//Locale ID (LCID) Chart: http://msdn.microsoft.com/en-us/library/0h88fahh(VS.85).aspx
			switch(lcid)
			{
			case 1031:
			case 3079:
			case 5127:
			case 2055:
			case 4103: this->currentLocale = "de"; break;
			default: this->currentLocale = defaultLocale ; break;
			}
#elif __APPLE__
			NSUserDefaults* defs = [NSUserDefaults standardUserDefaults];
			NSArray* languages = [defs objectForKey:@"AppleLanguages"];
			NSString* preferredLang = [languages objectAtIndex:0];
			this->currentLocale = String((char*)[preferredLang cStringUsingEncoding:1]);
#else
			BOOST_STATIC_ASSERT(false && "UNKNOWN SYSTEM FOR LOCALE!");
#endif
		}

		oDebugMsg("global", 0, "Current System Locale: \"" << this->currentLocale << "\" !");

		if(std::find(this->supportedLocales.begin(), this->supportedLocales.end(), this->currentLocale) == this->supportedLocales.end())
		{
			oDebugMsg("global", 0, "Unsupported locale: \"" << this->currentLocale << "\" using: \"" << defaultLocale << "\" insted!");
			this->currentLocale = defaultLocale;
		}

		oDebugMsg("global", 0, "Game Locale: \"" << this->currentLocale << "\" !");

		this->loadDirect(Orkige::PlatformUtil::getResourceDirectory() + "language/" + this->currentLocale + "/locale.cfg");

		SettingsBySection::const_iterator languageSectionIterator = this->mSettings.find("Language");
		oAssert(languageSectionIterator != this->mSettings.end());
		this->languageSettings = languageSectionIterator->second;
		oAssert(this->languageSettings);
	}
	//---------------------------------------------------------
	Localisation::~Localisation()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
