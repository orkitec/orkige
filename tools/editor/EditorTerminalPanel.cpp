/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalPanel.cpp - the dockable Terminal panel: an OS pty (the
// EditorTerminalPty seam) feeding the libvterm-backed screen model
// (EditorTerminalScreen), rendered as a mono-font cell grid, with keyboard
// input encoded by the pure EditorTerminalKeys table. The panel hosts a LIST of
// sessions, one per in-panel tab, each labelled with what is running inside it
// (the OSC title, else the pty's foreground process name; a recognised agent CLI
// gets a robot glyph) via the pure EditorTerminalSession helpers. When the
// editor's MCP endpoint is live, the spawned shell's environment carries the
// connection material and a one-line `claude mcp add ...` hint is shown, so an
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
#include "EditorTabMenu.h"
#include "EditorTheme.h"
#include "IconsFontAwesome6.h"

#include <imgui_internal.h>	// FindWindowByName + DockId (first-appearance dock)

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
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
		int		uid = 0;			//!< stable, monotonic ImGui id suffix

		// app-aware tab title: the last OSC title + the last polled foreground
		// process name; either can be empty. terminalTabLabel() composes them.
		std::string	vtTitle;
		std::string	procName;
		std::chrono::steady_clock::time_point lastProcPoll{};

		// linear selection in ABSOLUTE-line coords (scrollback + visible)
		bool	selecting = false;
		bool	hasSelection = false;
		int		anchorLine = 0;
		int		anchorCol = 0;
		int		headLine = 0;
		int		headCol = 0;
	};

	//! the terminal panel's process-static session LIST (v1: not persisted
	//! across editor runs - a fresh launch starts with one shell). Torn down at
	//! terminalPanelShutdown().
	struct TerminalPanelState
	{
		std::vector<std::unique_ptr<TerminalSession>>	sessions;
		int		active = 0;			//!< index of the tab whose grid renders
		int		nextUid = 1;		//!< monotonic id source for stable tab ids
		int		pendingClose = -1;	//!< a live session queued for close-confirm
		int		lastCols = 80;		//!< last rendered grid size, seeds spawns of
		int		lastRows = 24;		//!< background tabs never yet made active
	};

	TerminalPanelState& panel()
	{
		static TerminalPanelState instance;
		return instance;
	}

	//! draw glyph for a session's detected app class
	const char* glyphFor(TerminalGlyphClass glyphClass)
	{
		return glyphClass == TerminalGlyphClass::Agent
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
	void handleTerminalInput(TerminalSession& s)
	{
		ImGuiIO& io = ImGui::GetIO();
		const bool ctrl = io.KeyCtrl;
		const bool shift = io.KeyShift;
		const bool alt = io.KeyAlt;
		const bool super = io.KeySuper;
	#if defined(__APPLE__)
		const bool copyChord = super && ImGui::IsKeyPressed(ImGuiKey_C, false);
		const bool pasteChord = super && ImGui::IsKeyPressed(ImGuiKey_V, false);
	#else
		// Ctrl+Shift+C/V are the terminal copy/paste (Ctrl+C/V stay control codes)
		const bool copyChord =
			ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_C, false);
		const bool pasteChord =
			ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_V, false);
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
			return;
		}
		if (pasteChord)
		{
			char* clip = SDL_GetClipboardText();	// "" when empty, never null
			if (clip != nullptr && clip[0] != '\0')
			{
				s.pty->write(OrkigeEditor::terminalPasteEncoding(
					clip, s.screen->bracketedPaste()));
			}
			if (clip != nullptr)
			{
				SDL_free(clip);
			}
			return;
		}

		// printable text: the IME/text-input queue (skip while a Ctrl/Cmd chord
		// is held - those are control codes / editor chords, not text)
		if (!ctrl && !super)
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
			{
				const ImWchar ch = io.InputQueueCharacters[i];
				if (ch == 0 || ch == '\t' || ch == '\r' || ch == '\n')
				{
					continue;	// specials come through the key path below
				}
				s.pty->write(encodeUtf8(static_cast<std::uint32_t>(ch)));
			}
		}

		TermMods mods;
		mods.ctrl = ctrl;
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
			}
		}

		// Ctrl+<letter/symbol> control codes (never with Super; Ctrl+Shift+C/V
		// were consumed above as copy/paste)
		if (ctrl && !super)
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
					}
				}
			}
		}
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
		return gotOutput;
	}

	//! append a fresh, not-yet-spawned session (spawned lazily once a grid size
	//! is known) and make it the active tab
	TerminalSession& addSession()
	{
		TerminalPanelState& p = panel();
		auto s = std::make_unique<TerminalSession>();
		s->uid = p.nextUid++;
		p.sessions.push_back(std::move(s));
		p.active = static_cast<int>(p.sessions.size()) - 1;
		return *p.sessions.back();
	}

	//! terminate + drop the session at `index`, fixing the active index
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
		const int count = static_cast<int>(p.sessions.size());
		p.active = terminalIndexAfterClose(count, index, p.active);
		p.sessions.erase(p.sessions.begin() + index);
		if (p.active < 0)
		{
			p.active = 0;
		}
	}

	//! render one session's grid + input into the current content region. `s`
	//! is the ACTIVE session (only its body runs). Sets state.terminalFocused.
	void drawSessionGrid(TerminalSession& s, EditorState& state, float cellW,
		float cellH, bool gotOutput, ImFont* mono)
	{
		TerminalPanelState& p = panel();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const int newCols = std::max(1, static_cast<int>(avail.x / cellW));
		const int newRows = std::max(1, static_cast<int>(avail.y / cellH));
		p.lastCols = newCols;
		p.lastRows = newRows;

		if (newCols != s.cols || newRows != s.rows)
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
		const bool hovered = ImGui::IsWindowHovered();
		const ImVec2 mouse = ImGui::GetMousePos();
		const TermCursor cur = s.screen->cursor();
		const int cursorLine = scrollbackCount + cur.row;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImFontBaked* fontBaked = ImGui::GetFontBaked();
		const ImU32 selBg = IM_COL32(60, 90, 140, 255);
		int hoverLine = -1;
		int hoverCol = 0;

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
				if (hovered && mouse.y >= pos.y && mouse.y < pos.y + cellH)
				{
					hoverLine = line;
					hoverCol = std::max(0, std::min(s.cols,
						static_cast<int>((mouse.x - pos.x) / cellW)));
				}
				ImGui::Dummy(ImVec2(s.cols * cellW, cellH));
			}
		}
		clipper.End();

		// --- selection via mouse drag over the grid ---
		if (hoverLine >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			s.selecting = true;
			s.hasSelection = false;
			s.anchorLine = hoverLine;
			s.anchorCol = hoverCol;
			s.headLine = hoverLine;
			s.headCol = hoverCol;
		}
		else if (s.selecting && hoverLine >= 0 &&
			ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			s.headLine = hoverLine;
			s.headCol = hoverCol;
			s.hasSelection = true;
		}
		if (s.selecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			s.selecting = false;
		}

		if (gotOutput && s.followTail)
		{
			ImGui::SetScrollY(ImGui::GetScrollMaxY());
		}
		s.followTail = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		if (mono != nullptr)
		{
			ImGui::PopFont();
		}

		state.terminalFocused = panelFocused && !s.exited;
		if (state.terminalFocused && s.pty)
		{
			handleTerminalInput(s);
		}
	}
}

