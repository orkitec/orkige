/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalScreen.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalScreen.cpp - the ONLY translation unit that includes libvterm.
// It turns pty bytes into the editor's own cell grid + cursor vocabulary
// (EditorTerminalScreen.h). The VT core is intentionally quarantined here so it
// stays swappable behind the seam: a future pure-VT library with a stable API
// and a Windows build could replace libvterm without a caller noticing.
//
// libvterm exposes the visible grid, scrollback push-lines, cursor and a few
// terminal properties (alt-screen, cursor visibility, mouse mode) through its
// screen callbacks, but does NOT publish DECCKM (application cursor keys) or
// bracketed-paste (DEC 2004) state. A tiny private-mode sniffer over the same
// byte stream recovers exactly those two, so the input encoder can send the
// arrow form the app asked for and paste can be bracketed.
#include "EditorTerminalScreen.h"

extern "C" {
#include <vterm.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! append one Unicode codepoint as UTF-8 to `out`
		void appendUtf8(std::string& out, std::uint32_t cp)
		{
			if (cp <= 0x7f)
			{
				out.push_back(static_cast<char>(cp));
			}
			else if (cp <= 0x7ff)
			{
				out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
			}
			else if (cp <= 0xffff)
			{
				out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
			}
			else if (cp <= 0x10ffff)
			{
				out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
			}
		}
	}

	struct EditorTerminalScreen::Impl
	{
		VTerm*			vt = nullptr;
		VTermScreen*	screen = nullptr;
		int				cols = 1;
		int				rows = 1;
		std::size_t		scrollbackLimit = 5000;

		TermColor		defaultFg{ 0xcc, 0xcc, 0xcc };
		TermColor		defaultBg{ 0x1a, 0x1a, 0x1a };

		bool			cursorVisible = true;
		bool			altScreen = false;
		bool			mouseTracking = false;

		// DECCKM / bracketed-paste, recovered by the private-mode sniffer
		bool			appCursorKeys = false;
		bool			bracketedPaste = false;

		// the reply sink: VT-core-generated answers to terminal queries (Primary
		// DA, cursor-position report, ...) go here so the panel can feed them
		// back to the pty's input. Empty until setResponder().
		std::function<void(char const*, std::size_t)> responder;

		// scrollback ring (front = oldest); each entry one full row of cells
		std::deque<std::vector<TermCell>> scrollback;

		// --- private-mode sniffer state (CSI ? Pm ; Pm h|l) -----------------
		enum class Sniff { Ground, Esc, Csi, Params };
		Sniff			sniff = Sniff::Ground;
		int				sniffParam = 0;
		bool			sniffHasParam = false;
		std::vector<int> sniffParams;

		//! resolve a libvterm colour (indexed/default/rgb) to a concrete RGB
		TermColor resolve(VTermColor col, bool isForeground) const
		{
			if (VTERM_COLOR_IS_DEFAULT_FG(&col))
			{
				return this->defaultFg;
			}
			if (VTERM_COLOR_IS_DEFAULT_BG(&col))
			{
				return this->defaultBg;
			}
			vterm_screen_convert_color_to_rgb(this->screen, &col);
			if (VTERM_COLOR_IS_RGB(&col))
			{
				return TermColor{ col.rgb.red, col.rgb.green, col.rgb.blue };
			}
			return isForeground ? this->defaultFg : this->defaultBg;
		}

		//! convert a libvterm cell into the editor's TermCell
		TermCell convert(VTermScreenCell const& src) const
		{
			TermCell out;
			for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && src.chars[i]; ++i)
			{
				appendUtf8(out.glyph, src.chars[i]);
			}
			out.width = src.width;
			out.fg = this->resolve(src.fg, true);
			out.bg = this->resolve(src.bg, false);
			out.attrs.bold = src.attrs.bold != 0;
			out.attrs.underline = src.attrs.underline != 0;
			out.attrs.italic = src.attrs.italic != 0;
			out.attrs.reverse = src.attrs.reverse != 0;
			out.attrs.strike = src.attrs.strike != 0;
			return out;
		}

		//! run one byte through the private-mode sniffer, updating DECCKM /
		//! bracketed-paste. The byte is NOT consumed - it also feeds libvterm.
		void sniffByte(unsigned char b)
		{
			switch (this->sniff)
			{
				case Sniff::Ground:
					if (b == 0x1b) { this->sniff = Sniff::Esc; }
					break;
				case Sniff::Esc:
					this->sniff = (b == '[') ? Sniff::Csi : Sniff::Ground;
					break;
				case Sniff::Csi:
					if (b == '?')
					{
						this->sniff = Sniff::Params;
						this->sniffParam = 0;
						this->sniffHasParam = false;
						this->sniffParams.clear();
					}
					else
					{
						// re-enter Esc directly if this byte starts a new ESC,
						// otherwise a non-private CSI we do not track
						this->sniff = (b == 0x1b) ? Sniff::Esc : Sniff::Ground;
					}
					break;
				case Sniff::Params:
					if (b >= '0' && b <= '9')
					{
						this->sniffParam = this->sniffParam * 10 + (b - '0');
						this->sniffHasParam = true;
					}
					else if (b == ';')
					{
						this->sniffParams.push_back(this->sniffParam);
						this->sniffParam = 0;
						this->sniffHasParam = false;
					}
					else if (b == 'h' || b == 'l')
					{
						if (this->sniffHasParam)
						{
							this->sniffParams.push_back(this->sniffParam);
						}
						const bool set = (b == 'h');
						for (int p : this->sniffParams)
						{
							if (p == 1) { this->appCursorKeys = set; }
							else if (p == 2004) { this->bracketedPaste = set; }
						}
						this->sniff = Sniff::Ground;
					}
					else
					{
						this->sniff = (b == 0x1b) ? Sniff::Esc : Sniff::Ground;
					}
					break;
			}
		}

		// --- libvterm screen callbacks (static; user = this Impl) -----------
		static int cbSbPushline(int cols, VTermScreenCell const* cells, void* user)
		{
			Impl* self = static_cast<Impl*>(user);
			std::vector<TermCell> line;
			line.reserve(static_cast<std::size_t>(cols));
			for (int c = 0; c < cols; ++c)
			{
				line.push_back(self->convert(cells[c]));
			}
			self->scrollback.push_back(std::move(line));
			while (self->scrollback.size() > self->scrollbackLimit)
			{
				self->scrollback.pop_front();
			}
			return 1;
		}

		// the VT core's OUTPUT: bytes the terminal must send back on its input
		// channel - query replies (Primary DA, cursor-position report), status
		// answers. Forwarded to the responder (the pty's input) so query-driven
		// apps do not stall.
		static void cbOutput(char const* s, std::size_t len, void* user)
		{
			Impl* self = static_cast<Impl*>(user);
			if (self->responder && len > 0)
			{
				self->responder(s, len);
			}
		}

		static int cbSettermprop(VTermProp prop, VTermValue* val, void* user)
		{
			Impl* self = static_cast<Impl*>(user);
			switch (prop)
			{
				case VTERM_PROP_CURSORVISIBLE:
					self->cursorVisible = val->boolean != 0;
					break;
				case VTERM_PROP_ALTSCREEN:
					self->altScreen = val->boolean != 0;
					break;
				case VTERM_PROP_MOUSE:
					self->mouseTracking = val->number != VTERM_PROP_MOUSE_NONE;
					break;
				default:
					break;
			}
			return 1;
		}
	};

	namespace
	{
		VTermScreenCallbacks const kScreenCallbacks = {
			nullptr,	// damage - we read cells on demand
			nullptr,	// moverect
			nullptr,	// movecursor - we read the cursor live from state
			&EditorTerminalScreen::Impl::cbSettermprop,
			nullptr,	// bell
			nullptr,	// resize
			&EditorTerminalScreen::Impl::cbSbPushline,
			nullptr,	// sb_popline
			nullptr,	// sb_clear
			nullptr		// sb_pushline4
		};
	}

	EditorTerminalScreen::EditorTerminalScreen(int cols, int rows,
		int scrollbackLimit)
		: mImpl(std::make_unique<Impl>())
	{
		mImpl->cols = std::max(1, cols);
		mImpl->rows = std::max(1, rows);
		mImpl->scrollbackLimit =
			static_cast<std::size_t>(std::max(0, scrollbackLimit));
		mImpl->vt = vterm_new(mImpl->rows, mImpl->cols);
		vterm_set_utf8(mImpl->vt, 1);
		// capture the VT core's reply bytes (Primary DA answers etc.) so the
		// panel can hand them back to the pty - see setResponder()
		vterm_output_set_callback(mImpl->vt, &Impl::cbOutput, mImpl.get());
		mImpl->screen = vterm_obtain_screen(mImpl->vt);
		vterm_screen_set_callbacks(mImpl->screen, &kScreenCallbacks,
			mImpl.get());
		vterm_screen_enable_altscreen(mImpl->screen, 1);
		// seed the default palette + our dark default fg/bg so unstyled cells
		// resolve to a readable colour without a preceding SGR
		VTermState* state = vterm_obtain_state(mImpl->vt);
		VTermColor fg;
		VTermColor bg;
		vterm_color_rgb(&fg, mImpl->defaultFg.r, mImpl->defaultFg.g,
			mImpl->defaultFg.b);
		vterm_color_rgb(&bg, mImpl->defaultBg.r, mImpl->defaultBg.g,
			mImpl->defaultBg.b);
		vterm_state_set_default_colors(state, &fg, &bg);
		vterm_screen_reset(mImpl->screen, 1);
	}

	EditorTerminalScreen::~EditorTerminalScreen()
	{
		if (mImpl->vt)
		{
			vterm_free(mImpl->vt);
			mImpl->vt = nullptr;
		}
	}

	void EditorTerminalScreen::write(char const* bytes, std::size_t len)
	{
		for (std::size_t i = 0; i < len; ++i)
		{
			mImpl->sniffByte(static_cast<unsigned char>(bytes[i]));
		}
		vterm_input_write(mImpl->vt, bytes, len);
		vterm_screen_flush_damage(mImpl->screen);
	}

	void EditorTerminalScreen::write(std::string const& bytes)
	{
		this->write(bytes.data(), bytes.size());
	}

	void EditorTerminalScreen::setResponder(
		std::function<void(char const*, std::size_t)> responder)
	{
		mImpl->responder = std::move(responder);
	}

	void EditorTerminalScreen::resize(int cols, int rows)
	{
		mImpl->cols = std::max(1, cols);
		mImpl->rows = std::max(1, rows);
		vterm_set_size(mImpl->vt, mImpl->rows, mImpl->cols);
		vterm_screen_flush_damage(mImpl->screen);
	}

	int EditorTerminalScreen::cols() const { return mImpl->cols; }
	int EditorTerminalScreen::rows() const { return mImpl->rows; }

	TermCell EditorTerminalScreen::cell(int row, int col) const
	{
		if (row < 0 || row >= mImpl->rows || col < 0 || col >= mImpl->cols)
		{
			TermCell blank;
			blank.fg = mImpl->defaultFg;
			blank.bg = mImpl->defaultBg;
			return blank;
		}
		VTermPos pos;
		pos.row = row;
		pos.col = col;
		VTermScreenCell src;
		if (vterm_screen_get_cell(mImpl->screen, pos, &src))
		{
			return mImpl->convert(src);
		}
		TermCell blank;
		blank.fg = mImpl->defaultFg;
		blank.bg = mImpl->defaultBg;
		return blank;
	}

	TermCursor EditorTerminalScreen::cursor() const
	{
		VTermState* state = vterm_obtain_state(mImpl->vt);
		VTermPos pos;
		vterm_state_get_cursorpos(state, &pos);
		TermCursor out;
		out.row = pos.row;
		out.col = pos.col;
		out.visible = mImpl->cursorVisible;
		return out;
	}

	int EditorTerminalScreen::scrollbackCount() const
	{
		return static_cast<int>(mImpl->scrollback.size());
	}

	TermCell EditorTerminalScreen::scrollbackCell(int lineFromTop, int col) const
	{
		if (lineFromTop >= 0 &&
			lineFromTop < static_cast<int>(mImpl->scrollback.size()))
		{
			std::vector<TermCell> const& line = mImpl->scrollback[lineFromTop];
			if (col >= 0 && col < static_cast<int>(line.size()))
			{
				return line[col];
			}
		}
		TermCell blank;
		blank.fg = mImpl->defaultFg;
		blank.bg = mImpl->defaultBg;
		return blank;
	}

	bool EditorTerminalScreen::applicationCursorKeys() const
	{
		return mImpl->appCursorKeys;
	}

	bool EditorTerminalScreen::bracketedPaste() const
	{
		return mImpl->bracketedPaste;
	}

	bool EditorTerminalScreen::altScreen() const { return mImpl->altScreen; }
	bool EditorTerminalScreen::mouseTracking() const
	{
		return mImpl->mouseTracking;
	}

	std::string EditorTerminalScreen::dumpVisible() const
	{
		std::string out;
		for (int r = 0; r < mImpl->rows; ++r)
		{
			std::string line;
			for (int c = 0; c < mImpl->cols; ++c)
			{
				TermCell cellValue = this->cell(r, c);
				if (cellValue.width == 0)
				{
					continue;	// the trailing half of a wide glyph
				}
				line += cellValue.glyph.empty() ? " " : cellValue.glyph;
			}
			while (!line.empty() && line.back() == ' ')
			{
				line.pop_back();
			}
			out += line;
			if (r + 1 < mImpl->rows)
			{
				out.push_back('\n');
			}
		}
		return out;
	}
}
