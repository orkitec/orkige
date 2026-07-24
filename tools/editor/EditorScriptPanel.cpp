// EditorScriptPanel.cpp - the embedded code editor (a docked window per open
// file) and the script debugger's Debug panel.
//
// Each open file is its OWN docked ImGui window (title = file name, dirty
// marker, stable ###path id) built on the imgui-color-text-edit widget, so
// several open files read as sibling tabs in one dock node like every other
// panel. A document opens via Asset-browser double-click, the Inspector's
// "Open in Internal Editor" button or a one-shot EditorState::scriptOpenRequest
// (break-hits route through it). Syntax highlight follows the file kind - Lua,
// JSON, Markdown, an XML definition for the engine's XML formats, plain text
// otherwise; completion (from the engine's own truth - the generated Lua API
// index, the scriptable-component registry, live scripting-state globals and
// the document's identifiers) is Lua-only, and so is the breakpoint gutter.
// Cmd/Ctrl+S saves the focused document (during play the scripts watcher
// hot-reloads the running player - no second reload path).
//
// The Debug panel (drawDebugPanel) carries the debugger's transport (Continue /
// Step In / Over / Out), the call-stack pane and the locals/upvalues pane; it
// docks in the bottom group beside Console and auto-opens/focuses on a break.
//
// The open-document state is TU-local (the windows are pure UI); everything
// shared - the breakpoint store, the break state, the locals cache - rides
// EditorState/PlaySession so the MCP verbs see the same truth.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorApp.h"
#include "EditorTabMenu.h"

#include "EditorTabActions.h"	// the shared close-set semantics (tab menus)
#include "EditorTextDiagnostics.h"	// live parse "squiggles" (XML/Lua lines)
#include "ExternalEditor.h"	// parseFileLineRefs / FileLineRef (error markers)
#include "GeneratedLuaApi.h"
#include "IconsFontAwesome6.h"
#include "ScriptCompletion.h"

#include <core_base/TypeManager.h>
#include <core_script/ScriptRuntime.h>

