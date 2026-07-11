/********************************************************************
	created:	Wednesday 2026/07/08 at 22:00
	filename: 	ConfigFileUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ConfigFileUtil_h__8_7_2026__22_00_00__
#define __ConfigFileUtil_h__8_7_2026__22_00_00__

//! @file ConfigFileUtil.h
//! @brief the one per-flavor seam of Ogre::ConfigFile section iteration
//! @remarks Ogre::ConfigFile exists in classic OGRE 14 AND Ogre-Next with
//! the same loading surface, but the section accessor drifted: classic 14
//! replaced the 1.x getSectionIterator() with getSettingsBySection()
//! (by-value multimaps), Ogre-Next kept the 1.x iterator (by-pointer
//! multimaps). The flavor-neutral consumers (UiAtlas' .ogui loader,
//! GuiFactory's layout loader) iterate through THIS helper
//! so the flavor #if lives in exactly one place.

#include <OgreConfigFile.h>

#include <map>

namespace Orkige
{
	namespace ConfigFileUtil
	{
		typedef std::map<Ogre::String, Ogre::ConfigFile::SettingsMultiMap>
			SectionMap;

		//! all sections of a loaded config file as section name -> settings
		//! copies (the files here are tiny: .ogui atlases, widget layouts)
		inline SectionMap getSections(Ogre::ConfigFile & configFile)
		{
			SectionMap sections;
#ifdef ORKIGE_RENDER_CLASSIC
			for(auto const & section : configFile.getSettingsBySection())
			{
				sections[section.first] = section.second;
			}
#else
			Ogre::ConfigFile::SectionIterator sectionIterator =
				configFile.getSectionIterator();
			while(sectionIterator.hasMoreElements())
			{
				const Ogre::String name = sectionIterator.peekNextKey();
				sections[name] = *sectionIterator.getNext();
			}
#endif
			return sections;
		}
	}
}

#endif //__ConfigFileUtil_h__8_7_2026__22_00_00__
