/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalScreen.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTerminalScreen_h__28_7_2026__12_00_00__
#define __EditorTerminalScreen_h__28_7_2026__12_00_00__

//! @file EditorTerminalScreen.h
//! @brief the terminal's VT screen model: bytes in -> a cell grid + cursor out.
//! This header speaks ONLY the editor's own cell/grid/cursor vocabulary; the
//! actual VT220/xterm parsing is libvterm, confined entirely to the .cpp (the
//! FontBakeImpl.cpp single-file-lib precedent). Keeping the VT core behind this
//! seam makes it swappable - if a maturing pure-VT library grows a stable API
//! and Windows support it can replace libvterm without touching a caller. The
//! model is engine-free and drivable headlessly (EditorTerminalScreenTests feed
//! scripted escape sequences and assert the grid).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace OrkigeEditor
{
	//! a resolved 8-bit-per-channel colour (indexed/default/truecolour already
	//! collapsed to RGB by the model)
	struct TermColor
	{
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;
	};

	//! the drawable attributes of one cell (the subset the panel renders)
	struct TermCellAttrs
	{
		bool bold = false;
		bool underline = false;
		bool italic = false;
		bool reverse = false;
		bool strike = false;
	};

	//! one screen cell: its glyph (UTF-8; empty = blank), resolved colours and
	//! attributes, and its display width (0 = the right half of a wide glyph,
	//! 1 = normal, 2 = a double-width glyph occupying this and the next column)
	struct TermCell
	{
		std::string		glyph;
		TermColor		fg;
		TermColor		bg;
		TermCellAttrs	attrs;
		int				width = 1;
	};

	//! the cursor position (0-based row/col in the visible grid) + visibility
	struct TermCursor
	{
		int		row = 0;
		int		col = 0;
		bool	visible = true;
	};

	//! The VT screen model. Feed it pty output with write(); read the resulting
	//! visible grid, cursor and scrollback for rendering. All libvterm state
	//! lives behind the pimpl.
	class EditorTerminalScreen
	{
	public:
		//! @param cols,rows the initial grid size (clamped to >= 1)
		//! @param scrollbackLimit max lines retained above the visible grid
		EditorTerminalScreen(int cols, int rows, int scrollbackLimit = 5000);
		~EditorTerminalScreen();

		EditorTerminalScreen(EditorTerminalScreen const&) = delete;
		EditorTerminalScreen& operator=(EditorTerminalScreen const&) = delete;

		//! feed raw bytes from the pty into the parser
		void write(char const* bytes, std::size_t len);
		void write(std::string const& bytes);

		//! The terminal REPLY channel. A conforming terminal answers the queries
		//! apps send it - Primary Device Attributes (ESC [ c), cursor-position /
		//! device-status reports (ESC [ 6n / ESC [ 5n) and the like - on the
		//! input channel. The VT core generates those replies itself; this seam
		//! hands the emitted bytes back so the panel can forward them to the
		//! pty's input (without it a shell like fish stalls waiting for a Primary
		//! DA answer and disables features). @p responder is invoked from within
		//! write() on the same thread. Pass an empty function to detach. The
		//! header stays VT-library-free: the responder is a plain byte sink.
		void setResponder(std::function<void(char const*, std::size_t)> responder);

		//! resize the grid to cols x rows. Scrollback is NOT reflowed (a v1
		//! limit): existing pushed-off lines keep their old width.
		void resize(int cols, int rows);

		int cols() const;
		int rows() const;

		//! read a visible cell (row in [0,rows), col in [0,cols)); out-of-range
		//! returns a blank cell in the default colours
		TermCell cell(int row, int col) const;

		//! the cursor in the visible grid
		TermCursor cursor() const;

		//! number of scrollback lines currently retained (above the top row)
		int scrollbackCount() const;

		//! read a scrollback cell. `lineFromTop` 0 is the OLDEST retained line,
		//! scrollbackCount()-1 the line just above the visible grid.
		TermCell scrollbackCell(int lineFromTop, int col) const;

		//! DECCKM: the app requested APPLICATION cursor keys (arrows send SS3
		//! ESC O A instead of CSI ESC [ A). Feeds the input encoder so vim/less
		//! get the arrow form they asked for.
		bool applicationCursorKeys() const;

		//! the app enabled BRACKETED PASTE (DEC mode 2004): a paste should be
		//! wrapped in ESC [ 200 ~ ... ESC [ 201 ~ so the app can tell pasted
		//! text from typed input.
		bool bracketedPaste() const;

		//! the alternate screen buffer is active (a full-screen app like vim)
		bool altScreen() const;

		//! the app requested mouse tracking (any DEC mouse mode). The panel does
		//! not forward mouse events in v1, but this is reported honestly.
		bool mouseTracking() const;

		//! a plain-text dump of the visible grid, rows joined by '\n' with
		//! trailing blanks trimmed - a test/debug convenience.
		std::string dumpVisible() const;

		//! the private state (libvterm handle + grid caches). Only the NAME is
		//! public so the file-local libvterm screen callbacks can reference it;
		//! the definition lives entirely in the .cpp.
		struct Impl;

	private:
		std::unique_ptr<Impl> mImpl;
	};
}

#endif //__EditorTerminalScreen_h__28_7_2026__12_00_00__
