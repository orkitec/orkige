/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorTextDiagnostics.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorTextDiagnostics.h"

#include <core_util/MaterialAsset.h>
#include <core_util/MeshAsset.h>
#include <core_util/SfxAsset.h>
#include <engine_gui/GuiLayout.h>

#include <tinyxml2.h>

#include <cctype>
#include <cstdlib>

namespace OrkigeEditor
{
	namespace
	{
		//! is `text` empty/whitespace-only - not an error to shout about
		//! while someone is still typing a brand new document
		bool isBlank(std::string const& text)
		{
			for (const char c : text)
			{
				if (std::isspace(static_cast<unsigned char>(c)) == 0)
				{
					return false;
				}
			}
			return true;
		}

		//! the first run of digits right after `marker` in `text` (0 if
		//! `marker` is absent or not followed by digits) - every house
		//! parser's error already names its line, differing only in the
		//! marker text that precedes the number ("line N: ..." for
		//! MaterialAsset, "... on line N" for GuiLayoutDoc)
		int extractLineAfter(std::string const& text, std::string const& marker)
		{
			const std::size_t pos = text.find(marker);
			if (pos == std::string::npos)
			{
				return 0;
			}
			const char* digits = text.c_str() + pos + marker.size();
			char* end = nullptr;
			const long line = std::strtol(digits, &end, 10);
			return (end != digits && end != nullptr && line > 0)
				? static_cast<int>(line) : 0;
		}

		//! a lower-case, dot-prefixed extension ("Lua" or ".LUA" -> ".lua")
		std::string normalizeExtension(std::string const& extension)
		{
			std::string ext = extension;
			if (!ext.empty() && ext.front() != '.')
			{
				ext.insert(ext.begin(), '.');
			}
			for (char& c : ext)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return ext;
		}
	}

	TextDiagnostic xmlDiagnostic(std::string const& text)
	{
		TextDiagnostic diagnostic;
		if (isBlank(text))
		{
			return diagnostic;
		}
		tinyxml2::XMLDocument document;
		if (document.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS)
		{
			diagnostic.valid = false;
			diagnostic.line = document.ErrorLineNum();	// tinyxml2 is 1-based
			diagnostic.message =
				document.ErrorStr() != nullptr ? document.ErrorStr() : "";
		}
		return diagnostic;
	}

	TextDiagnostic omatDiagnostic(std::string const& text)
	{
		TextDiagnostic diagnostic;
		if (isBlank(text))
		{
			return diagnostic;
		}
		Orkige::MaterialAsset::ParsedMaterial parsed;
		Orkige::String error;
		if (!Orkige::MaterialAsset::parse(text, parsed, &error))
		{
			diagnostic.valid = false;
			diagnostic.message = error;
			diagnostic.line = extractLineAfter(error, "line ");
		}
		return diagnostic;
	}

	TextDiagnostic omeshDiagnostic(std::string const& text)
	{
		TextDiagnostic diagnostic;
		if (isBlank(text))
		{
			return diagnostic;
		}
		// checkSyntax builds the geometry with a placeholder outline standing in
		// for `extrude`/`revolve` references, so a typo in a directive, key or
		// value is reported here while a missing `.oshape` file is not (the
		// editor cannot resolve project assets from this pure seam)
		Orkige::String error;
		if (!Orkige::MeshAsset::checkSyntax(text, &error))
		{
			diagnostic.valid = false;
			diagnostic.message = error;
			diagnostic.line = extractLineAfter(error, "line ");
		}
		return diagnostic;
	}

	TextDiagnostic ouiDiagnostic(std::string const& text)
	{
		TextDiagnostic diagnostic;
		if (isBlank(text))
		{
			return diagnostic;
		}
		Orkige::GuiLayoutDoc document;
		Orkige::String error;
		if (!Orkige::GuiLayoutDoc::parse(text, document, error))
		{
			diagnostic.valid = false;
			diagnostic.message = error;
			diagnostic.line = extractLineAfter(error, "on line ");
		}
		return diagnostic;
	}

