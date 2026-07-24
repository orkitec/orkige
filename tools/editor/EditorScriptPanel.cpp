// EditorScriptPanel.cpp - the embedded Lua script editor + debugger panel.
//
// A docked panel of tabbed TextEditor widgets (the imgui-color-text-edit
// port) over the open project's scripts:
//   * open via Asset-browser double-click, the toolbar's script picker or a
//     one-shot EditorState::scriptOpenRequest (break-hits route through it)
//   * Lua syntax highlight; the palette follows the editor theme variant
//   * completion from the engine's own truth (the generated Lua API index +
//     the scriptable-component registry + live scripting-state globals +
//     the document's identifiers - see ScriptCompletion)
//   * a breakpoint gutter (click toggles; red dot) over the per-project
//     ScriptBreakpointStore - the play session pushes changes live
//   * Cmd/Ctrl+S saves the active tab (during play the scripts watcher
//     hot-reloads the running player automatically - no second reload path)
//   * while the session is paused at a script break: the current-line
//     marker, a Continue / Step In / Step Over / Step Out toolbar (F5 / F11 /
//     F10 / Shift+F11, with Cmd/Ctrl+Alt+C/I/O/U alternates), a call-stack
//     pane and a locals/upvalues pane (tables expand on demand, bounded)
//
// The open-tab state is TU-local (the tabs are pure UI); everything shared -
// the breakpoint store, the break state, the locals cache - rides
// EditorState/PlaySession so the MCP verbs see the same truth.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorApp.h"

#include "ExternalEditor.h"
#include "GeneratedLuaApi.h"
#include "ScriptCompletion.h"

#include <core_base/TypeManager.h>
#include <core_script/ScriptRuntime.h>

