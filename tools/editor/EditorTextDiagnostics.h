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

	//! @brief parse `text` as XML (the engine's scene/prefab/project/xliff
	//! carrier) and report the FIRST syntax problem, or valid. Pure - the
	//! tinyxml2 probe behind the editor's live squiggles for XML documents.
	TextDiagnostic xmlDiagnostic(std::string const& text);

	//! @brief extract the 1-based line a Lua load error names for `chunkName`
	//! (errors read "chunkName:line: message"); 0 when no line is legible.
	//! Pure string work - the compile itself happens behind ScriptRuntime.
	int luaErrorLine(std::string const& error, std::string const& chunkName);
}

#endif //__EditorTextDiagnostics_h__24_7_2026__12_00_00__
