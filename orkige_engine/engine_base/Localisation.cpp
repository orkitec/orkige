/********************************************************************
	created:	Thursday 2010/11/11 at 15:18
	filename: 	Localisation.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/


#include "engine_base/Localisation.h"
#include <core_util/PlatformUtil.h>
#include <core_util/foreach.h>
#include "engine_graphic/Engine.h"
// TODO(Phase 1): engine_gocomponent is not ported yet; the ModelComponent reload
// in setupResources() returns with it (see ORKIGE_ENGINE_HAS_GOCOMPONENT below).
#ifdef ORKIGE_ENGINE_HAS_GOCOMPONENT
#include <core_game/GameObjectManager.h>
#include <engine_gocomponent/ModelComponent.h>
#endif

#if defined(__APPLE__) && defined(__OBJC__)
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
	Localisation::Localisation(Orkige::String const & _currentLocale, Orkige::String const & validLocales, Orkige::String const & defaultLocale)
	{
		this->loadLocaleCfg(_currentLocale, validLocales, defaultLocale);
	}
	//---------------------------------------------------------
	Localisation::~Localisation()
	{
	}
	//---------------------------------------------------------
	void Localisation::loadLocaleCfg(Orkige::String const & currentLocale, Orkige::String const & validLocales, Orkige::String const & defaultLocale)
	{
		// list of country specific top level domains
		//http://de.wikipedia.org/wiki/Liste_l%C3%A4nderspezifischer_Top-Level-Domains

		this->clear();
		this->currentLocale = currentLocale;
		this->supportedLocales = Ogre::StringUtil::split(validLocales, ",");
		
		oAssertDesc(!defaultLocale.empty(), "Localization: no default language set");
		oAssertDesc(!this->supportedLocales.empty(), "Localization: no supported language found");

		if(this->currentLocale.empty())
		{
#ifdef WIN32

			//@TODO: extend for all languages
			//http://msdn.microsoft.com/en-us/goglobal/bb964664.aspx
			LCID lcid = GetSystemDefaultLCID();


			// http://msdn.microsoft.com/en-us/library/dd318103%28v=vs.85%29.aspx
			// http://msdn.microsoft.com/en-us/library/dd464799%28v=VS.85%29.aspx
			// TODO use GetLocaleInfoEx() for vista and above
			// returns German, English, French, ...
			char buf[256];
			int ret = GetLocaleInfo( 
				LOCALE_SYSTEM_DEFAULT, 
				/*LOCALE_SABBREVLANGNAME,*/	LOCALE_SENGLANGUAGE, /*LOCALE_SNATIVELANGNAME, */
				buf, 
				sizeof(buf)-1);


			//Locale ID (LCID) Chart: 
			// http://msdn.microsoft.com/en-us/library/0h88fahh(VS.85).aspx
			// http://msdn.microsoft.com/en-us/library/cc233968%28v=PROT.10%29.aspx
			// http://msdn.microsoft.com/en-us/goglobal/bb895996
			switch(lcid)
			{
			case 1031:
			case 3079:
			case 5127:
			case 2055:
			case 4103: this->currentLocale = "de"; break;

			case 1036:
			case 2060:
			case 3084:
			case 5132:
			case 4108: this->currentLocale = "fr"; break;

			case 3082:
			case 1034:
			case 11274:
			case 16394:
			case 13322:
			case 9226:
			case 5130:
			case 7178:
			case 12298:
			case 4106:
			case 18442:
			case 2058:
			case 19466:
			case 6154:
			case 10250:
			case 20490:
			case 15370:
			case 17418:
			case 14346:
			case 8202: this->currentLocale = "es"; break;

			case 1040:
			case 2064: this->currentLocale = "it"; break;

			case 1043:
			case 2067: this->currentLocale = "nl"; break;

			case 1041: this->currentLocale = "ja"; break;

			case 2052:
			case 4100:
			case 1028:
			case 3076:
			case 5124: this->currentLocale = "zh"; break;

			default: this->currentLocale = defaultLocale; break;
			}
#elif __APPLE__
#ifdef __OBJC__
			
			this->currentLocale = defaultLocale;

			// possible values
			// http://developer.apple.com/library/ios/#documentation/MacOSX/Conceptual/BPInternational/BPInternational.pdf
			// http://www.loc.gov/standards/iso639-2/php/English_list.php
			// http://www.plasmaworks.com/forums/index.php?action=printpage;topic=70.0
			
			// about different mac versions
			//http://developer.apple.com/library/mac/#qa/qa1391/_index.html			
			
			NSUserDefaults* defs = [NSUserDefaults standardUserDefaults];
			NSArray* languages = [defs objectForKey:@"AppleLanguages"];
			// From Mac OS X v10.4 (Tiger) on this is using the standard canonical form as defined by BCP 47 / RFC 4646
			// entries are: en, fr, de, es, it, nl, sv, nb, ja, "zh-Hans", da, fi, pt, "zh-Hant", ko, hu
			
			for (unsigned int each = 0; each < [languages count]; each++)
			{
				NSString* preferredLang = [languages objectAtIndex:each];
				String lang = String((char*)[preferredLang cStringUsingEncoding:1]).substr(0, 2); // substr because of "zh-Hans", "zh-Hant"
								
				if (std::find(this->supportedLocales.begin(), this->supportedLocales.end(), lang) != this->supportedLocales.end())
				{
					this->currentLocale = lang;
					break;
				}
			}
			
