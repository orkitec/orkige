/********************************************************************
	created:	Thursday 2026/07/30 at 09:00
	filename: 	TextMarkup.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __TextMarkup_h__30_7_2026__09_00_00__
#define __TextMarkup_h__30_7_2026__09_00_00__

//! @file TextMarkup.h
//! @brief inline RICH TEXT: the ONE markup grammar the gui's styled-run text
//! speaks, as a PURE parser (tag stream -> runs; no font, no atlas, no
//! renderer) plus the font-aware cell builder that feeds those runs through the
//! shared line-breaker. Both halves are headless-unit-testable, which is why
//! every malformed-input verdict below is a test rather than a hope.
//!
//! GRAMMAR (small, escapable, and the same for a label, a textbox and world
//! text - one dialect, no per-widget dialects):
//!   [c=RRGGBB] ... [/c]     a COLOUR span (hex, 8 digits = RRGGBBAA)
//!   [f=NAME] ... [/f]       a FONT span; NAME is the role name an atlas font
//!                           declares (`heading`) or a `[Font.N]` index (`24`)
//!   [sprite=NAME]           an inline atlas sprite (self-closing, one cell)
//!   [[                      a literal '[' (the only escape the grammar needs)
//! Spans nest per attribute (an inner `[c=..]` restores the outer colour at its
//! `[/c]`), and a span that is still open at the end of the text closes there.
//!
//! VERDICTS - warn and stay readable, never crash and never eat the text:
//!   * an unknown tag (`[b]`, `[c]`, `[colour=red]`) is emitted VERBATIM as
//!     text, so the author sees exactly what they typed,
//!   * a malformed colour (`[c=nothex]`) is verbatim text too,
//!   * a `[` with no closing `]` is verbatim text to the end of the string,
//!   * a stray `[/c]` / `[/f]` with nothing open is dropped,
//!   * an unknown font or sprite NAME is resolved by the CALLER (it owns the
//!     atlas): the run then draws in the default font / the sprite is dropped,
//!     and the text around it still reads.
//! Each of those appends one human-readable line to Parse::diagnostics; the
//! element logs them once per rebuild (a dirty rebuild, never per frame).
//!
//! MEASUREMENT flows through the ONE core: buildCells runs
//! TextWrap::buildRun per text run (so kerning, CJK break opportunities, the
//! per-element text scale and the space metric are the same code a plain
//! caption uses) and adds one atomic fixed-advance cell per inline sprite.
//! TextWrap::wrap then breaks the whole stream, so a line may break inside a
//! run, between runs, or before a sprite that no longer fits - and every cell
//! keeps its own colour/glyph across that break.

#include "engine_module/EnginePrerequisites.h"
#include "engine_gui/TextWrap.h"

#include <vector>

namespace Orkige
{
	class UiFont;
	class UiGlyph;
	class UiSprite;

	namespace TextMarkup
	{
		//! @brief one parsed run: a stretch of text carrying the colour/font that
		//! were open at that point, or a single inline sprite
		struct ORKIGE_ENGINE_DLL Run
		{
			enum Kind
			{
				RK_Text = 0,	//!< draw `text` in `colour` / `fontRef`
				RK_Sprite		//!< draw the atlas sprite named `sprite`
			};

			Kind	kind = RK_Text;
			String	text;			//!< RK_Text: the literal text (escapes resolved)
			String	sprite;			//!< RK_Sprite: the atlas sprite name
			//! an explicit `[c=..]` colour was open here (else the element's own)
			bool	hasColour = false;
			float	colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			//! an explicit `[f=..]` font reference was open here ("" = the
			//! element's default font). A NAME or a decimal index - the caller
			//! resolves it through the atlas (@see UiAtlas::resolveFontRef).
			String	fontRef;
		};

		//! @brief the parse result: the runs in document order plus what the
		//! parser had to complain about
		struct ORKIGE_ENGINE_DLL Parse
		{
			std::vector<Run>	runs;
			std::vector<String>	diagnostics;
			//! @brief did the text carry any RECOGNISED tag? False for plain text
			//! (which yields exactly one run), so a caller can assert that markup
			//! mode leaves plain strings alone.
			bool				sawMarkup = false;

			void clear()
			{
				this->runs.clear();
				this->diagnostics.clear();
				this->sawMarkup = false;
			}
		};

		//! @brief parse @p text into styled runs. Always succeeds: malformed input
		//! degrades to verbatim text plus a diagnostic (@see the file header).
		//! Adjacent text between tags collapses into one run; an empty run is
		//! never emitted.
		ORKIGE_ENGINE_DLL void parse(String const & text, Parse & out);

		//! @brief a run whose atlas references the caller already resolved: a text
		//! run carries its font, a sprite run its sprite
		struct ORKIGE_ENGINE_DLL ResolvedRun
		{
			UiFont const *		font = NULL;	//!< the text run's font
			UiSprite const *	sprite = NULL;	//!< non-NULL = an inline sprite
			String				text;			//!< the text run's text
			Color				colour = Color(1.0f, 1.0f, 1.0f, 1.0f);
		};

		//! @brief what one wrap cell draws: a glyph, an inline sprite, or nothing
		//! (a space / newline cell), in the colour of the run it came from
		struct ORKIGE_ENGINE_DLL CellAttr
		{
			UiGlyph const *		glyph = NULL;
			UiSprite const *	sprite = NULL;
			Color				colour = Color(1.0f, 1.0f, 1.0f, 1.0f);
			//! @brief how far DOWN this cell sits inside the line (px): the
			//! block's line height minus this run's own. Glyphs draw top-aligned
			//! at the pen, so a small run beside a heading run would otherwise
			//! float at the tall run's cap height; dropping each run by the
			//! difference sits them on a common BOTTOM edge - one line of mixed
			//! sizes then reads as one line, and an inline icon sits with the
			//! text instead of above it. 0 for every cell of a single-size
			//! element, which is why such an element draws byte-identically.
			float				dropY = 0.0f;
		};

		//! @brief build the wrap cells of a whole run list: one cell per glyph /
		//! space / newline (through TextWrap::buildRun, so the metrics are the
		//! shared ones) and one atomic cell per inline sprite. @p cells and
		//! @p attrs come out the same length and index-aligned.
		//! @param textScale the per-element glyph SIZE multiplier - every metric
		//! is multiplied by it, an inline sprite included, so a scaled rich-text
		//! element measures and draws at one size.
		//! @param lineHeight IN: the element's own line height (its default font,
		//! already scaled); OUT: raised to fit the tallest font and sprite used,
		//! so a heading run or a tall icon does not overlap the next line.
		//! @remarks A style boundary breaks the kerning pair: the first glyph of a
		//! run gets the font's letter spacing rather than a kern against the last
		//! glyph of the previous run.
		ORKIGE_ENGINE_DLL void buildCells(std::vector<ResolvedRun> const & runs,
			float textScale, std::vector<WrapCell> & cells,
			std::vector<CellAttr> & attrs, float & lineHeight);
	}
}

#endif //__TextMarkup_h__30_7_2026__09_00_00__
