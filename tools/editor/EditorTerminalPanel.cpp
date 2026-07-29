/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalPanel.cpp - the editor's embedded terminals: an OS pty (the
// EditorTerminalPty seam) feeding the libvterm-backed screen model
// (EditorTerminalScreen), rendered as a mono-font cell grid, with keyboard
// input encoded by the pure EditorTerminalKeys table. Each session is its OWN
// dockable ImGui window (the code editor's one-window-per-file pattern), sibling
// tabs in the bottom dock group, each titled with what is running inside it (the
// OSC title, else the pty's foreground process name; a recognised agent CLI gets
// its generated badge glyph) via the pure EditorTerminalSession helpers. Because
// a dock-tab title renders in the DEFAULT UI font (which carries the icon/badge
// glyphs), the title identifies its tenant crisply. When the editor's MCP
// endpoint is live, the spawned shell's environment carries the connection
// material and a compact `claude mcp add ...` copy affordance is offered, so an
// agent started inside can drive the very editor it lives in.
//
// The terminal is DELIBERATELY not an MCP verb: a headless agent spawning UI
// shells is out of scope (and a laundering path). It is a human affordance.
#include "EditorApp.h"
#include "EditorTerminalPanel.h"
#include "EditorTerminalPty.h"
#include "EditorTerminalScreen.h"
#include "EditorTerminalKeys.h"
#include "EditorTerminalSession.h"
#include "EditorTheme.h"
#include "ExternalEditor.h"	// terminalPathTokenAt / resolveTerminalPath (Cmd-click open)
#include "IconsFontAwesome6.h"

#include "engine_util/PlatformWindow.h"	// probeDeadClipboardRequestor (selfcheck)

