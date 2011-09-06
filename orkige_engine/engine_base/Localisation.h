/********************************************************************
	created:	Thursday 2010/11/11 at 15:17
	filename: 	Localisation.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __Localisation_h__11_11_2010__15_17_25__
#define __Localisation_h__11_11_2010__15_17_25__

#include "engine_module/EnginePrerequisites.h"
#include <core_util/Singleton.h>
#include "engine_util/StringUtil.h"
#include <core_event/EventHandler.h>

namespace Orkige
{
	class Localisation : public Ogre::ConfigFile, public Orkige::Singleton<Localisation>, public Orkige::EventHandler
	{
		DECL_OSINGLETON(Localisation);
		//-Types--------------------------------------------
	public:
	protected:
	private:
		//-Variables----------------------------------------
	public:
	protected:
	private:
		Orkige::StringVector supportedLocales;
		Orkige::String currentLocale;
		Orkige::String localeFileName;
		SettingsMultiMap* languageSettings;
		Orkige::String directories;
		//-Methods------------------------------------------
	public:
		//! create Localisation, if currentLocale is empty it tries to get the current systems locale
		//! if set locale is not supported in validLocale the defaultLocale is used
		Localisation(Orkige::String const & currentLocale = Orkige::StringUtil::BLANK, Orkige::String const & validLocales = "en,de", Orkige::String const & defaultLocale = "en");
		//! destructor
		virtual ~Localisation();
		//! load given locale settings
		void loadLocaleCfg(Orkige::String const & currentLocale = Orkige::StringUtil::BLANK, Orkige::String const & validLocales = "en,de", Orkige::String const & defaultLocale = "en");
		//! get localized string from given id if id is not found the id itself is returned
		inline Orkige::String const & getLocalized(Orkige::String const & id);
		//! get current set locale
		inline String const & getCurrentLocale();
		//! setup resource directories (Category Language) for current locale
		//! @param directories comma separated list of directories
		void setupResources(String const & directories);
		//! call setupResources when scene rendering is done
		void setupResourcesDelayed(String const & directories);
	protected:
		bool onFrameEnded(Event const & event);
	private:
	};
	//----------------------------------------------------
	inline Orkige::String const & Localisation::getLocalized(Orkige::String const & id)
	{
		SettingsMultiMap::const_iterator i = this->languageSettings->find(id);
		if (i == this->languageSettings->end())
		{
			return id;
		}

		return i->second;
	}
	//----------------------------------------------------
	inline String const & Localisation::getCurrentLocale()
	{
		return this->currentLocale;
	}
}

#endif //__Localisation_h__11_11_2010__15_17_25__
