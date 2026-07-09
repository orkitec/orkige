// EditorConsole.cpp - the editor Console: engine/editor/remote log line
// store pump, the SDL log output hook, the Lua REPL and the Console panel.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <core_debug/CVarCommand.h>
#include <core_debugnet/DebugProtocol.h>
#include <core_script/ScriptRuntime.h>

#include <string>

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

// the Console's Log tab: engine (Ogre) log + editor SDL_Log lines +
// [remote] player lines, severity-coloured, with clear / auto-scroll /
// text filter controls
void drawConsoleLogTab(EditorConsole& console)
{
	if (ImGui::Button("Clear"))
	{
		console.clear();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &console.autoScroll);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	console.filter.Draw("##consolefilter");
	ImGui::SetItemTooltip("filter (\"incl,-excl\" syntax)");
	if (ImGui::BeginChild("##consolelines", ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar))
	{
		std::lock_guard<std::mutex> lock(console.mutex);
		for (ConsoleLine const& line : console.lines)
		{
			if (!console.filter.PassFilter(line.text.c_str()))
			{
				continue;
			}
			switch (line.level)
			{
			case ConsoleLevel::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.95f, 0.80f, 0.25f, 1.0f));
				break;
			case ConsoleLevel::Error:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
				break;
			case ConsoleLevel::Info:
			default:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImGui::GetStyleColorVec4(ImGuiCol_Text));
				break;
			}
			ImGui::TextUnformatted(line.text.c_str());
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
		if (ImGui::BeginTabBar("##consoletabs"))
		{
			if (ImGui::BeginTabItem("Log"))
			{
				drawConsoleLogTab(console);
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
	}
	ImGui::End();
}
