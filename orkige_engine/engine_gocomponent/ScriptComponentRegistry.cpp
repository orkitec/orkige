/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	ScriptComponentRegistry.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/ScriptComponentRegistry.h"
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_base/EngineLog.h"
#include <core_game/GameObject.h>

#include <algorithm>
#include <filesystem>

namespace Orkige
{
	namespace
	{
		//! the marker suffix: a behavior script is an attachable component KIND
		//! iff its file name ends in this
		const char * const kComponentSuffix = ".component.lua";
	}
	//---------------------------------------------------------
	ScriptComponentRegistry::ScriptComponentRegistry()
	{
	}
	//---------------------------------------------------------
	ScriptComponentRegistry& ScriptComponentRegistry::getSingleton()
	{
		return *ScriptComponentRegistry::getSingletonPtr();
	}
	//---------------------------------------------------------
	ScriptComponentRegistry* ScriptComponentRegistry::getSingletonPtr()
	{
		// a function-local static: self-initialising in every process (editor,
		// player, tests) with no boot ordering to get wrong, and torn down after
		// main() returns (the destructor deliberately does NOT touch the factory,
		// which may already be gone at that point - clear() is a runtime call)
		static ScriptComponentRegistry instance;
		return &instance;
	}
	//---------------------------------------------------------
	char const * ScriptComponentRegistry::componentSuffix()
	{
		return kComponentSuffix;
	}
	//---------------------------------------------------------
	String ScriptComponentRegistry::componentNameForFile(String const & fileName)
	{
		// reduce a path to its base file name first, so "scripts/a/player.component.lua"
		// and "player.component.lua" derive the same kind
		const String base = std::filesystem::path(fileName).filename().string();
		const String suffix = kComponentSuffix;
		if (base.size() <= suffix.size())
		{
			return "";
		}
		if (base.compare(base.size() - suffix.size(), suffix.size(), suffix) != 0)
		{
			return "";	// a plain .lua library or any other file - not a kind
		}
		return base.substr(0, base.size() - suffix.size());
	}
	//---------------------------------------------------------
	void ScriptComponentRegistry::scanProject(String const & scriptsDirectory,
		String const & projectRoot)
	{
		// idempotent: drop the previous project's kinds (and their factory
		// aliases) before rebuilding, so a project switch never leaks kinds
		this->clear();
		if (scriptsDirectory.empty())
		{
			return;
		}
		std::error_code ec;
		const std::filesystem::path root =
			projectRoot.empty() ? std::filesystem::path(scriptsDirectory)
								 : std::filesystem::path(projectRoot);
		if (!std::filesystem::is_directory(scriptsDirectory, ec))
		{
			return;	// no scripts/ folder yet - nothing to discover
		}
		for (std::filesystem::recursive_directory_iterator it(scriptsDirectory, ec), end;
			it != end && !ec; it.increment(ec))
		{
			if (!it->is_regular_file(ec))
			{
				continue;
			}
			const String fileName = it->path().filename().string();
			const String name = ScriptComponentRegistry::componentNameForFile(fileName);
			if (name.empty())
			{
				continue;	// not a *.component.lua file
			}
			// keep the name space honest: a kind may not shadow a real C++
			// component type, and two files sharing a base name would collide -
			// the first wins, both cases are logged, neither aborts the scan
			if (GameObject::isComponentRegistered(TypeInfo(name)))
			{
				EngineLogCapture::logError(String("ScriptComponentRegistry: script '") +
					fileName + "' would shadow the registered component type '" +
					name + "' - not registering it as a component kind");
				continue;
			}
			if (this->mNameToFile.find(name) != this->mNameToFile.end())
			{
				EngineLogCapture::logError(String("ScriptComponentRegistry: two "
					"scripts derive the component kind '") + name + "' - keeping '" +
					this->mNameToFile[name] + "', ignoring '" + fileName + "'");
				continue;
			}
			// store the path RELATIVE to the project root (with forward slashes),
			// so it resolves like any script through ScriptRuntime
			std::filesystem::path relative =
				std::filesystem::relative(it->path(), root, ec);
			String relativePath = ec ? it->path().string() : relative.generic_string();
			this->mNameToFile[name] = relativePath;
			GameObject::registerComponentAlias<ScriptComponent>(TypeInfo(name));
		}
		oDebugMsg("script", 0, "ScriptComponentRegistry: " << this->mNameToFile.size()
			<< " script component kind(s) discovered under '" << scriptsDirectory << "'");
	}
	//---------------------------------------------------------
	void ScriptComponentRegistry::clear()
	{
		for (std::map<String, String>::const_iterator it = this->mNameToFile.begin();
			it != this->mNameToFile.end(); ++it)
		{
			GameObject::unregisterComponentType(TypeInfo(it->first));
		}
		this->mNameToFile.clear();
	}
	//---------------------------------------------------------
	bool ScriptComponentRegistry::isScriptComponent(String const & name) const
	{
		return this->mNameToFile.find(name) != this->mNameToFile.end();
	}
	//---------------------------------------------------------
	String ScriptComponentRegistry::scriptFileForComponent(String const & name) const
	{
		std::map<String, String>::const_iterator it = this->mNameToFile.find(name);
		return it != this->mNameToFile.end() ? it->second : String("");
	}
	//---------------------------------------------------------
	StringVector ScriptComponentRegistry::componentNames() const
	{
		StringVector names;
		names.reserve(this->mNameToFile.size());
		for (std::map<String, String>::const_iterator it = this->mNameToFile.begin();
			it != this->mNameToFile.end(); ++it)
		{
			names.push_back(it->first);
		}
		std::sort(names.begin(), names.end());
		return names;
	}
	//---------------------------------------------------------
}
