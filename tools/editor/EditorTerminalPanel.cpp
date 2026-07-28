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
// input encoded by the pure EditorTerminalKeys table. One session (v1). When
// the editor's MCP endpoint is live, the spawned shell's environment carries
// the connection material and a one-line `claude mcp add ...` hint is shown, so
// an agent started inside can drive the very editor it lives in.
//
// The terminal is DELIBERATELY not an MCP verb: a headless agent spawning UI
// shells is out of scope (and a laundering path). It is a human affordance.
#include "EditorApp.h"
#include "EditorTerminalPanel.h"
#include "EditorTerminalPty.h"
#include "EditorTerminalScreen.h"
#include "EditorTerminalKeys.h"
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

	//! the one terminal session (v1: a single shell). Process-static so it
	//! survives panel-tab hide/show; torn down at terminalPanelShutdown().
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

		// linear selection in ABSOLUTE-line coords (scrollback + visible)
		bool	selecting = false;
		bool	hasSelection = false;
		int		anchorLine = 0;
		int		anchorCol = 0;
		int		headLine = 0;
		int		headCol = 0;
	};

	TerminalSession& session()
	{
		static TerminalSession instance;
		return instance;
	}

	ImU32 cellColor(TermColor c)
	{
		return IM_COL32(c.r, c.g, c.b, 255);
	}

	//! spawn the shell for `state`'s project (cwd = project root, else home),
	//! seeding the MCP env when the endpoint is live.
	void spawnSession(EditorState& state, std::string const& mcpUrl,
		std::string const& mcpTokenFile, int cols, int rows)
	{
		TerminalSession& s = session();
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

	//! build the selected text (reading order, newline between lines)
	std::string selectionText(TerminalSession const& s)
	{
		int aLine = 0;
		int aCol = 0;
		int bLine = 0;
		int bCol = 0;
		if (!orderedSelection(s, aLine, aCol, bLine, bCol))
		{
			return std::string();
		}
		std::string out;
		for (int line = aLine; line <= bLine; ++line)
		{
			const int startCol = (line == aLine) ? aCol : 0;
			const int endCol = (line == bLine) ? bCol : s.cols;
			std::string row;
			for (int col = startCol; col < endCol && col < s.cols; ++col)
			{
				TermCell cellValue = absoluteCell(s, line, col);
				if (cellValue.width == 0)
				{
					continue;
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
			const std::string text = selectionText(s);
			if (!text.empty())
			{
				ImGui::SetClipboardText(text.c_str());
			}
			return;
		}
		if (pasteChord)
		{
			const char* clip = ImGui::GetClipboardText();
			if (clip != nullptr && clip[0] != '\0')
			{
				std::string paste = clip;
				if (s.screen->bracketedPaste())
				{
					paste = std::string("\x1b[200~") + paste + "\x1b[201~";
				}
				s.pty->write(paste);
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
}

void drawTerminalPanel(EditorState& state, ViewSettings& viewSettings,
	std::string const& mcpUrl, std::string const& mcpTokenFile, bool* visible)
{
	(void)viewSettings;
	TerminalSession& s = session();

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

	// --- header: shell + status + restart -----------------------------------
	if (s.spawned && !s.exited)
	{
		ImGui::TextColored(ImVec4(0.42f, 0.78f, 0.47f, 1.0f), ICON_FA_TERMINAL);
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
			s.spawned = false;	// a fresh spawn happens below
			s.exited = false;
			s.hasSelection = false;
		}
	}

	// --- the MCP connect hint (only when the endpoint is live) ---------------
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
	ImGui::Separator();

	// --- the grid area ------------------------------------------------------
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const int newCols = std::max(1, static_cast<int>(avail.x / cellW));
	const int newRows = std::max(1, static_cast<int>(avail.y / cellH));

	// spawn on first interactive appearance (deferred until the grid size is
	// known so the shell starts at the right dimensions)
	if (!s.spawned && !s.exited)
	{
		spawnSession(state, mcpUrl, mcpTokenFile, newCols, newRows);
	}

	if (s.spawned && s.screen)
	{
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

		// bounded per-frame drain so an output flood degrades gracefully
		bool gotOutput = false;
		std::size_t total = 0;
		std::vector<char> buffer(4096);
		while (total < kReadCap && s.pty)
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
		if (s.pty && !s.pty->isAlive())
		{
			s.exited = true;
		}

		const int scrollbackCount = s.screen->scrollbackCount();
		const int totalLines = scrollbackCount + s.rows;

		if (mono != nullptr)
		{
			ImGui::PushFont(mono);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(26, 26, 26, 255));
		ImGui::BeginChild("##termgrid", ImVec2(0.0f, 0.0f), false,
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);

		const bool panelFocused =
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const bool hovered = ImGui::IsWindowHovered();
		const ImVec2 mouse = ImGui::GetMousePos();
		const TermCursor cur = s.screen->cursor();
		const int cursorLine = scrollbackCount + cur.row;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 selBg = IM_COL32(60, 90, 140, 255);
		int hoverLine = -1;
		int hoverCol = 0;

		// a list clipper draws only the visible lines - a 5000-line scrollback
		// never turns into a million draw calls, and the clipper positions each
		// line's cursor correctly for the current scroll
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
					if (!cellValue.glyph.empty() && cellValue.glyph != " ")
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
				// the cursor block on its line
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
				// hovered cell (for selection gestures below)
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

		// auto-scroll to the newest output unless the user scrolled up; then
		// re-read whether the view sits at the bottom
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

		// keyboard goes to the pty while the panel holds focus; the editor's
		// global shortcuts stand down this frame (state.terminalFocused)
		state.terminalFocused = panelFocused && !s.exited;
		if (state.terminalFocused && s.pty)
		{
			handleTerminalInput(s);
		}
	}
	else
	{
		state.terminalFocused = false;
	}

	ImGui::End();
}

namespace OrkigeEditor
{
	void terminalPanelShutdown()
	{
		TerminalSession& s = session();
		if (s.pty)
		{
			s.pty->terminate();
		}
		s.pty.reset();
		s.screen.reset();
		s.spawned = false;
		s.exited = true;
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
		bool sawRed = false;
		for (int r = 0; r < 24 && !sawRed; ++r)
		{
			for (int c = 0; c < 80; ++c)
			{
				TermCell cellValue = screen.cell(r, c);
				if (cellValue.glyph == "R" && cellValue.fg.r >= 180 &&
					cellValue.fg.g <= 90 && cellValue.fg.b <= 90)
				{
					sawRed = true;
					break;
				}
			}
		}
		check(sawRed, "SGR colour parsed (red foreground)");
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

		SDL_Log("orkige_editor: terminal-test %s",
			exitCode == 0 ? "PASSED" : "FAILED");
		return exitCode;
	}
}
