// EditorConsole.cpp - the editor Console: engine/editor/remote log line
// store pump, the SDL log output hook, the Lua REPL and the Console panel.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorTabMenu.h"
#include "EditorTheme.h"
#include "ExternalEditor.h"

#include <core_debug/CVarCommand.h>
#include <core_debugnet/DebugProtocol.h>
#include <core_script/ScriptRuntime.h>

#include <algorithm>
#include <string>
#include <vector>

//! per-frame pump: move the engine log lines the EngineLogCapture service
//! collected since the last frame into the Console (severity string ->
//! Console level; the service replaced the editor's own Ogre::LogListener,
//! sharing one capture implementation with PlayerDebugLink)
void drainEngineLogIntoConsole(Orkige::EngineLogCapture& capture,
	EditorConsole& console)
{
	for (Orkige::EngineLogCapture::Line const& line : capture.drain())
	{
		ConsoleLevel level = ConsoleLevel::Info;
		if (line.level == "warning")
		{
			level = ConsoleLevel::Warning;
		}
		else if (line.level == "error")
		{
			level = ConsoleLevel::Error;
		}
		// a RECOVERED render-backend probe, not a failure: the shader
		// preprocessor first asks the resource system for an #include, and
		// for a system header (metal_stdlib) that lookup throws, is caught,
		// and the compiler resolves the header itself - the backend logs the
		// caught exception at error level anyway, so it is downgraded here
		// to keep the Console's red lines meaningful
		if (level == ConsoleLevel::Error &&
			line.text.find("FileNotFoundException") != std::string::npos &&
			line.text.find("metal_stdlib") != std::string::npos)
		{
			level = ConsoleLevel::Info;
		}
		console.addLine(level, line.text);
	}
}

void SDLCALL consoleSdlLogOutput(void* userdata, int category,
	SDL_LogPriority priority, const char* message)
{
	SdlLogHook* hook = static_cast<SdlLogHook*>(userdata);
	ConsoleLevel level = ConsoleLevel::Info;
	if (priority >= SDL_LOG_PRIORITY_ERROR)
	{
		level = ConsoleLevel::Error;
	}
	else if (priority == SDL_LOG_PRIORITY_WARN)
	{
		level = ConsoleLevel::Warning;
	}
	if (hook->console)
	{
		hook->console->addLine(level, message);
	}
	if (hook->previous)
	{
		hook->previous(hook->previousUserdata, category, priority, message);
	}
}

// run the console buffer through the ScriptRuntime seam, capture returns/
// errors - in a no-scripting build this reports the honest "scripting
// disabled" error instead of not existing
void runLuaConsoleInput(EditorState& state, PlaySession& session)
{
	std::string code(state.luaInput);

	// a set/get/find/reset line drives the cvar command grammar, not Lua (the
	// stable, scripting-free console surface - see core_debug/CVarCommand.h)
	if (Orkige::CVarCommand::isCommand(code))
	{
		namespace Protocol = Orkige::DebugProtocol;
		state.luaHistory.push_back("> " + code);
		Orkige::String cvarName;
		Orkige::String cvarValue;
		if (session.client.isConnected() &&
			Orkige::CVarCommand::parseSet(code, cvarName, cvarValue))
		{
			// during Play a `set` tunes the RUNNING game: send it over the
			// debug link (the player applies it via CVarManager::setString and
			// answers with an error message on a bad name/value)
			Orkige::DebugMessage message(Protocol::MSG_SET_CVAR);
			message.set(Protocol::FIELD_CVAR_NAME, cvarName);
			message.set(Protocol::FIELD_VALUE, cvarValue);
			session.client.send(message);
			state.luaHistory.push_back("[remote] set " + cvarName + " = " +
				cvarValue);
		}
		else
		{
			// not playing (or a get/find/reset): run against the editor's own
			// registry - informational, and the offline-tuning path
			state.luaHistory.push_back(Orkige::CVarCommand::run(code));
		}
		state.luaScrollToBottom = true;
		return;
	}

	state.luaHistory.push_back("lua> " + code);
	const Orkige::ScriptRuntime::Result result =
		Orkige::ScriptRuntime::getSingleton().runString(code);
	if (!result.success)
	{
		state.luaHistory.push_back("error: " + result.error);
	}
	else if (result.returnValues.empty())
	{
		state.luaHistory.push_back("ok");
	}
	else
	{
		std::string out;
		for (std::size_t i = 0; i < result.returnValues.size(); ++i)
		{
			if (i > 0)
			{
				out += "\t";
			}
			out += result.returnValues[i];
		}
		state.luaHistory.push_back(out);
	}
	state.luaScrollToBottom = true;
}