#include <TextEditor.h>
#include <imgui_internal.h>	// FindWindowByName / GetWindowDockID (docking)

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace
{
	namespace fs = std::filesystem;

	//! one open code-editor document (its own docked window)
	struct ScriptDocument
	{
		std::string absolutePath;
		std::string relativePath;	//!< project-relative ('/'; abs when loose)
		std::string title;			//!< the window's tab label (file name)
		std::string windowId;		//!< "<title>###<absolutePath>" (stable per path)
		bool isLua = false;			//!< breakpoint gutter + completion are Lua-only
		std::unique_ptr<TextEditor> editor;
		std::size_t savedUndoIndex = 0;	//!< GetUndoIndex() at last save/load
		int pendingScrollLine = 0;		//!< 1-based; scroll+cursor on next draw
		bool wantFocus = false;			//!< raise/focus this window on the next draw
		bool open = true;				//!< false once the window's x was clicked
		bool closeRequested = false;	//!< a dirty close awaits the save/discard ask
		bool discardEdits = false;		//!< the user chose Discard - removal may proceed
		bool dockAssigned = false;		//!< docked into the shared node once
		unsigned int dockId = 0;		//!< the window's dock node last frame
		//! the marker inputs the editor was last rebuilt with (rebuild-on-change)
		std::size_t markedErrorCount = static_cast<std::size_t>(-1);
		unsigned int markedBreakSeq = 0;
		bool markedBroken = false;
		//! live parse diagnostics ("squiggles"): what the format's own parser
		//! says about the CURRENT buffer, refreshed once the text sits still
		enum class LiveCheck { None, Lua, Xml };
		LiveCheck liveCheck = LiveCheck::None;
		std::size_t checkedUndoIndex = static_cast<std::size_t>(-1);
		std::size_t lastSeenUndoIndex = static_cast<std::size_t>(-1);
		int stableFrames = 0;			//!< frames the buffer sat unchanged
		OrkigeEditor::TextDiagnostic parseState;	//!< the current verdict
		unsigned int parseRevision = 0;	//!< bumps when parseState changes
		unsigned int markedParseRevision = 0;
		//! 0-based line -> message for every error the markers carry (the
		//! gutter's red "!" badges + their hover text; rebuilt with the markers)
		std::map<int, std::string> errorLineMessages;

		bool isDirty() const
		{
			return this->editor &&
				this->editor->GetUndoIndex() != this->savedUndoIndex;
		}
	};

	//! the TU-local UI state (documents + shared debug bookkeeping)
	struct PanelState
	{
		std::vector<std::unique_ptr<ScriptDocument>> docs;
		//! the document window that held focus last frame (the save target)
		ScriptDocument* focused = nullptr;
		//! completion symbols cache + the project root/registry size they were
		//! built for (rebuilt when either moves)
		Orkige::ScriptCompletionSymbols symbols;
		std::string symbolsProjectRoot = "?";	//!< "?" = never built
		std::size_t symbolsKindCount = static_cast<std::size_t>(-1);
		//! the theme variant the shared palette was last built for
		int paletteVariant = -1;
		TextEditor::Palette palette;
		//! the dock node the document windows share (lands beside Scene on the
		//! first open; new documents join wherever the group currently lives)
		unsigned int sharedDockId = 0;
		//! the dirty document currently asking save/discard/cancel (one modal
		//! at a time; further close-requested documents queue behind it)
		ScriptDocument* confirmClose = nullptr;
		//! a tab context-menu action deferred to after the draw loop (the menu
		//! runs mid-iteration; the close-set applies against the stable list)
		OrkigeEditor::TabAction tabAction = OrkigeEditor::TabAction::None;
		std::size_t tabActionTarget = 0;
		//! the break sequence the documents last auto-focused for
		unsigned int focusedBreakSeq = 0;
		//! the break sequence the Debug panel last auto-focused for
		unsigned int debugFocusSeq = 0;
		//! one-shot: dock the Debug panel beside Console the first time it shows
		bool debugDockedOnce = false;
	};

	PanelState& panel()
	{
		static PanelState state;
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

	//! the lower-case extension of a path (".lua", ".oscene", ...)
	std::string lowerExt(std::string const& path)
	{
		std::string ext = fs::path(path).extension().string();
		for (char& c : ext)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return ext;
	}

	//! a minimal XML language definition authored through the widget's custom-
	//! language API (the engine's XML formats: .oscene/.oprefab/.orkproj/.xlf):
	//! tag/attribute names as identifiers, quoted attribute values as strings,
	//! <!-- --> comments, and the markup punctuation coloured
	const TextEditor::Language* xmlLanguage()
	{
		static const TextEditor::Language language = []
		{
			TextEditor::Language lang;
			lang.name = "XML";
			lang.caseSensitive = true;
			lang.commentStart = "<!--";
			lang.commentEnd = "-->";
			lang.hasSingleQuotedStrings = true;
			lang.hasDoubleQuotedStrings = true;
			lang.isPunctuation = [](ImWchar c)
			{
				return c == '<' || c == '>' || c == '/' || c == '=' ||
					c == '?' || c == '!';
			};
			// tag and attribute names: an XML Name (letters/'_' start, then
			// letters/digits/'-'/'_'/':'/'.')
			lang.getIdentifier = [](TextEditor::Iterator start,
				TextEditor::Iterator end)
			{
				auto isNameStart = [](ImWchar c)
				{
					return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						c == '_';
				};
				auto isNameChar = [&isNameStart](ImWchar c)
				{
					return isNameStart(c) || (c >= '0' && c <= '9') ||
						c == '-' || c == ':' || c == '.';
				};
				TextEditor::Iterator i = start;
				if (i != end && isNameStart(*i))
				{
					for (++i; i != end && isNameChar(*i); ++i)
					{
					}
					return i;
				}
				return start;
			};
			return lang;
		}();
		return &language;
	}

	//! the highlighter for a file kind (nullptr = plain text). JSON ships with
	//! the widget; the engine's XML formats get the custom XML definition; our
	//! bespoke text grammars (.oui/.omat/.oshape/...) open as plain text
	const TextEditor::Language* languageForFile(std::string const& path)
	{
		const std::string ext = lowerExt(path);
		if (ext == ".lua")
		{
			return TextEditor::Language::Lua();
		}
		if (ext == ".json" || ext == ".jsonl")
		{
			return TextEditor::Language::Json();
		}
		if (ext == ".md")
		{
			return TextEditor::Language::Markdown();
		}
		// native-module game code opens with real highlighting too
		if (ext == ".c")
		{
			return TextEditor::Language::C();
		}
		// the engine's shader sources (the generated-shader library, the
		// grade shaders) - engine-dev editing; game materials stay generated
		if (ext == ".glsl" || ext == ".vert" || ext == ".frag")
		{
			return TextEditor::Language::Glsl();
		}
		if (ext == ".hlsl")
		{
			return TextEditor::Language::Hlsl();
		}
		if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
			ext == ".hpp" || ext == ".hh" || ext == ".inl" || ext == ".mm")
		{
			return TextEditor::Language::Cpp();
		}
		if (ext == ".oscene" || ext == ".oprefab" || ext == ".orkproj" ||
			ext == ".orkmeta" || ext == ".xlf" || ext == ".xml")
		{
			return xmlLanguage();
		}
		return nullptr;
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

	//! the project-relative ('/') form of an absolute path, or the absolute
	//! path itself outside a project
	std::string relativeDocPath(EditorState& state,
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

	//! save one document back to its file (LF endings; the play watcher reloads)
	bool saveDocument(ScriptDocument& doc)
	{
		if (!doc.editor)
		{
			return false;
		}
		std::ofstream file(doc.absolutePath,
			std::ios::binary | std::ios::trunc);
		if (!file)
		{
			SDL_Log("script editor: could not write '%s'",
				doc.absolutePath.c_str());
			return false;
		}
		file << doc.editor->GetText();
		file.close();
		doc.savedUndoIndex = doc.editor->GetUndoIndex();
		SDL_Log("script editor: saved %s", doc.relativePath.c_str());
		return true;
	}

	//! (re)build the completion symbol set from the engine's own truth
	void rebuildCompletionSymbols(EditorState& state)
	{
		PanelState& ui = panel();
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

	//! the completion callback the widget calls while the popup is up (Lua only)
	void completionCallback(TextEditor::AutoCompleteState& complete)
	{
		ScriptDocument* doc = static_cast<ScriptDocument*>(complete.userData);
		if (doc == nullptr || !doc->editor)
		{
			return;
		}
		const int line = static_cast<int>(complete.line);
		const std::string before = doc->editor->GetSectionText(line, 0, line,
			static_cast<int>(complete.searchTermStartColumn));
		std::vector<std::string> documentIdentifiers;
		doc->editor->IterateIdentifiers(
			[&documentIdentifiers](std::string const& identifier)
			{
				documentIdentifiers.push_back(identifier);
			});
		complete.suggestions = Orkige::suggestCompletions(panel().symbols,
			before, complete.searchTerm, documentIdentifiers, 40);
	}

	//! open (or focus) a file as a document window; line > 0 scrolls to it
	ScriptDocument* openDocument(EditorState& state,
		std::string const& absolutePath, int line)
	{
		PanelState& ui = panel();
		std::error_code ignored;
		const std::string canonical =
			fs::weakly_canonical(absolutePath, ignored).string();
		const std::string key = canonical.empty() ? absolutePath : canonical;
		for (auto& doc : ui.docs)
		{
			if (doc->absolutePath == key)
			{
				doc->wantFocus = true;
				doc->open = true;
				if (line > 0)
				{
					doc->pendingScrollLine = line;
				}
				return doc.get();
			}
		}
		std::string text;
		if (!readFileText(key, text))
		{
			SDL_Log("script editor: could not read '%s'", key.c_str());
			return nullptr;
		}
		auto doc = std::make_unique<ScriptDocument>();
		doc->absolutePath = key;
		doc->relativePath = relativeDocPath(state, key);
		doc->title = fs::path(key).filename().string();
		// the id after ### keeps the window/docking identity stable per path
		doc->windowId = doc->title + "###" + key;
		doc->isLua = lowerExt(key) == ".lua";
		// live parse diagnostics follow the format's own parser: Lua compiles
		// through the ScriptRuntime seam, the XML kinds parse via tinyxml2;
		// the bespoke text grammars have no incremental parser - no live check
		{
			const std::string ext = lowerExt(key);
			if (doc->isLua)
			{
				doc->liveCheck = ScriptDocument::LiveCheck::Lua;
			}
			else if (ext == ".oscene" || ext == ".oprefab" ||
				ext == ".orkproj" || ext == ".orkmeta" || ext == ".xlf" ||
				ext == ".xml")
			{
				doc->liveCheck = ScriptDocument::LiveCheck::Xml;
			}
		}
		doc->editor = std::make_unique<TextEditor>();
		const TextEditor::Language* language = languageForFile(key);
		if (language != nullptr)
		{
			doc->editor->SetLanguage(language);
		}
		doc->editor->SetText(text);
		doc->editor->SetShowWhitespacesEnabled(false);
		doc->editor->SetPalette(ui.palette);
		doc->savedUndoIndex = doc->editor->GetUndoIndex();
		if (line > 0)
		{
			doc->pendingScrollLine = line;
		}
		doc->wantFocus = true;
		// completion is Lua-only (the engine API surface is a Lua truth)
		if (doc->isLua)
		{
			TextEditor::AutoCompleteConfig completeConfig;
			completeConfig.callback = completionCallback;
			completeConfig.userData = doc.get();
			doc->editor->SetAutoCompleteConfig(&completeConfig);
		}
		ui.docs.push_back(std::move(doc));
		return ui.docs.back().get();
	}

	//! does this document match the debugger's paused file
	bool docMatchesBreakFile(ScriptDocument const& doc, std::string const& file)
	{
		return !file.empty() && Orkige::ScriptDebugCore::chunkMatchesFile(
			doc.relativePath, file);
	}

	//! rebuild a document's markers when their inputs moved: script errors
	//! carrying a file:line in THIS document, and the paused line
	//! @brief live parse pass: once the buffer has sat unchanged for a few
	//! frames, run the format's own parser over it and record the first
	//! problem (the "squiggle" a proper editor shows while typing). Lua
	//! compiles - never runs - through ScriptRuntime::checkSyntax (skipped
	//! honestly in scripting-off builds); the XML kinds parse via the pure
	//! tinyxml2 probe. The verdict feeds refreshMarkers through parseRevision.
	void liveSyntaxCheck(ScriptDocument& doc)
	{
		if (doc.liveCheck == ScriptDocument::LiveCheck::None || !doc.editor)
		{
			return;
		}
		const std::size_t undo = doc.editor->GetUndoIndex();
		if (undo != doc.lastSeenUndoIndex)
		{
			doc.lastSeenUndoIndex = undo;	// still typing - restart the wait
			doc.stableFrames = 0;
			return;
		}
		if (undo == doc.checkedUndoIndex || ++doc.stableFrames < 18)
		{
			return;
		}
		doc.checkedUndoIndex = undo;
		OrkigeEditor::TextDiagnostic verdict;
		if (doc.liveCheck == ScriptDocument::LiveCheck::Lua)
		{
			if (!Orkige::ScriptRuntime::available())
			{
				return;	// cannot check honestly - keep no verdict at all
			}
			Orkige::String error;
			if (!Orkige::ScriptRuntime::getSingleton().checkSyntax(
				doc.editor->GetText(), doc.relativePath, &error))
			{
				verdict.valid = false;
				verdict.message = error;
				verdict.line =
					OrkigeEditor::luaErrorLine(error, doc.relativePath);
			}
		}
		else
		{
			verdict = OrkigeEditor::xmlDiagnostic(doc.editor->GetText());
		}
		if (verdict.valid != doc.parseState.valid ||
			verdict.line != doc.parseState.line ||
			verdict.message != doc.parseState.message)
		{
			doc.parseState = verdict;
			++doc.parseRevision;	// refreshMarkers rebuilds on this
		}
	}

	//! the three answers the dirty-close ask offers - shared by the modal's
	//! buttons and the selfcheck seam (the same semantics, minus the click)
	enum class ConfirmChoice { Save, Discard, Cancel };

	void resolveConfirmClose(PanelState& ui, ScriptDocument& doc,
		ConfirmChoice choice)
	{
		switch (choice)
		{
		case ConfirmChoice::Save:
			saveDocument(doc);
			doc.closeRequested = false;
			doc.open = false;	// now clean - the sweep removes it
			break;
		case ConfirmChoice::Discard:
			doc.closeRequested = false;
			doc.discardEdits = true;
			doc.open = false;
			break;
		case ConfirmChoice::Cancel:
			doc.closeRequested = false;
			break;
		}
		ui.confirmClose = nullptr;
	}

	void refreshMarkers(ScriptDocument& doc, PlaySession& session)
	{
		const bool broken = session.debugBroken &&
			docMatchesBreakFile(doc, session.debugBreakFile);
		if (doc.markedErrorCount == session.scriptErrorMessages.size() &&
			doc.markedBreakSeq == session.debugBreakSeq &&
			doc.markedBroken == broken &&
			doc.markedParseRevision == doc.parseRevision)
		{
			return;
		}
		doc.markedErrorCount = session.scriptErrorMessages.size();
		doc.markedBreakSeq = session.debugBreakSeq;
		doc.markedBroken = broken;
		doc.markedParseRevision = doc.parseRevision;
		doc.editor->ClearMarkers();
		doc.errorLineMessages.clear();
		// error presentation: ONLY the gutter's red "!" badge + hover message
		// (drawGutterCell reads errorLineMessages) - no line-number tint, no
		// line tint (both made the code hard to read; the marker survives
		// fully transparent to keep its tooltip plumbing)
		const ImU32 errorColor = themeColor(Orkige::editorErrorTextColor());
		const ImU32 noTint = IM_COL32(0, 0, 0, 0);
		(void)errorColor;
		for (std::string const& message : session.scriptErrorMessages)
		{
			for (Orkige::FileLineRef const& reference :
				Orkige::parseFileLineRefs(message))
			{
				if (reference.line > 0 &&
					Orkige::ScriptDebugCore::chunkMatchesFile(
						Orkige::ScriptDebugCore::normalizeChunk(
							reference.path), doc.relativePath))
				{
					doc.editor->AddMarker(reference.line - 1, noTint,
						noTint, "script error", message);
					doc.errorLineMessages[reference.line - 1] = message;
				}
			}
		}
		if (broken && session.debugBreakLine > 0)
		{
			const ImU32 breakColor = IM_COL32(230, 180, 60, 255);
			doc.editor->AddMarker(session.debugBreakLine - 1, breakColor,
				IM_COL32(230, 180, 60, 40), "paused here",
				"execution paused at this line");
		}
		// the live parse verdict: the format's own parser rejecting the
		// CURRENT buffer (anchored to its line when the parser named one) -
		// same badge-not-tint presentation as the runtime errors
		if (!doc.parseState.valid)
		{
			const int line =
				doc.parseState.line > 0 ? doc.parseState.line - 1 : 0;
			doc.editor->AddMarker(line, noTint, noTint,
				"syntax error", doc.parseState.message);
			doc.errorLineMessages[line] = doc.parseState.message;
		}
	}

	//! the breakpoint gutter (Lua documents only): an invisible click target
	//! per visible line, a red dot where a breakpoint is set and an arrow on
	//! the paused line
	void drawGutterCell(EditorState& state, PlaySession& session,
		ScriptDocument& doc, TextEditor::Decorator& decorator)
	{
		const int line = decorator.line + 1;	// widget lines are zero-based
		ImGui::InvisibleButton("bp",
			ImVec2(decorator.width, decorator.height));
		// the gutter is the breakpoint click target on Lua documents only
		if (doc.isLua && ImGui::IsItemClicked())
		{
			state.breakpoints.toggle(doc.relativePath, line);
		}
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 minimum = ImGui::GetItemRectMin();
		const ImVec2 maximum = ImGui::GetItemRectMax();
		// two side-by-side slots so the glyphs never overlap: the error "!"
		// badge on the LEFT (in front of the line number), the breakpoint dot
		// / paused arrow on the RIGHT
		const float width = maximum.x - minimum.x;
		const ImVec2 centreY(0.0f, (minimum.y + maximum.y) * 0.5f);
		const ImVec2 dotCentre(minimum.x + width * 0.72f, centreY.y);
		const float radius = decorator.height * 0.28f;
		if (doc.isLua && state.breakpoints.has(doc.relativePath, line))
		{
			drawList->AddCircleFilled(dotCentre, radius,
				IM_COL32(229, 73, 58, 255));
		}
		else if (doc.isLua && ImGui::IsItemHovered())
		{
			drawList->AddCircle(dotCentre, radius, IM_COL32(229, 73, 58, 120));
		}
		// an error line carries a red "!" IN FRONT of the line number with
		// the message on hover - the annotation itself, nothing tinted. The
		// widget's gutter runs [margin][numbers][decorator][text], so the
		// badge overdraws into the one-glyph LEFT margin, located from the
		// widget's own layout formula (leftMargin/decorationMargin = 1 glyph,
		// digits from the line count - TextEditor.cpp updateSidebarWidth)
		const auto errorEntry = doc.errorLineMessages.find(decorator.line);
		if (errorEntry != doc.errorLineMessages.end())
		{
			const float glyph = decorator.glyphSize.x;
			// the widget's own digit formula, replicated verbatim (it rounds
			// UP at powers of ten - a hand-rolled loop would drift a glyph);
			// the sidebar runs [2-glyph margin][digits][1-glyph gap][this
			// decorator cell] (the margin width is the port's own patch)
			const int digits = static_cast<int>(
				std::log10(doc.editor->GetLineCount() + 1) + 1.0f);
			const float numbersLeft = minimum.x - (1 + digits) * glyph;
			const ImU32 badge = themeColor(Orkige::editorErrorTextColor());
			// the "!" sits in the margin, a small gap in front of the number
			const ImVec2 badgePos(numbersLeft - glyph, minimum.y);
			drawList->AddText(badgePos, badge, "!");
			// plus a red underline under the line number itself
			const float underlineY = minimum.y + decorator.height - 1.0f;
			drawList->AddLine(ImVec2(numbersLeft, underlineY),
				ImVec2(numbersLeft + digits * glyph, underlineY), badge, 1.5f);
			if (ImGui::IsItemHovered() || ImGui::IsMouseHoveringRect(badgePos,
				ImVec2(badgePos.x + glyph, badgePos.y + decorator.height)))
			{
				ImGui::SetTooltip("%s", errorEntry->second.c_str());
			}
		}
		if (session.debugBroken && line == session.debugBreakLine &&
			docMatchesBreakFile(doc, session.debugBreakFile))
		{
			// the paused-here arrow, drawn over/beside the dot
			const ImU32 arrowColor = IM_COL32(230, 180, 60, 255);
			const float half = decorator.height * 0.22f;
			drawList->AddTriangleFilled(
				ImVec2(dotCentre.x - half, centreY.y - half),
				ImVec2(dotCentre.x + half, centreY.y),
				ImVec2(dotCentre.x - half, centreY.y + half), arrowColor);
		}
	}

	//! draw one document as its own docked window; returns nothing (the
	//! caller reaps closed windows). Sets ui.focused when this one has focus.
	void drawDocumentWindow(EditorState& state, PlaySession& session,
		ScriptDocument& doc)
	{
		PanelState& ui = panel();
		// dock a NEW window into the shared node (beside Scene on the first
		// open); retry next frame until the target node exists
		if (!doc.dockAssigned)
		{
			ImGuiID target = ui.sharedDockId;
			if (target == 0)
			{
				ImGuiWindow* scene = ImGui::FindWindowByName("Scene");
				if (scene != nullptr && scene->DockId != 0)
				{
					target = scene->DockId;
				}
			}
			if (target != 0)
			{
				ImGui::SetNextWindowDockID(target, ImGuiCond_Always);
				doc.dockAssigned = true;
			}
		}
		if (doc.wantFocus)
		{
			ImGui::SetNextWindowFocus();
			doc.wantFocus = false;
		}
		ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
		ImGuiWindowFlags flags = doc.isDirty()
			? ImGuiWindowFlags_UnsavedDocument : 0;
		const bool shown = ImGui::Begin(doc.windowId.c_str(), &doc.open, flags);
		doc.dockId = ImGui::GetWindowDockID();
		if (doc.dockId != 0)
		{
			ui.sharedDockId = doc.dockId;
		}
		// right-clicking the (docked) tab: the standard tab actions. The
		// chosen action is DEFERRED - the menu runs mid-iteration over the
		// document list, the close-set applies after the loop
		// (computeTabsToClose owns the semantics)
		if (ImGui::BeginPopupContextItem())
		{
			std::size_t myIndex = 0;
			for (std::size_t i = 0; i < ui.docs.size(); ++i)
			{
				if (ui.docs[i].get() == &doc)
				{
					myIndex = i;
					break;
				}
			}
			const std::size_t count = ui.docs.size();
			auto pick = [&](OrkigeEditor::TabAction action)
			{
				ui.tabAction = action;
				ui.tabActionTarget = myIndex;
			};
			if (ImGui::MenuItem("Close"))
			{
				pick(OrkigeEditor::TabAction::Close);
			}
			if (ImGui::MenuItem("Close Others", nullptr, false, count > 1))
			{
				pick(OrkigeEditor::TabAction::CloseOthers);
			}
			if (ImGui::MenuItem("Close to the Right", nullptr, false,
				myIndex + 1 < count))
			{
				pick(OrkigeEditor::TabAction::CloseRight);
			}
			if (ImGui::MenuItem("Close All"))
			{
				pick(OrkigeEditor::TabAction::CloseAll);
			}
			ImGui::EndPopup();
		}
		if (!shown)
		{
			ImGui::End();
			return;
		}
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			ui.focused = &doc;
			state.scriptPanelFocused = true;
		}
		// a dirty document shows its save/discard controls right in the window
		// - the tab's unsaved dot alone offers no mouse path to save (the
		// Cmd/Ctrl+S shortcut and the native Save menu item work as well)
		if (doc.isDirty())
		{
			if (ImGui::SmallButton("Save"))
			{
				saveDocument(doc);
			}
			if (ImGui::IsItemHovered())
			{
#ifdef __APPLE__
				ImGui::SetTooltip("Save (Cmd+S)");
#else
				ImGui::SetTooltip("Save (Ctrl+S)");
#endif
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Revert"))
			{
				std::string text;
				if (readFileText(doc.absolutePath, text))
				{
					doc.editor->SetText(text);
					doc.savedUndoIndex = doc.editor->GetUndoIndex();
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"Discard the edits and reload the file from disk");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("unsaved changes");
		}
		liveSyntaxCheck(doc);
		refreshMarkers(doc, session);
		if (doc.pendingScrollLine > 0)
		{
			doc.editor->SetCursor(doc.pendingScrollLine - 1, 0);
			doc.editor->ScrollToLine(doc.pendingScrollLine - 1,
				TextEditor::Scroll::alignMiddle);
			doc.pendingScrollLine = 0;
		}
		// every document carries the gutter: on Lua it is the breakpoint
		// click target + dot; on every kind it shows the red "!" error
		// badges (drawGutterCell gates the Lua-only halves itself)
		{
			ScriptDocument* docPointer = &doc;
			EditorState* statePointer = &state;
			PlaySession* sessionPointer = &session;
			doc.editor->SetLineDecorator(-2.0f,
				[statePointer, sessionPointer, docPointer](
					TextEditor::Decorator& decorator)
				{
					drawGutterCell(*statePointer, *sessionPointer,
						*docPointer, decorator);
				});
		}
		ImFont* mono = Orkige::editorMonoFont();
		if (mono != nullptr)
		{
			ImGui::PushFont(mono);
		}
		doc.editor->Render("##code", ImVec2(0, 0));
		// the widget shows the text I-beam only while it is FOCUSED and the
		// mouse is over the text; hovering an UNfocused editor left the arrow.
		// The widget submits its child as the last item, so completing the
		// hover case here gives the I-beam over the whole code editor.
		if (ImGui::IsItemHovered())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		}
		if (mono != nullptr)
		{
			ImGui::PopFont();
		}
		ImGui::End();
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

	//! one transport button: an icon-font glyph with a "Name (Shortcut)" tooltip
	bool transportButton(const char* glyph, const char* tooltip)
	{
		const bool clicked = ImGui::Button(glyph);
		ImGui::SetItemTooltip("%s", tooltip);
		return clicked;
	}

	//! @brief can we arm a BREAK ON NEXT STATEMENT right now? A live desktop-ish
	//! session that is running or frame-paused and NOT already broken (the
	//! browser player cannot block its main thread, so a web session is out).
	bool canBreakNext(PlaySession const& session)
	{
		return session.client.isConnected() && !session.debugBroken &&
			!session.onBrowser &&
			(session.mode == PlaySession::Mode::Playing ||
				session.mode == PlaySession::Mode::Paused);
	}

	//! the debug transport (Break / Continue / Step Over / In / Out) - the
	//! keyboard shortcuts stay global (handleScriptDebugShortcuts). Break arms
	//! while running/paused; the rest drive a held break.
	void drawDebugTransport(PlaySession& session)
	{
		namespace Protocol = Orkige::DebugProtocol;
		// Break on Next Statement: the ONE control enabled while NOT broken -
		// it pauses into wherever the scripts run next
		ImGui::BeginDisabled(!canBreakNext(session));
		if (transportButton(ICON_FA_CIRCLE_PAUSE,
			"Break on Next Statement (Cmd/Ctrl+Alt+B)"))
		{
			sendDebugBreakNext(session);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!session.debugBroken);
		if (transportButton(ICON_FA_PLAY, "Continue (F5)"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_RESUME);
		}
		ImGui::SameLine();
		if (transportButton(ICON_FA_ARROW_RIGHT, "Step Over (F10)"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_OVER);
		}
		ImGui::SameLine();
		if (transportButton(ICON_FA_ARROW_DOWN, "Step In (F11)"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_IN);
		}
		ImGui::SameLine();
		if (transportButton(ICON_FA_ARROW_UP, "Step Out (Shift+F11)"))
		{
			sendDebugCommand(session, Protocol::MSG_DEBUG_STEP_OUT);
		}
		ImGui::EndDisabled();
	}

	//! the call-stack + locals panes (shown while broken)
	void drawDebugPanes(EditorState& state, ViewSettings& viewSettings,
		PlaySession& session)
	{
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
					requestDebugLocals(session, session.debugSelectedFrame, {});
					// jump the code editor to the frame's line
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
	}
}

//---------------------------------------------------------------------------
void scriptPanelOpenFile(EditorState& state, ViewSettings& viewSettings,
	std::string const& path, int line)
{
	(void)viewSettings;	// document windows carry no panel visibility flag
	state.scriptOpenRequest = path;
	state.scriptOpenLine = line;
}
//---------------------------------------------------------------------------
void scriptPanelCloseAll()
{
	panel().docs.clear();
	panel().focused = nullptr;
	panel().symbolsProjectRoot = "?";
}
//---------------------------------------------------------------------------
bool scriptPanelHasUnsavedEdits()
{
	for (auto const& doc : panel().docs)
	{
		if (doc->isDirty())
		{
			return true;
		}
	}
	return false;
}
//---------------------------------------------------------------------------
bool scriptPanelSaveActiveIfFocused(EditorState& state)
{
	if (!state.scriptPanelFocused || panel().focused == nullptr)
	{
		return false;
	}
	saveDocument(*panel().focused);
	return true;
}
//---------------------------------------------------------------------------
bool scriptDocumentDockedWithNode(unsigned int sceneDockId)
{
	if (sceneDockId == 0)
	{
		return false;
	}
	for (auto const& doc : panel().docs)
	{
		if (doc->open && doc->dockId == sceneDockId)
		{
			return true;
		}
	}
	return false;
}
//---------------------------------------------------------------------------
bool scriptPanelTestDirtyDocument(std::string const& path,
	std::string const& text)
{
	// canonical compare: the opener may store the symlink-resolved form of a
	// temp path (macOS /var vs /private/var)
	std::error_code ec;
	const fs::path wanted = fs::weakly_canonical(path, ec);
	for (auto const& doc : panel().docs)
	{
		const fs::path held = fs::weakly_canonical(doc->absolutePath, ec);
		if ((doc->absolutePath == path || held == wanted) && doc->editor)
		{
			// an undo-recorded edit - the buffer equivalent of typing (SetText
			// would reset the undo index and leave the document "clean")
			doc->editor->ReplaceTextInCurrentCursor(text);
			return doc->isDirty();
		}
	}
	return false;
}
//---------------------------------------------------------------------------
void scriptPanelTestCloseAll()
{
	PanelState& ui = panel();
	ui.tabAction = OrkigeEditor::TabAction::CloseAll;
	ui.tabActionTarget = 0;
}
//---------------------------------------------------------------------------
std::string scriptPanelTestConfirmPath()
{
	PanelState& ui = panel();
	return ui.confirmClose != nullptr ? ui.confirmClose->absolutePath
		: std::string();
}
//---------------------------------------------------------------------------
bool scriptPanelTestResolveConfirm(int choice)
{
	PanelState& ui = panel();
	if (ui.confirmClose == nullptr || choice < 0 || choice > 2)
	{
		return false;
	}
	resolveConfirmClose(ui, *ui.confirmClose,
		static_cast<ConfirmChoice>(choice));
	return true;
}
//---------------------------------------------------------------------------
std::size_t scriptPanelTestDocumentCount()
{
	return panel().docs.size();
}
//---------------------------------------------------------------------------
std::string scriptPanelActiveSyntaxError(std::string& outPath, int& outLine)
{
	PanelState& ui = panel();
	ScriptDocument* pick = nullptr;
	if (ui.focused != nullptr && !ui.focused->parseState.valid)
	{
		pick = ui.focused;
	}
	else
	{
		for (auto const& doc : ui.docs)
		{
			if (!doc->parseState.valid)
			{
				pick = doc.get();
				break;
			}
		}
	}
	if (pick == nullptr)
	{
		outPath.clear();
		outLine = 0;
		return std::string();
	}
	outPath = pick->absolutePath;
	outLine = pick->parseState.line;
	return pick->relativePath + (pick->parseState.line > 0
		? (":" + std::to_string(pick->parseState.line)) : std::string()) +
		": " + pick->parseState.message;
}
//---------------------------------------------------------------------------
void handleScriptDebugShortcuts(EditorState& state, PlaySession& session)
{
	namespace Protocol = Orkige::DebugProtocol;
	(void)state;
	ImGuiIO& io = ImGui::GetIO();
	const bool commandAlt = (io.KeySuper || io.KeyCtrl) && io.KeyAlt;
	// Break on Next Statement (Cmd/Ctrl+Alt+B): the one debug shortcut that
	// fires while the session is RUNNING (or frame-paused), not broken - it
	// arms a one-shot pause into wherever the scripts execute next
	if (canBreakNext(session) &&
		commandAlt && ImGui::IsKeyPressed(ImGuiKey_B, false))
	{
		sendDebugBreakNext(session);
	}
	if (!session.debugBroken)
	{
		return;
	}
	// platform-conventional function keys, plus Cmd/Ctrl+Alt letter alternates
	// for keyboards where the F-row is awkward (macOS defaults)
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
void drawScriptDocuments(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, ViewSettings& viewSettings)
{
	(void)core;
	PanelState& ui = panel();

	// consume a pending open request BEFORE drawing so the window exists
	if (!state.scriptOpenRequest.empty())
	{
		const std::string absolute = resolveProjectFilePath(state.project,
			state.scriptOpenRequest);
		openDocument(state, absolute.empty()
			? state.scriptOpenRequest : absolute, state.scriptOpenLine);
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
	const int variant = static_cast<int>(Orkige::currentEditorThemeVariant());
	if (ui.paletteVariant != variant)
	{
		ui.paletteVariant = variant;
		ui.palette = makeScriptPalette(Orkige::currentEditorThemeVariant());
		for (auto& doc : ui.docs)
		{
			doc->editor->SetPalette(ui.palette);
		}
	}

	// on a NEW break, open/focus its file at the hit line exactly once
	if (session.debugBroken && ui.focusedBreakSeq != session.debugBreakSeq)
	{
		ui.focusedBreakSeq = session.debugBreakSeq;
		if (!session.debugBreakFile.empty())
		{
			const std::string absolute = resolveProjectFilePath(
				state.project, session.debugBreakFile);
			openDocument(state, absolute.empty()
				? session.debugBreakFile : absolute, session.debugBreakLine);
		}
	}

	// the focus flag is recomputed each frame from the windows below
	state.scriptPanelFocused = false;
	ui.focused = nullptr;
	for (auto& doc : ui.docs)
	{
		drawDocumentWindow(state, session, *doc);
	}

	// a tab context-menu action picked during the draw loop applies now,
	// against the stable list (the pure close-set owns the semantics)
	if (ui.tabAction != OrkigeEditor::TabAction::None)
	{
		const std::vector<bool> close = OrkigeEditor::computeTabsToClose(
			ui.docs.size(), ui.tabActionTarget, ui.tabAction);
		for (std::size_t i = 0; i < close.size(); ++i)
		{
			if (close[i])
			{
				ui.docs[i]->open = false;
			}
		}
		ui.tabAction = OrkigeEditor::TabAction::None;
	}

	// closing sweep: a clean (or explicitly discarded) document goes away; a
	// DIRTY close stays open and queues for the save/discard/cancel ask below
	for (auto it = ui.docs.begin(); it != ui.docs.end();)
	{
		ScriptDocument& doc = **it;
		if (!doc.open)
		{
			if (doc.isDirty() && !doc.discardEdits)
			{
				doc.open = true;	// stays visible while we ask
				doc.closeRequested = true;
				++it;
				continue;
			}
			if (ui.focused == &doc)
			{
				ui.focused = nullptr;
			}
			if (ui.confirmClose == &doc)
			{
				ui.confirmClose = nullptr;
			}
			it = ui.docs.erase(it);
		}
		else
		{
			++it;
		}
	}
	// promote the next queued dirty close into the (single) modal slot
	if (ui.confirmClose == nullptr)
	{
		for (auto& doc : ui.docs)
		{
			if (doc->closeRequested)
			{
				ui.confirmClose = doc.get();
				break;
			}
		}
	}
	if (ui.confirmClose != nullptr)
	{
		ImGui::OpenPopup("Unsaved Changes###ScriptCloseConfirm");
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
			ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal("Unsaved Changes###ScriptCloseConfirm",
			nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ScriptDocument& doc = *ui.confirmClose;
			ImGui::Text("'%s' has unsaved changes.", doc.title.c_str());
			ImGui::Spacing();
			bool resolved = false;
			if (ImGui::Button("Save"))
			{
				resolveConfirmClose(ui, doc, ConfirmChoice::Save);
				resolved = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard"))
			{
				resolveConfirmClose(ui, doc, ConfirmChoice::Discard);
				resolved = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				resolveConfirmClose(ui, doc, ConfirmChoice::Cancel);
				resolved = true;
			}
			if (resolved)
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	// Cmd/Ctrl+S while a document window has focus (the native macOS menu
	// routes here too via saveCurrentDocument -> scriptPanelSaveActiveIfFocused)
	if (state.scriptPanelFocused && ui.focused != nullptr)
	{
		ImGuiIO& io = ImGui::GetIO();
		if ((io.KeySuper || io.KeyCtrl) && !io.KeyShift &&
			ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			saveDocument(*ui.focused);
		}
	}
}
//---------------------------------------------------------------------------
void drawDebugPanel(EditorState& state, PlaySession& session,
	ViewSettings& viewSettings, bool* visible)
{
	PanelState& ui = panel();

	// dock beside Console the first time the panel shows
	if (!ui.debugDockedOnce)
	{
		ImGuiWindow* console = ImGui::FindWindowByName("Console");
		if (console != nullptr && console->DockId != 0)
		{
			ImGui::SetNextWindowDockID(console->DockId, ImGuiCond_FirstUseEver);
			ui.debugDockedOnce = true;
		}
	}
	// on a NEW break, pull the panel to the front exactly once
	if (session.debugBroken && ui.debugFocusSeq != session.debugBreakSeq)
	{
		ui.debugFocusSeq = session.debugBreakSeq;
		ImGui::SetNextWindowFocus();
	}

	ImGui::SetNextWindowSize(ImVec2(560, 220), ImGuiCond_FirstUseEver);
	const bool debugShown = ImGui::Begin("Debug###Debug", visible);
	OrkigeEditor::editorPanelTabMenu(visible);
	if (!debugShown)
	{
		ImGui::End();
		return;
	}

	if (session.debugBroken)
	{
		ImGui::TextColored(ImVec4(0.90f, 0.71f, 0.24f, 1.0f), "Paused %s:%d",
			session.debugBreakFile.c_str(), session.debugBreakLine);
	}
	else
	{
		ImGui::TextDisabled("Not paused. Set a breakpoint (the gutter of a Lua "
			"document) and Play to debug.");
	}
	drawDebugTransport(session);
	ImGui::Separator();

	if (session.debugBroken)
	{
		drawDebugPanes(state, viewSettings, session);
	}
	else
	{
		ImGui::TextDisabled("(no active break)");
	}
	ImGui::End();
}
