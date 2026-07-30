/********************************************************************
	created:	Sunday 2026/07/26 at 10:00
	filename: 	TextWrap.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __TextWrap_h__26_7_2026__10_00_00__
#define __TextWrap_h__26_7_2026__10_00_00__

//! @file TextWrap.h
//! @brief the PURE greedy line-breaker shared by every text element (a
//! label's UiCaption, a textbox's UiMarkupText and the styled runs of inline
//! rich text - @see TextMarkup.h). The caller
//! turns its glyph/sprite/space stream into a flat list of WrapCells (plain
//! floats + flags, no font or renderer types) and this decides where the
//! lines break to fit a pixel width; the caller reads the per-cell line index
//! and line-relative pen back out and places its quads.
//! @remarks Break rules (the standard ones): a latin run breaks at spaces; a
//! CJK codepoint may break between any two glyphs (the caller flags each CJK
//! cell breakBefore); a single run wider than the width HARD-breaks at the
//! glyph that no longer fits (no overflow); an explicit '\n' (a forcedBreak
//! cell) always starts a new line; kerning that a cell carries as leadKern is
//! DROPPED when the cell begins a line, so it applies within a line and never
//! across a break. Being pure floats, the algorithm is unit-tested headlessly
//! against synthetic widths as well as through a real baked font.

#include "engine_module/EnginePrerequisites.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	class UiFont;
	class UiGlyph;

	//! @brief one placeable unit on a line (a glyph, an inline sprite, a space
	//! or a '\n'). Lengths are DEVICE pixels (already *Scaled by the caller).
	struct ORKIGE_ENGINE_DLL WrapCell
	{
		//! kerning added BEFORE this cell (the gap to the previous cell). Dropped
		//! when the cell begins a line, so kerning never leaks across a break.
		float	leadKern = 0.0f;
		//! the cell's own pen advance (a glyph's advance, a sprite's width, the
		//! space length). The next cell's pen = this cell's pen + leadKern + advance.
		float	advance = 0.0f;
		//! the cell's inked width (used for the right-edge overflow test). 0 for a
		//! space / '\n' (they never trigger a wrap).
		float	width = 0.0f;
		//! a whitespace cell: it never triggers a wrap and its width is excluded
		//! from a line's measured extent (trailing spaces collapse at a break).
		bool	space = false;
		//! a break opportunity exists BEFORE this cell (a CJK boundary). A break
		//! after a space is derived automatically (the first non-space wins).
		bool	breakBefore = false;
		//! a hard '\n': ends the current line and emits nothing.
		bool	forcedBreak = false;
		//! @brief byte offset of this cell's first byte in the source string.
		//! buildRun fills it; a caller that assembles cells itself (the markup
		//! walk) may leave it 0 - only the caret queries below read it.
		size_t	byteOffset = 0;
	};

	//! @brief the broken layout: a line index + line-relative left pen per input
	//! cell, plus each line's measured width and the line count.
	struct ORKIGE_ENGINE_DLL WrapResult
	{
		std::vector<int>	lineOf;		//!< line index of each input cell
		std::vector<float>	penX;		//!< line-relative left pen of each cell (px)
		std::vector<float>	lineWidth;	//!< measured width of each line (px)
		int					lineCount = 0;

		void clear()
		{
			this->lineOf.clear();
			this->penX.clear();
			this->lineWidth.clear();
			this->lineCount = 0;
		}
	};

	namespace TextWrap
	{
		//! @brief is a codepoint a CJK / Kana / Hangul glyph that may break
		//! between any two neighbours (the standard no-inter-word-space scripts)?
		ORKIGE_ENGINE_DLL bool isBreakableIdeograph(unsigned int codepoint);

		//! @brief greedily break @p cells to fit @p maxWidth pixels. @p maxWidth
		//! <= 0 disables width wrapping (only forcedBreak cells split lines). The
		//! result has one lineOf/penX entry per input cell; lineWidth/lineCount
		//! summarise the lines. @see the file header for the break rules.
		ORKIGE_ENGINE_DLL void wrap(std::vector<WrapCell> const & cells,
			float maxWidth, WrapResult & out);

		//! @brief build the wrap cells for a single-font UTF-8 run (a caption).
		//! Each glyph, space and '\n' becomes one cell (in @p cells); @p glyphs
		//! carries the glyph pointer per cell (NULL for a space/newline) so the
		//! caller emits quads for the inked ones. A glyph carries its leading
		//! kerning as leadKern (dropped at a line start) and CJK glyphs are
		//! flagged breakBefore. Metrics come through the font's *Scaled getters.
		//! @param textScale a per-element glyph SIZE multiplier applied to EVERY
		//! metric (advance, inked width, space, kerning, letter spacing), so a
		//! scaled caption wraps at the width its scaled glyphs actually occupy.
		//! 1 = the font's baked size (the metrics are then byte-identical to the
		//! unscaled call).
		ORKIGE_ENGINE_DLL void buildRun(UiFont const & font, String const & utf8,
			std::vector<WrapCell> & cells,
			std::vector<UiGlyph const *> & glyphs,
			float textScale = 1.0f);

		//! @brief where a caret sits in a wrapped run: the visual line it is on
		//! and the line-relative pen (px) it draws at.
		struct ORKIGE_ENGINE_DLL CaretSpot
		{
			int		line = 0;		//!< visual (wrapped) line index
			float	penX = 0.0f;	//!< line-relative pen of the caret (px)
		};

		//! @brief locate the caret at byte offset @p byteIndex in a wrapped run.
		//! The caret sits BEFORE the first cell whose byteOffset reaches it (so a
		//! caret at the end of the text lands after the last placed cell, and a
		//! caret right after a '\n' opens the next line). Cells must carry
		//! byteOffset (@see buildRun) and @p wrapped must be that run's wrap.
		ORKIGE_ENGINE_DLL CaretSpot locateCaret(std::vector<WrapCell> const & cells,
			WrapResult const & wrapped, size_t byteIndex);

		//! @brief the byte offset each VISUAL line of a wrapped run starts at
		//! (size == wrapped.lineCount; entry 0 is always 0). A line opened by a
		//! '\n' starts just after it. Lets a viewer slice the source text at soft
		//! line boundaries - the greedy break of a suffix that starts on a line
		//! boundary reproduces the same following breaks, so a sliced view wraps
		//! exactly like the full text does.
		ORKIGE_ENGINE_DLL void lineStartBytes(std::vector<WrapCell> const & cells,
			WrapResult const & wrapped, size_t textLength,
			std::vector<size_t> & out);
	}
}

#endif //__TextWrap_h__26_7_2026__10_00_00__
