/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	Project.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __Project_h__7_7_2026__12_00_00__
#define __Project_h__7_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <map>

namespace Orkige
{
	//! @brief a game project on disk - the Unity-style "open a project, not a
	//! scene" unit the editor and player work in.
	//! @remarks A project is a folder with a project.orkproj manifest plus the
	//! standard subdirectories scenes/, assets/ and scripts/ (scripts/ is
	//! reserved for the upcoming Lua script components). The manifest is
	//! deliberately NOT an XMLArchive: the archive stream format is positional
	//! and unreadable, while the manifest must stay hand-editable and
	//! forward-compatible - it is a small semantic XML document written and
	//! read through tinyxml2 directly:
	//! @code
	//! <OrkigeProject version="1">
	//!     <Name>My Game</Name>
	//!     <MainScene>scenes/main.oscene</MainScene>
	//!     <Settings>
	//!         <Setting key="some.key" value="some value"/>
	//!     </Settings>
	//! </OrkigeProject>
	//! @endcode
	//! The free-form settings map carries string key/value pairs so later
	//! milestones (native module config, per-platform export settings) can add
	//! keys without a format change; unknown elements are ignored on load.
	class ORKIGE_CORE_DLL Project
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
		static const int PROJECT_FORMAT_VERSION;		//!< version written into every saved manifest
		static const String MANIFEST_FILE_NAME;		//!< "project.orkproj"
		static const String SCENES_DIR_NAME;			//!< "scenes"
		static const String ASSETS_DIR_NAME;			//!< "assets"
		static const String SCRIPTS_DIR_NAME;			//!< "scripts" (reserved for script components)
		static const String SCENE_FILE_EXTENSION;		//!< ".oscene"
		//! the Ogre resource group name the runtimes register a project's
		//! asset/scene locations under (a dedicated group so switching
		//! projects can tear everything down via destroyResourceGroup); a
		//! plain string here - the core stays renderer-independent
		static const String RESOURCE_GROUP_NAME;		//!< "OrkigeProject"
	protected:
	private:
		String						mRootDirectory;	//!< absolute project root ("" until loaded)
		String						mName;				//!< human-readable project name
		String						mMainScene;		//!< project-relative main scene path ("" = none yet)
		std::map<String, String>	mSettings;			//!< free-form forward-compat settings
		//--- Methods -----------------------------------------
	public:
		//! @brief the manifest file a project path names: a directory maps to
		//! "<dir>/project.orkproj", a *.orkproj file is taken as-is.
		//! @return the manifest path, or "" when path names neither
		static String resolveManifestPath(String const & path);

		//! @brief create a new project skeleton: the root directory (created
		//! if missing), the scenes/assets/scripts subdirectories and a fresh
		//! manifest with mainScene "scenes/main.oscene". Refused when the
		//! directory already contains a manifest. On success the given
		//! project instance holds the loaded new project.
		//! @param rootDirectory the project folder
		//! @param name project name ("" = the folder's name)
		//! @param errorMessage optional - receives a printable reason on failure
		static bool create(String const & rootDirectory, String const & name,
			Project & project, String * errorMessage = 0);

		//! @brief load a project from a directory or a .orkproj manifest path.
		//! Fails (with an honest message) on a missing/unparseable manifest,
		//! a wrong root element, a manifest version newer than this build, a
		//! missing/empty Name or a non-relative MainScene. The main scene
		//! FILE is allowed to be missing (a fresh project saves it later) -
		//! callers report that when they actually try to open it.
		//! On failure the instance is left unloaded (never half-loaded).
		bool load(String const & path, String * errorMessage = 0);

		//! save the manifest to "<root>/project.orkproj" (requires a loaded
		//! project - root and name are set)
		bool save(String * errorMessage = 0) const;

		//! has a project been loaded/created into this instance
		bool isLoaded() const { return !mRootDirectory.empty(); }
		//! back to the unloaded state
		void close();

		String const & getName() const { return mName; }
		void setName(String const & name) { mName = name; }

		//! the main scene, project-relative (e.g. "scenes/main.oscene"; "" = none)
		String const & getMainScene() const { return mMainScene; }
		void setMainScene(String const & projectRelativePath) { mMainScene = projectRelativePath; }
		//! absolute path of the main scene ("" when no main scene is set)
		String getMainScenePath() const;

		//! absolute project root directory ("" until loaded)
		String const & getRootDirectory() const { return mRootDirectory; }
		String getScenesDirectory() const;		//!< absolute "<root>/scenes"
		String getAssetsDirectory() const;		//!< absolute "<root>/assets"
		String getScriptsDirectory() const;	//!< absolute "<root>/scripts"

		//! absolute path for a project-relative one (absolute input passes through)
		String resolvePath(String const & projectRelativePath) const;
		//! @brief project-relative form of an absolute path (forward slashes);
		//! "" when the path does not live inside the project root
		String makeProjectRelative(String const & absolutePath) const;

		//! @brief all scene files under scenes/ (recursive), as sorted
		//! project-relative paths ("scenes/main.oscene"); empty when the
		//! directory does not exist (yet)
		StringVector listScenes() const;

		//--- settings (string key/values, forward-compat) ----
		String getSetting(String const & key, String const & defaultValue = "") const;
		void setSetting(String const & key, String const & value);
		bool hasSetting(String const & key) const;
		std::map<String, String> const & getSettings() const { return mSettings; }
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__Project_h__7_7_2026__12_00_00__