void drawTerminalPanel(EditorState& state, ViewSettings& viewSettings,
	std::string const& mcpUrl, std::string const& mcpTokenFile, bool* visible)
{
	(void)viewSettings;
	TerminalPanelState& p = panel();

	// dock into the bottom group (beside Console/Assets/Debug/Source Control)
	// the FIRST time the panel appears, so opening it from the View menu tabs in
	// there rather than floating - mirrors the Source Control panel. FirstUseEver
	// respects any later move the user makes.
	for (const char* anchorName : { "Console", "Stats", "Assets###Assets",
		"Debug###Debug", ICON_FA_CODE_BRANCH " Source Control###SourceControl" })
	{
		ImGuiWindow* anchor = ImGui::FindWindowByName(anchorName);
		if (anchor && anchor->DockId != 0)
		{
			ImGui::SetNextWindowDockID(anchor->DockId, ImGuiCond_FirstUseEver);
			break;
		}
	}

	if (!ImGui::Begin(ICON_FA_TERMINAL " Terminal###Terminal", visible))
	{
		ImGui::End();
		return;
	}
	OrkigeEditor::editorPanelTabMenu(visible);

	// automated runs never open a shell (the pollution-hygiene rule); the
	// selfcheck drives the pty seam directly (runTerminalSelfCheck)
	if (gAutomatedRun)
	{
		ImGui::TextDisabled("The terminal is inactive during automated runs.");
		state.terminalFocused = false;
		ImGui::End();
		return;
	}

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

	// at least one session always exists (the first shell); a fresh launch
	// opens with one - the count is not persisted across editor runs (v1)
	if (p.sessions.empty())
	{
		addSession();
	}
	if (p.active < 0 || p.active >= static_cast<int>(p.sessions.size()))
	{
		p.active = 0;
	}

	// EVERY session drains this frame (background agents must keep reading or a
	// full pty buffer stalls them); only the active one renders below. The
	// still-unspawned tabs spawn at the last known grid size so a background
	// "+" tab comes up sized even before it is first shown.
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

	// --- the MCP connect hint (only when the endpoint is live) ---------------
	// (global: every session's shell inherits the same ORKIGE_MCP_* env)
	if (!mcpUrl.empty())
	{
		std::string connectCmd =
			"claude mcp add --transport http orkige " + mcpUrl;
		if (!mcpTokenFile.empty())
		{
			connectCmd += " --header \"Authorization: Bearer "
				"$(cat \\\"$ORKIGE_MCP_TOKEN_FILE\\\")\"";
		}
		ImGui::TextDisabled(ICON_FA_ROBOT
			" This editor's MCP endpoint is live in this shell "
			"(ORKIGE_MCP_URL). Connect an agent:");
		ImGui::TextWrapped("%s", connectCmd.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton(ICON_FA_COPY "###termMcpCopy"))
		{
			ImGui::SetClipboardText(connectCmd.c_str());
		}
		ImGui::SetItemTooltip("Copy the connect command");
	}

	// --- the session tab bar ------------------------------------------------
	int requestClose = -1;	// a tab whose close (x) was clicked this frame
	bool requestAdd = false;
	bool renderedActive = false;
	if (ImGui::BeginTabBar("##termtabs", ImGuiTabBarFlags_AutoSelectNewTabs |
		ImGuiTabBarFlags_TabListPopupButton |
		ImGuiTabBarFlags_FittingPolicyScroll))
	{
		for (std::size_t i = 0; i < p.sessions.size(); ++i)
		{
			TerminalSession& s = *p.sessions[i];
			const TerminalTabLabel label = terminalTabLabel(s.vtTitle,
				s.procName, static_cast<int>(i) + 1);
			// glyph + text + a stable ###id so a relabel never re-creates the tab
			std::string tabLabel = std::string(glyphFor(label.glyph)) + " " +
				label.text + "###term" + std::to_string(s.uid);
			bool open = true;
			if (ImGui::BeginTabItem(tabLabel.c_str(), &open,
				ImGuiTabItemFlags_None))
			{
				p.active = static_cast<int>(i);
				renderedActive = true;

				// per-session header: shell / exited + restart
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
					drawSessionGrid(s, state, cellW, cellH,
						gotOutput[i], mono);
				}
				else
				{
					state.terminalFocused = false;
				}
				ImGui::EndTabItem();
			}
			if (!open)
			{
				requestClose = static_cast<int>(i);
			}
		}
		// the "+" new-session control (a trailing tab button)
		if (ImGui::TabItemButton("+###termadd",
			ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
		{
			requestAdd = true;
		}
		ImGui::SetItemTooltip("New terminal session");
		ImGui::EndTabBar();
	}
	if (!renderedActive)
	{
		// no tab body ran this frame (e.g. the panel is collapsed to just the
		// tab bar): the pty must not be left holding the editor's shortcuts
		state.terminalFocused = false;
	}

	// --- close handling (deferred until after the tab bar) ------------------
	// closing a tab whose child is still alive asks first; a dead one just goes.
	if (requestClose >= 0 && requestClose < static_cast<int>(p.sessions.size()))
	{
		TerminalSession& s = *p.sessions[requestClose];
		const bool alive = s.pty && s.pty->isAlive() && !s.exited;
		if (alive)
		{
			p.pendingClose = requestClose;
			ImGui::OpenPopup("Close terminal?###termclose");
		}
		else
		{
			closeSession(requestClose);
		}
	}
	if (ImGui::BeginPopupModal("Close terminal?###termclose", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (p.pendingClose >= 0 &&
			p.pendingClose < static_cast<int>(p.sessions.size()))
		{
			TerminalSession& s = *p.sessions[p.pendingClose];
			const TerminalTabLabel label = terminalTabLabel(s.vtTitle,
				s.procName, p.pendingClose + 1);
			ImGui::Text("\"%s\" is still running. Close it and terminate the "
				"process?", label.text.c_str());
			ImGui::Separator();
			if (ImGui::Button("Close"))
			{
				closeSession(p.pendingClose);
				p.pendingClose = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				p.pendingClose = -1;
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			ImGui::CloseCurrentPopup();	// the session vanished under us
		}
		ImGui::EndPopup();
	}
	if (requestAdd)
	{
		addSession();
	}

	ImGui::End();
}

namespace OrkigeEditor
{
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
		p.active = 0;
		p.pendingClose = -1;
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

	#if !defined(_WIN32)
		// 1) colour + text: a printed SGR sequence lands in the grid as red text
		pty->write("printf '\\033[31mREDWORD\\033[0m\\r\\n'\n");
		check(pumpUntil(5000, [&]
			{
				return screen.dumpVisible().find("REDWORD") != std::string::npos;
			}),
			"printed text reaches the grid");
		// the RED check pumps on its own condition: the text condition above can
		// fire on the ECHOED command line (it literally contains REDWORD,
		// uncoloured) a read ahead of the printf's coloured output - a split a
		// sanitizer-slowed run exposes while a fast one gets both in one chunk
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
		check(pumpUntil(5000, redCellVisible), "SGR colour parsed (red foreground)");
	#endif

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

	#if !defined(_WIN32)
		// 2b) paste seam: the paste ENCODING is verbatim unless the app enabled
		//     bracketed paste (then it is framed), and the encoded bytes reach
		//     the pty. cat is still running, so a pasted line echoes back.
		check(terminalPasteEncoding("PASTEWORD\n", false) == "PASTEWORD\n",
			"plain paste encodes verbatim");
		check(terminalPasteEncoding("X", true) == "\x1b[200~X\x1b[201~",
			"bracketed paste is framed with ESC[200~/201~");
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
				SDL_SetClipboardText(sel.c_str());
				char* got = SDL_GetClipboardText();
				check(got != nullptr && sel == got,
					"copied selection reaches the OS clipboard (SDL)");
				if (got != nullptr)
				{
					SDL_free(got);
				}
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

			// an OSC title fed to session B is detected as the agent label; A,
			// with no title, falls back to its numbered terminal label
			b.write("\x1b]0;claude\x07");
			const TerminalTabLabel labelB = terminalTabLabel(b.getTitle(), "", 2);
			check(labelB.text == "claude" &&
				labelB.glyph == TerminalGlyphClass::Agent,
				"session B tab label detects the agent from its OSC title");
			const TerminalTabLabel labelA = terminalTabLabel(a.getTitle(), "", 1);
			check(labelA.text == "Terminal 1" &&
				labelA.glyph == TerminalGlyphClass::Terminal,
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

		SDL_Log("orkige_editor: terminal-test %s",
			exitCode == 0 ? "PASSED" : "FAILED");
		return exitCode;
	}
}
