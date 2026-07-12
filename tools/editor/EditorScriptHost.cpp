// EditorScriptHost - editor-tool execution (see the header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorScriptHost.h"

#include "EditorApp.h"				// EditorConsole + ConsoleLevel
#include "EditorControlServer.h"	// EditorControlServer + EditorControlContext
#include "EditorCore.h"

#include <core_script/ScriptRuntime.h>
#include <core_debugnet/DebugProtocol.h>

namespace Orkige
{
	namespace
	{
		//! the CURATED editor-verb surface a tool reaches through `editor.<verb>`
		//! - each maps 1:1 onto an MCP tool name, so a tool and an agent author
		//! the scene through the exact same vocabulary (the shared-surface story).
		//! Play / test-runner / export / runtime-debug verbs are deliberately
		//! omitted: an editor tool is a one-shot SCENE-AUTHORING command, not a
		//! run/debug driver. An un-listed name is simply nil in the sandbox (an
		//! honest error), so the surface stays intentional.
		char const* const kEditorVerbs[] = {
			// reads
			"get_state", "list_hierarchy", "get_object", "get_component",
			"list_addable_components", "list_assets", "list_paintable_assets",
			"list_paint_prefabs", "read_project_file", "list_project_files",
			// object CRUD
			"create_object", "delete_object", "duplicate_object",
			"rename_object", "reparent_object", "set_active",
			// component CRUD (script components ride the SAME generic verbs)
			"add_component", "remove_component", "set_component",
			// selection
			"select",
			// 2D grid painting
			"paint_asset", "paint_prefab", "erase_cell",
			// scene lifecycle
			"save_scene", "open_scene", "new_scene",
			// project-file authoring + prefabs + levels
			"write_project_file", "import_asset", "create_prefab",
			"instantiate_prefab", "add_scene_to_levels",
		};
	}
	//---------------------------------------------------------
	EditorScriptHost::EditorScriptHost()
	{
	}
	//---------------------------------------------------------
	EditorScriptHost::~EditorScriptHost()
	{
	}
	//---------------------------------------------------------
	bool EditorScriptHost::scriptingAvailable()
	{
		return ScriptRuntime::available();
	}
	//---------------------------------------------------------
	void EditorScriptHost::scanProject(String const& scriptsDirectory)
	{
		mTools = scanEditorTools(scriptsDirectory);
	}
	//---------------------------------------------------------
	void EditorScriptHost::clear()
	{
		mTools.clear();
	}
	//---------------------------------------------------------
	EditorScriptTool const* EditorScriptHost::findByName(String const& name) const
	{
		for (EditorScriptTool const& tool : mTools)
		{
			if (tool.name == name)
			{
				return &tool;
			}
		}
		return nullptr;
	}
	//---------------------------------------------------------
	void EditorScriptHost::logToConsole(String const& text)
	{
		if (mContext && mContext->console)
		{
			mContext->console->addLine(ConsoleLevel::Info, text);
		}
	}
	//---------------------------------------------------------
	bool EditorScriptHost::dispatchVerb(String const& verb,
		ScriptValueMap const& args, ScriptValueMap& reply, String& error)
	{
		if (!mDispatcher || !mContext)
		{
			error = "editor." + verb + ": no active editor context";
			return false;
		}
		DebugMessage request(verb);
		for (auto const& field : args.fields)
		{
			request.set(field.first, field.second);
		}
		for (auto const& list : args.lists)
		{
			request.setList(list.first, list.second);
		}
		DebugMessage out;
		const bool ok =
			mDispatcher->dispatchLocalVerb(request, *mContext, out);
		for (auto const& field : out.fields)
		{
			reply.fields[field.first] = field.second;
		}
		for (auto const& list : out.lists)
		{
			reply.lists[list.first] = list.second;
		}
		if (!ok)
		{
			error = out.get(DebugProtocol::FIELD_MESSAGE);
			if (error.empty())
			{
				error = "editor." + verb + " was refused";
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	void EditorScriptHost::ensureEditorApi()
	{
		if (mApiRegistered || !ScriptRuntime::available())
		{
			return;
		}
		ScriptRuntime& runtime = ScriptRuntime::getSingleton();
		for (char const* verbName : kEditorVerbs)
		{
			const String verb = verbName;
			runtime.registerHostFunction("editor", verbName,
				[this, verb](ScriptValueMap const& args, ScriptValueMap& reply,
					String& error) -> bool
				{
					return this->dispatchVerb(verb, args, reply, error);
				});
		}
		// editor.log(text): write a line to the Console (not a verb - it is the
		// tool's own trace, not a scene mutation)
		runtime.registerFunction("editor", "log",
			[this](String const& text)
			{
				this->logToConsole("[tool] " + text);
			});
		mApiRegistered = true;
	}
	//---------------------------------------------------------
	EditorScriptHost::RunResult EditorScriptHost::runTool(
		EditorScriptTool const& tool, EditorControlContext const& context)
	{
		RunResult result;
		mContext = &context;
		if (!ScriptRuntime::available())
		{
			result.error =
				"scripting is disabled in this build (ORKIGE_SCRIPTING=OFF)";
			this->logToConsole("[tool] " + tool.label + ": " + result.error);
			mContext = nullptr;
			return result;
		}
		if (!mDispatcher || !context.core)
		{
			result.error = "editor script host is not wired to a dispatcher";
			mContext = nullptr;
			return result;
		}
		this->ensureEditorApi();
		ScriptRuntime& runtime = ScriptRuntime::getSingleton();
		EditorCore& core = *context.core;

		this->logToConsole("[tool] running '" + tool.label + "' (" +
			tool.name + ")");

		// bracket the whole run in ONE undo step; a script error rolls it back
		core.beginScriptTransaction();
		String error;
		bool loaded = false;
		{
			// owner-tag any (hypothetical) events.subscribe the run makes so it
			// could not outlive the one-shot. The game-runtime bus is not even
			// installed in the editor process, so this is belt-and-suspenders.
			ScriptCallScope ownerScope(this);
			// loading the file RUNS its top-level chunk in a fresh sandbox - that
			// IS the one-shot execution; the sandbox drops when the instance does
			optr<ScriptInstance> instance =
				runtime.loadScriptInstance(tool.path, &error);
			loaded = static_cast<bool>(instance);
		}
		const std::size_t count = core.endScriptTransaction(loaded,
			"Run Tool: " + tool.label);

		result.ran = true;
		result.ok = loaded;
		result.commandCount = count;
		if (!loaded)
		{
			result.error = error;
			this->logToConsole("[tool] '" + tool.label + "' FAILED: " + error);
			this->logToConsole("[tool] rolled back - no partial edits applied");
		}
		else
		{
			this->logToConsole("[tool] '" + tool.label + "' done: " +
				std::to_string(count) + " undoable change(s) folded into one "
				"undo step");
		}
		mContext = nullptr;
		return result;
	}
	//---------------------------------------------------------
	EditorScriptHost::RunResult EditorScriptHost::runToolByName(
		String const& name, EditorControlContext const& context)
	{
		EditorScriptTool const* tool = this->findByName(name);
		if (!tool)
		{
			RunResult result;
			result.error = "no editor tool named '" + name + "'";
			return result;
		}
		return this->runTool(*tool, context);
	}
}
