/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorTextDiagnostics.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTextDiagnostics_h__24_7_2026__12_00_00__
#define __EditorTextDiagnostics_h__24_7_2026__12_00_00__

#include <string>

namespace OrkigeEditor
{
	//! @brief one live parse diagnostic for an open text document: the first
	//! problem the format's own parser reports, anchored to a 1-based line
	//! (0 = the parser gave no line - show the message document-wide).
	struct TextDiagnostic
	{
		bool valid = true;			//!< false = the text does not parse
		int line = 0;				//!< 1-based problem line (0 = unknown)
		std::string message;		//!< the parser's own words
	};

	//! @brief parse `text` as XML (the engine's XMLArchive carriers -
	//! .oscene/.oprefab/.orkproj/.orkmeta/.olevels/.oactions/.olayers - plus
	//! the XLIFF .xlf/bare .xml) and report the FIRST syntax problem, or
	//! valid. Pure - the tinyxml2 probe behind the editor's live squiggles
	//! for XML documents. tinyxml2 does not validate a declaration's
	//! CONTENT, only its `<?...?>` shape, so this reads the house
	//! XMLArchive's non-standard `<?1.0, UTF-8, yes?>` prolog exactly like a
	//! normal `<?xml version="1.0"...?>` one - no special-casing needed.
	TextDiagnostic xmlDiagnostic(std::string const& text);

	//! @brief parse `text` as a `.omat` PBS material asset (@see
	//! core_util/MaterialAsset.h) and report the first problem, or valid.
	//! Wraps that pure parser, whose own error already reads "line N: ...".
	TextDiagnostic omatDiagnostic(std::string const& text);

	//! @brief parse `text` as a `.oui` declarative UI layout (@see
	//! engine_gui/GuiLayout.h) and report the first problem, or valid. Wraps
	//! the SAME pure parser `GuiFactory::loadLayout` runs at runtime; its
	//! own error already reads "... on line N".
	TextDiagnostic ouiDiagnostic(std::string const& text);

	//! @brief extract the 1-based line a Lua load error names for `chunkName`
	//! (errors read "chunkName:line: message"); 0 when no line is legible.
	//! Pure string work - the compile itself happens behind ScriptRuntime.
	int luaErrorLine(std::string const& error, std::string const& chunkName);

	//! @brief the broad text-document family a house file extension names -
	//! the embedded editor's highlighting seam (@see
	//! EditorScriptPanel.cpp::languageForFile). Source-code kinds the editor
	//! maps directly (Lua/C/C++/GLSL/HLSL/Markdown, and a few
	//! deliberately-plain extensions) are NOT part of this table and read as
	//! PlainText here - the caller checks those first and only consults this
	//! classifier for the house text formats plus the sniff fallback below.
	enum class TextDocumentKind
	{
		PlainText,		//!< no recognizable house format (or a deliberately
						//!< plain one, e.g. .txt)
		Xml,			//!< the engine's XMLArchive carriers + XLIFF + a bare
						//!< .xml (@see xmlDiagnostic)
		Json,			//!< .json/.jsonl (a `.oanim`'s kept Lottie source, or
						//!< a bare JSON file)
		OrkigeConfig,	//!< .oui/.ogui/.omat/.oshape (line-based key/value or
						//!< directive text - no live-check distinction here,
						//!< @see liveCheckKindForExtension for that)
	};

	//! @brief the document kind for a file extension (with or without the
	//! leading dot, any case). Every house XMLArchive/XLIFF extension maps
	//! to Xml, the config-text family to OrkigeConfig, `.json`/`.jsonl` to
	//! Json; anything else (including an extension this table has simply
	//! never heard of) is PlainText.
	TextDocumentKind textDocumentKindForExtension(std::string const& extension);

	//! @brief a content sniff for a document whose EXTENSION named no known
	//! kind (an unrecognized/absent extension only - an explicit extension
	//! map always wins over sniffing, and never returns OrkigeConfig: there
	//! is no unambiguous content signature for that family, it is
	//! extension-only). Tolerant of a leading UTF-8 BOM and leading blank
	//! lines; looks only at the first non-blank content: a `<?xml`/`<?1.0`
	//! prolog or a bare `<` root tag reads as Xml (both parse fine through
	//! tinyxml2 - @see xmlDiagnostic), a leading `{`/`[` reads as Json,
	//! anything else is PlainText. Pure string inspection - no parse is
	//! attempted here.
	TextDocumentKind sniffTextDocumentKind(std::string const& text);

	//! @brief which live parser (if any) checks a file extension's CURRENT
	//! buffer while it sits idle in the embedded editor (@see
	//! EditorScriptPanel.cpp's ScriptDocument::liveCheck). Xml/Omat/Oui wrap
	//! this module's xmlDiagnostic/omatDiagnostic/ouiDiagnostic; Lua compiles
	//! through ScriptRuntime::checkSyntax instead (a runtime seam, not a
	//! pure one - the caller still owns that branch). `.ogui` (its parser is
	//! Ogre::ConfigFile-bound, not a cheap pure seam), `.oshape`
	//! (VectorShapeAsset::parse reports pass/fail with no line/message) and
	//! `.json`/`.jsonl` (JsonValue::parse likewise reports no line/message)
	//! stay None HONESTLY - highlighting only, no invented diagnostic.
	enum class LiveCheckKind { None, Lua, Xml, Omat, Oui };

	//! @brief the live-check kind for a file extension (with or without the
	//! leading dot, any case).
	LiveCheckKind liveCheckKindForExtension(std::string const& extension);
}

#endif //__EditorTextDiagnostics_h__24_7_2026__12_00_00__