	TextDiagnostic osfxDiagnostic(std::string const& text)
	{
		TextDiagnostic diagnostic;
		if (isBlank(text))
		{
			return diagnostic;
		}
		Orkige::SfxDesc parsed;
		Orkige::String error;
		if (!Orkige::SfxAsset::parse(text, parsed, &error))
		{
			diagnostic.valid = false;
			diagnostic.message = error;
			diagnostic.line = extractLineAfter(error, "line ");
		}
		return diagnostic;
	}

	TextDocumentKind textDocumentKindForExtension(std::string const& extension)
	{
		const std::string ext = normalizeExtension(extension);
		// the house XMLArchive carriers - .oscene/.oprefab/.orkproj/
		// .orkmeta/.olevels/.oactions/.olayers all serialize through the
		// same XMLArchive/tinyxml2 pair - plus XLIFF (.xlf) and a bare .xml
		if (ext == ".oscene" || ext == ".oprefab" || ext == ".orkproj" ||
			ext == ".orkmeta" || ext == ".olevels" || ext == ".oactions" ||
			ext == ".olayers" || ext == ".xlf" || ext == ".xml")
		{
			return TextDocumentKind::Xml;
		}
		if (ext == ".json" || ext == ".jsonl")
		{
			return TextDocumentKind::Json;
		}
		if (ext == ".oui" || ext == ".ogui" || ext == ".omat" ||
			ext == ".oshape" || ext == ".omesh" || ext == ".osfx")
		{
			return TextDocumentKind::OrkigeConfig;
		}
		return TextDocumentKind::PlainText;
	}

	TextDocumentKind sniffTextDocumentKind(std::string const& text)
	{
		std::size_t i = 0;
		const std::size_t n = text.size();
		// tolerate a leading UTF-8 BOM
		if (n >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
			static_cast<unsigned char>(text[1]) == 0xBB &&
			static_cast<unsigned char>(text[2]) == 0xBF)
		{
			i = 3;
		}
		// skip leading whitespace/blank lines
		while (i < n && std::isspace(static_cast<unsigned char>(text[i])) != 0)
		{
			++i;
		}
		if (i >= n)
		{
			return TextDocumentKind::PlainText;
		}
		if (text[i] == '<')
		{
			// both a standard `<?xml ...?>` prolog and the house XMLArchive's
			// `<?1.0, UTF-8, yes?>` one, as well as a bare root tag with no
			// prolog at all, are XML - tinyxml2 parses all three
			return TextDocumentKind::Xml;
		}
		if (text[i] == '{' || text[i] == '[')
		{
			return TextDocumentKind::Json;
		}
		return TextDocumentKind::PlainText;
	}

	LiveCheckKind liveCheckKindForExtension(std::string const& extension)
	{
		const std::string ext = normalizeExtension(extension);
		if (ext == ".lua")
		{
			return LiveCheckKind::Lua;
		}
		if (textDocumentKindForExtension(ext) == TextDocumentKind::Xml)
		{
			return LiveCheckKind::Xml;
		}
		if (ext == ".omat")
		{
			return LiveCheckKind::Omat;
		}
		if (ext == ".omesh")
		{
			return LiveCheckKind::Omesh;
		}
		if (ext == ".oui")
		{
			return LiveCheckKind::Oui;
		}
		if (ext == ".osfx")
		{
			return LiveCheckKind::Osfx;
		}
		return LiveCheckKind::None;
	}

	int luaErrorLine(std::string const& error, std::string const& chunkName)
	{
		// the loader prefixes errors with the chunk name: "name:line: message".
		// The name may appear more than once (wrapped in loader decoration) -
		// take the first occurrence that is IMMEDIATELY followed by ":<int>:"
		if (chunkName.empty())
		{
			return 0;
		}
		std::size_t name = error.find(chunkName);
		while (name != std::string::npos)
		{
			const std::size_t colon = name + chunkName.size();
			if (colon < error.size() && error[colon] == ':')
			{
				const char* digits = error.c_str() + colon + 1;
				char* end = nullptr;
				const long line = std::strtol(digits, &end, 10);
				if (end != digits && end != nullptr && *end == ':' && line > 0)
				{
					return static_cast<int>(line);
				}
			}
			name = error.find(chunkName, name + 1);
		}
		return 0;
	}
}