#endif //__OBJC__
#else
			//static_assert(false && "UNKNOWN SYSTEM FOR LOCALE!");
			this->currentLocale = defaultLocale;	//FIXME: find the right locale here (pe)

#endif
		}

		oDebugMsg("global", 0, "Current System Locale: \"" << this->currentLocale << "\" !");

		if(std::find(this->supportedLocales.begin(), this->supportedLocales.end(), this->currentLocale) == this->supportedLocales.end())
		{
			oDebugMsg("global", 0, "Unsupported locale: \"" << this->currentLocale << "\" using: \"" << defaultLocale << "\" insted!");
			this->currentLocale = defaultLocale;
		}

		oDebugMsg("global", 0, "Game Locale: \"" << this->currentLocale << "\" !");

		this->loadFromResourceSystem(this->currentLocale + "/locale.cfg", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

		// OGRE 14: ConfigFile::mSettings stores the SettingsMultiMap by value now
		SettingsBySection_::iterator languageSectionIterator = this->mSettings.find("Language");
		oAssert(languageSectionIterator != this->mSettings.end());
		this->languageSettings = &languageSectionIterator->second;
		oAssert(this->languageSettings);
	}
	//---------------------------------------------------------
	void Localisation::setupResources(String const & directories, String const & locType)
	{
		this->directories = directories;
		if(Ogre::ResourceGroupManager::getSingleton().resourceGroupExists("Language"))
		{
			Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup("Language");
		}

		Ogre::ResourceGroupManager::getSingleton().createResourceGroup("Language");
		Orkige::StringVector dirs = Ogre::StringUtil::split(this->directories, ",");
		foreach(Orkige::String const & dir, dirs)
		{
			Orkige::String archName = "language/" + this->currentLocale + "/" + dir;
			if(locType == "FileSystem")
			{
				archName = PlatformUtil::getResourceDirectory() + archName;
			}
			Ogre::ResourceGroupManager::getSingleton().addResourceLocation(archName, locType, "Language");
		}
		Ogre::ResourceGroupManager::getSingleton().initialiseResourceGroup("Language");
		// OGRE 14: getMovableObjectIterator() became getMovableObjects(),
		// EntityFactory::FACTORY_TYPE_NAME became MOT_ENTITY
		for(auto const & vt : Engine::getSingleton().getSceneManager()->getMovableObjects(Ogre::MOT_ENTITY))
		{
			Ogre::Entity* e = static_cast<Ogre::Entity*>(vt.second);
			oAssert(e);
			e->_deinitialise();
		}

#ifdef ORKIGE_ENGINE_HAS_GOCOMPONENT
		GameObjectManager::GameObjectMap gos = GameObjectManager::getSingleton().getGameObjects();
		foreach(GameObjectManager::GameObjectMap::value_type const & vt, gos)
		{
			optr<GameObject> go = vt.second;
			if(go->hasComponent<ModelComponent>())
			{
				go->getComponentPtr<ModelComponent>()->loadModel(go->getComponentPtr<ModelComponent>()->getCurrentModelFileName());
			}
		}
#endif //ORKIGE_ENGINE_HAS_GOCOMPONENT
	}
	//---------------------------------------------------------
	void Localisation::setupResourcesDelayed(String const & directories)
	{
		this->directories = directories;
		this->registerEvent(Engine::FrameRenderingQueuedEvent, &Localisation::onFrameEnded, this);
	}
	//---------------------------------------------------------
	Orkige::String Localisation::getLocalizedFormatted(Orkige::String const & id, Orkige::StringVector const & args)
	{
		Orkige::String localized = this->getLocalized(id);
		for(std::size_t i = 0; i < args.size(); i++)
		{
			std::stringstream sstr;
			sstr << "%%" << i << "%%";
			// no boost: plain std::string replace-all
			const Orkige::String placeholder = sstr.str();
			for(std::size_t pos = localized.find(placeholder); pos != Orkige::String::npos; pos = localized.find(placeholder, pos + args[i].length()))
			{
				localized.replace(pos, placeholder.length(), args[i]);
			}
		}
		return localized;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool Localisation::onFrameEnded(Event const & event)
	{
		this->setupResources(this->directories, Engine::getSingleton().getDefaultLocationType());
		this->unregisterEvent(Engine::FrameRenderingQueuedEvent);
		return true;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
