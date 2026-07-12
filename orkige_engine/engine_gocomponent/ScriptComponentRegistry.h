/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	ScriptComponentRegistry.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptComponentRegistry_h__12_7_2026__10_00_00__
#define __ScriptComponentRegistry_h__12_7_2026__10_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "core_util/String.h"

#include <map>

namespace Orkige
{
	//! @brief the project-scoped catalogue of SCRIPT COMPONENT KINDS: every
	//! behavior script whose file name ends in ".component.lua" is an attachable
	//! component whose KIND NAME is the file's base name
	//! (scripts/player.component.lua -> kind "player"). Plain ".lua" files are
	//! libraries/helpers and never attachable.
	//!
	//! @remarks The registry turns each discovered kind into a factory ALIAS for
	//! the one ScriptComponent class (GameObject::registerComponentAlias): so
	//! GameObject::addComponent(TypeInfo("player")) creates a ScriptComponent
	//! that binds itself to scripts/player.component.lua, isComponentRegistered
	//! /getRegisteredComponentTypes report the kind, and several DIFFERENT script
	//! kinds coexist on one object because each has its own container key while
	//! sharing the class. A ScriptComponent created under a kind key resolves its
	//! script file back through scriptFileForComponent (@see
	//! ScriptComponent::onAdd, which reads the container key).
	//!
	//! Discovery is a plain directory walk (no scripting backend needed), so it
	//! also runs in ORKIGE_SCRIPTING=OFF builds - a scene carrying script
	//! components then still LOADS; the components are simply inert (their
	//! ScriptRuntime load fails honestly). The runtimes and the editor rescan on
	//! project open; clear() drops every alias on project close/switch.
	//!
	//! A process-wide instance (the component factory it registers into is
	//! process-wide too); scanning is idempotent (clear + rebuild). Kind-name
	//! collisions are refused, keeping the name space honest: a script whose kind
	//! name equals a real C++ component type ("TransformComponent") is skipped,
	//! and the first of two scripts sharing a base name wins (both logged).
	class ORKIGE_ENGINE_DLL ScriptComponentRegistry
	{
		//--- Types -------------------------------------------
	public:
		//--- Variables ---------------------------------------
	private:
		//! kind name -> project-relative script path (forward slashes)
		std::map<String, String>	mNameToFile;
		//--- Methods -----------------------------------------
	public:
		//! the process-wide registry (self-initialising - no boot wiring)
		static ScriptComponentRegistry& getSingleton();
		//! @see getSingleton (never NULL)
		static ScriptComponentRegistry* getSingletonPtr();

		//! @brief (re)scan `scriptsDirectory` (absolute) recursively for
		//! "*.component.lua" files: rebuild the kind->file map and (re)register a
		//! factory alias per kind. Stored paths are RELATIVE to `projectRoot`
		//! (they resolve like any script through ScriptRuntime::resolveScriptPath).
		//! Clears the previous registration first, so it is safe to call on every
		//! project open. An empty/missing directory clears the registry.
		void scanProject(String const & scriptsDirectory,
			String const & projectRoot);

		//! forget every discovered kind and unregister its factory alias
		void clear();

		//! is `name` a discovered script component kind
		bool isScriptComponent(String const & name) const;
		//! the project-relative script file for a kind ("" when unknown)
		String scriptFileForComponent(String const & name) const;
		//! every discovered kind name, sorted (the Add Component / MCP list)
		StringVector componentNames() const;
		//! how many kinds are registered
		inline std::size_t count() const { return this->mNameToFile.size(); }

		//! the filename suffix that marks a script as a component kind
		static char const * componentSuffix();
		//! @brief the kind name for a file name/path: its base name with the
		//! ".component.lua" suffix stripped; "" when the name is not a
		//! "*.component.lua" file (a plain library script or any other file)
		static String componentNameForFile(String const & fileName);
	protected:
	private:
		//! only getSingleton constructs the process-wide instance
		ScriptComponentRegistry();
		//! non-copyable (there is one registry)
		ScriptComponentRegistry(ScriptComponentRegistry const &) = delete;
		ScriptComponentRegistry & operator=(ScriptComponentRegistry const &) = delete;
	};
}

#endif //__ScriptComponentRegistry_h__12_7_2026__10_00_00__
