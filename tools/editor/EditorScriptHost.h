// EditorScriptHost - runs EDITOR SCRIPT TOOLS (*.editor.lua) on demand.
//
// An editor tool is a project script the human runs from the Tools menu (and an
// agent runs over MCP with run_editor_script): it executes ONCE in a fresh
// editor-side Lua sandbox whose `editor.*` table routes through the SAME verb
// handler the MCP endpoint uses (EditorControlServer::dispatchLocalVerb) - so a
// tool authors the scene through exactly the surface an agent already drives.
//
// The whole run is bracketed in ONE EditorCore script transaction, so a tool's
// edits collapse into a SINGLE undo step; a tool that errors is ROLLED BACK,
// leaving no partial edits, and the error (with its file:line) is logged to the
// Console.
//
// Editor tools are one-shot: the editor never ticks components, runs no update
// loop and delivers no events. The game-runtime Lua tables (world / events /
// tween / ...) are NOT installed in the editor process (it never calls
// ScriptComponent::ensureScriptApi), so the message bus is simply ABSENT from an
// editor-script sandbox - a tool that references `events` gets an honest nil
// error. A ScriptCallScope still brackets the run so that even a future change
// installing the bus could not let a stray subscription outlive the one-shot.
//
// Discovery (the *.editor.lua scan + label parsing) is the pure EditorScriptTools
// TU in orkige_editor_core; this shell adds the sandbox + the editor.* verbs.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include "EditorScriptTools.h"

#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	class EditorControlServer;
	struct EditorControlContext;
	struct ScriptValueMap;	//!< core_script/ScriptRuntime.h (verb args/reply)

	//! @brief the editor's script-tool host: discovers *.editor.lua tools in the
	//! open project and runs one on demand in a sandbox whose `editor.*` table
	//! rides the verb handler. One instance, owned by the editor shell.
	class EditorScriptHost
	{
	public:
		//! outcome of runTool
		struct RunResult
		{
			bool ran = false;			//!< the file was found and executed
			bool ok = false;			//!< executed without a script error
			String error;				//!< the script error (file:line ...) when !ok
			std::size_t commandCount = 0;	//!< undoable commands folded (committed) / rolled back
		};

		EditorScriptHost();
		~EditorScriptHost();

		//! @brief the verb dispatcher the editor.* table routes through (the
		//! editor's EditorControlServer, reused purely as a synchronous verb
		//! executor - it need not be listening). Set once at boot.
		void setDispatcher(EditorControlServer* dispatcher)
		{
			mDispatcher = dispatcher;
		}

		//! (re)scan the open project's scripts directory (absolute) for tools.
		//! Safe to call on project open and on any scripts/ write.
		void scanProject(String const& scriptsDirectory);
		//! forget every discovered tool (project close/switch)
		void clear();
		//! the discovered tools, sorted by label (the Tools menu source)
		std::vector<EditorScriptTool> const& tools() const { return mTools; }
		bool empty() const { return mTools.empty(); }
		//! find a tool by its stable name (nullptr when unknown)
		EditorScriptTool const* findByName(String const& name) const;

		//! is a scripting backend available (false in ORKIGE_SCRIPTING=OFF
		//! builds - the Tools menu then shows a disabled note and running is an
		//! honest no-op)
		static bool scriptingAvailable();

		//! @brief run one tool once in a fresh sandbox, bracketed in ONE undo
		//! step; on a script error the run is rolled back (no partial edits) and
		//! the error is reported. `context` supplies the objects the editor.*
		//! verbs bridge to (and the Console the tool's editor.log writes to).
		RunResult runTool(EditorScriptTool const& tool,
			EditorControlContext const& context);
		//! @brief run a tool by its stable name (RunResult.ran is false with an
		//! error when no such tool exists)
		RunResult runToolByName(String const& name,
			EditorControlContext const& context);

	private:
		//! register the `editor.*` host table on the ScriptRuntime once (the
		//! closures capture `this`; the per-run context lives in mContext)
		void ensureEditorApi();
		//! @brief run one editor verb through the dispatcher during a tool run:
		//! convert the script's `args` map into a verb request, dispatch it, and
		//! convert the reply back into `reply`. Returns false with `error` set
		//! (which the seam raises as a Lua error at the call site) when the verb
		//! is refused/fails or no dispatch context is active.
		bool dispatchVerb(String const& verb, ScriptValueMap const& args,
			ScriptValueMap& reply, String& error);
		//! write a line to the running tool's Console (editor.log) - no-op with
		//! no active context
		void logToConsole(String const& text);

		std::vector<EditorScriptTool> mTools;
		EditorControlServer* mDispatcher = nullptr;
		//! set for the duration of a runTool call (the editor.* closures read it)
		EditorControlContext const* mContext = nullptr;
		bool mApiRegistered = false;
	};
}