namespace
{

//! one deferred console-reference action: a click parsed a file:line reference
//! and asked to open it (or peek it). Executed AFTER the console mutex is
//! released so a file read / process spawn never stalls the log appenders.
struct PendingRefAction
{
	enum class Kind { None, Open, Peek } kind = Kind::None;
	Orkige::FileLineRef ref;
};

//! resolve a parsed console reference to an absolute path (project-relative refs
//! resolve against the open project root) and open it in the EMBEDDED code
//! editor at that line (the context menu keeps the external-editor route)
void openConsoleRef(EditorState& state, Orkige::FileLineRef const& ref)
{
	const std::string absolute =
		resolveProjectFilePath(state.project, ref.path);
	if (gViewSettings)
	{
		scriptPanelOpenFile(state, *gViewSettings, absolute, ref.line);
	}
}

//! fill the transient quick-peek payload for a console reference (~20 source
//! lines around the target, the target line highlighted) and request the popup
void requestConsolePeek(EditorState& state, Orkige::FileLineRef const& ref)
{
	const std::string absolute =
		resolveProjectFilePath(state.project, ref.path);
	int firstLine = 0;
	state.peekLines =
		Orkige::readFileLinesAround(absolute, ref.line, 10, firstLine);
	state.peekFirstLine = firstLine;
	state.peekTargetLine = ref.line;
	state.peekTitle = ref.path + ":" + std::to_string(ref.line);
	state.openPeekPopup = true;
}

// the Console's Log tab: engine (Ogre) log + editor SDL_Log lines +
// [remote] player lines, severity-coloured, with clear / auto-scroll /
// text filter controls. A line carrying a "file:line" reference (a Lua error, a
// [build] compiler diagnostic) is clickable: left-click opens it in the
// external editor, Alt/Option-click (or the right-click menu) peeks it inline.
void drawConsoleLogTab(EditorState& state, EditorConsole& console)
{
	if (ImGui::Button("Clear"))
	{
		console.clear();
	}
	ImGui::SameLine();
	// one clipboard block: the selected lines when a selection exists, else
	// the whole filtered view (per-line copy lives in the right-click menu) -
	// ImGui's clipboard is the system clipboard through the SDL backend
	bool copyRequested = ImGui::Button("Copy");
	ImGui::SetItemTooltip(
		"copy the selected lines (else everything shown) to the clipboard\n"
		"select: click \xC2\xB7 shift-click range \xC2\xB7 drag \xC2\xB7 "
		"Cmd/Ctrl-click toggle \xC2\xB7 Cmd/Ctrl+C copies");
	ImGui::SameLine();
	Orkige::compactCheckbox("Auto-scroll", &console.autoScroll);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	console.filter.Draw("##consolefilter");
	ImGui::SetItemTooltip("filter (\"incl,-excl\" syntax)");
	// a click is deferred past the mutex-guarded loop (see PendingRefAction)
	PendingRefAction pending;
	if (ImGui::BeginChild("##consolelines", ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar))
	{
		std::lock_guard<std::mutex> lock(console.mutex);
		const ImGuiIO& io = ImGui::GetIO();
		// Cmd/Ctrl+C on the focused log doubles as the Copy button
		copyRequested = copyRequested || (ImGui::IsWindowFocused() &&
			(io.KeyCtrl || io.KeySuper) &&
			ImGui::IsKeyPressed(ImGuiKey_C, false));
		if (copyRequested)
		{
			const bool haveSelection = std::any_of(console.lines.begin(),
				console.lines.end(),
				[](ConsoleLine const& l) { return l.selected; });
			std::string block;
			for (ConsoleLine const& copyLine : console.lines)
			{
				if (haveSelection ? copyLine.selected
					: console.filter.PassFilter(copyLine.text.c_str()))
				{
					block += copyLine.text;
					block += '\n';
				}
			}
			ImGui::SetClipboardText(block.c_str());
		}
		// a range operation (shift-click / drag) selects what the user SEES:
		// only lines passing the live filter join the range
		auto selectRange = [&console](int anchor, int index)
		{
			const int lo = std::min(anchor, index);
			const int hi = std::max(anchor, index);
			for (int k = 0; k < static_cast<int>(console.lines.size()); ++k)
			{
				console.lines[k].selected = k >= lo && k <= hi &&
					console.filter.PassFilter(console.lines[k].text.c_str());
			}
		};
		int lineIndex = 0;
		int fullIndex = -1;
		for (ConsoleLine& line : console.lines)
		{
			++fullIndex;
			++lineIndex;
			if (!console.filter.PassFilter(line.text.c_str()))
			{
				continue;
			}
			switch (line.level)
			{
			case ConsoleLevel::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text,
					Orkige::editorWarningTextColor());
				break;
			case ConsoleLevel::Error:
				ImGui::PushStyleColor(ImGuiCol_Text,
					Orkige::editorErrorTextColor());
				break;
			case ConsoleLevel::Info:
			default:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImGui::GetStyleColorVec4(ImGuiCol_Text));
				break;
			}
			// a full-row Selectable carries the selection highlight; the text
			// draws on top so lines keep their level colour (and any "##"/"%"
			// sequences in log text never reach a widget's label parsing)
			ImGui::PushID(lineIndex);
			const ImVec2 textPos = ImGui::GetCursorScreenPos();
			const float textWidth = ImGui::CalcTextSize(line.text.c_str()).x;
			ImGui::Selectable("##line", line.selected, ImGuiSelectableFlags_None,
				ImVec2(std::max(textWidth,
					ImGui::GetContentRegionAvail().x), 0.0f));
			ImGui::GetWindowDrawList()->AddText(textPos,
				ImGui::GetColorU32(ImGuiCol_Text), line.text.c_str());
			// selection gestures: click selects, shift-click extends from the
			// anchor, Cmd/Ctrl-click toggles, holding the button and dragging
			// sweeps the range under the cursor
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				if (io.KeyShift && console.selectionAnchor >= 0)
				{
					selectRange(console.selectionAnchor, fullIndex);
				}
				else if (io.KeyCtrl || io.KeySuper)
				{
					line.selected = !line.selected;
					console.selectionAnchor = fullIndex;
				}
				else
				{
					for (ConsoleLine& other : console.lines)
					{
						other.selected = false;
					}
					line.selected = true;
					console.selectionAnchor = fullIndex;
				}
			}
			else if (console.selectionAnchor >= 0 && ImGui::IsItemHovered() &&
				ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				selectRange(console.selectionAnchor, fullIndex);
			}
			// clickable file:line references (the FIRST one on the line);
			// opening moved to DOUBLE-click so a plain click can select
			const std::vector<Orkige::FileLineRef> refs =
				Orkige::parseFileLineRefs(line.text);
			if (!refs.empty())
			{
				Orkige::FileLineRef const& ref = refs.front();
				if (ImGui::IsItemHovered())
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					// underline the line as the clickable affordance
					const ImVec2 mn = ImGui::GetItemRectMin();
					const ImVec2 mx = ImGui::GetItemRectMax();
					ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y),
						ImVec2(mx.x, mx.y), ImGui::GetColorU32(ImGuiCol_Text));
					ImGui::SetTooltip("%s:%d\ndouble-click: open in editor  "
						"\xC2\xB7  Alt-click / right-click: peek",
						ref.path.c_str(), ref.line);
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						pending.kind = PendingRefAction::Kind::Open;
						pending.ref = ref;
					}
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && io.KeyAlt)
				{
					pending.kind = PendingRefAction::Kind::Peek;
					pending.ref = ref;
				}
			}
			// every line answers a right-click with a copy menu; lines that
			// carry a file:line reference add the open/peek actions
			if (ImGui::BeginPopupContextItem("##consoleline"))
			{
				if (ImGui::MenuItem("Copy Line"))
				{
					ImGui::SetClipboardText(line.text.c_str());
				}
				if (!refs.empty())
				{
					ImGui::Separator();
					if (ImGui::MenuItem("Open in External Editor"))
					{
						pending.kind = PendingRefAction::Kind::Open;
						pending.ref = refs.front();
					}
					if (ImGui::MenuItem("Peek"))
					{
						pending.kind = PendingRefAction::Kind::Peek;
						pending.ref = refs.front();
					}
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
		}
		if (console.scrollToBottom)
		{
			if (console.autoScroll)
			{
				ImGui::SetScrollHereY(1.0f);
			}
			console.scrollToBottom = false;
		}
	}
	ImGui::EndChild();
	// run the deferred action now the console mutex is released
	if (pending.kind == PendingRefAction::Kind::Open)
	{
		openConsoleRef(state, pending.ref);
	}
	else if (pending.kind == PendingRefAction::Kind::Peek)
	{
		requestConsolePeek(state, pending.ref);
	}
}

