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
#include <boost/static_assert.hpp>
#include <boost/algorithm/string.hpp>
#include "engine_graphic/Engine.h"
#include <core_game/GameObjectManager.h>
#include <engine_gocomponent/ModelComponent.h>

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
		this->clear();
		this->currentLocale = currentLocale;
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
			//BOOST_STATIC_ASSERT(false && "UNKNOWN SYSTEM FOR LOCALE!");
			this->currentLocale = "de";
			oDebugMsg("global", 0, "blubb: " << setlocale(LC_ALL,NULL));
#endif
		}

        oDebugMsg("global", 0, "2blubb: " << setlocale(LC_ALL,NULL));
		oDebugMsg("global", 0, "zack azck Current System Locale: \"" << this->currentLocale << "\" !");

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
	void Localisation::setupResources(String const & directories)
	{
		this->directories = directories;
		if(Ogre::ResourceGroupManager::getSingleton().resourceGroupExists("Language"))
		{
			Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup("Language");
		}

		Ogre::ResourceGroupManager::getSingleton().createResourceGroup("Language");
		Orkige::StringVector dirs;
		boost::split(dirs, this->directories, boost::is_any_of(","));
		foreach(Orkige::String const & dir, dirs)
		{
			Ogre::ResourceGroupManager::getSingleton().addResourceLocation(Orkige::PlatformUtil::getResourceDirectory() + "language/" + this->currentLocale + "/" + dir, "FileSystem", "Language");
		}
		Ogre::ResourceGroupManager::getSingleton().initialiseResourceGroup("Language");
		Ogre::SceneManager::MovableObjectIterator iterator = Engine::getSingleton().getSceneManager()->getMovableObjectIterator(Ogre::EntityFactory::FACTORY_TYPE_NAME);
		while(iterator.hasMoreElements())
		{
			Ogre::Entity* e = static_cast<Ogre::Entity*>(iterator.getNext());
			oAssert(e);
			e->_deinitialise();
		}

		GameObjectManager::GameObjectMap gos = GameObjectManager::getSingleton().getGameObjects();
		foreach(GameObjectManager::GameObjectMap::value_type const & vt, gos)
		{
			optr<GameObject> go = vt.second;
			if(go->hasComponent<ModelComponent>())
			{
				go->getComponentPtr<ModelComponent>()->loadModel(go->getComponentPtr<ModelComponent>()->getCurrentModelFileName());
			}
		}
	}
	//---------------------------------------------------------
	void Localisation::setupResourcesDelayed(String const & directories)
	{
		this->directories = directories;
		this->registerEvent(Engine::FrameRenderingQueuedEvent, &Localisation::onFrameEnded, this);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool Localisation::onFrameEnded(Event const & event)
	{
		this->setupResources(this->directories);
		this->unregisterEvent(Engine::FrameRenderingQueuedEvent);
		return true;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