#include <TextEditor.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace
{
	namespace fs = std::filesystem;

	//! one open script document
	struct ScriptTab
	{
		std::string absolutePath;
		std::string relativePath;	//!< project-relative ('/'; abs when loose)
		std::string title;			//!< the tab label (file name)
		std::unique_ptr<TextEditor> editor;
		std::size_t savedUndoIndex = 0;	//!< GetUndoIndex() at last save/load
		int pendingScrollLine = 0;		//!< 1-based; scroll+cursor on next draw
		bool wantFocus = false;			//!< select this tab on the next draw
		bool open = true;				//!< false once the tab's x was clicked
		//! the marker inputs the editor was last rebuilt with (rebuild-on-change)
		std::size_t markedErrorCount = static_cast<std::size_t>(-1);
		unsigned int markedBreakSeq = 0;
		bool markedBroken = false;

		bool isDirty() const
		{
			return this->editor &&
				this->editor->GetUndoIndex() != this->savedUndoIndex;
		}
	};

	//! the panel's TU-local state
	struct ScriptPanelState
	{
		std::vector<std::unique_ptr<ScriptTab>> tabs;
		//! the tab shown last frame (locals/debug toolbars key off it)
		ScriptTab* active = nullptr;
		//! completion symbols cache + the project root/registry size they were
		//! built for (rebuilt when either moves)
		Orkige::ScriptCompletionSymbols symbols;
		std::string symbolsProjectRoot = "?";	//!< "?" = never built
		std::size_t symbolsKindCount = static_cast<std::size_t>(-1);
		//! the theme variant the shared palette was last built for
		int paletteVariant = -1;
		TextEditor::Palette palette;
		//! one-shot docking beside the Scene panel on first open
		bool dockedOnce = false;
		//! the break sequence the panel last auto-focused for
		unsigned int focusedBreakSeq = 0;
	};

	ScriptPanelState& panel()
	{
		static ScriptPanelState state;
		return state;
	}

	//! ImU32 from a theme colour
	ImU32 themeColor(ImVec4 const& colour)
	{
		return ImGui::ColorConvertFloat4ToU32(colour);
	}

	//! the TextEditor palette for the current theme variant: the widget's own
	//! dark/light base with the background swapped to the editor's recessed
	//! region ground so the code area reads like the other browsing panes
	TextEditor::Palette makeScriptPalette(Orkige::EditorThemeVariant variant)
	{
		TextEditor::Palette palette =
			variant == Orkige::EditorThemeVariant::Light
				? TextEditor::GetLightPalette()
				: TextEditor::GetDarkPalette();
		palette[static_cast<std::size_t>(TextEditor::Color::background)] =
			themeColor(Orkige::editorRegionBackground());
		return palette;
	}

	//! read a whole file ("" + false on failure)
	bool readFileText(std::string const& path, std::string& outText)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();
		outText = buffer.str();
		return true;
	}

	//! the project-relative ('/') form of an absolute script path, or the
	//! absolute path itself outside a project
	std::string relativeScriptPath(EditorState& state,
		std::string const& absolutePath)
	{
		if (state.project.isLoaded())
		{
			const std::string relative =
				state.project.makeProjectRelative(absolutePath);
			if (!relative.empty() && relative != ".")
			{
				return Orkige::ScriptDebugCore::normalizeChunk(relative);
			}
		}
		return Orkige::ScriptDebugCore::normalizeChunk(absolutePath);
	}

	//! save one tab back to its file (LF endings; the play watcher hot-reloads)
	bool saveTab(ScriptTab& tab)
	{
		if (!tab.editor)
		{
			return false;
		}
		std::ofstream file(tab.absolutePath, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			SDL_Log("script panel: could not write '%s'",
				tab.absolutePath.c_str());
			return false;
		}
		file << tab.editor->GetText();
		file.close();
		tab.savedUndoIndex = tab.editor->GetUndoIndex();
		SDL_Log("script panel: saved %s", tab.relativePath.c_str());
		return true;
	}

	//! (re)build the completion symbol set from the engine's own truth
	void rebuildCompletionSymbols(EditorState& state)
	{
		ScriptPanelState& ui = panel();
		ui.symbols = Orkige::ScriptCompletionSymbols();
		Orkige::addLuaKeywords(ui.symbols);
		Orkige::addApiIndexSymbols(ui.symbols, Orkige::kGeneratedLuaApiIndex);
		// the reflected script surface: the scriptable-component registry
		// (self.<field> / world.<accessor>) + each kind's property schema
		std::vector<Orkige::ReflectedKindSymbols> kinds;
		for (Orkige::ScriptComponentAccess const& access :
			Orkige::ScriptRuntime::componentAccessRegistry())
		{
			Orkige::ReflectedKindSymbols kind;
			kind.selfField = access.injectSelf ? access.name : "";
			kind.worldAccessor = access.worldAccessor;
			if (access.type != nullptr)
			{
				kind.kindName = access.type->getName();
				if (Orkige::PropertySchema const* schema =
					Orkige::TypeManager::getSingleton().getPropertySchema(
						access.type->getId()))
				{
					for (Orkige::PropertyDesc const& property :
						schema->properties())
					{
						kind.properties.push_back(property.name);
					}
				}
			}
			kinds.push_back(kind);
		}
		Orkige::addReflectedKinds(ui.symbols, kinds);
		// the live scripting state: every registered global table / usertype
		// with its enumerated members (engine:*, gui:*, the works)
		if (Orkige::ScriptRuntime::available())
		{
			Orkige::ScriptRuntime& runtime =
				Orkige::ScriptRuntime::getSingleton();
			for (Orkige::String const& name : runtime.globalNames())
			{
				Orkige::addRuntimeTable(panel().symbols, name,
					runtime.globalMemberNames(name));
			}
		}
		ui.symbols.finalize();
		ui.symbolsProjectRoot = state.project.isLoaded()
			? state.project.getRootDirectory() : std::string();
		ui.symbolsKindCount =
			Orkige::ScriptRuntime::componentAccessRegistry().size();
	}

	//! the completion callback the widget calls while the popup is up
	void completionCallback(TextEditor::AutoCompleteState& complete)
	{
		ScriptTab* tab = static_cast<ScriptTab*>(complete.userData);
		if (tab == nullptr || !tab->editor)
		{
			return;
		}
		const int line = static_cast<int>(complete.line);
		const std::string before = tab->editor->GetSectionText(line, 0, line,
			static_cast<int>(complete.searchTermStartColumn));
		std::vector<std::string> documentIdentifiers;
		tab->editor->IterateIdentifiers(
			[&documentIdentifiers](std::string const& identifier)
			{
				documentIdentifiers.push_back(identifier);
			});
		complete.suggestions = Orkige::suggestCompletions(panel().symbols,
			before, complete.searchTerm, documentIdentifiers, 40);
	}

	//! open (or focus) a script file as a tab; line > 0 scrolls to it
	ScriptTab* openTab(EditorState& state, std::string const& absolutePath,
		int line)
	{
		ScriptPanelState& ui = panel();
		std::error_code ignored;
		const std::string canonical =
			fs::weakly_canonical(absolutePath, ignored).string();
		const std::string key = canonical.empty() ? absolutePath : canonical;
		for (auto& tab : ui.tabs)
		{
			if (tab->absolutePath == key)
			{
				tab->wantFocus = true;
				tab->open = true;
				if (line > 0)
				{
					tab->pendingScrollLine = line;
				}
				return tab.get();
			}
		}
		std::string text;
		if (!readFileText(key, text))
		{
			SDL_Log("script panel: could not read '%s'", key.c_str());
			return nullptr;
		}
		auto tab = std::make_unique<ScriptTab>();
		tab->absolutePath = key;
		tab->relativePath = relativeScriptPath(state, key);
		tab->title = fs::path(key).filename().string();
		tab->editor = std::make_unique<TextEditor>();
		tab->editor->SetLanguage(TextEditor::Language::Lua());
		tab->editor->SetText(text);
		tab->editor->SetShowWhitespacesEnabled(false);
		tab->savedUndoIndex = tab->editor->GetUndoIndex();
		if (line > 0)
		{
			tab->pendingScrollLine = line;
		}
		tab->wantFocus = true;
		// completion: engine API + keywords + the document's identifiers
		TextEditor::AutoCompleteConfig completeConfig;
		completeConfig.callback = completionCallback;
		completeConfig.userData = tab.get();
		tab->editor->SetAutoCompleteConfig(&completeConfig);
		ui.tabs.push_back(std::move(tab));
		return ui.tabs.back().get();
	}

	//! does this tab's document match the debugger's paused file
	bool tabMatchesBreakFile(ScriptTab const& tab, std::string const& file)
	{
		return !file.empty() && Orkige::ScriptDebugCore::chunkMatchesFile(
			tab.relativePath, file);
	}

	//! rebuild the active tab's markers when their inputs moved: script
	//! errors carrying a file:line in THIS document, and the paused line
	void refreshMarkers(ScriptTab& tab, PlaySession& session)
	{
		const bool broken = session.debugBroken &&
			tabMatchesBreakFile(tab, session.debugBreakFile);
		if (tab.markedErrorCount == session.scriptErrorMessages.size() &&
			tab.markedBreakSeq == session.debugBreakSeq &&
			tab.markedBroken == broken)
		{
			return;
		}
		tab.markedErrorCount = session.scriptErrorMessages.size();
		tab.markedBreakSeq = session.debugBreakSeq;
		tab.markedBroken = broken;
		tab.editor->ClearMarkers();
		const ImU32 errorColor =
			themeColor(Orkige::editorErrorTextColor());
		for (std::string const& message : session.scriptErrorMessages)
		{
			for (Orkige::FileLineRef const& reference :
				Orkige::parseFileLineRefs(message))
			{
				if (reference.line > 0 &&
					Orkige::ScriptDebugCore::chunkMatchesFile(
						Orkige::ScriptDebugCore::normalizeChunk(
							reference.path), tab.relativePath))
				{
					tab.editor->AddMarker(reference.line - 1, errorColor,
						errorColor, "script error", message);
				}
			}
		}
		if (broken && session.debugBreakLine > 0)
		{
			const ImU32 breakColor = IM_COL32(230, 180, 60, 255);
			tab.editor->AddMarker(session.debugBreakLine - 1, breakColor,
				IM_COL32(230, 180, 60, 40), "paused here",
				"execution paused at this line");
		}
	}

	//! the breakpoint gutter: an invisible click target per visible line, a
	//! red dot where a breakpoint is set and an arrow on the paused line
	void drawGutterCell(EditorState& state, PlaySession& session,
		ScriptTab& tab, TextEditor::Decorator& decorator)
	{
		const int line = decorator.line + 1;	// widget lines are zero-based
		ImGui::InvisibleButton("bp",
			ImVec2(decorator.width, decorator.height));
		if (ImGui::IsItemClicked())
		{
			state.breakpoints.toggle(tab.relativePath, line);
		}
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		const ImVec2 centre((minimum.x + maximum.x) * 0.5f,
			(minimum.y + maximum.y) * 0.5f);
		const float radius = decorator.height * 0.28f;
		if (state.breakpoints.has(tab.relativePath, line))
		{
			drawList->AddCircleFilled(centre, radius,
				IM_COL32(229, 73, 58, 255));
		}
		else if (ImGui::IsItemHovered())
		{
			drawList->AddCircle(centre, radius, IM_COL32(229, 73, 58, 120));
		}
		if (session.debugBroken && line == session.debugBreakLine &&
			tabMatchesBreakFile(tab, session.debugBreakFile))
		{
			// the paused-here arrow, drawn over/beside the dot
			const ImU32 arrowColor = IM_COL32(230, 180, 60, 255);
			const float half = decorator.height * 0.22f;
			drawList->AddTriangleFilled(
				ImVec2(centre.x - half, centre.y - half),
				ImVec2(centre.x + half, centre.y),
				ImVec2(centre.x - half, centre.y + half), arrowColor);
		}
	}

	//! one locals row (recursing into expanded tables, bounded depth)
	void drawLocalsRows(PlaySession& session, int frameIndex,
		std::vector<std::string> const& path, int depth)
	{
		const std::string key = debugLocalsKey(frameIndex, path);
		const auto cached = session.debugLocalsCache.find(key);
		if (cached == session.debugLocalsCache.end())
		{
			requestDebugLocals(session, frameIndex, path);
			ImGui::TextDisabled("...");
			return;
		}
		if (cached->second.empty())
		{
			ImGui::TextDisabled(path.empty() ? "(no locals)" : "(empty)");
			return;
		}
		for (PlaySession::DebugVariableRow const& row : cached->second)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			const bool expandable = row.expandable && depth < 3;
			bool expanded = false;
			if (expandable)
			{
				std::vector<std::string> childPath = path;
				childPath.push_back(row.name);
				const std::string childKey =
					debugLocalsKey(frameIndex, childPath);
				ImGui::PushID(childKey.c_str());
				expanded = ImGui::TreeNodeEx(row.name.c_str(),
					ImGuiTreeNodeFlags_SpanAvailWidth);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(row.value.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("%s", row.scope.c_str());
				if (expanded)
				{
					drawLocalsRows(session, frameIndex, childPath, depth + 1);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			else
			{
				ImGui::TreeNodeEx(row.name.c_str(),
					ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
					ImGuiTreeNodeFlags_SpanAvailWidth);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(row.value.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("%s", row.scope.c_str());
			}
		}
	}

	//! the debug toolbar (Continue / Step In / Over / Out) - only while broken
	void drawDebugToolbar(PlaySession& session)
	{
		namespace Protocol = Orkige::DebugProtocol;
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.90f, 0.71f, 0.24f, 1.0f), "Paused %s:%d",
			session.debugBreakFile.c_str(), session.debugBreakLine);
		ImGui::SameLine();
		if (ImGui::Button("Continue"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_RESUME);
		}
		ImGui::SetItemTooltip("Continue (F5)");
		ImGui::SameLine();
		if (ImGui::Button("Step Over"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_OVER);
		}
		ImGui::SetItemTooltip("Step Over (F10)");
		ImGui::SameLine();
		if (ImGui::Button("Step In"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_IN);
		}
		ImGui::SetItemTooltip("Step In (F11)");
		ImGui::SameLine();
		if (ImGui::Button("Step Out"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_OUT);
		}
		ImGui::SetItemTooltip("Step Out (Shift+F11)");
	}

	//! the call-stack + locals panes shown below the editor while broken
	void drawDebugPanes(EditorState& state, ViewSettings& viewSettings,
		PlaySession& session)
	{
		const float paneHeight = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
		ImGui::PushStyleColor(ImGuiCol_ChildBg,
			Orkige::editorRegionBackground());
		ImGui::BeginChild("##debugPanes", ImVec2(0, paneHeight), true);
		if (ImGui::BeginTable("##debugSplit", 2,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Call Stack",
				ImGuiTableColumnFlags_WidthFixed,
				ImGui::GetContentRegionAvail().x * 0.38f);
			ImGui::TableSetupColumn("Locals");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("Call Stack");
			ImGui::BeginChild("##stack", ImVec2(0, 0));
			for (std::size_t i = 0; i < session.debugStack.size(); ++i)
			{
				PlaySession::DebugStackFrame const& frame =
					session.debugStack[i];
				std::string label = frame.source;
				if (frame.line > 0)
				{
					label += ":" + std::to_string(frame.line);
				}
				if (!frame.function.empty())
				{
					label += "  " + frame.function;
				}
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Selectable(label.c_str(),
					static_cast<int>(i) == session.debugSelectedFrame))
				{
					session.debugSelectedFrame = static_cast<int>(i);
					requestDebugLocals(session, session.debugSelectedFrame,
						{});
					// jump the editor to the frame's line
					if (frame.line > 0 && frame.source != "[host]")
					{
						scriptPanelOpenFile(state, viewSettings, frame.source,
							frame.line);
					}
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::TableSetColumnIndex(1);
			ImGui::TextDisabled("Locals");
			ImGui::BeginChild("##locals", ImVec2(0, 0));
			if (ImGui::BeginTable("##localsTable", 3,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Value");
				ImGui::TableSetupColumn("Scope",
					ImGuiTableColumnFlags_WidthFixed, 70.0f);
				drawLocalsRows(session, session.debugSelectedFrame, {}, 0);
				ImGui::EndTable();
			}
			ImGui::EndChild();
			ImGui::EndTable();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}
}

//---------------------------------------------------------------------------
void scriptPanelOpenFile(EditorState& state, ViewSettings& viewSettings,
	std::string const& path, int line)
{
	state.scriptOpenRequest = path;
	state.scriptOpenLine = line;
	if (!viewSettings.showScriptPanel)
	{
		viewSettings.showScriptPanel = true;
		viewSettings.save();
	}
}
//---------------------------------------------------------------------------
void scriptPanelCloseAll()
{
	panel().tabs.clear();
	panel().active = nullptr;
	panel().symbolsProjectRoot = "?";
}
//---------------------------------------------------------------------------
bool scriptPanelHasUnsavedEdits()
{
	for (auto const& tab : panel().tabs)
	{
		if (tab->isDirty())
		{
			return true;
		}
	}
	return false;
}
//---------------------------------------------------------------------------
bool scriptPanelSaveActiveIfFocused(EditorState& state)
{
	if (!state.scriptPanelFocused || panel().active == nullptr)
	{
		return false;
	}
	saveTab(*panel().active);
	return true;
}
//---------------------------------------------------------------------------
void handleScriptDebugShortcuts(EditorState& state, PlaySession& session)
{
	namespace Protocol = Orkige::DebugProtocol;
	(void)state;
	if (!session.debugBroken)
	{
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	const bool commandAlt = (io.KeySuper || io.KeyCtrl) && io.KeyAlt;
	// platform-conventional function keys, plus Cmd/Ctrl+Alt letter
	// alternates for keyboards where the F-row is awkward (macOS defaults)
	if (ImGui::IsKeyPressed(ImGuiKey_F5, false) ||
		(commandAlt && ImGui::IsKeyPressed(ImGuiKey_C, false)))
	{
		sendDebugCommand(session, Protocol::MSG_DEBUG_RESUME);
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_F10, false) ||
		(commandAlt && ImGui::IsKeyPressed(ImGuiKey_O, false)))
	{
		sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_OVER);
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
	{
		sendDebugCommand(session, io.KeyShift
			? Protocol::MSG_DEBUG_STEP_OUT : Protocol::MSG_DEBUG_STEP_IN);
	}
	else if (commandAlt && ImGui::IsKeyPressed(ImGuiKey_I, false))
	{
		sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_IN);
	}
	else if (commandAlt && ImGui::IsKeyPressed(ImGuiKey_U, false))
	{
		sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_OUT);
	}
}
//---------------------------------------------------------------------------
void drawScriptPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, ViewSettings& viewSettings, bool* visible)
{
	(void)core;
	ScriptPanelState& ui = panel();

	// consume a pending open request BEFORE drawing so the tab exists
	if (!state.scriptOpenRequest.empty())
	{
		const std::string absolute = resolveProjectFilePath(state.project,
			state.scriptOpenRequest);
		openTab(state, absolute.empty() ? state.scriptOpenRequest : absolute,
			state.scriptOpenLine);
		state.scriptOpenRequest.clear();
		state.scriptOpenLine = 0;
	}

	// keep the completion truth fresh (project switch / late registrations)
	const std::string projectRoot = state.project.isLoaded()
		? state.project.getRootDirectory() : std::string();
	if (ui.symbolsProjectRoot != projectRoot ||
		ui.symbolsKindCount !=
			Orkige::ScriptRuntime::componentAccessRegistry().size())
	{
		rebuildCompletionSymbols(state);
	}

	// the shared palette follows the theme
	const int variant =
		static_cast<int>(Orkige::currentEditorThemeVariant());
	if (ui.paletteVariant != variant)
	{
		ui.paletteVariant = variant;
		ui.palette = makeScriptPalette(Orkige::currentEditorThemeVariant());
		for (auto& tab : ui.tabs)
		{
			tab->editor->SetPalette(ui.palette);
		}
	}

	// dock beside the Scene panel the first time the panel opens
	dockPreviewBesideSceneOnce("Script###Script", ui.dockedOnce);

	ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Script###Script", visible))
	{
		state.scriptPanelFocused = false;
		ImGui::End();
		return;
	}
	state.scriptPanelFocused = ImGui::IsWindowFocused(
		ImGuiFocusedFlags_RootAndChildWindows);

	// --- toolbar row: script picker + save + the debug strip ---------------
	if (ImGui::Button("Open"))
	{
		ImGui::OpenPopup("##scriptPicker");
	}
	if (ImGui::BeginPopup("##scriptPicker"))
	{
		bool any = false;
		if (state.project.isLoaded())
		{
			for (AssetBrowserItem const& item :
				enumerateProjectAssets(state.project))
			{
				if (item.kind != AssetKind::Script || item.isFolder)
				{
					continue;
				}
				any = true;
				if (ImGui::MenuItem(item.relativePath.c_str()))
				{
					openTab(state, item.absolutePath, 0);
				}
			}
		}
		if (!any)
		{
			ImGui::TextDisabled(state.project.isLoaded()
				? "no scripts in this project" : "no project open");
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	{
		ScriptTab* active = ui.active;
		const bool canSave = active != nullptr && active->isDirty();
		ImGui::BeginDisabled(!canSave);
		if (ImGui::Button("Save"))
		{
			saveTab(*active);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Save the active script ("
			ORKIGE_EDITOR_MOD_LABEL "+S)");
	}
	if (session.debugBroken)
	{
		drawDebugToolbar(session);
	}

	// on a NEW break, focus its tab/line exactly once
	if (session.debugBroken && ui.focusedBreakSeq != session.debugBreakSeq)
	{
		ui.focusedBreakSeq = session.debugBreakSeq;
		if (!session.debugBreakFile.empty())
		{
			const std::string absolute = resolveProjectFilePath(
				state.project, session.debugBreakFile);
			openTab(state, absolute.empty()
				? session.debugBreakFile : absolute, session.debugBreakLine);
		}
	}

	// --- the tab bar + active editor ---------------------------------------
	ScriptTab* activeThisFrame = nullptr;
	const float editorHeight = session.debugBroken
		? -(ImGui::GetTextLineHeightWithSpacing() * 8.0f +
			ImGui::GetStyle().ItemSpacing.y * 2.0f)
		: 0.0f;
	if (ui.tabs.empty())
	{
		ImGui::Dummy(ImVec2(0, 8));
		ImGui::TextDisabled("Double-click a script in the Assets browser, or "
			"use Open above.");
	}
	else if (ImGui::BeginTabBar("##scriptTabs",
		ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
	{
		for (auto& tab : ui.tabs)
		{
			ImGuiTabItemFlags flags = tab->isDirty()
				? ImGuiTabItemFlags_UnsavedDocument : 0;
			if (tab->wantFocus)
			{
				flags |= ImGuiTabItemFlags_SetSelected;
				tab->wantFocus = false;
			}
			ImGui::PushID(tab->absolutePath.c_str());
			const bool shown = ImGui::BeginTabItem(tab->title.c_str(),
				&tab->open, flags);
			if (shown)
			{
				activeThisFrame = tab.get();
				refreshMarkers(*tab, session);
				if (tab->pendingScrollLine > 0)
				{
					tab->editor->SetCursor(tab->pendingScrollLine - 1, 0);
					tab->editor->ScrollToLine(tab->pendingScrollLine - 1,
						TextEditor::Scroll::alignMiddle);
					tab->pendingScrollLine = 0;
				}
				// the breakpoint gutter (two glyphs wide)
				ScriptTab* tabPointer = tab.get();
				EditorState* statePointer = &state;
				PlaySession* sessionPointer = &session;
				tab->editor->SetLineDecorator(-2.0f,
					[statePointer, sessionPointer, tabPointer](
						TextEditor::Decorator& decorator)
					{
						drawGutterCell(*statePointer, *sessionPointer,
							*tabPointer, decorator);
					});
				ImFont* mono = Orkige::editorMonoFont();
				if (mono != nullptr)
				{
					ImGui::PushFont(mono);
				}
				tab->editor->Render("##code", ImVec2(0, editorHeight));
				if (mono != nullptr)
				{
					ImGui::PopFont();
				}
				ImGui::EndTabItem();
			}
			ImGui::PopID();
		}
		ImGui::EndTabBar();
	}
	ui.active = activeThisFrame;

	// a closed dirty tab discards its edits - say so instead of silence
	for (auto it = ui.tabs.begin(); it != ui.tabs.end();)
	{
		if (!(*it)->open)
		{
			if ((*it)->isDirty())
			{
				SDL_Log("script panel: closed '%s' with unsaved edits "
					"(discarded)", (*it)->relativePath.c_str());
			}
			if (ui.active == it->get())
			{
				ui.active = nullptr;
			}
			it = ui.tabs.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Cmd/Ctrl+S while the panel has focus (the native macOS menu routes here
	// too via saveCurrentDocument -> scriptPanelSaveActiveIfFocused)
	if (state.scriptPanelFocused && ui.active != nullptr)
	{
		ImGuiIO& io = ImGui::GetIO();
		if ((io.KeySuper || io.KeyCtrl) && !io.KeyShift &&
			ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			saveTab(*ui.active);
		}
	}

	// --- the debugger panes (call stack + locals) while broken --------------
	if (session.debugBroken)
	{
		drawDebugPanes(state, viewSettings, session);
	}

	ImGui::End();
}
