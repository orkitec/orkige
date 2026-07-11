/**************************************************************
	created:	2010/09/07 at 22:22
	filename: 	ResourceUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "engine_util/ResourceUtil.h"
// explicit (the neutral umbrella carries math only): the resource
// subsystem exists identically in classic OGRE 14 and Ogre-Next
#include <OgreResourceGroupManager.h>
#include <OgreArchive.h>
#include <core_util/foreach.h>

namespace Orkige
{
	namespace ResourceUtil
	{
		//---------------------------------------------------------
		String findPath(String const & fileName)
		{
			String path;
			String const & group = Ogre::ResourceGroupManager::getSingleton().findGroupContainingResource(fileName);
			Ogre::FileInfoList fil = *Ogre::ResourceGroupManager::getSingleton().findResourceFileInfo(group, fileName);
			foreach(Ogre::FileInfo const & fileInfo, fil)
			{
				path = fileInfo.archive->getName() + String("/");
				oDebugMsg("ressources", 0, fileName + " -> " + fileInfo.archive->getName());
			}
			if(path.empty())
			{
				oDebugMsg("ressources", 0, fileName + " <- not found in any resource path!");
			}
			return path;
		}
		//---------------------------------------------------------
#ifndef ORKIGE_RENDER_NEXT
		// classic-only: Ogre-Next dropped getResourceManagers(); the
		// single caller is the classic-only gui zone
		void removeUnusedResources()
		{
			//GuiManager::getSingleton().destroyAllViews();
			// OGRE 14: getResourceManagerIterator() is deprecated, iterate the map
			Ogre::ResourceGroupManager::ResourceManagerMap const & resMgrs = Ogre::ResourceGroupManager::getSingleton().getResourceManagers();
			foreach(Ogre::ResourceGroupManager::ResourceManagerMap::value_type const & vt, resMgrs)
			{
				vt.second->unloadUnreferencedResources();
			}
		}
#endif //ORKIGE_RENDER_NEXT

	};
}