// the transient quick-peek popup: a read-only window of ~20 source lines around
// a clicked console reference, the target line highlighted. A plain (non-modal)
// popup, so a click-away or Esc closes it; no editing, no dedicated panel.
void drawConsolePeekPopup(EditorState& state)
{
	if (state.openPeekPopup)
	{
		ImGui::OpenPopup("##consolepeek");
		state.openPeekPopup = false;
	}
	ImGui::SetNextWindowSize(ImVec2(560.0f, 300.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopup("##consolepeek"))
	{
		ImGui::TextDisabled("%s", state.peekTitle.c_str());
		ImGui::Separator();
		if (ImGui::BeginChild("##peeklines", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			if (state.peekLines.empty())
			{
				ImGui::TextDisabled("(could not read the file)");
			}
			for (std::size_t i = 0; i < state.peekLines.size(); ++i)
			{
				const int lineNo =
					state.peekFirstLine + static_cast<int>(i);
				const bool target = (lineNo == state.peekTargetLine);
				if (target)
				{
					ImGui::PushStyleColor(ImGuiCol_Text,
						Orkige::editorWarningTextColor());
				}
				ImGui::Text("%5d  %s", lineNo, state.peekLines[i].c_str());
				if (target)
				{
					ImGui::PopStyleColor();
				}
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
}

} // namespace

// The Console panel: the former Lua Console grown into a proper console -
// a Log tab streaming the engine/editor/remote log lines plus the Lua REPL
// as a second tab.
void drawConsolePanel(EditorState& state, PlaySession& session,
	EditorConsole& console, bool* visible)
{
	if (ImGui::Begin("Console", visible))
	{
		OrkigeEditor::editorPanelTabMenu(visible);
		if (ImGui::BeginTabBar("##consoletabs"))
		{
			if (ImGui::BeginTabItem("Log"))
			{
				drawConsoleLogTab(state, console);
				ImGui::EndTabItem();
			}
			// the script REPL tab only exists while a scripting runtime is
			// live (a ORKIGE_SCRIPTING=OFF build shows just the Log tab)
			if (Orkige::ScriptRuntime::available() &&
				ImGui::BeginTabItem("Lua"))
			{
				const float footerHeight =
					ImGui::GetFrameHeightWithSpacing() * 4.0f;
				if (ImGui::BeginChild("history", ImVec2(0, -footerHeight),
					ImGuiChildFlags_Borders))
				{
					for (std::string const& line : state.luaHistory)
					{
						ImGui::TextWrapped("%s", line.c_str());
					}
					if (state.luaScrollToBottom)
					{
						ImGui::SetScrollHereY(1.0f);
						state.luaScrollToBottom = false;
					}
				}
				ImGui::EndChild();
				ImGui::InputTextMultiline("##luainput", state.luaInput,
					sizeof(state.luaInput),
					ImVec2(-1.0f, ImGui::GetFrameHeightWithSpacing() * 3.0f));
				if (ImGui::Button("Run"))
				{
					runLuaConsoleInput(state, session);
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear"))
				{
					state.luaHistory.clear();
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		// the transient file:line quick-peek popup (opened by a console click)
		drawConsolePeekPopup(state);
	}
	ImGui::End();
}
