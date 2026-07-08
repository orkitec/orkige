/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	Project.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <tinyxml2.h>

#include "core_project/Project.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace Orkige
{
	const int Project::PROJECT_FORMAT_VERSION = 1;
	const String Project::MANIFEST_FILE_NAME = "project.orkproj";
	const String Project::SCENES_DIR_NAME = "scenes";
	const String Project::ASSETS_DIR_NAME = "assets";
	const String Project::SCRIPTS_DIR_NAME = "scripts";
	const String Project::SCENE_FILE_EXTENSION = ".oscene";
	const String Project::RESOURCE_GROUP_NAME = "OrkigeProject";
	//---------------------------------------------------------
	namespace
	{
		//! deposit an error message when the caller asked for one
		void projectError(String * errorMessage, String const & text)
		{
			if (errorMessage)
			{
				*errorMessage = text;
			}
		}
		//! manifest XML element/attribute names
		const char * const ELEMENT_ROOT = "OrkigeProject";
		const char * const ELEMENT_NAME = "Name";
		const char * const ELEMENT_MAIN_SCENE = "MainScene";
		const char * const ELEMENT_SETTINGS = "Settings";
		const char * const ELEMENT_SETTING = "Setting";
		const char * const ATTRIBUTE_VERSION = "version";
		const char * const ATTRIBUTE_KEY = "key";
		const char * const ATTRIBUTE_VALUE = "value";
	}
	//---------------------------------------------------------
	String Project::resolveManifestPath(String const & path)
	{
		if (path.empty())
		{
			return String();
		}
		std::error_code ignored;
		const std::filesystem::path fsPath(path);
		if (std::filesystem::is_directory(fsPath, ignored))
		{
			return (fsPath / MANIFEST_FILE_NAME).string();
		}
		if (fsPath.filename().string() == MANIFEST_FILE_NAME ||
			fsPath.extension().string() == ".orkproj")
		{
			return path;
		}
		return String();
	}
	//---------------------------------------------------------
	bool Project::create(String const & rootDirectory, String const & name,
		Project & project, String * errorMessage)
	{
		if (rootDirectory.empty())
		{
			projectError(errorMessage, "no project directory given");
			return false;
		}
		std::error_code fsError;
		const std::filesystem::path root =
			std::filesystem::absolute(rootDirectory, fsError)
				.lexically_normal();
		if (std::filesystem::exists(root / MANIFEST_FILE_NAME, fsError))
		{
			projectError(errorMessage, "'" + root.string() +
				"' is already an Orkige project (it contains " +
				MANIFEST_FILE_NAME + ")");
			return false;
		}
		for (String const & subdirectory : { SCENES_DIR_NAME, ASSETS_DIR_NAME,
			SCRIPTS_DIR_NAME })
		{
			std::filesystem::create_directories(root / subdirectory, fsError);
			if (fsError)
			{
				projectError(errorMessage, "could not create '" +
					(root / subdirectory).string() + "': " + fsError.message());
				return false;
			}
		}
		Project created;
		created.mRootDirectory = root.string();
		created.mName = name.empty() ? root.filename().string() : name;
		if (created.mName.empty())
		{
			// a root like "/" has no filename to borrow
			projectError(errorMessage, "no project name given and '" +
				root.string() + "' has no directory name to use");
			return false;
		}
		created.mMainScene = SCENES_DIR_NAME + "/main" + SCENE_FILE_EXTENSION;
		if (!created.save(errorMessage))
		{
			return false;
		}
		project = created;
		return true;
	}
	//---------------------------------------------------------
	bool Project::load(String const & path, String * errorMessage)
	{
		const String manifestPath = resolveManifestPath(path);
		if (manifestPath.empty())
		{
			projectError(errorMessage, "'" + path + "' is not a project: "
				"expected a project directory or a .orkproj manifest file");
			return false;
		}
		std::error_code fsError;
		if (!std::filesystem::exists(manifestPath, fsError))
		{
			projectError(errorMessage, "no project manifest at '" +
				manifestPath + "'");
			return false;
		}
		tinyxml2::XMLDocument document;
		if (document.LoadFile(manifestPath.c_str()) != tinyxml2::XML_SUCCESS)
		{
			projectError(errorMessage, "could not parse '" + manifestPath +
				"': " + document.ErrorStr());
			return false;
		}
		tinyxml2::XMLElement * root = document.RootElement();
		if (!root || String(root->Name()) != ELEMENT_ROOT)
		{
			projectError(errorMessage, "'" + manifestPath + "' is not an "
				"Orkige project manifest (root element must be <" +
				String(ELEMENT_ROOT) + ">)");
			return false;
		}
		const int version = root->IntAttribute(ATTRIBUTE_VERSION, 0);
		if (version < 1 || version > PROJECT_FORMAT_VERSION)
		{
			projectError(errorMessage, "'" + manifestPath + "' has manifest "
				"version " + std::to_string(version) + " - this build "
				"understands 1.." + std::to_string(PROJECT_FORMAT_VERSION));
			return false;
		}
		const tinyxml2::XMLElement * nameElement =
			root->FirstChildElement(ELEMENT_NAME);
		const String name = (nameElement && nameElement->GetText())
			? nameElement->GetText() : String();
		if (name.empty())
		{
			projectError(errorMessage, "'" + manifestPath + "' has no <" +
				String(ELEMENT_NAME) + "> - every project needs a name");
			return false;
		}
		const tinyxml2::XMLElement * mainSceneElement =
			root->FirstChildElement(ELEMENT_MAIN_SCENE);
		const String mainScene = (mainSceneElement && mainSceneElement->GetText())
			? mainSceneElement->GetText() : String();
		if (!mainScene.empty() && std::filesystem::path(mainScene).is_absolute())
		{
			projectError(errorMessage, "'" + manifestPath + "' has an absolute "
				"<" + String(ELEMENT_MAIN_SCENE) + "> ('" + mainScene +
				"') - the main scene must be project-relative");
			return false;
		}
		std::map<String, String> settings;
		if (const tinyxml2::XMLElement * settingsElement =
			root->FirstChildElement(ELEMENT_SETTINGS))
		{
			for (const tinyxml2::XMLElement * setting =
				settingsElement->FirstChildElement(ELEMENT_SETTING);
				setting; setting = setting->NextSiblingElement(ELEMENT_SETTING))
			{
				const char * key = setting->Attribute(ATTRIBUTE_KEY);
				if (!key || !*key)
				{
					projectError(errorMessage, "'" + manifestPath + "' has a <" +
						String(ELEMENT_SETTING) + "> without a key attribute");
					return false;
				}
				const char * value = setting->Attribute(ATTRIBUTE_VALUE);
				settings[key] = value ? value : "";
			}
		}
		// everything validated - only now touch this instance
		mRootDirectory = std::filesystem::absolute(
			std::filesystem::path(manifestPath).parent_path(), fsError)
				.lexically_normal().string();
		mName = name;
		mMainScene = mainScene;
		mSettings = std::move(settings);
		return true;
	}
	//---------------------------------------------------------
	bool Project::save(String * errorMessage) const
	{
		if (!isLoaded())
		{
			projectError(errorMessage, "no project loaded - nothing to save");
			return false;
		}
		if (mName.empty())
		{
			projectError(errorMessage, "the project has no name - refusing to "
				"save an invalid manifest");
			return false;
		}
		tinyxml2::XMLDocument document;
		document.InsertEndChild(document.NewDeclaration());
		tinyxml2::XMLElement * root = document.NewElement(ELEMENT_ROOT);
		root->SetAttribute(ATTRIBUTE_VERSION, PROJECT_FORMAT_VERSION);
		document.InsertEndChild(root);
		tinyxml2::XMLElement * nameElement = document.NewElement(ELEMENT_NAME);
		nameElement->SetText(mName.c_str());
		root->InsertEndChild(nameElement);
		if (!mMainScene.empty())
		{
			tinyxml2::XMLElement * mainSceneElement =
				document.NewElement(ELEMENT_MAIN_SCENE);
			mainSceneElement->SetText(mMainScene.c_str());
			root->InsertEndChild(mainSceneElement);
		}
		if (!mSettings.empty())
		{
			tinyxml2::XMLElement * settingsElement =
				document.NewElement(ELEMENT_SETTINGS);
			root->InsertEndChild(settingsElement);
			for (auto const & [key, value] : mSettings)
			{
				tinyxml2::XMLElement * settingElement =
					document.NewElement(ELEMENT_SETTING);
				settingElement->SetAttribute(ATTRIBUTE_KEY, key.c_str());
				settingElement->SetAttribute(ATTRIBUTE_VALUE, value.c_str());
				settingsElement->InsertEndChild(settingElement);
			}
		}
		const String manifestPath = (std::filesystem::path(mRootDirectory) /
			MANIFEST_FILE_NAME).string();
		if (document.SaveFile(manifestPath.c_str()) != tinyxml2::XML_SUCCESS)
		{
			projectError(errorMessage, "could not write '" + manifestPath +
				"': " + document.ErrorStr());
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	void Project::close()
	{
		mRootDirectory.clear();
		mName.clear();
		mMainScene.clear();
		mSettings.clear();
	}
	//---------------------------------------------------------
	String Project::getMainScenePath() const
	{
		if (mMainScene.empty())
		{
			return String();
		}
		return resolvePath(mMainScene);
	}
	//---------------------------------------------------------
	String Project::getScenesDirectory() const
	{
		return resolvePath(SCENES_DIR_NAME);
	}
	//---------------------------------------------------------
	String Project::getAssetsDirectory() const
	{
		return resolvePath(ASSETS_DIR_NAME);
	}
	//---------------------------------------------------------
	String Project::getScriptsDirectory() const
	{
		return resolvePath(SCRIPTS_DIR_NAME);
	}
	//---------------------------------------------------------
	String Project::resolvePath(String const & projectRelativePath) const
	{
		if (!isLoaded())
		{
			return String();
		}
		const std::filesystem::path relative(projectRelativePath);
		if (relative.is_absolute())
		{
			return projectRelativePath;
		}
		return (std::filesystem::path(mRootDirectory) / relative)
			.lexically_normal().string();
	}
	//---------------------------------------------------------
	String Project::makeProjectRelative(String const & absolutePath) const
	{
		if (!isLoaded() || absolutePath.empty())
		{
			return String();
		}
		std::error_code fsError;
		const std::filesystem::path absolute = std::filesystem::absolute(
			absolutePath, fsError).lexically_normal();
		const std::filesystem::path relative =
			absolute.lexically_relative(mRootDirectory);
		if (relative.empty() || relative.native().starts_with(".."))
		{
			return String(); // outside the project root
		}
		return relative.generic_string();
	}
	//---------------------------------------------------------
	StringVector Project::listScenes() const
	{
		StringVector scenes;
		if (!isLoaded())
		{
			return scenes;
		}
		std::error_code fsError;
		const std::filesystem::path scenesDirectory(getScenesDirectory());
		if (!std::filesystem::is_directory(scenesDirectory, fsError))
		{
			return scenes;
		}
		for (std::filesystem::recursive_directory_iterator
			iterator(scenesDirectory, fsError), end;
			!fsError && iterator != end; iterator.increment(fsError))
		{
			if (iterator->is_regular_file(fsError) &&
				iterator->path().extension().string() == SCENE_FILE_EXTENSION)
			{
				scenes.push_back(makeProjectRelative(iterator->path().string()));
			}
		}
		std::sort(scenes.begin(), scenes.end());
		return scenes;
	}
	//---------------------------------------------------------
	String Project::getSetting(String const & key,
		String const & defaultValue) const
	{
		const std::map<String, String>::const_iterator found =
			mSettings.find(key);
		return (found != mSettings.end()) ? found->second : defaultValue;
	}
	//---------------------------------------------------------
	void Project::setSetting(String const & key, String const & value)
	{
		mSettings[key] = value;
	}
	//---------------------------------------------------------
	bool Project::hasSetting(String const & key) const
	{
		return mSettings.find(key) != mSettings.end();
	}
	//---------------------------------------------------------
}
