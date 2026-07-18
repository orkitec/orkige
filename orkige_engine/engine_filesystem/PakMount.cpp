/********************************************************************
	created:	Friday 2026/07/18 at 05:00
	filename: 	PakMount.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_filesystem/PakMount.h"
#include "engine_filesystem/PakArchive.h"

#include <OgreArchiveFactory.h>
#include <OgreArchiveManager.h>
#include <OgreResourceGroupManager.h>

#include <map>

namespace Orkige
{
	namespace PakMount
	{
		const String ARCHIVE_TYPE = "OrkigePak";

		namespace
		{
			//! the internal separator that composes a UNIQUE resource-location
			//! name from (pakPath, prefix) - a control byte that never occurs in
			//! a filesystem path, so the name round-trips through Ogre as an
			//! opaque key AND is unique per mount
			const char kNameSeparator = '\x1f';

			String makeLocationName(String const & pakPath, String const & prefix)
			{
				if(prefix.empty())
				{
					return pakPath;
				}
				return pakPath + kNameSeparator + prefix;
			}

			//! the pak archive factory: a stateless Ogre::ArchiveFactory whose
			//! createInstance looks the (zipPath, prefix) of a location up in the
			//! registry the mount() call populated. ONE instance per process,
			//! registered with Ogre::ArchiveManager on first mount.
			class PakArchiveFactory : public Ogre::ArchiveFactory
			{
			public:
				struct MountSpec
				{
					Ogre::String zipPath;
					Ogre::String prefix;
				};

				const Ogre::String& getType() const override
				{
					static const Ogre::String type = ARCHIVE_TYPE;
					return type;
				}

				using Ogre::ArchiveFactory::createInstance;
				Ogre::Archive* createInstance(const Ogre::String& name,
					bool readOnly) override
				{
					(void)readOnly;	// pak archives are always read-only
					std::map<Ogre::String, MountSpec>::const_iterator it =
						this->mMounts.find(name);
					if(it == this->mMounts.end())
					{
						// a location registered without a mount() spec: treat the
						// whole name as the zip path, no sub-tree
						return OGRE_NEW PakArchive(name, ARCHIVE_TYPE, name, "");
					}
					return OGRE_NEW PakArchive(name, ARCHIVE_TYPE,
						it->second.zipPath, it->second.prefix);
				}

				void destroyInstance(Ogre::Archive* archive) override
				{
					OGRE_DELETE archive;
				}

				void registerMount(Ogre::String const & name,
					Ogre::String const & zipPath, Ogre::String const & prefix)
				{
					MountSpec spec;
					spec.zipPath = zipPath;
					spec.prefix = prefix;
					this->mMounts[name] = spec;
				}

				void unregisterMount(Ogre::String const & name)
				{
					this->mMounts.erase(name);
				}

			private:
				std::map<Ogre::String, MountSpec> mMounts;
			};

			//! the process-wide factory + its one-time ArchiveManager registration
			PakArchiveFactory& factory()
			{
				static PakArchiveFactory instance;
				static bool registered = false;
				if(!registered)
				{
					Ogre::ArchiveManager::getSingleton().addArchiveFactory(
						&instance);
					registered = true;
				}
				return instance;
			}

			Ogre::String resolveGroup(String const & groupName)
			{
				return groupName.empty()
					? Ogre::String(
						Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)
					: groupName;
			}
		}
		//---------------------------------------------------------
		String normalizeMountPoint(String const & mountPoint)
		{
			String prefix = mountPoint;
			// "\\" -> "/" so an author's Windows-style mount point still resolves
			for(char & c : prefix)
			{
				if(c == '\\')
				{
					c = '/';
				}
			}
			// a leading "./" is noise
			while(prefix.size() >= 2 && prefix[0] == '.' && prefix[1] == '/')
			{
				prefix.erase(0, 2);
			}
			// a bare "/" (or leading slashes) means "the root" - no sub-tree
			while(!prefix.empty() && prefix[0] == '/')
			{
				prefix.erase(0, 1);
			}
			if(prefix.empty())
			{
				return String();
			}
			if(prefix.back() != '/')
			{
				prefix += '/';
			}
			return prefix;
		}
		//---------------------------------------------------------
		void mount(String const & pakPath, String const & mountPoint,
			String const & groupName)
		{
			const String prefix = normalizeMountPoint(mountPoint);
			const Ogre::String locationName = makeLocationName(pakPath, prefix);
			const Ogre::String group = resolveGroup(groupName);
			Ogre::ResourceGroupManager & groups =
				Ogre::ResourceGroupManager::getSingleton();
			// idempotent: a repeat mount of the same (path, sub-tree, group) is
			// a no-op (the location already indexes the pak's entries)
			if(groups.resourceGroupExists(group) &&
				groups.resourceLocationExists(locationName, group))
			{
				return;
			}
			factory().registerMount(locationName, pakPath, prefix);
			groups.addResourceLocation(locationName, ARCHIVE_TYPE, group,
				false /*recursive - the pak indexes its own tree*/);
			oDebugMsg("filesystem", 0, "mounted pak '" << pakPath
				<< (prefix.empty() ? "" : "' sub-tree '") << prefix
				<< "' into group '" << group << "'");
		}
		//---------------------------------------------------------
		void unmount(String const & pakPath, String const & mountPoint,
			String const & groupName)
		{
			const String prefix = normalizeMountPoint(mountPoint);
			const Ogre::String locationName = makeLocationName(pakPath, prefix);
			const Ogre::String group = resolveGroup(groupName);
			Ogre::ResourceGroupManager & groups =
				Ogre::ResourceGroupManager::getSingleton();
			if(groups.resourceGroupExists(group) &&
				groups.resourceLocationExists(locationName, group))
			{
				groups.removeResourceLocation(locationName, group);
			}
			factory().unregisterMount(locationName);
		}
	}
}
