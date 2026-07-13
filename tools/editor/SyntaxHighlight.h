// SyntaxHighlight.h - the lightweight, per-line source highlighter behind the
// Inspector's read-only text-asset preview. Pure and headless (no ImGui/SDL)
// so the editor_core unit suite pins it down.
//
// The model is deliberately simple: one LINE of text plus a format family in,
// an ordered, gap-free run of coloured spans out (every byte of the line is
// covered exactly once). Three families cover the engine's text assets:
//   * Lua     - `--` comments, single/double-quoted strings, numbers, keywords
//   * IniLike - `[Section]` headers, the leading key token, `#`/`//` comments,
//               strings, numbers, `@key` localisation references
//   * Markup  - XML/JSON: `<tag>`/JSON keys, quoted strings, numbers,
//               `<!-- -->` and `//` comments
// Scanning is single-pass per line (no regex, no backtracking) so even a
// pathologically long line stays O(n). Multi-line constructs (Lua long
// comments, XML comments spanning lines) are treated per line - a preview, not
// a compiler.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#ifndef ORKIGE_SYNTAXHIGHLIGHT_H_14072026
#define ORKIGE_SYNTAXHIGHLIGHT_H_14072026

#include <cstddef>
#include <string>
#include <vector>

namespace Orkige
{
	//! the highlighting grammar family a preview uses
	enum class SyntaxFormat
	{
		PlainText,	//!< no highlighting - one Text span over the whole line
		Lua,		//!< `.lua`
		IniLike,	//!< `.oui`/`.ogui`/`.omat`/`.oshape`
		Markup,		//!< XML/JSON: `.oscene`/`.xlf`/`.json`/`.olayers`/...
	};

	//! the semantic class of one highlighted span (mapped to a muted colour by
	//! syntaxTokenColor when the Inspector draws it)
	enum class SyntaxToken
	{
		Text,		//!< default foreground (identifiers, punctuation, whitespace)
		Comment,
		String,
		Number,
		Keyword,	//!< a Lua keyword, or an IniLike line's leading key token
		Section,	//!< an `[Ini Section]`, an XML tag, a JSON key
		Reference,	//!< an `@key` localisation reference (IniLike)
	};

	//! one coloured run: the half-open byte range [begin, end) within the source
	//! line and the token class colouring it
	struct SyntaxSpan
	{
		std::size_t	begin = 0;
		std::size_t	end = 0;
		SyntaxToken	token = SyntaxToken::Text;
	};

	//! @brief highlight ONE line (no trailing newline expected). The returned
	//! spans are ordered, non-overlapping and gap-free: they cover exactly
	//! [0, line.size()), with Text spans filling anything between the classified
	//! tokens. An empty line yields no spans. Single pass, O(line length).
	std::vector<SyntaxSpan> highlightLine(std::string const& line,
		SyntaxFormat format);

	//! @brief the grammar family for a file extension (with or without the dot,
	//! any case). Unknown/plain kinds map to PlainText.
	SyntaxFormat syntaxFormatForExtension(std::string const& extension);

	//! @brief a muted RGBA colour (packed 0xRRGGBBAA) for a token class, tuned
	//! for the dark or light editor theme. Consistent, low-saturation tones so
	//! the preview reads as one with the surrounding UI.
	unsigned int syntaxTokenColor(SyntaxToken token, bool dark);
}

#endif // ORKIGE_SYNTAXHIGHLIGHT_H_14072026