#include <imgui_internal.h>	// FindWindowByName + DockId (first-appearance dock)

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using namespace OrkigeEditor;

	constexpr std::size_t kReadCap = 64 * 1024;	//!< bounded pty read per frame
	constexpr int kScrollbackLines = 5000;
	//! how often the pty's foreground process name is polled (a syscall + a
	//! /proc read or libproc call - cheap, but never per frame)
	constexpr std::chrono::milliseconds kProcPollInterval{ 1000 };

	//! one terminal session: its pty, VT screen, view/selection state and the
	//! two app-detection signals (the OSC title + the polled foreground process
	//! name) that drive its tab label. The panel holds a LIST of these.
	struct TerminalSession
	{
		std::unique_ptr<TerminalPty>			pty;
		std::unique_ptr<EditorTerminalScreen>	screen;
		int		cols = 0;
		int		rows = 0;
		bool	spawned = false;
		bool	exited = false;
		std::string	shell;
		std::string	lastError;
		bool	followTail = true;	//!< keep the view pinned to the newest line
		//!< the user typed/pasted last frame - re-pin + jump to the prompt on the
		//!< next scroll decision (input is handled after the grid submits, so the
		//!< re-pin lands one frame later, imperceptibly)
		bool	sentInputPending = false;
		//!< the grid's absolute line count last time it was drawn; a jump (new
		//!< output, a resize, or a re-shown backgrounded tab) means the content
		//!< grew and a pinned view must re-glue to the tail
		int		lastTotalLines = -1;
		int		uid = 0;			//!< stable, monotonic ImGui id suffix
		bool	open = true;		//!< the session's dock window is open
		bool	dockAssigned = false;	//!< first-appearance dock done
		bool	closeRequested = false;	//!< an alive session queued for confirm

		// app-aware window title: the last OSC title + the last polled foreground
		// process name; either can be empty. terminalSessionTabLabel() composes
		// the tab label from the STICKY classification below plus these signals.
		std::string	vtTitle;
		std::string	procName;
		std::chrono::steady_clock::time_point lastProcPoll{};
		// STICKY per-session agent classification: set from process name AND
		// title, held across status-ticker title flips until the foreground
		// reverts to a shell (terminalUpdateStickyAgent). The live title becomes
		// the tab tooltip, never the classified tab's label.
		TerminalAgent	stickyAgent = TerminalAgent::None;

		// linear selection in ABSOLUTE-line coords (scrollback + visible)
		bool	selecting = false;
		bool	hasSelection = false;
		int		anchorLine = 0;
		int		anchorCol = 0;
		int		headLine = 0;
		int		headCol = 0;
	};

	//! the terminals' process-static session LIST - one dockable window each (v1:
	//! not persisted across editor runs, a fresh launch starts with one shell).
	//! Torn down at terminalPanelShutdown().
	struct TerminalPanelState
	{
		std::vector<std::unique_ptr<TerminalSession>>	sessions;
		int		nextUid = 1;		//!< monotonic id source for stable window ids
		int		pendingClose = -1;	//!< a live session queued for close-confirm
		int		pendingSpawns = 0;	//!< New Terminal requests awaiting a spawn
		bool	prevPanelOpen = false;	//!< last frame's panel flag (edge detect)
		int		lastCols = 80;		//!< last rendered grid size, seeds background
		int		lastRows = 24;		//!< sessions never yet made the frame's front
		unsigned int	sharedDockId = 0;	//!< the bottom dock node siblings share
	};

	TerminalPanelState& panel()
	{
		static TerminalPanelState instance;
		return instance;
	}

	//! real-event test state (ORKIGE_EDITOR_TERMINAL_UITEST): the opt-in that
	//! lets drawTerminalPanel run under an automated run, and the probe the
	//! front session republishes each frame so the driver can target cells +
	//! assert the selection/focus the REAL mouse + copy path produced.
	bool gTerminalAutomatedUiTest = false;
	OrkigeEditor::TerminalPanelProbe gTerminalProbe;

	//! the leading glyph for a session's window title: a recognised agent draws
	//! its GENERATED private-use badge (baked into the UI-font atlas), everything
	//! else the plain terminal glyph. Returned as a UTF-8 string so the badge
	//! codepoint composes with the title text.
	std::string leadingGlyph(TerminalTabLabel const& label)
	{
		if (label.agent != TerminalAgent::None)
		{
			return encodeUtf8(terminalAgentBadgeCodepoint(label.agent));
		}
		return label.glyph == TerminalGlyphClass::Agent
			? ICON_FA_ROBOT : ICON_FA_TERMINAL;
	}

	ImU32 cellColor(TermColor c)
	{
		return IM_COL32(c.r, c.g, c.b, 255);
	}

	//! spawn the shell for `state`'s project (cwd = project root, else home),
	//! seeding the MCP env when the endpoint is live.
	void spawnSession(TerminalSession& s, EditorState& state,
		std::string const& mcpUrl, std::string const& mcpTokenFile,
		int cols, int rows)
	{
		s.pty = createTerminalPty();
		s.screen = std::make_unique<EditorTerminalScreen>(
			std::max(1, cols), std::max(1, rows), kScrollbackLines);
		TermPtySpec spec;
		s.shell = defaultShell();
		spec.shell = s.shell;
		spec.loginShell = true;
		spec.cols = std::max(1, cols);
		spec.rows = std::max(1, rows);
		if (state.project.isLoaded())
		{
			spec.cwd = state.project.getRootDirectory();
		}
		if (!mcpUrl.empty())
		{
			spec.env.emplace_back("ORKIGE_MCP_URL", mcpUrl);
			if (!mcpTokenFile.empty())
			{
				spec.env.emplace_back("ORKIGE_MCP_TOKEN_FILE", mcpTokenFile);
			}
		}
		// Claude-IDE auto-connect: when this editor hosts the IDE endpoint,
		// seed a spawned `claude` so it selects THIS editor as its IDE (the
		// discovery env the CLI reads - it finds our ~/.claude/ide/<port>.lock
		// and dials the WebSocket back). Off (no env) when the endpoint is off.
		if (state.ide.ssePort > 0)
		{
			spec.env.emplace_back("CLAUDE_CODE_SSE_PORT",
				std::to_string(state.ide.ssePort));
			spec.env.emplace_back("ENABLE_IDE_INTEGRATION", "true");
		}
		s.cols = spec.cols;
		s.rows = spec.rows;
		if (s.pty->spawn(spec))
		{
			s.spawned = true;
			s.exited = false;
			s.lastError.clear();
			// route the VT core's query replies (Primary DA, cursor-position
			// reports, ...) back into the pty's input, so a shell like fish does
			// not stall waiting for an answer and disable features. The pty and
			// screen share the session lifetime, torn down together, so the raw
			// pointer the sink captures stays valid for every write() call.
			TerminalPty* ptyRaw = s.pty.get();
			s.screen->setResponder(
				[ptyRaw](char const* data, std::size_t len)
				{
					ptyRaw->write(data, len);
				});
		}
		else
		{
			s.lastError = s.pty->errorMessage();
			s.spawned = false;
			s.exited = true;
		}
	}

	//! map an ImGui key to our TermKey (None if it is not a terminal special key)
	TermKey mapImGuiKey(ImGuiKey key)
	{
		switch (key)
		{
			case ImGuiKey_Enter:
			case ImGuiKey_KeypadEnter:	return TermKey::Enter;
			case ImGuiKey_Backspace:	return TermKey::Backspace;
			case ImGuiKey_Tab:			return TermKey::Tab;
			case ImGuiKey_Escape:		return TermKey::Escape;
			case ImGuiKey_UpArrow:		return TermKey::Up;
			case ImGuiKey_DownArrow:	return TermKey::Down;
			case ImGuiKey_LeftArrow:	return TermKey::Left;
			case ImGuiKey_RightArrow:	return TermKey::Right;
			case ImGuiKey_Home:			return TermKey::Home;
			case ImGuiKey_End:			return TermKey::End;
			case ImGuiKey_PageUp:		return TermKey::PageUp;
			case ImGuiKey_PageDown:		return TermKey::PageDown;
			case ImGuiKey_Insert:		return TermKey::Insert;
			case ImGuiKey_Delete:		return TermKey::Delete;
			case ImGuiKey_F1:			return TermKey::F1;
			case ImGuiKey_F2:			return TermKey::F2;
			case ImGuiKey_F3:			return TermKey::F3;
			case ImGuiKey_F4:			return TermKey::F4;
			case ImGuiKey_F5:			return TermKey::F5;
			case ImGuiKey_F6:			return TermKey::F6;
			case ImGuiKey_F7:			return TermKey::F7;
			case ImGuiKey_F8:			return TermKey::F8;
			case ImGuiKey_F9:			return TermKey::F9;
			case ImGuiKey_F10:			return TermKey::F10;
			case ImGuiKey_F11:			return TermKey::F11;
			case ImGuiKey_F12:			return TermKey::F12;
			default:					return TermKey::None;
		}
	}

	//! normalize the selection so (aLine,aCol) precedes (bLine,bCol) in reading
	//! order; returns false when the selection is empty
	bool orderedSelection(TerminalSession const& s, int& aLine, int& aCol,
		int& bLine, int& bCol)
	{
		if (!s.hasSelection)
		{
			return false;
		}
		aLine = s.anchorLine; aCol = s.anchorCol;
		bLine = s.headLine; bCol = s.headCol;
		if (aLine > bLine || (aLine == bLine && aCol > bCol))
		{
			std::swap(aLine, bLine);
			std::swap(aCol, bCol);
		}
		return aLine != bLine || aCol != bCol;
	}

	//! the glyph of an absolute line/col (scrollback below the visible grid)
	TermCell absoluteCell(TerminalSession const& s, int line, int col)
	{
		const int sb = s.screen->scrollbackCount();
		if (line < sb)
		{
			return s.screen->scrollbackCell(line, col);
		}
		return s.screen->cell(line - sb, col);
	}

	//! build the selected text (reading order, newline between lines). Thin
	//! wrapper over the exposed pure OrkigeEditor::terminalSelectionText so the
	//! panel and the headless selfcheck share ONE extraction path.
	std::string selectionText(TerminalSession const& s)
	{
		if (!s.hasSelection || !s.screen)
		{
			return std::string();
		}
		return OrkigeEditor::terminalSelectionText(*s.screen, s.cols,
			s.anchorLine, s.anchorCol, s.headLine, s.headCol);
	}

	//! build one absolute grid line as a string where index == cell column (a
	//! wide/non-ASCII cell reads as a space, i.e. a boundary): paths are ASCII, so
	//! this preserves column identity for the pure terminalPathTokenAt extractor
	//! while never splitting a path across a multi-byte glyph.
	std::string absoluteLineText(TerminalSession const& s, int line)
	{
		std::string out;
		out.reserve(static_cast<std::size_t>(s.cols));
		for (int col = 0; col < s.cols; ++col)
		{
			TermCell cellValue = absoluteCell(s, line, col);
			char ch = ' ';
			if (cellValue.glyph.size() == 1)
			{
				const unsigned char b0 =
					static_cast<unsigned char>(cellValue.glyph[0]);
				if (b0 >= 0x20 && b0 < 0x7f)
				{
					ch = static_cast<char>(b0);
				}
			}
			out.push_back(ch);
		}
		return out;
	}

	//! the resolved terminal link under a hovered cell (a Cmd/Ctrl+click target).
	struct TerminalLink
	{
		bool		resolved = false;
		std::string	absolutePath;	//!< the existing file to open
		int			line = 0;		//!< 1-based target line (0 = none)
		int			hoverLine = 0;	//!< absolute grid line to underline
		std::size_t	beginCol = 0;	//!< [beginCol, endCol) span to underline
		std::size_t	endCol = 0;
	};

	//! extract + resolve the path token under absolute cell (line,col): the pure
	//! terminalPathTokenAt over the line text, then resolveTerminalPath against the
	//! project root / session cwd / absolute with a real fs::exists probe. Returns
	//! a resolved link only when the file EXISTS (so only real files underline).
	TerminalLink terminalLinkAt(TerminalSession& s, EditorState& state,
		int line, int col)
	{
		TerminalLink link;
		if (!s.screen || col < 0 || col >= s.cols)
		{
			return link;
		}
		const std::string lineText = absoluteLineText(s, line);
		Orkige::TerminalPathToken token;
		if (!Orkige::terminalPathTokenAt(lineText,
			static_cast<std::size_t>(col), token))
		{
			return link;
		}
		// the session's working directory: the project root at spawn (v1 - a live
		// cwd via OSC 7 is a future refinement). Project root drives both slots.
		std::string projectRoot;
		if (state.project.isLoaded())
		{
			projectRoot = state.project.getRootDirectory();
		}
		const char* home = std::getenv("HOME");
		const std::string absolute = Orkige::resolveTerminalPath(
			token.path, projectRoot, projectRoot, home ? home : "",
			[](std::string const& candidate)
			{
				std::error_code ec;
				return std::filesystem::exists(candidate, ec) &&
					!std::filesystem::is_directory(candidate, ec);
			});
		if (absolute.empty())
		{
			return link;
		}
		link.resolved = true;
		link.absolutePath = absolute;
		link.line = token.line;
		link.hoverLine = line;
		link.beginCol = token.begin;
		link.endCol = token.end;
		return link;
	}

	//! decode the first Unicode codepoint of a UTF-8 glyph string (0 when empty
	//! or malformed). The terminal cells hold whole grapheme strings; only the
	//! leading codepoint decides whether the mono atlas can draw the cell.
	std::uint32_t decodeFirstCodepoint(std::string const& glyph)
	{
		if (glyph.empty())
		{
			return 0;
		}
		const unsigned char b0 = static_cast<unsigned char>(glyph[0]);
		if (b0 < 0x80)
		{
			return b0;
		}
		auto cont = [&](std::size_t i) -> std::uint32_t
		{
			return (i < glyph.size())
				? (static_cast<unsigned char>(glyph[i]) & 0x3fu) : 0u;
		};
		if ((b0 & 0xe0) == 0xc0)
		{
			return ((b0 & 0x1fu) << 6) | cont(1);
		}
		if ((b0 & 0xf0) == 0xe0)
		{
			return ((b0 & 0x0fu) << 12) | (cont(1) << 6) | cont(2);
		}
		if ((b0 & 0xf8) == 0xf0)
		{
			return ((b0 & 0x07u) << 18) | (cont(1) << 12) | (cont(2) << 6) |
				cont(3);
		}
		return 0;
	}

	//! true when the absolute line/col falls inside the current selection
	bool inSelection(TerminalSession const& s, int line, int col)
	{
		int aLine = 0;
		int aCol = 0;
		int bLine = 0;
		int bCol = 0;
		if (!orderedSelection(s, aLine, aCol, bLine, bCol))
		{
			return false;
		}
		if (line < aLine || line > bLine)
		{
			return false;
		}
		if (line == aLine && col < aCol)
		{
			return false;
		}
		if (line == bLine && col >= bCol)
		{
			return false;
		}
		return true;
	}

	//! feed focused keyboard input to the pty (text via the IME queue, special
	//! keys + control chords via the pure encoder). Copy/paste stay the editor's.
	//! Returns true when bytes were actually sent to the child (typed text, a
	//! paste, a special key or a control chord) - the "user sent input" signal
	//! the follow contract re-pins on. A copy chord is NOT input.
	bool handleTerminalInput(TerminalSession& s)
	{
		bool sentInput = false;
		ImGuiIO& io = ImGui::GetIO();
		const bool shift = io.KeyShift;
		const bool alt = io.KeyAlt;
		// PHYSICAL modifier state. ImGui with io.ConfigMacOSXBehaviors (the default
		// on macOS) SWAPS Cmd(Super) and Ctrl at io.AddKeyEvent() time (imgui.h:
		// "we swap Cmd(Super) and Ctrl keys"), so the physical Cmd key arrives as
		// io.KeyCtrl and the physical Ctrl key as io.KeySuper. Read the PHYSICAL
		// keys here so the terminal maps them to the right role regardless: on
		// macOS Cmd = copy/paste, Ctrl = the C0 control codes (SIGINT et al.) - a
		// bare `super`/`ctrl` read would send Cmd+C as a SIGINT and never copy.
	#if defined(__APPLE__)
		const bool physicalCmd = io.KeyCtrl;	// swapped: Cmd -> io.KeyCtrl
		const bool physicalCtrl = io.KeySuper;	// swapped: Ctrl -> io.KeySuper
	#else
		const bool physicalCmd = io.KeySuper;
		const bool physicalCtrl = io.KeyCtrl;
	#endif
	#if defined(__APPLE__)
		// Cmd+C / Cmd+V are copy/paste; Ctrl+C/V stay C0 control codes below
		const bool copyChord =
			physicalCmd && ImGui::IsKeyPressed(ImGuiKey_C, false);
		const bool pasteChord =
			physicalCmd && ImGui::IsKeyPressed(ImGuiKey_V, false);
	#else
		// Ctrl+Shift+C/V are the terminal copy/paste (Ctrl+C/V stay control codes)
		const bool copyChord =
			physicalCtrl && shift && ImGui::IsKeyPressed(ImGuiKey_C, false);
		const bool pasteChord =
			physicalCtrl && shift && ImGui::IsKeyPressed(ImGuiKey_V, false);
	#endif

		if (copyChord)
		{
			// copy the drag-selection to the OS pasteboard via SDL directly
			// (not ImGui's clipboard - the editor's hand-rolled ImGui backend
			// wires that to SDL, but going straight to SDL keeps the copy path
			// window-and-ImGui-free so the selfcheck can drive it headlessly).
			// With no selection Cmd/Ctrl+C is a no-op (the house rule: on macOS
			// Cmd+C copies, Ctrl+C stays the SIGINT control code below).
			const std::string text = selectionText(s);
			if (!text.empty())
			{
				SDL_SetClipboardText(text.c_str());
			}
			return false;	// a copy is not input into the child
		}
		if (pasteChord)
		{
			char* clip = SDL_GetClipboardText();	// "" when empty, never null
			if (clip != nullptr && clip[0] != '\0')
			{
				s.pty->write(OrkigeEditor::terminalPasteEncoding(
					clip, s.screen->bracketedPaste()));
				sentInput = true;
			}
			if (clip != nullptr)
			{
				SDL_free(clip);
			}
			return sentInput;
		}

		// printable text: the IME/text-input queue (skip while a Ctrl/Cmd chord
		// is held - those are control codes / editor chords, not text)
		if (!physicalCtrl && !physicalCmd)
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
			{
				const ImWchar ch = io.InputQueueCharacters[i];
				if (ch == 0 || ch == '\t' || ch == '\r' || ch == '\n')
				{
					continue;	// specials come through the key path below
				}
				s.pty->write(encodeUtf8(static_cast<std::uint32_t>(ch)));
				sentInput = true;
			}
		}

		TermMods mods;
		mods.ctrl = physicalCtrl;
		mods.shift = shift;
		mods.alt = alt;

		// the terminal special keys
		for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
		{
			const ImGuiKey key = static_cast<ImGuiKey>(k);
			const TermKey term = mapImGuiKey(key);
			if (term == TermKey::None)
			{
				continue;
			}
			if (ImGui::IsKeyPressed(key, true))
			{
				s.pty->write(encodeTermKey(term, mods,
					s.screen->applicationCursorKeys()));
				sentInput = true;
			}
		}

		// Ctrl+<letter/symbol> control codes (never with the Cmd/copy modifier;
		// Ctrl+Shift+C/V were consumed above as copy/paste). physicalCtrl is the
		// real Ctrl key on both platforms (macOS un-swaps it above), so Ctrl+C
		// stays SIGINT while Cmd+C copies.
		if (physicalCtrl && !physicalCmd)
		{
			// most platforms suppress the text char while Ctrl is held, so the
			// control codes come from the letter keys directly
			static const struct { ImGuiKey key; std::uint32_t cp; } kCtrlKeys[] =
			{
				{ ImGuiKey_A, 'a' }, { ImGuiKey_B, 'b' }, { ImGuiKey_C, 'c' },
				{ ImGuiKey_D, 'd' }, { ImGuiKey_E, 'e' }, { ImGuiKey_F, 'f' },
				{ ImGuiKey_G, 'g' }, { ImGuiKey_H, 'h' }, { ImGuiKey_I, 'i' },
				{ ImGuiKey_J, 'j' }, { ImGuiKey_K, 'k' }, { ImGuiKey_L, 'l' },
				{ ImGuiKey_M, 'm' }, { ImGuiKey_N, 'n' }, { ImGuiKey_O, 'o' },
				{ ImGuiKey_P, 'p' }, { ImGuiKey_Q, 'q' }, { ImGuiKey_R, 'r' },
				{ ImGuiKey_S, 's' }, { ImGuiKey_T, 't' }, { ImGuiKey_U, 'u' },
				{ ImGuiKey_V, 'v' }, { ImGuiKey_W, 'w' }, { ImGuiKey_X, 'x' },
				{ ImGuiKey_Y, 'y' }, { ImGuiKey_Z, 'z' },
				{ ImGuiKey_Space, ' ' }, { ImGuiKey_Backslash, '\\' },
				{ ImGuiKey_LeftBracket, '[' }, { ImGuiKey_RightBracket, ']' },
			};
			for (auto const& entry : kCtrlKeys)
			{
				if (ImGui::IsKeyPressed(entry.key, true))
				{
					const std::string bytes = encodeControlChar(entry.cp, mods);
					if (!bytes.empty())
					{
						s.pty->write(bytes);
						sentInput = true;
					}
				}
			}
		}
		return sentInput;
	}

	//! pump a session's pty into its screen (bounded), refresh liveness, and
	//! refresh the two tab-title signals. Runs for EVERY session each frame -
	//! background tabs must keep draining or a chatty agent fills the pty buffer
	//! and stalls, and their titles must stay live. Returns true when new output
	//! arrived (the active tab uses that to keep the view pinned to the tail).
	bool drainSession(TerminalSession& s)
	{
		if (!s.spawned || !s.screen || !s.pty)
		{
			return false;
		}
		bool gotOutput = false;
		std::size_t total = 0;
		std::vector<char> buffer(4096);
		while (total < kReadCap)
		{
			const std::size_t n = s.pty->read(buffer.data(), buffer.size());
			if (n == 0)
			{
				break;
			}
			s.screen->write(buffer.data(), n);
			total += n;
			gotOutput = true;
		}
		if (!s.pty->isAlive())
		{
			s.exited = true;
		}
		// the OSC title is free to read every frame (a cached string); the
		// foreground process name is a syscall, so poll it at a low cadence
		s.vtTitle = s.screen->getTitle();
		const auto now = std::chrono::steady_clock::now();
		if (!s.exited &&
			(s.lastProcPoll.time_since_epoch().count() == 0 ||
				now - s.lastProcPoll >= kProcPollInterval))
		{
			s.procName = s.pty->foregroundProcessName();
			s.lastProcPoll = now;
		}
		// STICKY classification off BOTH signals: once a session is a known agent
		// it stays that agent (a status-ticker title cannot declassify it) until
		// the foreground process reverts to a shell.
		s.stickyAgent = terminalUpdateStickyAgent(s.stickyAgent, s.procName,
			s.vtTitle);
		return gotOutput;
	}

	//! append a fresh, not-yet-spawned session (spawned lazily once a grid size
	//! is known); its window auto-focuses on first appearance
	TerminalSession& addSession()
	{
		TerminalPanelState& p = panel();
		auto s = std::make_unique<TerminalSession>();
		s->uid = p.nextUid++;
		p.sessions.push_back(std::move(s));
		return *p.sessions.back();
	}

	//! terminate + drop the session at `index`
	void closeSession(int index)
	{
		TerminalPanelState& p = panel();
		if (index < 0 || index >= static_cast<int>(p.sessions.size()))
		{
			return;
		}
		if (p.sessions[index]->pty)
		{
			p.sessions[index]->pty->terminate();
		}
		p.sessions.erase(p.sessions.begin() + index);
	}

	//! render one session's grid + input into the current window's content
	//! region. Sets state.terminalFocused when this session's window has focus.
	void drawSessionGrid(TerminalSession& s, EditorState& state, float cellW,
		float cellH, bool gotOutput, ImFont* mono)
	{
		TerminalPanelState& p = panel();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const int newCols = std::max(1, static_cast<int>(avail.x / cellW));
		const int newRows = std::max(1, static_cast<int>(avail.y / cellH));
		p.lastCols = newCols;
		p.lastRows = newRows;

		const bool gridResized = (newCols != s.cols || newRows != s.rows);
		if (gridResized)
		{
			s.cols = newCols;
			s.rows = newRows;
			s.screen->resize(newCols, newRows);
			if (s.pty)
			{
				s.pty->resize(newCols, newRows);
			}
		}

		const int scrollbackCount = s.screen->scrollbackCount();
		const int totalLines = scrollbackCount + s.rows;

		if (mono != nullptr)
		{
			ImGui::PushFont(mono);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(26, 26, 26, 255));
		const std::string gridId = "##termgrid" + std::to_string(s.uid);
		ImGui::BeginChild(gridId.c_str(), ImVec2(0.0f, 0.0f), false,
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);

		const bool panelFocused =
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		// the screen position of absolute line 0, column 0 (already scroll-
		// adjusted): the reference the pure mouse->cell hit test maps against, so
		// a drag that runs past the visible rows still extends the selection
		const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();
		const ImVec2 gridMin = ImGui::GetWindowPos();
		const ImVec2 gridMax(gridMin.x + ImGui::GetWindowSize().x,
			gridMin.y + ImGui::GetWindowSize().y);
		const ImVec2 mouse = ImGui::GetMousePos();
		const TermCursor cur = s.screen->cursor();
		const int cursorLine = scrollbackCount + cur.row;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImFontBaked* fontBaked = ImGui::GetFontBaked();
		const ImU32 selBg = IM_COL32(60, 90, 140, 255);

		ImGuiListClipper clipper;
		clipper.Begin(totalLines, cellH);
		while (clipper.Step())
		{
			for (int line = clipper.DisplayStart; line < clipper.DisplayEnd;
				++line)
			{
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				for (int col = 0; col < s.cols; ++col)
				{
					TermCell cellValue = absoluteCell(s, line, col);
					if (cellValue.width == 0)
					{
						continue;	// trailing half of a wide glyph
					}
					const float x = pos.x + col * cellW;
					const int span = (cellValue.width == 2) ? 2 : 1;
					TermColor fg = cellValue.fg;
					TermColor bg = cellValue.bg;
					if (cellValue.attrs.reverse)
					{
						std::swap(fg, bg);
					}
					const bool selected = inSelection(s, line, col);
					const ImVec2 bgMin(x, pos.y);
					const ImVec2 bgMax(x + span * cellW, pos.y + cellH);
					if (selected)
					{
						drawList->AddRectFilled(bgMin, bgMax, selBg);
					}
					else if (bg.r != 26 || bg.g != 26 || bg.b != 26)
					{
						drawList->AddRectFilled(bgMin, bgMax, cellColor(bg));
					}
					const std::uint32_t cp =
						decodeFirstCodepoint(cellValue.glyph);
					const bool drawable = cp != 0 && (cp < 0x80 ||
						fontBaked == nullptr ||
						fontBaked->FindGlyphNoFallback(
							static_cast<ImWchar>(cp)) != nullptr);
					if (!cellValue.glyph.empty() && cellValue.glyph != " " &&
						drawable)
					{
						drawList->AddText(ImVec2(x, pos.y), cellColor(fg),
							cellValue.glyph.c_str(),
							cellValue.glyph.c_str() + cellValue.glyph.size());
						if (cellValue.attrs.underline)
						{
							drawList->AddLine(ImVec2(x, pos.y + cellH - 1.0f),
								ImVec2(x + span * cellW, pos.y + cellH - 1.0f),
								cellColor(fg));
						}
					}
				}
				if (cur.visible && line == cursorLine &&
					cur.col >= 0 && cur.col < s.cols)
				{
					const float cx = pos.x + cur.col * cellW;
					const ImU32 curCol =
						IM_COL32(220, 220, 220, panelFocused ? 200 : 90);
					const ImVec2 cMin(cx, pos.y);
					const ImVec2 cMax(cx + cellW, pos.y + cellH);
					if (panelFocused)
					{
						drawList->AddRectFilled(cMin, cMax, curCol);
					}
					else
					{
						drawList->AddRect(cMin, cMax, curCol);
					}
				}
				ImGui::Dummy(ImVec2(s.cols * cellW, cellH));
			}
		}
		clipper.End();

		const bool overGrid =
			ImGui::IsMouseHoveringRect(gridMin, gridMax) && ImGui::IsWindowHovered(
				ImGuiHoveredFlags_ChildWindows |
				ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

		// --- Cmd/Ctrl+click open: a path[:line] under the cursor is a link -------
		// While the link modifier is held (Cmd on macOS - ImGui with
		// ConfigMacOSXBehaviors swaps Cmd->io.KeyCtrl - Ctrl elsewhere, so
		// io.KeyCtrl is the right test on BOTH), a path-looking token under the
		// mouse that RESOLVES to an existing file underlines + shows the hand
		// cursor; a click opens it (at :line) via the asset browser's plain-file
		// policy. The plain (no-modifier) click keeps doing selection as today, and
		// a resolved-link click is consumed so it never also arms a drag-select.
		bool linkClickConsumed = false;
		if (overGrid && ImGui::GetIO().KeyCtrl && !s.selecting)
		{
			const TerminalGridPoint hit = terminalCellAtPoint(mouse.x, mouse.y,
				gridOrigin.x, gridOrigin.y, cellW, cellH, s.cols, totalLines);
			const TerminalLink link = terminalLinkAt(s, state, hit.line, hit.col);
			if (gTerminalAutomatedUiTest)
			{
				gTerminalProbe.linkResolved = link.resolved;
				gTerminalProbe.linkPath = link.absolutePath;
				gTerminalProbe.linkLine = link.line;
			}
			if (link.resolved)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				// underline the token's cells (the visual affordance while held)
				const float uy = gridOrigin.y +
					static_cast<float>(link.hoverLine) * cellH + cellH - 1.0f;
				const float ux0 = gridOrigin.x +
					static_cast<float>(link.beginCol) * cellW;
				const float ux1 = gridOrigin.x +
					static_cast<float>(link.endCol) * cellW;
				drawList->AddLine(ImVec2(ux0, uy), ImVec2(ux1, uy),
					IM_COL32(120, 170, 240, 255));
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					openPathHonoringInternalEditor(state, link.absolutePath,
						link.line);
					linkClickConsumed = true;
				}
			}
		}
		else if (gTerminalAutomatedUiTest)
		{
			gTerminalProbe.linkResolved = false;
		}

		// --- selection via mouse drag over the grid -----------------------------
		// Arm on a press inside the grid rect (IsMouseHoveringRect, not
		// IsWindowHovered: a held drag gives the child an ActiveId, which makes
		// IsWindowHovered() report false and silently froze the OLD selection).
		// Once armed, the head follows the mouse EVERY frame while the button is
		// held - the pure terminalCellAtPoint clamps a drag past the edges - so a
		// selection reliably arms for the Cmd/Ctrl copy chord below.
		if (overGrid && !linkClickConsumed &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const TerminalGridPoint hit = terminalCellAtPoint(mouse.x, mouse.y,
				gridOrigin.x, gridOrigin.y, cellW, cellH, s.cols, totalLines);
			s.selecting = true;
			s.hasSelection = false;
			s.anchorLine = hit.line;
			s.anchorCol = hit.col;
			s.headLine = hit.line;
			s.headCol = hit.col;
		}
		else if (s.selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const TerminalGridPoint hit = terminalCellAtPoint(mouse.x, mouse.y,
				gridOrigin.x, gridOrigin.y, cellW, cellH, s.cols, totalLines);
			s.headLine = hit.line;
			s.headCol = hit.col;
			if (s.headLine != s.anchorLine || s.headCol != s.anchorCol)
			{
				s.hasSelection = true;
			}
		}
		if (s.selecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			s.selecting = false;
		}

		// --- real-event test probe (only meaningful under the UI test) ----------
		// Publish THIS front session's live grid geometry + selection + focus so
		// the copy selfcheck can compute a cell's screen point and assert what the
		// real mouse drag armed. gridOrigin is absolute (line 0, col 0), already
		// scroll-adjusted - the same reference terminalCellAtPoint maps against.
		if (gTerminalAutomatedUiTest)
		{
			gTerminalProbe.hasFrontSession = true;
			gTerminalProbe.spawned = s.spawned;
			gTerminalProbe.exited = s.exited;
			gTerminalProbe.windowFocused = panelFocused;
			gTerminalProbe.gridOriginX = gridOrigin.x;
			gTerminalProbe.gridOriginY = gridOrigin.y;
			gTerminalProbe.cellW = cellW;
			gTerminalProbe.cellH = cellH;
			gTerminalProbe.cols = s.cols;
			gTerminalProbe.visibleRows = s.rows;
			gTerminalProbe.scrollbackCount = scrollbackCount;
			gTerminalProbe.hasSelection = s.hasSelection;
			gTerminalProbe.selectionText = selectionText(s);
		}

		// --- follow / pin the tail ----------------------------------------------
		// The pin must survive content-height changes. ImGui only recomputes a
		// window's ContentSize (hence GetScrollMaxY) at EndChild, so DURING
		// submission it still reports LAST frame's max: SetScrollY(GetScrollMaxY())
		// lands short of a tail that grew this frame, and the next frame's
		// at-bottom test - now reading the larger, updated max - wrongly concludes
		// "not at bottom" and clears the pin. That is why the terminal stopped
		// following once output first exceeded the viewport. The fix: while pinned
		// the follow state is driven by the pure terminalFollowDecision (never an
		// at-bottom read), and a pin scrolls to the FULL content height - an
		// overshoot ImGui clamps to the true max next frame, so the newest line
		// always lands at the bottom regardless of the stale in-frame ContentSize.
		ImGuiWindow* childWindow = ImGui::GetCurrentWindow();
		const float scrollMaxY = ImGui::GetScrollMaxY();
		const float scrollY = ImGui::GetScrollY();
		// at-bottom detection - used ONLY to RE-PIN while unpinned, where the
		// scroll is stable and GetScrollMaxY is accurate (within half a line)
		const float atBottomEps = std::max(2.0f, cellH * 0.5f);
		const bool atBottom = scrollY >= scrollMaxY - atBottomEps;
		// the user scrolled up: a wheel-up over the grid, or the vertical scrollbar
		// grabbed while not at the bottom (a scrollbar drag into history). Either
		// UNPINS; returning to the bottom (atBottom) re-pins.
		const bool childHovered = ImGui::IsWindowHovered();
		const ImGuiID vScrollId =
			ImGui::GetWindowScrollbarID(childWindow, ImGuiAxis_Y);
		const bool scrollbarGrabbed =
			ImGui::GetCurrentContext()->ActiveId == vScrollId;
		const bool userScrolledAway =
			(childHovered && ImGui::GetIO().MouseWheel > 0.0f) ||
			(scrollbarGrabbed && !atBottom);
		// content grew this frame: new output, a grid resize, or a re-shown
		// backgrounded tab (its line count jumped while it was not being drawn)
		const bool contentGrew =
			gotOutput || gridResized || (totalLines != s.lastTotalLines);

		TerminalFollowInputs followIn;
		followIn.atBottom = atBottom;
		followIn.contentGrew = contentGrew;
		followIn.userScrolledAway = userScrolledAway;
		followIn.isSelecting = s.selecting;
		followIn.sentInput = s.sentInputPending;	// set by last frame's input
		followIn.wasFollowing = s.followTail;
		const TerminalFollowVerdict verdict = terminalFollowDecision(followIn);
		if (verdict.pinToBottom)
		{
			ImGui::SetScrollY(static_cast<float>(totalLines) * cellH);
		}
		s.followTail = verdict.followTail;
		s.sentInputPending = false;
		s.lastTotalLines = totalLines;

		// publish the real ImGui scroll state for the follow-tail selfcheck leg
		// (post-decision: scrollY/scrollMaxY are this frame's live values, followTail
		// the pin verdict). The scroll set by SetScrollY above lands next frame.
		if (gTerminalAutomatedUiTest && gTerminalProbe.hasFrontSession)
		{
			gTerminalProbe.followTail = s.followTail;
			gTerminalProbe.scrollY = scrollY;
			gTerminalProbe.scrollMaxY = scrollMaxY;
			gTerminalProbe.totalLines = totalLines;
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		if (mono != nullptr)
		{
			ImGui::PopFont();
		}

		if (panelFocused && !s.exited)
		{
			// this session's window holds focus: it owns EVERY key (any other
			// window this frame must not clobber the flag - the caller reset it
			// to false before the window loop)
			state.terminalFocused = true;
			if (s.pty)
			{
				// remember whether the user sent input this frame; next frame's
				// follow decision re-pins + jumps to the prompt on it (input is
				// handled after the grid submits, so it lands one frame later)
				s.sentInputPending = handleTerminalInput(s);
			}
		}
	}

	//! the compact MCP connect affordance (only when the endpoint is live): a
	//! the ready-made `claude mcp add ...` line for the live MCP endpoint, or ""
	//! when the endpoint is off. Offered from the terminal tab's right-click menu
	//! (the project `.mcp.json` auto-registers the endpoint, so this is only a
	//! convenience for a shell started outside the project directory).
	std::string mcpConnectCommand(std::string const& mcpUrl,
		std::string const& mcpTokenFile)
	{
		if (mcpUrl.empty())
		{
			return std::string();
		}
		std::string connectCmd =
			"claude mcp add --transport http orkige " + mcpUrl;
		if (!mcpTokenFile.empty())
		{
			connectCmd += " --header \"Authorization: Bearer "
				"$(cat \\\"$ORKIGE_MCP_TOKEN_FILE\\\")\"";
		}
		return connectCmd;
	}

	//! draw ONE session as its own dockable, top-level window (the code-editor
	//! one-window-per-file pattern). First appearance docks into the shared
	//! bottom node; the visible title tracks the tenant live behind a stable
	//! ###id. Toggling `s.open` false (the window x or the tab menu Close) queues
	//! the session for the caller's confirm-if-alive reap.
	void drawSessionWindow(TerminalSession& s, int index, EditorState& state,
		float cellW, float cellH, bool gotOutput, ImFont* mono,
		std::string const& mcpUrl, std::string const& mcpTokenFile)
	{
		TerminalPanelState& p = panel();
		const TerminalTabLabel label = terminalSessionTabLabel(s.stickyAgent,
			s.vtTitle, s.procName, index + 1);
		// "<glyph> <name>###terminal<uid>": the stable ###id keeps the window +
		// docking identity while the VISIBLE part (badge + tenant name) updates
		// live. A CLASSIFIED agent shows a STABLE badge + canonical name; the
		// live status-ticker title rides the tab TOOLTIP instead (below).
		const std::string windowId = leadingGlyph(label) + " " + label.text +
			"###terminal" + std::to_string(s.uid);
		// publish the EXACT Begin() id (even before the shown-check, so a driver
		// can SetWindowFocus a still-backgrounded dock tab to bring it front)
		if (gTerminalAutomatedUiTest && index == 0)
		{
			gTerminalProbe.frontWindowId = windowId;
		}
		// the live VT title, filtered to what the UI font can render (a leading
		// sparkle the font lacks would draw '?') - shown as the tab's tooltip.
		const std::string tabTooltip = terminalFilterRenderable(s.vtTitle);

		// first-appearance dock into the shared bottom node (beside Console/
		// Assets/Source Control), retried until the target node resolves; later
		// sessions dock beside the ones already there.
		if (!s.dockAssigned)
		{
			ImGuiID target = p.sharedDockId;
			if (target == 0)
			{
				for (const char* anchorName : { "Console", "Stats",
					"Assets###Assets", "Debug###Debug",
					ICON_FA_CODE_BRANCH " Source Control###SourceControl" })
				{
					ImGuiWindow* anchor = ImGui::FindWindowByName(anchorName);
					if (anchor && anchor->DockId != 0)
					{
						target = anchor->DockId;
						break;
					}
				}
			}
			if (target != 0)
			{
				ImGui::SetNextWindowDockID(target, ImGuiCond_Always);
				s.dockAssigned = true;
			}
		}
		ImGui::SetNextWindowSize(ImVec2(640, 360), ImGuiCond_FirstUseEver);

		bool open = s.open;
		const bool shown = ImGui::Begin(windowId.c_str(), &open);
		// right after Begin the "last item" is this window's dock TAB (or title
		// bar) - so a tooltip here shows the live status-ticker title on hover
		// while the tab label itself stays the stable badge + canonical name.
		if (!tabTooltip.empty())
		{
			ImGui::SetItemTooltip("%s", tabTooltip.c_str());
		}
		const ImGuiID dockId = ImGui::GetWindowDockID();
		if (dockId != 0)
		{
			p.sharedDockId = dockId;	// siblings dock beside this one
		}
		// the docked-tab right-click menu: spawn another / close this one, and -
		// only while the MCP endpoint is live - copy the ready `claude mcp add`
		// line (the project .mcp.json already auto-registers it; this is the
		// convenience for a shell started outside the project directory)
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("New Terminal"))
			{
				++p.pendingSpawns;
			}
			const std::string connectCmd =
				mcpConnectCommand(mcpUrl, mcpTokenFile);
			if (!connectCmd.empty())
			{
				if (ImGui::MenuItem("Copy MCP connect command"))
				{
					ImGui::SetClipboardText(connectCmd.c_str());
				}
				ImGui::SetItemTooltip("This editor's MCP endpoint is live in this "
					"shell (ORKIGE_MCP_URL):\n%s", connectCmd.c_str());
			}
			if (ImGui::MenuItem("Close"))
			{
				open = false;
			}
			ImGui::EndPopup();
		}
		s.open = open;
		if (!shown)
		{
			ImGui::End();
			return;
		}

		// header row (default UI font - the icon/badge glyphs live there): the
		// "+" new-terminal button and the shell / exited+restart line. The MCP
		// connect command lives in the tab's right-click menu (the project
		// .mcp.json auto-registers the endpoint, so no header button is needed).
		if (ImGui::SmallButton("+###termnew"))
		{
			++p.pendingSpawns;
		}
		ImGui::SetItemTooltip("New terminal");
		ImGui::SameLine();
		if (s.spawned && !s.exited)
		{
			ImGui::TextColored(ImVec4(0.42f, 0.78f, 0.47f, 1.0f),
				ICON_FA_TERMINAL);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", s.shell.c_str());
		}
		else if (s.exited)
		{
			const std::string exitMsg = s.lastError.empty()
				? std::string("The shell exited.")
				: ("Could not start a shell: " + s.lastError);
			ImGui::TextDisabled("%s", exitMsg.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton(ICON_FA_ROTATE " Restart"))
			{
				s.spawned = false;	// a fresh spawn happens next frame
				s.exited = false;
				s.hasSelection = false;
				s.vtTitle.clear();
				s.procName.clear();
				s.lastProcPoll = {};
			}
		}
		ImGui::Separator();

		if (s.spawned && s.screen)
		{
			drawSessionGrid(s, state, cellW, cellH, gotOutput, mono);
		}
		ImGui::End();
	}
}

