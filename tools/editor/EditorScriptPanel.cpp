/********************************************************************
	created:	Friday 2026/07/24 at 12:00
	filename: 	EditorScriptPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorScriptPanel.cpp - the embedded code editor (a docked window per open
// file) and the script debugger's Debug panel.
//
// Each open file is its OWN docked ImGui window (title = a leading file-format
// glyph + file name, dirty marker, stable ###path id) built on the
// imgui-color-text-edit widget, so several open files read as sibling tabs in
// one dock node like every other panel; the glyph + its tab-label tint come
// from the ONE fallback map FileFormatIcon.h - the same one the asset
// browser draws unthumbnailed rows with. A document opens via Asset-browser
// double-click, the Inspector's
// "Open in Internal Editor" button or a one-shot EditorState::scriptOpenRequest
// (break-hits route through it). Syntax highlight follows the file kind -
// Lua, JSON, Markdown, a custom XML definition for the engine's XMLArchive/
// XLIFF formats, a custom config-text definition for its line-based
// .oui/.ogui/.omat/.oshape formats, plain text for anything else its
// extension is unrecognized (unless the CONTENT sniffs as XML/JSON - see
// OrkigeEditor::sniffTextDocumentKind); completion (from the engine's own
// truth - the generated Lua API index, the scriptable-component registry,
// live scripting-state globals and the document's identifiers) is Lua-only,
// and so is the breakpoint gutter.
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
#include "FileFormatIcon.h"	// the tab's leading glyph + tint
#include "EditorLineDiff.h"	// git-gutter change markers (pure line diff)
#include "ExternalEditor.h"	// parseFileLineRefs / FileLineRef (error markers)
#include "GeneratedLuaApi.h"
#include "IconsFontAwesome6.h"
#include "ScriptCompletion.h"

#include <core_base/TypeManager.h>
#include <core_script/ScriptRuntime.h>

#include <TextEditor.h>
#include <imgui_internal.h>	// FindWindowByName / GetWindowDockID (docking)

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <cctype>

namespace
{
	namespace fs = std::filesystem;

	//! one open code-editor document (its own docked window)
	struct ScriptDocument
	{
		std::string absolutePath;
		std::string relativePath;	//!< project-relative ('/'; abs when loose)
		std::string title;			//!< the window's tab label (file name)
		std::string windowId;		//!< "<glyph>  <title>###<absolutePath>" (stable per path)
		//! the file-kind tint (@see FileFormatIcon.h) pushed as ImGuiCol_Text
		//! for the Begin() call only, so the DOCKED TAB LABEL reads in the
		//! same family colour the asset browser draws that extension with -
		//! window body text stays the normal colour, popped right after
		//! Begin() returns
		ImU32 tabTint = IM_COL32_WHITE;
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
		//! says about the CURRENT buffer, refreshed once the text sits
		//! still. The kind itself is the pure, unit-tested
		//! OrkigeEditor::liveCheckKindForExtension (@see
		//! EditorTextDiagnostics.h for which formats stay honestly
		//! diagnostic-free).
		OrkigeEditor::LiveCheckKind liveCheck = OrkigeEditor::LiveCheckKind::None;
		std::size_t checkedUndoIndex = static_cast<std::size_t>(-1);
		std::size_t lastSeenUndoIndex = static_cast<std::size_t>(-1);
		int stableFrames = 0;			//!< frames the buffer sat unchanged
		OrkigeEditor::TextDiagnostic parseState;	//!< the current verdict
		unsigned int parseRevision = 0;	//!< bumps when parseState changes
		unsigned int markedParseRevision = 0;
		//! 0-based line -> message for every error the markers carry (the
		//! gutter's red "!" badges + their hover text; rebuilt with the markers)
		std::map<int, std::string> errorLineMessages;
		//! git change markers: the file's baseline (the git index blob) split
		//! into lines the widget's way, fetched at open + on save (never per
		//! keystroke). gitTracked stays false outside a repo / for an untracked
		//! file / when git is absent - the gutter then shows no change bars.
		bool gitChecked = false;		//!< a baseline fetch has been attempted
		bool gitTracked = false;		//!< a baseline exists (file is tracked)
		std::vector<std::string> gitBaselineLines;
		//! the current diff verdict (per-line states + deletion gaps) the gutter
		//! renders; recomputed from the live buffer vs the baseline, debounced
		OrkigeEditor::LineDiff gitDiff;
		std::size_t gitDiffUndoIndex = static_cast<std::size_t>(-1);
		std::size_t gitLastSeenUndoIndex = static_cast<std::size_t>(-1);
		int gitStableFrames = 0;		//!< frames the buffer sat unchanged

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
		//! the change-hunk inspection popup. A hover over a change marker shows a
		//! transient tooltip; a CLICK pins a floating window (Esc / click-away /
		//! its close box dismiss it) carrying the hunk's before/after and a
		//! Revert. The hunk is captured by VALUE so the popup survives a diff
		//! recompute; the before/after text is sliced live from the document.
		ScriptDocument* popupDoc = nullptr;	//!< the pinned popup's doc (null=none)
		OrkigeEditor::DiffHunk popupHunk;	//!< the pinned hunk
		int popupPinnedFrame = -1;			//!< the frame the pin click landed on
		//! per-frame hover (reset before the documents draw; set by the gutter)
		ScriptDocument* hoverDoc = nullptr;
		OrkigeEditor::DiffHunk hoverHunk;
		bool hoverActive = false;
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

	//! a minimal XML language definition authored through the widget's
	//! custom-language API (the engine's XMLArchive carriers -
	//! .oscene/.oprefab/.orkproj/.orkmeta/.olevels/.oactions/.olayers - plus
	//! XLIFF .xlf and a bare .xml): tag/attribute names as identifiers,
	//! quoted attribute values as strings, <!-- --> comments, and the
	//! markup punctuation coloured
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

	//! the engine's line-based config-text formats - `.oui`/`.ogui`
	//! (`[Section id]` headers, `key = value`/`key value` entries) and
	//! `.omat`/`.oshape` (no sections; each line opens with a directive
	//! keyword followed by numbers/asset names) - share one shape closely
	//! enough for one definition: `#`/`;` line comments, bare words as
	//! identifiers (the widget's per-token API has no notion of "start of
	//! line", so a key and an asset-name value read alike - both are
	//! bespoke words, same as the XML def above does not distinguish a tag
	//! name from an attribute name), numbers (leading '-' included, so
	//! "-1.00" colours as one literal), and the two BOUNDED vocabularies
	//! that are unambiguous wherever they appear: `.oui`'s widget section
	//! TYPES (declarations - the id after them stays a plain identifier) and
	//! `.omat`/`.oshape`'s directive words (keywords - exactly the "first
	//! word is a keyword" reading a directive line wants)
	const TextEditor::Language* orkigeConfigLanguage()
	{
		static const TextEditor::Language language = []
		{
			TextEditor::Language lang;
			lang.name = "OrkigeConfig";
			lang.caseSensitive = true;
			lang.singleLineComment = "#";
			lang.singleLineCommentAlt = ";";
			lang.isPunctuation = [](ImWchar c)
			{
				return c == '[' || c == ']' || c == '=' || c == ':';
			};
			auto isNameStart = [](ImWchar c)
			{
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
					c == '_';
			};
			auto isNameChar = [isNameStart](ImWchar c)
			{
				return isNameStart(c) || (c >= '0' && c <= '9') ||
					c == '.' || c == '-';
			};
			lang.getIdentifier = [isNameStart, isNameChar](
				TextEditor::Iterator start, TextEditor::Iterator end)
			{
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
			lang.getNumber = [](TextEditor::Iterator start,
				TextEditor::Iterator end)
			{
				auto isDigit = [](ImWchar c) { return c >= '0' && c <= '9'; };
				TextEditor::Iterator i = start;
				if (i != end && *i == '-')
				{
					++i;
				}
				if (i == end || !isDigit(*i))
				{
					return start;	// a bare '-'/non-numeric: no token
				}
				for (; i != end && isDigit(*i); ++i)
				{
				}
				if (i != end && *i == '.')
				{
					TextEditor::Iterator afterDot = i;
					++afterDot;
					if (afterDot != end && isDigit(*afterDot))
					{
						for (i = afterDot; i != end && isDigit(*i); ++i)
						{
						}
					}
				}
				return i;
			};
			// .oui's widget-tree section types (Util/make_gui_atlas.py's
			// .ogui carries its own [Texture]/[Font.N]/[Sprites] vocabulary,
			// which stays plain identifiers - harmless, never colliding)
			lang.declarations = {
				"Layout", "Label", "Button", "CheckBox", "TextBox", "Slider",
				"ProgressBar", "TextEntry", "DecorWidget", "Panel",
				"ScrollView", "ListView", "DropDown", "Modal", "ToggleGroup",
				"TabBar",
			};
			// .omat + .oshape directive words (@see core_util/MaterialAsset.h,
			// core_util/VectorShapeAsset.h)
			lang.keywords = {
				"version", "albedo", "albedoTexture", "metalness",
				"roughness", "normalTexture", "emissive", "emissiveTexture",
				"alphaTest", "twoSided",
				"fill", "contour", "v", "hole", "mask", "morph", "texture",
				"stroke",
			};
			return lang;
		}();
		return &language;
	}

	//! extensions the editor already treats as plain text ON PURPOSE (not
	//! merely undecided) - they do NOT fall through to the content sniff
	//! below even though languageForFile answers nullptr for them too
	bool isKnownPlainExtension(std::string const& ext)
	{
		return ext == ".txt" || ext == ".cmake" || ext == ".py";
	}

	//! the highlighter for a file kind (nullptr = plain text). JSON ships
	//! with the widget; the engine's XMLArchive/XLIFF formats get the custom
	//! XML definition; the engine's config-text formats get the custom
	//! OrkigeConfig definition; an extension this editor has never heard of
	//! sniffs its CONTENT (@see OrkigeEditor::sniffTextDocumentKind) rather
	//! than guessing blind - a known-plain extension (.txt/.cmake/.py) is
	//! excluded from that sniff, since it already has a settled (plain)
	//! answer, not an undecided one
	const TextEditor::Language* languageForFile(std::string const& path,
		std::string const& text)
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
		if (isKnownPlainExtension(ext))
		{
			return nullptr;
		}
		// the pure, unit-tested extension map (@see EditorTextDiagnostics.h):
		// the house XMLArchive carriers + XLIFF/bare .xml get the custom XML
		// definition, the line-based config-text family (.oui/.ogui/.omat/
		// .oshape) the custom OrkigeConfig one - the SAME classifier the
		// live-check routing (liveCheckKindForExtension) reads its Xml cases
		// from, so highlighting and diagnostics never drift out of sync
		switch (OrkigeEditor::textDocumentKindForExtension(ext))
		{
		case OrkigeEditor::TextDocumentKind::Xml:
			return xmlLanguage();
		case OrkigeEditor::TextDocumentKind::OrkigeConfig:
			return orkigeConfigLanguage();
		case OrkigeEditor::TextDocumentKind::Json:
			return TextEditor::Language::Json();
		case OrkigeEditor::TextDocumentKind::PlainText:
			break;	// fall through to the content sniff below
		}
		// a genuinely unrecognized extension: sniff the content instead of
		// defaulting silently to plain text (an agent-authored file with a
		// bespoke extension is still XML/JSON to the eye)
		switch (OrkigeEditor::sniffTextDocumentKind(text))
		{
		case OrkigeEditor::TextDocumentKind::Xml:
			return xmlLanguage();
		case OrkigeEditor::TextDocumentKind::Json:
			return TextEditor::Language::Json();
		case OrkigeEditor::TextDocumentKind::PlainText:
		case OrkigeEditor::TextDocumentKind::OrkigeConfig:
		default:
			return nullptr;
		}
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

	//! recompute the change-marker verdict from the LIVE buffer vs the git
	//! baseline (cheap; the caller debounces / size-gates it). Clears the
	//! verdict for an untracked document (no markers).
	void recomputeGitDiff(ScriptDocument& doc)
	{
		if (!doc.gitTracked || !doc.editor)
		{
			doc.gitDiff = OrkigeEditor::LineDiff();
			return;
		}
		doc.gitDiff = OrkigeEditor::computeLineDiff(doc.gitBaselineLines,
			OrkigeEditor::splitLines(doc.editor->GetText()));
		doc.gitDiffUndoIndex = doc.editor->GetUndoIndex();
	}

	//! revert ONE hunk in the buffer: the pure applyHunkRevert swaps the hunk's
	//! current lines for its baseline lines, and the whole-buffer result is
	//! applied through the widget's own text-mutation path (SelectAll +
	//! ReplaceTextInCurrentCursor - ONE transaction, so the widget's undo stack
	//! captures it as a single undoable step). The DISK is never touched (the
	//! user saves normally); the change markers refresh at once (the debounced
	//! live tick would reach the same verdict).
	void revertHunkInDocument(ScriptDocument& doc,
		OrkigeEditor::DiffHunk const& hunk)
	{
		if (!doc.editor)
		{
			return;
		}
		const std::vector<std::string> current =
			OrkigeEditor::splitLines(doc.editor->GetText());
		const std::vector<std::string> reverted = OrkigeEditor::applyHunkRevert(
			current, doc.gitBaselineLines, hunk);
		std::string text;
		for (std::size_t i = 0; i < reverted.size(); ++i)
		{
			text += reverted[i];
			if (i + 1 < reverted.size())
			{
				text += "\n";	// splitLines/join round-trips the buffer exactly
			}
		}
		doc.editor->SelectAll();
		doc.editor->ReplaceTextInCurrentCursor(text);
		recomputeGitDiff(doc);
	}

	//! fetch (or refetch) the git-index baseline for a document: resolve the
	//! repo root + relative path honestly (`git -C <dir> rev-parse
	//! --show-toplevel`, which handles worktrees), then read the staged blob
	//! (`git show :<relpath>`). A file outside a repo, an untracked file, or a
	//! machine with no git in PATH leaves the document UNTRACKED - no markers,
	//! silently (an untracked file's all-added spam helps nobody). Runs at open
	//! + on save, NEVER per keystroke.
	void refreshGitBaseline(ScriptDocument& doc)
	{
		doc.gitChecked = true;
		doc.gitTracked = false;
		doc.gitBaselineLines.clear();
		const std::string dir = fs::path(doc.absolutePath).parent_path().string();
		std::string output;
		int exitCode = 0;
		if (dir.empty() ||
			!runProcessCaptured({ "git", "-C", dir, "rev-parse",
				"--show-toplevel" }, output, exitCode) || exitCode != 0)
		{
			recomputeGitDiff(doc);	// no repo / git absent - stays untracked
			return;
		}
		std::string root = output;
		while (!root.empty() && (root.back() == '\n' || root.back() == '\r'))
		{
			root.pop_back();
		}
		std::error_code ec;
		const std::string relative =
			fs::relative(doc.absolutePath, root, ec).generic_string();
		if (root.empty() || ec || relative.empty() ||
			relative.rfind("..", 0) == 0)
		{
			recomputeGitDiff(doc);
			return;
		}
		std::string blob;
		if (!runProcessCaptured({ "git", "-C", root, "show", ":" + relative },
			blob, exitCode) || exitCode != 0)
		{
			recomputeGitDiff(doc);	// untracked file - no baseline, no markers
			return;
		}
		doc.gitTracked = true;
		doc.gitBaselineLines = OrkigeEditor::splitLines(blob);
		recomputeGitDiff(doc);
	}

	//! the debounced LIVE recompute: once the buffer has sat unchanged for a few
	//! frames (~300ms), refresh the change markers from the current text. A file
	//! past the size cap does not live-diff - it refreshes only at open / save.
	void gitDiffLiveTick(ScriptDocument& doc)
	{
		if (!doc.gitTracked || !doc.editor)
		{
			return;
		}
		const std::size_t undo = doc.editor->GetUndoIndex();
		if (undo != doc.gitLastSeenUndoIndex)
		{
			doc.gitLastSeenUndoIndex = undo;	// still typing - restart the wait
			doc.gitStableFrames = 0;
			return;
		}
		if (undo == doc.gitDiffUndoIndex || ++doc.gitStableFrames < 18)
		{
			return;
		}
		if (!OrkigeEditor::shouldLiveDiff(doc.editor->GetLineCount()))
		{
			doc.gitDiffUndoIndex = undo;	// too big to live-diff - await a save
			return;
		}
		recomputeGitDiff(doc);
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
		// re-baseline against the index: a save leaves the index untouched but a
		// stage done outside the editor may have moved it - the honest refetch
		refreshGitBaseline(doc);
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
		// a small leading file-kind glyph (@see FileFormatIcon.h), the SAME
		// mapping the asset browser draws icons from - visible portion only,
		// the id after ### keeps the window/docking identity stable per path
		const OrkigeEditor::FileFormatIcon formatIcon =
			OrkigeEditor::fileFormatIcon(lowerExt(key));
		doc->tabTint = IM_COL32(formatIcon.color.r, formatIcon.color.g,
			formatIcon.color.b, 255);
		doc->windowId = std::string(formatIcon.glyph) + "  " + doc->title +
			"###" + key;
		doc->isLua = lowerExt(key) == ".lua";
		// live parse diagnostics follow the format's own parser: Lua compiles
		// through the ScriptRuntime seam (a runtime seam, decided here since
		// liveCheckKindForExtension is pure); the XMLArchive/XLIFF kinds
		// parse via tinyxml2 and .omat/.oui wrap their own pure parsers
		// through the SAME classifier - the rest stay honestly diagnostic-
		// free (@see EditorTextDiagnostics.h)
		doc->liveCheck = OrkigeEditor::liveCheckKindForExtension(lowerExt(key));
		doc->editor = std::make_unique<TextEditor>();
		const TextEditor::Language* language = languageForFile(key, text);
		if (language != nullptr)
		{
			doc->editor->SetLanguage(language);
		}
		doc->editor->SetText(text);
		doc->editor->SetShowWhitespacesEnabled(false);
		doc->editor->SetPalette(ui.palette);
		doc->savedUndoIndex = doc->editor->GetUndoIndex();
		// the git-index baseline for the gutter change markers (untracked / no
		// repo / no git leaves it markerless, silently)
		refreshGitBaseline(*doc);
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
	//! honestly in scripting-off builds); the XMLArchive/XLIFF kinds parse
	//! via the pure tinyxml2 probe; .omat/.oui wrap MaterialAsset::parse /
	//! GuiLayoutDoc::parse. The verdict feeds refreshMarkers through
	//! parseRevision.
	void liveSyntaxCheck(ScriptDocument& doc)
	{
		if (doc.liveCheck == OrkigeEditor::LiveCheckKind::None || !doc.editor)
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
		if (doc.liveCheck == OrkigeEditor::LiveCheckKind::Lua)
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
		else if (doc.liveCheck == OrkigeEditor::LiveCheckKind::Xml)
		{
			verdict = OrkigeEditor::xmlDiagnostic(doc.editor->GetText());
		}
		else if (doc.liveCheck == OrkigeEditor::LiveCheckKind::Omat)
		{
			verdict = OrkigeEditor::omatDiagnostic(doc.editor->GetText());
		}
		else
		{
			verdict = OrkigeEditor::ouiDiagnostic(doc.editor->GetText());
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

	//! one tinted code line inside a hunk popup: a full-width background band
	//! (removed = red, added = green) with the line's text over it. An empty
	//! line still shows a band so a blank-line change reads.
	void drawTintedCodeLine(std::string const& text, ImU32 background)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		float width = ImGui::GetContentRegionAvail().x;
		width = std::max(width, 160.0f);
		const float height = ImGui::GetTextLineHeight();
		ImGui::GetWindowDrawList()->AddRectFilled(pos,
			ImVec2(pos.x + width, pos.y + height), background);
		ImGui::TextUnformatted(text.empty() ? " " : text.c_str());
	}

	//! render one line list (baseline or current) tinted + clamped to
	//! kMaxHunkPreviewLines, then an honest "... N more" when it overflows
	void drawHunkLineList(std::vector<std::string> const& lines, ImU32 background)
	{
		int remaining = 0;
		const int shown =
			OrkigeEditor::clampHunkPreview(static_cast<int>(lines.size()),
				remaining);
		for (int i = 0; i < shown; ++i)
		{
			drawTintedCodeLine(lines[i], background);
		}
		if (remaining > 0)
		{
			ImGui::TextDisabled("... %d more", remaining);
		}
	}

	//! the hunk-inspection popup body (shared by the hover tooltip and the
	//! pinned window): the original (baseline) lines red-tinted and, when the
	//! hunk kept current lines, the current lines green-tinted - a compact
	//! before/after. The pinned window adds the Revert button.
	void drawHunkPopupBody(ScriptDocument& doc,
		OrkigeEditor::DiffHunk const& hunk, bool pinned)
	{
		using OrkigeEditor::HunkKind;
		const char* kindText = hunk.kind == HunkKind::Added ? "Added"
			: (hunk.kind == HunkKind::Deleted ? "Removed" : "Modified");
		ImGui::TextDisabled("%s", kindText);
		const std::vector<std::string> baseLines =
			OrkigeEditor::hunkBaselineLines(doc.gitBaselineLines, hunk);
		std::vector<std::string> curLines;
		if (hunk.kind != HunkKind::Deleted && doc.editor)
		{
			curLines = OrkigeEditor::hunkCurrentLines(
				OrkigeEditor::splitLines(doc.editor->GetText()), hunk);
		}
		ImFont* mono = Orkige::editorMonoFont();
		if (mono != nullptr)
		{
			ImGui::PushFont(mono);
		}
		// the baseline (original) lines - red; a Deleted hunk shows only these
		const ImU32 removedBg = IM_COL32(120, 44, 40, 90);
		const ImU32 addedBg = IM_COL32(44, 104, 52, 90);
		if (!baseLines.empty())
		{
			drawHunkLineList(baseLines, removedBg);
		}
		if (!curLines.empty())
		{
			if (!baseLines.empty())
			{
				ImGui::Spacing();
			}
			drawHunkLineList(curLines, addedBg);
		}
		if (mono != nullptr)
		{
			ImGui::PopFont();
		}
		if (pinned)
		{
			ImGui::Separator();
			if (ImGui::Button("Revert Hunk"))
			{
				revertHunkInDocument(doc, hunk);
				panel().popupDoc = nullptr;	// the marker is gone - close the popup
			}
			ImGui::SameLine();
			ImGui::TextDisabled("restores the index version in the buffer");
		}
	}

	//! after a document renders, present its change-hunk popup(s): a hover
	//! tooltip (from the gutter's per-frame hover record) and, when this
	//! document owns the pinned popup, the floating window with Revert. The
	//! pinned window dismisses on Esc, its close box, or a click-away.
	void drawHunkPopups(ScriptDocument& doc)
	{
		PanelState& ui = panel();
		// the hover tooltip - unless this exact hunk is already pinned here
		const bool pinnedHere = ui.popupDoc == &doc;
		if (ui.hoverActive && ui.hoverDoc == &doc &&
			!(pinnedHere && ui.popupHunk.curStart == ui.hoverHunk.curStart &&
				ui.popupHunk.baseStart == ui.hoverHunk.baseStart))
		{
			if (ImGui::BeginTooltip())
			{
				drawHunkPopupBody(doc, ui.hoverHunk, false);
				ImGui::EndTooltip();
			}
		}
		if (!pinnedHere)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(
			ImVec2(ImGui::GetMousePos().x + 14.0f, ImGui::GetMousePos().y + 14.0f),
			ImGuiCond_Appearing);
		bool open = true;
		if (ImGui::Begin("Change Hunk###hunkPopup", &open,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			drawHunkPopupBody(doc, ui.popupHunk, true);
		}
		const bool hovered = ImGui::IsWindowHovered(
			ImGuiHoveredFlags_RootAndChildWindows);
		ImGui::End();
		// dismiss: the close box, Esc, or a click OUTSIDE (but not the very
		// click that pinned it this frame - the gutter click lands elsewhere)
		const bool clickAway = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!hovered && ImGui::GetFrameCount() != ui.popupPinnedFrame;
		if (!open || clickAway ||
			ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			ui.popupDoc = nullptr;
		}
	}

	//! the breakpoint gutter (Lua documents only): an invisible click target
	//! per visible line, a red dot where a breakpoint is set and an arrow on
	//! the paused line
	void drawGutterCell(EditorState& state, PlaySession& session,
		ScriptDocument& doc, bool showGitMarkers,
		TextEditor::Decorator& decorator)
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
		// git change markers ride the FAR-LEFT gutter edge (before the number
		// margin), so they never disturb the line numbers, the breakpoint dot or
		// the error "!". A green bar = an added line, blue = a modified line, a
		// small red triangle marks where a run of lines was deleted. Hovering a
		// marker inspects the hunk; a click pins the popup (@see drawHunkPopups).
		if (showGitMarkers && !doc.gitDiff.states.empty())
		{
			const float glyph = decorator.glyphSize.x;
			// the widget's own digit formula (see the error badge below) locates
			// the number column; the change bar sits a margin-width to its left
			const int digits = static_cast<int>(
				std::log10(doc.editor->GetLineCount() + 1) + 1.0f);
			const float numbersLeft = minimum.x - (1 + digits) * glyph;
			const float barLeft = numbersLeft - 2.0f * glyph + 1.0f;
			const float barWidth = std::max(2.0f, glyph * 0.22f);
			if (decorator.line <
				static_cast<int>(doc.gitDiff.states.size()))
			{
				ImU32 colour = 0;
				switch (doc.gitDiff.states[decorator.line])
				{
				case OrkigeEditor::LineChange::Added:
					colour = IM_COL32(80, 170, 90, 255);
					break;
				case OrkigeEditor::LineChange::Modified:
					colour = IM_COL32(70, 140, 210, 255);
					break;
				case OrkigeEditor::LineChange::None:
					break;
				}
				if (colour != 0)
				{
					drawList->AddRectFilled(ImVec2(barLeft, minimum.y),
						ImVec2(barLeft + barWidth, maximum.y), colour);
				}
			}
			// deletion triangles: a gap sits ABOVE this line's index (drawn at
			// the top edge), and an end-of-file deletion (index == line count)
			// hangs off the last line's bottom edge
			const std::vector<int>& gaps = doc.gitDiff.deletions;
			const ImU32 deletionColour = IM_COL32(210, 90, 80, 255);
			const float triangle = decorator.height * 0.30f;
			auto drawDeletion = [&](float edgeY)
			{
				drawList->AddTriangleFilled(ImVec2(barLeft, edgeY - triangle),
					ImVec2(barLeft, edgeY + triangle),
					ImVec2(barLeft + triangle, edgeY), deletionColour);
			};
			if (std::binary_search(gaps.begin(), gaps.end(), decorator.line))
			{
				drawDeletion(minimum.y);
			}
			if (decorator.line == doc.editor->GetLineCount() - 1 &&
				std::binary_search(gaps.begin(), gaps.end(),
					doc.editor->GetLineCount()))
			{
				drawDeletion(maximum.y);
			}
			// hover/click inspection: the marker strip (the change-bar column,
			// left of the line numbers) is a hover+click target. A hover records
			// this line's hunk for the tooltip drawHunkPopups shows after Render;
			// a click PINS it. The change bar sits left of the breakpoint
			// InvisibleButton, so this reads the raw mouse without stealing it.
			int hunkIndex =
				OrkigeEditor::hunkForCurrentLine(doc.gitDiff, decorator.line);
			if (hunkIndex < 0)
			{
				hunkIndex =
					OrkigeEditor::hunkForDeletionGap(doc.gitDiff, decorator.line);
				if (hunkIndex < 0 &&
					decorator.line == doc.editor->GetLineCount() - 1)
				{
					// the end-of-file deletion hangs off the last line's bottom
					hunkIndex = OrkigeEditor::hunkForDeletionGap(doc.gitDiff,
						doc.editor->GetLineCount());
				}
			}
			if (hunkIndex >= 0)
			{
				const ImVec2 hitMin(barLeft - 2.0f, minimum.y);
				const ImVec2 hitMax(numbersLeft, maximum.y);
				if (ImGui::IsMouseHoveringRect(hitMin, hitMax))
				{
					PanelState& ui = panel();
					OrkigeEditor::DiffHunk const& hunk =
						doc.gitDiff.hunks[hunkIndex];
					ui.hoverActive = true;
					ui.hoverDoc = &doc;
					ui.hoverHunk = hunk;
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						const bool samePinned = ui.popupDoc == &doc &&
							ui.popupHunk.curStart == hunk.curStart &&
							ui.popupHunk.baseStart == hunk.baseStart;
						ui.popupDoc = samePinned ? nullptr : &doc;
						ui.popupHunk = hunk;
						ui.popupPinnedFrame = ImGui::GetFrameCount();
					}
				}
			}
		}
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
		ViewSettings& viewSettings, ScriptDocument& doc)
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
		// tint the DOCKED TAB LABEL (only) in the file-kind colour: ImGui
		// captures the currently-pushed ImGuiCol_Text into this window's own
		// per-tab style the moment Begin() docks it, so popping right after
		// Begin() returns leaves the window BODY text at the normal colour -
		// only the tab strip reads the tint
		ImGui::PushStyleColor(ImGuiCol_Text, doc.tabTint);
		const bool shown = ImGui::Begin(doc.windowId.c_str(), &doc.open, flags);
		ImGui::PopStyleColor();
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
		// the header row: the git change-markers toggle (always present, so it
		// is the panel's "small control"), then the dirty document's Save/Revert
		{
			const bool on = viewSettings.showScriptGitMarkers;
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(
				on ? ImGuiCol_Text : ImGuiCol_TextDisabled));
			if (ImGui::SmallButton(ICON_FA_CODE_COMPARE "###gitMarkers"))
			{
				viewSettings.showScriptGitMarkers = !on;
				viewSettings.save();
			}
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Gutter change markers: %s\n"
					"green added, blue modified, red deletion - versus the git "
					"index. Tracked files in a git repo only.",
					on ? "on" : "off");
			}
		}
		// a dirty document shows its save/discard controls right in the window
		// - the tab's unsaved dot alone offers no mouse path to save (the
		// Cmd/Ctrl+S shortcut and the native Save menu item work as well)
		if (doc.isDirty())
		{
			ImGui::SameLine();
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
					recomputeGitDiff(doc);	// baseline unchanged; refresh markers
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
		gitDiffLiveTick(doc);
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
			const bool showGitMarkers = viewSettings.showScriptGitMarkers;
			doc.editor->SetLineDecorator(-2.0f,
				[statePointer, sessionPointer, docPointer, showGitMarkers](
					TextEditor::Decorator& decorator)
				{
					drawGutterCell(*statePointer, *sessionPointer,
						*docPointer, showGitMarkers, decorator);
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
		// the change-hunk popup rides OUTSIDE the document window (a top-level
		// tooltip + the pinned floating window), fed by the gutter's per-frame
		// hover/pin record captured during this document's Render just above
		if (viewSettings.showScriptGitMarkers)
		{
			drawHunkPopups(doc);
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
bool scriptPanelTestGitMarkers(std::string const& path, bool showMarkers,
	std::vector<int>& outStates, std::vector<int>& outDeletions)
{
	outStates.clear();
	outDeletions.clear();
	std::error_code ec;
	const fs::path wanted = fs::weakly_canonical(path, ec);
	for (auto const& doc : panel().docs)
	{
		const fs::path held = fs::weakly_canonical(doc->absolutePath, ec);
		if (doc->absolutePath != path && held != wanted)
		{
			continue;
		}
		if (!doc->gitTracked)
		{
			return false;	// untracked / no baseline - the gutter shows nothing
		}
		// the toggle-off case reads exactly as the gutter draws it: empty
		if (!showMarkers)
		{
			return true;
		}
		recomputeGitDiff(*doc);	// synchronous, so the read is deterministic
		for (OrkigeEditor::LineChange change : doc->gitDiff.states)
		{
			outStates.push_back(static_cast<int>(change));
		}
		outDeletions = doc->gitDiff.deletions;
		return true;
	}
	return false;
}
//---------------------------------------------------------------------------
bool scriptPanelTestApplyGitEditProbe(std::string const& path)
{
	std::error_code ec;
	const fs::path wanted = fs::weakly_canonical(path, ec);
	for (auto const& doc : panel().docs)
	{
		const fs::path held = fs::weakly_canonical(doc->absolutePath, ec);
		if ((doc->absolutePath != path && held != wanted) || !doc->editor ||
			!doc->gitTracked || doc->gitBaselineLines.size() < 2)
		{
			continue;
		}
		std::vector<std::string> lines = doc->gitBaselineLines;
		lines[0] += " -- probe-modified";	// a modified first line
		// a brand-new line after the unchanged anchor line 1 so the modify and
		// the add stay DISTINCT hunks (adjacent they would merge into one)
		lines.insert(lines.begin() + 2, "-- probe-added line");
		std::string text;
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			text += lines[i];
			if (i + 1 < lines.size())
			{
				text += "\n";
			}
		}
		doc->editor->SetText(text);	// buffer only - the disk is never touched
		return true;
	}
	return false;
}
//---------------------------------------------------------------------------
namespace
{
	//! the open document at `path` (canonical-compared), or nullptr
	ScriptDocument* findOpenDocument(std::string const& path)
	{
		std::error_code ec;
		const fs::path wanted = fs::weakly_canonical(path, ec);
		for (auto const& doc : panel().docs)
		{
			const fs::path held = fs::weakly_canonical(doc->absolutePath, ec);
			if (doc->absolutePath == path || held == wanted)
			{
				return doc.get();
			}
		}
		return nullptr;
	}
}
//---------------------------------------------------------------------------
bool scriptPanelTestApplySingleHunkEdit(std::string const& path)
{
	ScriptDocument* doc = findOpenDocument(path);
	if (doc == nullptr || !doc->editor || !doc->gitTracked ||
		doc->gitBaselineLines.empty())
	{
		return false;
	}
	// modify ONLY the first baseline line -> exactly one Modified hunk
	std::vector<std::string> lines = doc->gitBaselineLines;
	lines[0] += " -- single-hunk probe";
	std::string text;
	for (std::size_t i = 0; i < lines.size(); ++i)
	{
		text += lines[i];
		if (i + 1 < lines.size())
		{
			text += "\n";
		}
	}
	doc->editor->SetText(text);	// buffer only - the disk is never touched
	return true;
}
//---------------------------------------------------------------------------
bool scriptPanelTestHunkSlice(std::string const& path, int hunkIndex,
	std::vector<std::string>& outBaseline, std::vector<std::string>& outCurrent)
{
	outBaseline.clear();
	outCurrent.clear();
	ScriptDocument* doc = findOpenDocument(path);
	if (doc == nullptr || !doc->editor || !doc->gitTracked)
	{
		return false;
	}
	recomputeGitDiff(*doc);	// synchronous, deterministic read
	if (hunkIndex < 0 ||
		hunkIndex >= static_cast<int>(doc->gitDiff.hunks.size()))
	{
		return false;
	}
	OrkigeEditor::DiffHunk const& hunk = doc->gitDiff.hunks[hunkIndex];
	outBaseline = OrkigeEditor::hunkBaselineLines(doc->gitBaselineLines, hunk);
	outCurrent = OrkigeEditor::hunkCurrentLines(
		OrkigeEditor::splitLines(doc->editor->GetText()), hunk);
	return true;
}
//---------------------------------------------------------------------------
bool scriptPanelTestRevertFirstHunk(std::string const& path,
	std::string& outPre, std::string& outPost, std::string& outAfterUndo,
	int& outHunksAfter, int& outChangedLinesAfter)
{
	ScriptDocument* doc = findOpenDocument(path);
	if (doc == nullptr || !doc->editor || !doc->gitTracked)
	{
		return false;
	}
	recomputeGitDiff(*doc);
	if (doc->gitDiff.hunks.empty())
	{
		return false;
	}
	outPre = doc->editor->GetText();
	const OrkigeEditor::DiffHunk hunk = doc->gitDiff.hunks.front();
	revertHunkInDocument(*doc, hunk);	// widget-undoable path + marker refresh
	outPost = doc->editor->GetText();
	outHunksAfter = static_cast<int>(doc->gitDiff.hunks.size());
	outChangedLinesAfter = 0;
	for (OrkigeEditor::LineChange change : doc->gitDiff.states)
	{
		if (change != OrkigeEditor::LineChange::None)
		{
			++outChangedLinesAfter;
		}
	}
	outChangedLinesAfter +=
		static_cast<int>(doc->gitDiff.deletions.size());
	// the widget's undo stack captured the revert as ONE step - undo restores
	// the pre-revert buffer exactly
	doc->editor->Undo();
	recomputeGitDiff(*doc);
	outAfterUndo = doc->editor->GetText();
	return true;
}
//---------------------------------------------------------------------------
bool scriptPanelTestNavigateHunk(std::string const& path, int fromLine,
	bool forward, int& outLine)
{
	outLine = -1;
	ScriptDocument* doc = findOpenDocument(path);
	if (doc == nullptr || !doc->editor || !doc->gitTracked)
	{
		return false;
	}
	recomputeGitDiff(*doc);
	if (doc->gitDiff.hunks.empty())
	{
		return false;
	}
	outLine = OrkigeEditor::navigateHunkLine(doc->gitDiff.hunks, fromLine,
		forward);
	return true;
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
	// the gutter re-records the change-hunk hover each frame (cleared here)
	ui.hoverActive = false;
	ui.hoverDoc = nullptr;
	for (auto& doc : ui.docs)
	{
		drawDocumentWindow(state, session, viewSettings, *doc);
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
			if (ui.popupDoc == &doc)
			{
				ui.popupDoc = nullptr;	// the pinned hunk popup outlives nothing
			}
			if (ui.hoverDoc == &doc)
			{
				ui.hoverDoc = nullptr;
				ui.hoverActive = false;
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
		// change navigation: Cmd/Ctrl+Alt+Down / +Up jump the editor to the next
		// / previous git change hunk (the same Cmd/Ctrl+Alt chord family the
		// debugger's step alternates use; the widget takes bare Alt+arrows for
		// move-line, so the chord stays clear of it). Reuses the scroll+cursor
		// path the error footer already drives via pendingScrollLine.
		const bool commandAlt = (io.KeySuper || io.KeyCtrl) && io.KeyAlt;
		ScriptDocument& focused = *ui.focused;
		if (commandAlt && focused.gitTracked && focused.editor &&
			!focused.gitDiff.hunks.empty())
		{
			int direction = 0;
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
			{
				direction = 1;
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
			{
				direction = -1;
			}
			if (direction != 0)
			{
				int cursorLine = 0;
				int cursorColumn = 0;
				focused.editor->GetMainCursor(cursorLine, cursorColumn);
				const int target = OrkigeEditor::navigateHunkLine(
					focused.gitDiff.hunks, cursorLine, direction > 0);
				if (target >= 0)
				{
					const int clamped = std::min(target,
						focused.editor->GetLineCount() - 1);
					focused.pendingScrollLine = clamped + 1;	// 1-based
				}
			}
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

	if (session.debugBroken && !session.debugBreakError.empty())
	{
		// an error break: the crash message is the headline (distinct from a
		// breakpoint pause). The honest failure still flows on Continue.
		ImGui::TextColored(Orkige::editorErrorTextColor(),
			"SCRIPT ERROR %s:%d", session.debugBreakFile.c_str(),
			session.debugBreakLine);
		ImGui::TextWrapped("%s", session.debugBreakError.c_str());
	}
	else if (session.debugBroken)
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
	ImGui::SameLine();
	// "Break on Errors": persisted in ViewSettings and pushed to a running
	// player on connect + on toggle (updatePlaySession). Armed = a runtime Lua
	// error PAUSES the game at the error instead of just disabling the instance.
	if (ImGui::Checkbox("Break on Errors", &viewSettings.breakOnScriptErrors))
	{
		viewSettings.save();
	}
	ImGui::SetItemTooltip("%s", "Pause the game AT an uncaught Lua error (jump "
		"to the erroring line with its stack + locals). On Continue the error "
		"still disables the instance - this only defers it.");
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