void drawTerminalPanel(EditorState& state, ViewSettings& viewSettings,
	std::string const& mcpUrl, std::string const& mcpTokenFile, bool* panelOpen)
{
	(void)viewSettings;
	TerminalPanelState& p = panel();

	// automated runs never open a shell (the pollution-hygiene rule); the
	// headless selfcheck drives the pty seam directly (runTerminalSelfCheck).
	// The ONE exception is the real-event copy selfcheck, which must exercise
	// the true ImGui grid + mouse + copy-chord path a seam cannot see - it opts
	// in explicitly (terminalPanelSetAutomatedUiTest).
	if (gAutomatedRun && !gTerminalAutomatedUiTest)
	{
		state.terminalFocused = false;
		p.prevPanelOpen = false;
		return;
	}
	// the front session republishes its geometry/selection each frame it draws;
	// clear the "drew this frame" flag so a backgrounded/absent front reads false
	gTerminalProbe.hasFrontSession = false;

	const bool wantOpen = (panelOpen != nullptr) && *panelOpen;
	// rising edge (View > Terminal turned on) with no sessions: spawn one
	if (wantOpen && !p.prevPanelOpen && p.sessions.empty() &&
		p.pendingSpawns == 0)
	{
		p.pendingSpawns = 1;
	}
	// falling edge (turned off) with sessions live: request-close them all (each
	// alive one still asks first, via the reap below)
	if (!wantOpen && p.prevPanelOpen)
	{
		for (auto& s : p.sessions)
		{
			s->open = false;
		}
	}
	// consume New Terminal requests (View > New Terminal / the "+" / re-open)
	while (p.pendingSpawns > 0)
	{
		addSession();
		--p.pendingSpawns;
	}

	if (p.sessions.empty())
	{
		state.terminalFocused = false;
		if (panelOpen != nullptr)
		{
			*panelOpen = false;
		}
		p.prevPanelOpen = false;
		return;
	}

	// measure one mono cell (the grid's font); works outside any window
	ImFont* mono = Orkige::editorMonoFont();
	if (mono != nullptr)
	{
		ImGui::PushFont(mono);
	}
	const float cellW = std::max(1.0f, ImGui::CalcTextSize("W").x);
	const float cellH = ImGui::GetTextLineHeight();
	if (mono != nullptr)
	{
		ImGui::PopFont();
	}

	// EVERY session drains this frame (a backgrounded agent must keep reading or
	// a full pty buffer stalls it). A still-unspawned session spawns at the last
	// known grid size so it comes up sized even before its window is first shown.
	std::vector<bool> gotOutput(p.sessions.size(), false);
	for (std::size_t i = 0; i < p.sessions.size(); ++i)
	{
		TerminalSession& s = *p.sessions[i];
		if (!s.spawned && !s.exited)
		{
			spawnSession(s, state, mcpUrl, mcpTokenFile, p.lastCols, p.lastRows);
		}
		gotOutput[i] = drainSession(s);
	}

	// draw each session's window; focus is recomputed from them (reset first so
	// a non-focused window never clobbers a focused one)
	state.terminalFocused = false;
	for (std::size_t i = 0; i < p.sessions.size(); ++i)
	{
		drawSessionWindow(*p.sessions[i], static_cast<int>(i), state, cellW,
			cellH, gotOutput[i], mono, mcpUrl, mcpTokenFile);
	}

	// --- close reap: a window whose x/menu toggled it closed. A dead session
	// just goes; an alive one stays visible and queues for the confirm modal
	// (one at a time, keyed by uid so a concurrent reap never mis-targets it).
	for (auto it = p.sessions.begin(); it != p.sessions.end();)
	{
		TerminalSession& s = **it;
		if (s.open)
		{
			++it;
			continue;
		}
		const bool alive = s.pty && s.pty->isAlive() && !s.exited;
		if (alive)
		{
			s.open = true;			// stays visible while we ask
			s.closeRequested = true;
			++it;
		}
		else
		{
			if (s.pty)
			{
				s.pty->terminate();
			}
			it = p.sessions.erase(it);
		}
	}
	// promote a queued alive-close into the single confirm modal (by uid)
	if (p.pendingClose < 0)
	{
		for (auto& s : p.sessions)
		{
			if (s->closeRequested)
			{
				p.pendingClose = s->uid;
				break;
			}
		}
	}
	if (p.pendingClose >= 0)
	{
		ImGui::OpenPopup("Close terminal?###termclose");
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
			ImVec2(0.5f, 0.5f));
	}
	if (ImGui::BeginPopupModal("Close terminal?###termclose", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		TerminalSession* pending = nullptr;
		std::size_t pendingIndex = 0;
		for (std::size_t i = 0; i < p.sessions.size(); ++i)
		{
			if (p.sessions[i]->uid == p.pendingClose)
			{
				pending = p.sessions[i].get();
				pendingIndex = i;
				break;
			}
		}
		if (pending != nullptr)
		{
			const TerminalTabLabel label = terminalSessionTabLabel(
				pending->stickyAgent, pending->vtTitle, pending->procName,
				static_cast<int>(pendingIndex) + 1);
			ImGui::Text("\"%s\" is still running. Close it and terminate the "
				"process?", label.text.c_str());
			ImGui::Separator();
			if (ImGui::Button("Close"))
			{
				closeSession(static_cast<int>(pendingIndex));
				p.pendingClose = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				pending->closeRequested = false;	// keep it open
				p.pendingClose = -1;
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			p.pendingClose = -1;
			ImGui::CloseCurrentPopup();	// the session vanished under us
		}
		ImGui::EndPopup();
	}

	// the panel-registry flag MIRRORS "at least one terminal window open": the
	// last window closing unchecks View > Terminal, re-checking it re-spawns one
	if (panelOpen != nullptr)
	{
		*panelOpen = !p.sessions.empty();
		p.prevPanelOpen = *panelOpen;
	}
	else
	{
		p.prevPanelOpen = !p.sessions.empty();
	}
}

namespace OrkigeEditor
{
	void terminalPanelRequestNewSession()
	{
		++panel().pendingSpawns;
	}

	int terminalPanelSessionCount()
	{
		return static_cast<int>(panel().sessions.size());
	}

	void terminalPanelSetAutomatedUiTest(bool enabled)
	{
		gTerminalAutomatedUiTest = enabled;
	}

	TerminalPanelProbe const& terminalPanelProbe()
	{
		return gTerminalProbe;
	}

	void terminalPanelTestWrite(std::string const& bytes)
	{
		for (auto& s : panel().sessions)
		{
			if (s && s->spawned && s->screen)
			{
				s->screen->write(bytes.data(), bytes.size());
				return;
			}
		}
	}

	TerminalProbeHit terminalPanelTestFind(std::string const& needle)
	{
		TerminalProbeHit hit;
		if (needle.empty())
		{
			return hit;
		}
		TerminalSession* front = nullptr;
		for (auto& s : panel().sessions)
		{
			if (s && s->spawned && s->screen)
			{
				front = s.get();
				break;
			}
		}
		if (front == nullptr)
		{
			return hit;
		}
		const int cols = front->cols;
		const int sb = front->screen->scrollbackCount();
		const int totalLines = sb + front->rows;
		for (int line = 0; line < totalLines; ++line)
		{
			std::string row;
			row.reserve(static_cast<std::size_t>(cols));
			for (int col = 0; col < cols; ++col)
			{
				TermCell cellValue = (line < sb)
					? front->screen->scrollbackCell(line, col)
					: front->screen->cell(line - sb, col);
				// one char per COLUMN so the returned start col indexes cells
				// directly (a needle is ASCII); a wide/empty cell reads as space
				char ch = ' ';
				if (cellValue.glyph.size() == 1)
				{
					const unsigned char b0 =
						static_cast<unsigned char>(cellValue.glyph[0]);
					if (b0 >= 0x20 && b0 < 0x7f)
					{
						ch = static_cast<char>(b0);
					}
				}
				row.push_back(ch);
			}
			const std::size_t at = row.find(needle);
			if (at != std::string::npos)
			{
				hit.line = line;
				hit.col = static_cast<int>(at);
				return hit;
			}
		}
		return hit;
	}

	void terminalPanelShutdown()
	{
		TerminalPanelState& p = panel();
		for (auto& s : p.sessions)
		{
			if (s && s->pty)
			{
				s->pty->terminate();
			}
		}
		p.sessions.clear();
		p.pendingClose = -1;
		p.pendingSpawns = 0;
		p.prevPanelOpen = false;
	}

	std::string terminalSelectionText(EditorTerminalScreen& screen, int cols,
		int anchorLine, int anchorCol, int headLine, int headCol)
	{
		int aLine = anchorLine;
		int aCol = anchorCol;
		int bLine = headLine;
		int bCol = headCol;
		if (aLine > bLine || (aLine == bLine && aCol > bCol))
		{
			std::swap(aLine, bLine);
			std::swap(aCol, bCol);
		}
		if (aLine == bLine && aCol == bCol)
		{
			return std::string();	// empty selection
		}
		const int sb = screen.scrollbackCount();
		auto absCell = [&](int line, int col) -> TermCell
		{
			return (line < sb) ? screen.scrollbackCell(line, col)
				: screen.cell(line - sb, col);
		};
		std::string out;
		for (int line = aLine; line <= bLine; ++line)
		{
			const int startCol = (line == aLine) ? aCol : 0;
			const int endCol = (line == bLine) ? bCol : cols;
			std::string row;
			for (int col = startCol; col < endCol && col < cols; ++col)
			{
				TermCell cellValue = absCell(line, col);
				if (cellValue.width == 0)
				{
					continue;	// the trailing half of a wide glyph
				}
				row += cellValue.glyph.empty() ? " " : cellValue.glyph;
			}
			while (!row.empty() && row.back() == ' ')
			{
				row.pop_back();
			}
			out += row;
			if (line != bLine)
			{
				out.push_back('\n');
			}
		}
		return out;
	}

	std::string terminalPasteEncoding(std::string const& clip, bool bracketed)
	{
		if (bracketed)
		{
			return std::string("\x1b[200~") + clip + "\x1b[201~";
		}
		return clip;
	}

	// ------------------------------------------------------------------------
	// The headless terminal selfcheck. Window-independent: it drives the pty
	// seam + the VT screen wrapper + the key encoder against a real shell and
	// exits. 0 pass / 2 fail / 77 skip (no pty/shell).
	// ------------------------------------------------------------------------
	int runTerminalSelfCheck()
	{
		using namespace std::chrono;
		int exitCode = 0;
		auto check = [&](bool cond, const char* what)
		{
			SDL_Log("orkige_editor: terminal-test %s: %s", what,
				cond ? "ok" : "FAILED");
			if (!cond)
			{
				exitCode = 2;
			}
		};

		std::unique_ptr<TerminalPty> pty = createTerminalPty();
		EditorTerminalScreen screen(80, 24, 1000);

		TermPtySpec spec;
		spec.cols = 80;
		spec.rows = 24;
		spec.loginShell = false;
	#if defined(_WIN32)
		spec.shell = "cmd.exe";
	#else
		spec.shell = "/bin/sh";
	#endif
		if (!pty->spawn(spec))
		{
			SDL_Log("orkige_editor: terminal-test SKIP (no pty/shell: %s)",
				pty->errorMessage().c_str());
			return 77;
		}

		// diagnostics: total bytes ever read + the first bytes seen, so a CI
		// failure names its tier - zero bytes = pipe plumbing, bytes without the
		// expected text = parsing/screen (dumped escaped on the failing check)
		std::size_t totalRead = 0;
		std::string firstBytes;
		auto noteRead = [&](char const* data, std::size_t n)
		{
			totalRead += n;
			if (firstBytes.size() < 300)
			{
				firstBytes.append(data,
					std::min(n, static_cast<std::size_t>(300 - firstBytes.size())));
			}
		};
		// pump child output into the screen for up to `ms` milliseconds
		auto pump = [&](int ms)
		{
			const auto deadline = steady_clock::now() + milliseconds(ms);
			std::vector<char> buf(4096);
			while (steady_clock::now() < deadline)
			{
				const std::size_t n = pty->read(buf.data(), buf.size());
				if (n > 0)
				{
					noteRead(buf.data(), n);
					screen.write(buf.data(), n);
				}
				else
				{
					std::this_thread::sleep_for(milliseconds(5));
				}
			}
		};
		// pump until `done()` holds, up to `maxMs` - CONDITION-driven so a cold
		// shell on a slow CI runner (conhost spin-up easily beats a fixed window)
		// gets its full budget, while a fast one moves on immediately
		auto pumpUntil = [&](int maxMs, std::function<bool()> done) -> bool
		{
			const auto deadline = steady_clock::now() + milliseconds(maxMs);
			std::vector<char> buf(4096);
			while (steady_clock::now() < deadline)
			{
				const std::size_t n = pty->read(buf.data(), buf.size());
				if (n > 0)
				{
					noteRead(buf.data(), n);
					screen.write(buf.data(), n);
				}
				if (done())
				{
					return true;
				}
				if (n == 0)
				{
					std::this_thread::sleep_for(milliseconds(5));
				}
			}
			return done();
		};

		// let the shell reach its first prompt
		pump(400);

		// a red glyph cell anywhere in the grid - the SGR assertion both
		// platforms share. It pumps on its OWN condition: the plain-text
		// condition below can fire on the ECHOED command line (it literally
		// contains REDWORD, uncoloured) a read ahead of the coloured output -
		// a split a sanitizer-slowed run exposes while a fast one gets both
		// in one chunk
		auto redCellVisible = [&]() -> bool
		{
			for (int r = 0; r < 24; ++r)
			{
				for (int c = 0; c < 80; ++c)
				{
					TermCell cellValue = screen.cell(r, c);
					if (cellValue.glyph == "R" && cellValue.fg.r >= 180 &&
						cellValue.fg.g <= 90 && cellValue.fg.b <= 90)
					{
						return true;
					}
				}
			}
			return false;
		};

	#if !defined(_WIN32)
		// 1) colour + text: a printed SGR sequence lands in the grid as red text
		pty->write("printf '\\033[31mREDWORD\\033[0m\\r\\n'\n");
	#else
		// 1) colour + text: cmd's `prompt $e` emits a REAL escape byte, so the
		//    next prompt renders an SGR-red word - the console interprets the
		//    escape into a buffer attribute and the pseudoconsole re-emits it
		//    as SGR on the output pipe, proving the whole colour path with no
		//    external tool
		pty->write(std::string("prompt $e[31mREDWORD$e[0m$g") +
			encodeTermKey(TermKey::Enter, {}));
	#endif
		check(pumpUntil(5000, [&]
			{
				return screen.dumpVisible().find("REDWORD") != std::string::npos;
			}),
			"printed text reaches the grid");
		check(pumpUntil(5000, redCellVisible), "SGR colour parsed (red foreground)");

		// 2) input seam: run a line-echoing filter and "type" a known word
		//    through the key encoder; the terminal echoes it back into the grid
	#if defined(_WIN32)
		// cmd echoes typed input; a bare line shows up on the next prompt
		pty->write(std::string("echo TYPEDWORD") +
			encodeTermKey(TermKey::Enter, {}));
	#else
		pty->write("cat\n");
		pump(300);
		pty->write(std::string("TYPEDWORD") +
			encodeTermKey(TermKey::Enter, {}));	// "TYPEDWORD\r"
	#endif
		const bool echoed = pumpUntil(5000, [&]
			{
				return screen.dumpVisible().find("TYPEDWORD")
					!= std::string::npos;
			});
		if (!echoed)
		{
			// name the failure tier: no bytes at all vs bytes without the text
			std::string escaped;
			for (char c : firstBytes)
			{
				if (c >= 32 && c < 127)
				{
					escaped.push_back(c);
				}
				else
				{
					char hex[8];
					std::snprintf(hex, sizeof(hex), "\\x%02x",
						static_cast<unsigned char>(c));
					escaped += hex;
				}
			}
			SDL_Log("terminal-test diag: totalRead=%zu firstBytes=[%s]",
				totalRead, escaped.c_str());
			SDL_Log("terminal-test diag: visible=[%.240s]",
				screen.dumpVisible().c_str());
		}
		check(echoed, "typed input echoes back through the grid");

		// 2b) multibyte round trip: an umlaut, a box-drawing bar and a BMP
		//     symbol - the glyph classes a full-screen text UI paints - go IN
		//     as UTF-8 bytes and must come back OUT through the whole pipe
		//     (the pseudoconsole decodes UTF-8 input into the wide console
		//     buffer and re-encodes its output; a POSIX tty echoes the bytes
		//     verbatim) into multi-byte grid cells
		const std::string utfWord = std::string("GL") + "\xC3\x9C" + "CK" +
			"\xE2\x94\x80" + "\xE2\x98\x83";	// GLÜCK + U+2500 + U+2603
	#if defined(_WIN32)
		pty->write(std::string("echo ") + utfWord +
			encodeTermKey(TermKey::Enter, {}));
	#else
		// cat is still running - the typed line echoes straight back
		pty->write(utfWord + encodeTermKey(TermKey::Enter, {}));
	#endif
		check(pumpUntil(5000, [&]
			{
				return screen.dumpVisible().find(utfWord) != std::string::npos;
			}),
			"multibyte UTF-8 text round-trips into the grid");

		// 2c) paste seam: the paste ENCODING is verbatim unless the app enabled
		//     bracketed paste (then it is framed) - pure, platform-independent
		check(terminalPasteEncoding("PASTEWORD\n", false) == "PASTEWORD\n",
			"plain paste encodes verbatim");
		check(terminalPasteEncoding("X", true) == "\x1b[200~X\x1b[201~",
			"bracketed paste is framed with ESC[200~/201~");
	#if !defined(_WIN32)
		// ... and the encoded bytes reach the pty: cat is still running, so a
		// pasted line echoes back
		pty->write(terminalPasteEncoding("PASTEWORD\n", false));
		check(pumpUntil(5000, [&]
			{
				return screen.dumpVisible().find("PASTEWORD")
					!= std::string::npos;
			}),
			"pasted bytes reach the pty and echo back through the grid");
	#endif

		// the child must still be ALIVE here - a shell that died early (broken
		// stdio) would make the exit-after-command check below pass VACUOUSLY
		check(pty->isAlive(), "child alive before the exit command");

		// 3) a control code ends the filter, then the shell exits
	#if !defined(_WIN32)
		{
			TermMods ctrl;
			ctrl.ctrl = true;
			pty->write(encodeControlChar('d', ctrl));	// EOF -> ends cat
			pump(200);
		}
		pty->write("exit\n");
	#else
		pty->write(std::string("exit") + encodeTermKey(TermKey::Enter, {}));
	#endif

		// 4) the child dies (poll, then assert). Keep draining output while we
		// wait - a shell blocked writing to a full pty buffer would never reach
		// its own exit() if nobody reads. Drained bytes still flow into the
		// screen: late echoes must land in the grid, not vanish into a void
		// buffer (a cold cmd.exe echoed AFTER the old fixed window and the
		// discard drain made a working round trip read as an echo failure).
		bool died = false;
		std::vector<char> drain(4096);
		for (int i = 0; i < 200; ++i)	// up to ~2s
		{
			const std::size_t dn = pty->read(drain.data(), drain.size());
			if (dn > 0)
			{
				screen.write(drain.data(), dn);
			}
			if (!pty->isAlive())
			{
				died = true;
				break;
			}
			std::this_thread::sleep_for(milliseconds(10));
		}
		if (!died)
		{
			pty->terminate();
			died = !pty->isAlive();
			check(died, "child terminates on terminate()");
		}
		else
		{
			check(true, "child exits after the exit command");
		}

		// terminate() is idempotent + closes the pty
		pty->terminate();
		check(!pty->isAlive(), "pty closed and child reaped");

		// 5) reply channel: the VT core answers a query on its input (Primary DA
		//    / cursor-position report) so a query-driven shell does not stall.
		//    The panel wires this to pty->write; here a capture responder proves
		//    the screen->responder path emits the report.
		{
			EditorTerminalScreen replyScreen(80, 24);
			std::string reply;
			replyScreen.setResponder([&](char const* data, std::size_t len)
				{
					reply.append(data, len);
				});
			replyScreen.write("\x1b[6n");	// device-status cursor-position query
			check(reply.rfind("\x1b[", 0) == 0 && !reply.empty() &&
				reply.back() == 'R', "cursor-position report answered (ESC[..R)");
			reply.clear();
			replyScreen.write("\x1b[c");		// Primary Device Attributes query
			check(!reply.empty() && reply.back() == 'c',
				"Primary Device Attributes answered (ESC[?..c)");
		}

		// 6) copy seam: the grid text of a selection is extracted verbatim and
		//    reaches the OS clipboard (via SDL). The extraction is pure; the SDL
		//    round trip needs the video subsystem, skipped where unavailable.
		{
			EditorTerminalScreen clip(20, 3, 100);
			clip.write("COPYME");
			const std::string sel =
				terminalSelectionText(clip, 20, 0, 0, 0, 6);
			check(sel == "COPYME", "selection text extracts the grid region");
			if (SDL_InitSubSystem(SDL_INIT_VIDEO))
			{
				// SAVE the user's clipboard first and RESTORE it after: a test run
				// must never leave a probe string sitting in the OS pasteboard
				char* saved = SDL_GetClipboardText();	// "" when empty, never null
				const std::string savedClip = saved ? saved : "";
				if (saved != nullptr)
				{
					SDL_free(saved);
				}
				SDL_SetClipboardText(sel.c_str());
				char* got = SDL_GetClipboardText();
				check(got != nullptr && sel == got,
					"copied selection reaches the OS clipboard (SDL)");
				if (got != nullptr)
				{
					SDL_free(got);
				}
				// owning the clipboard means answering other processes' requests,
				// and a requestor is free to exit before the answer is written -
				// on X11 the server then reports an asynchronous BadWindow that
				// Xlib's default handler turns into process death. Provoke
				// exactly that, pump the answer out and read the clipboard back:
				// an unguarded build never reaches the check at all.
				if (Orkige::PlatformWindow::probeDeadClipboardRequestor())
				{
					for (int i = 0; i < 4; ++i)
					{
						SDL_PumpEvents();
						SDL_Delay(5);
					}
					char* after = SDL_GetClipboardText();
					check(after != nullptr && sel == after,
						"answering a clipboard request from a process that "
						"already exited leaves this one alive and the clipboard "
						"intact (X error guard)");
					if (after != nullptr)
					{
						SDL_free(after);
					}
				}
				SDL_SetClipboardText(savedClip.c_str());	// restore
				SDL_QuitSubSystem(SDL_INIT_VIDEO);
			}
			else
			{
				SDL_Log("terminal-test: SDL video unavailable - clipboard "
					"round trip skipped (%s)", SDL_GetError());
			}
		}

		// 7) font coverage: the mono atlas bakes the TUI blocks (box drawing,
		//    braille spinners, ...) so terminal output renders instead of '?'.
		//    Build a headless atlas the way the editor does and assert the key
		//    codepoints have a real (non-fallback) baked glyph. Skipped when no
		//    system mono font exists (the merge needs a primary to merge into).
		{
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(64.0f, 64.0f);
			ImFont* mono = Orkige::loadMacSystemMonoFont(io, 13.0f, 1.0f,
				ORKIGE_EDITOR_ICON_FONT_DIR "/DejaVuSans.ttf");
			if (mono != nullptr)
			{
				// the agent badges bake onto the base font (Fonts[0]); the custom
				// rects must be reserved BEFORE the atlas is built (bake forces it)
				Orkige::bakeTerminalAgentBadges(io, 13.0f);
				unsigned char* pixels = nullptr;
				int atlasW = 0;
				int atlasH = 0;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasW, &atlasH);
				ImFontBaked* baked = mono->GetFontBaked(13.0f);
				auto baked_has = [&](unsigned int cp)
				{
					return baked != nullptr &&
						baked->FindGlyphNoFallback(
							static_cast<ImWchar>(cp)) != nullptr;
				};
				check(baked_has(0x2502), "box-drawing baked (U+2502)");
				check(baked_has(0x2588), "block element baked (U+2588)");
				check(baked_has(0x2026), "ellipsis baked (U+2026)");
				check(baked_has(0x2192), "arrow baked (U+2192)");
				check(baked_has(0x25cf), "geometric shape baked (U+25CF)");
				check(baked_has(0x28fe),
					"braille spinner baked (U+28FE, merged fallback)");
				// the agent-badge PUA glyphs resolve (non-fallback) in the atlas
				const OrkigeEditor::TerminalAgent badgeAgents[] = {
					OrkigeEditor::TerminalAgent::Claude,
					OrkigeEditor::TerminalAgent::Codex,
					OrkigeEditor::TerminalAgent::Opencode,
					OrkigeEditor::TerminalAgent::Aider,
					OrkigeEditor::TerminalAgent::Gemini,
					OrkigeEditor::TerminalAgent::Generic,
				};
				for (OrkigeEditor::TerminalAgent a : badgeAgents)
				{
					const unsigned int cp =
						OrkigeEditor::terminalAgentBadgeCodepoint(a);
					check(baked_has(cp), "agent badge glyph baked (PUA)");
				}
			}
			else
			{
				SDL_Log("terminal-test: no system mono font - glyph coverage "
					"leg skipped");
			}
			ImGui::DestroyContext();
		}

		// 8) multiple sessions: independent grids, the app-aware tab title seam
		//    (an OSC title on session B is detected as an agent) and a close that
		//    kills the child and shrinks the list.
		{
			// two VT screens are fully independent grids
			EditorTerminalScreen a(20, 4);
			EditorTerminalScreen b(20, 4);
			a.write("AAA");
			check(a.dumpVisible().substr(0, 3) == "AAA",
				"session A grid holds its own text");
			check(b.dumpVisible().find("AAA") == std::string::npos,
				"session B grid is independent of A");

			// an OSC title fed to session B classifies it as the Claude agent; A,
			// with no title, falls back to its numbered terminal label. The
			// classification is STICKY per session (process name AND title).
			b.write("\x1b]0;claude\x07");
			TerminalAgent stickyB = terminalUpdateStickyAgent(
				TerminalAgent::None, "claude", b.getTitle());
			const TerminalTabLabel labelB =
				terminalSessionTabLabel(stickyB, b.getTitle(), "claude", 2);
			check(labelB.text == "Claude" &&
				labelB.glyph == TerminalGlyphClass::Agent &&
				labelB.agent == TerminalAgent::Claude,
				"session B classifies as the Claude agent (badge + canonical name)");
			// the specific agent drives a specific PUA badge codepoint
			check(terminalAgentBadgeCodepoint(labelB.agent) == 0xE000u,
				"the Claude tenant maps to its generated badge codepoint");

			// claude drives its window title as a live STATUS TICKER whose
			// leading sparkle the UI font lacks. The classified tab LABEL must
			// stay the stable badge + "Claude" (never the ticker); the ticker
			// rides the tab TOOLTIP with the un-renderable codepoints filtered
			// out (nothing, not a '?').
			b.write("\x1b]0;\xe2\x9c\xb3 Check open file\x07");
			stickyB = terminalUpdateStickyAgent(stickyB, "claude", b.getTitle());
			const TerminalTabLabel tickerLabel =
				terminalSessionTabLabel(stickyB, b.getTitle(), "claude", 2);
			check(tickerLabel.text == "Claude" &&
				tickerLabel.agent == TerminalAgent::Claude &&
				tickerLabel.glyph == TerminalGlyphClass::Agent,
				"a status-ticker title leaves the classified tab label at Claude");
			check(terminalFilterRenderable(b.getTitle()) == "Check open file",
				"the ticker tooltip strips the un-renderable leading sparkle");
			// when claude exits the foreground reverts to the shell -> declassify
			const TerminalAgent afterExit =
				terminalUpdateStickyAgent(stickyB, "fish", "fish");
			check(afterExit == TerminalAgent::None,
				"the agent exiting (shell back in front) declassifies the session");

			const TerminalTabLabel labelA =
				terminalSessionTabLabel(TerminalAgent::None, a.getTitle(), "", 1);
			check(labelA.text == "Terminal 1" &&
				labelA.glyph == TerminalGlyphClass::Terminal &&
				labelA.agent == TerminalAgent::None,
				"session A falls back to the numbered terminal label");

			// a two-child pty list: closing one terminates its child and shrinks
			// the list, the other survives
			std::vector<std::unique_ptr<TerminalPty>> sessions;
			auto spawnOne = [&]() -> bool
			{
				std::unique_ptr<TerminalPty> child = createTerminalPty();
				TermPtySpec sp;
				sp.cols = 40;
				sp.rows = 12;
				sp.loginShell = false;
			#if defined(_WIN32)
				sp.shell = "cmd.exe";
			#else
				sp.shell = "/bin/sh";
			#endif
				if (!child->spawn(sp))
				{
					return false;
				}
				sessions.push_back(std::move(child));
				return true;
			};
			if (spawnOne() && spawnOne())
			{
				check(sessions.size() == 2, "two sessions spawned into the list");
				TerminalPty* survivor = sessions[1].get();
				sessions[0]->terminate();
				check(!sessions[0]->isAlive(),
					"the closed session's child died");
				sessions.erase(sessions.begin());
				check(sessions.size() == 1, "the session list shrank to one");
				check(sessions[0].get() == survivor,
					"the surviving session is the OTHER one");
				sessions[0]->terminate();
			}
			else
			{
				SDL_Log("terminal-test: a second pty was unavailable - the "
					"multi-session pty leg was skipped");
			}
		}

		// 9) the follow/pin contract driven headlessly through the SAME seam the
		//    panel uses (terminalFollowDecision + terminalScrollMax): a real VT
		//    screen grows, and with the pin held the computed target scroll TRACKS
		//    the growing max - the regression the panel's stale-GetScrollMaxY bug
		//    broke (the pin landed short of a grown tail, then unpinned itself).
		{
			const float cellH = 16.0f;
			const float viewH = 24.0f * cellH;	// a 24-row viewport
			EditorTerminalScreen follow(80, 24, kScrollbackLines);
			bool followTail = true;			// the pin state, as the panel holds it
			float lastTarget = -1.0f;
			bool targetTracksMax = true;
			bool stayedPinned = true;
			int lastTotalLines = -1;
			for (int burst = 0; burst < 40; ++burst)
			{
				// a burst of lines lands in the grid (pushes into scrollback)
				for (int line = 0; line < 5; ++line)
				{
					follow.write("terminal follow line of output\r\n");
				}
				const int totalLines = follow.scrollbackCount() + 24;
				TerminalFollowInputs in;
				in.contentGrew = (totalLines != lastTotalLines);
				in.wasFollowing = followTail;
				in.atBottom = true;		// the panel pins to the true bottom
				const TerminalFollowVerdict verdict =
					terminalFollowDecision(in);
				followTail = verdict.followTail;
				if (!followTail)
				{
					stayedPinned = false;	// a pinned view must never self-unpin
				}
				if (verdict.pinToBottom)
				{
					const float target =
						terminalScrollMax(totalLines, cellH, viewH);
					if (target < lastTarget)	// the tail only grows -> so does the target
					{
						targetTracksMax = false;
					}
					lastTarget = target;
				}
				lastTotalLines = totalLines;
			}
			check(stayedPinned,
				"a pinned terminal keeps following as its output grows");
			check(targetTracksMax && lastTarget > 0.0f,
				"the pinned scroll target tracks the growing content max");

			// a user scroll-up UNPINS, and returning to the bottom RE-PINS
			TerminalFollowInputs up;
			up.wasFollowing = true;
			up.userScrolledAway = true;
			up.contentGrew = true;
			const TerminalFollowVerdict unpinned = terminalFollowDecision(up);
			check(!unpinned.followTail && !unpinned.pinToBottom,
				"scrolling up unpins the terminal from the tail");
			TerminalFollowInputs downAgain;
			downAgain.wasFollowing = false;
			downAgain.atBottom = true;
			const TerminalFollowVerdict repinned =
				terminalFollowDecision(downAgain);
			check(repinned.followTail,
				"returning to the bottom re-pins the terminal to the tail");
		}

		SDL_Log("orkige_editor: terminal-test %s",
			exitCode == 0 ? "PASSED" : "FAILED");
		return exitCode;
	}
}
