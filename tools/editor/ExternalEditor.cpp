/********************************************************************
	created:	Monday 2026/07/13 at 12:00
	filename: 	ExternalEditor.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// ExternalEditor.cpp - the pure open-at-line service (see ExternalEditor.h).
#include "ExternalEditor.h"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <functional>	// resolveTerminalPath's injected exists probe
#include <string>
#include <vector>

namespace Orkige
{
namespace
{

//! a path token character: anything that is neither whitespace nor a colon (a
//! colon separates path from line/column, so it terminates the path)
bool isPathChar(char c)
{
	const unsigned char uc = static_cast<unsigned char>(c);
	return !std::isspace(uc) && c != ':';
}

//! a path-looking token carries a '.' (an extension) or a '/' (a directory
//! separator); this rejects bare numeric runs like a "12:30" timestamp
bool looksLikePath(std::string const& text, std::size_t begin, std::size_t end)
{
	for (std::size_t i = begin; i < end; ++i)
	{
		if (text[i] == '.' || text[i] == '/' || text[i] == '\\')
		{
			return true;
		}
	}
	return false;
}

//! parse a run of ASCII digits starting at `pos`; advances `pos` past them and
//! returns the value (0 when no digit is present)
int parseDigits(std::string const& text, std::size_t& pos)
{
	int value = 0;
	bool any = false;
	while (pos < text.size() &&
		std::isdigit(static_cast<unsigned char>(text[pos])))
	{
		value = value * 10 + (text[pos] - '0');
		++pos;
		any = true;
	}
	return any ? value : 0;
}

//! replace every occurrence of `what` in `s` with `with`
void replaceAll(std::string& s, std::string const& what, std::string const& with)
{
	if (what.empty())
	{
		return;
	}
	std::string::size_type at = 0;
	while ((at = s.find(what, at)) != std::string::npos)
	{
		s.replace(at, what.size(), with);
		at += with.size();
	}
}

} // namespace

std::vector<FileLineRef> parseFileLineRefs(std::string const& text)
{
	std::vector<FileLineRef> refs;
	const std::size_t n = text.size();
	std::size_t i = 0;
	while (i < n)
	{
		// a candidate reference is a ':' immediately followed by a digit, with a
		// path-looking run directly before it
		if (text[i] == ':' && i + 1 < n &&
			std::isdigit(static_cast<unsigned char>(text[i + 1])))
		{
			std::size_t pathBegin = i;
			while (pathBegin > 0 && isPathChar(text[pathBegin - 1]))
			{
				--pathBegin;
			}
			if (pathBegin < i && looksLikePath(text, pathBegin, i))
			{
				FileLineRef ref;
				ref.path = text.substr(pathBegin, i - pathBegin);
				ref.begin = pathBegin;
				std::size_t pos = i + 1;
				ref.line = parseDigits(text, pos);
				// an optional ":col" right after the line
				if (pos < n && text[pos] == ':' && pos + 1 < n &&
					std::isdigit(static_cast<unsigned char>(text[pos + 1])))
				{
					std::size_t colPos = pos + 1;
					ref.column = parseDigits(text, colPos);
					pos = colPos;
				}
				ref.end = pos;
				refs.push_back(ref);
				i = pos;
				continue;
			}
		}
		++i;
	}
	return refs;
}

std::vector<std::string> expandEditorCommand(std::string const& templ,
	std::string const& file, int line)
{
	// split on ASCII whitespace first so a {file} carrying a path WITH spaces
	// stays a single argv element
	std::vector<std::string> argv;
	std::size_t i = 0;
	const std::size_t n = templ.size();
	while (i < n)
	{
		while (i < n && std::isspace(static_cast<unsigned char>(templ[i])))
		{
			++i;
		}
		std::size_t begin = i;
		while (i < n && !std::isspace(static_cast<unsigned char>(templ[i])))
		{
			++i;
		}
		if (i <= begin)
		{
			continue;
		}
		std::string token = templ.substr(begin, i - begin);
		if (line > 0)
		{
			replaceAll(token, "{line}", std::to_string(line));
		}
		else
		{
			// no target line: drop a ":{line}" suffix and any lone {line} so the
			// argument is not left with a dangling colon
			replaceAll(token, ":{line}", "");
			replaceAll(token, "{line}", "");
		}
		replaceAll(token, "{file}", file);
		if (!token.empty())
		{
			argv.push_back(token);
		}
	}
	return argv;
}

std::vector<std::string> const& externalEditorCandidates()
{
	// leading token = the executable probed on PATH; the rest is that tool's
	// documented go-to-line invocation. Configuration data, not an endorsement.
	static const std::vector<std::string> candidates = {
		"code -g {file}:{line}",
		"subl {file}:{line}",
		"zed {file}:{line}",
	};
	return candidates;
}

EditorCommandResolution resolveEditorCommand(
	std::string const& configuredTemplate, std::string const& file, int line,
	EditorPathProbe const& probe, bool macOS)
{
	EditorCommandResolution result;
	// 1. an explicit user setting wins (it is the user's declared preference)
	if (!configuredTemplate.empty())
	{
		result.argv = expandEditorCommand(configuredTemplate, file, line);
		if (!result.argv.empty())
		{
			result.opensAtLine =
				configuredTemplate.find("{line}") != std::string::npos;
			result.source = "setting";
			return result;
		}
	}
	// 2. the first autodetected CLI editor present on PATH
	if (probe)
	{
		for (std::string const& candidate : externalEditorCandidates())
		{
			// the executable is the candidate's leading whitespace-delimited token
			std::size_t space = candidate.find(' ');
			std::string exe = candidate.substr(0, space);
			if (probe(exe))
			{
				result.argv = expandEditorCommand(candidate, file, line);
				result.opensAtLine = true;
				result.source = "detect:" + exe;
				return result;
			}
		}
	}
	// 3. the platform file opener (no line jump)
	result.argv = { macOS ? "open" : "xdg-open", file };
	result.opensAtLine = false;
	result.source = "opener";
	return result;
}

namespace
{

//! a terminal path-token character: alphanumerics plus the set a path can carry
//! (dir separators, dots, tilde, colon for :line + drive letters, and the few
//! other chars filenames use). Everything else - whitespace, quotes, brackets,
//! parens, pipes, '=' , '*' , '?' - is a token boundary, so a quoted or
//! parenthesised path yields just the path and adjacent shell syntax is excluded.
bool isTerminalPathChar(char c)
{
	const unsigned char uc = static_cast<unsigned char>(c);
	if (std::isalnum(uc) != 0)
	{
		return true;
	}
	switch (c)
	{
	case '.': case '/': case '\\': case '_': case '-': case '~':
	case ':': case '@': case '+': case '%':
		return true;
	default:
		return false;
	}
}

} // namespace

bool terminalPathTokenAt(std::string const& lineText, std::size_t col,
	TerminalPathToken& out)
{
	out = TerminalPathToken();
	const std::size_t n = lineText.size();
	if (col >= n || !isTerminalPathChar(lineText[col]))
	{
		return false;	// hovering whitespace / a boundary - no token
	}
	// expand to the surrounding maximal run of path-token characters
	std::size_t begin = col;
	while (begin > 0 && isTerminalPathChar(lineText[begin - 1]))
	{
		--begin;
	}
	std::size_t end = col + 1;
	while (end < n && isTerminalPathChar(lineText[end]))
	{
		++end;
	}
	std::string raw = lineText.substr(begin, end - begin);

	// split a trailing ":line[:col]" using the shared reference parser; it also
	// gates that the path portion looks like a path (carries '.' or '/')
	std::string path;
	int line = 0;
	int column = 0;
	std::size_t tokenLen = 0;
	std::vector<FileLineRef> refs = parseFileLineRefs(raw);
	if (!refs.empty() && refs.front().begin == 0)
	{
		FileLineRef const& ref = refs.front();
		path = ref.path;
		line = ref.line;
		column = ref.column;
		tokenLen = ref.end;	// path + ":line[:col]" span within raw
	}
	else
	{
		// no line suffix: the whole run is the candidate path, minus trailing
		// sentence punctuation (a period ending a sentence, a comma, a colon)
		std::size_t last = raw.size();
		while (last > 0 && (raw[last - 1] == '.' || raw[last - 1] == ',' ||
			raw[last - 1] == ';' || raw[last - 1] == ':'))
		{
			--last;
		}
		path = raw.substr(0, last);
		tokenLen = last;
		// reject a bare word (no separator/extension) - "non-path words rejected"
		if (path.empty() || !looksLikePath(path, 0, path.size()))
		{
			return false;
		}
	}
	if (path.empty())
	{
		return false;
	}
	out.path = path;
	out.line = line;
	out.column = column;
	out.begin = begin;
	out.end = begin + tokenLen;
	return true;
}

std::string resolveTerminalPath(std::string const& tokenPath,
	std::string const& projectRoot, std::string const& sessionCwd,
	std::string const& homeDir,
	std::function<bool(std::string const&)> const& exists)
{
	if (tokenPath.empty() || !exists)
	{
		return std::string();
	}
	std::string p = tokenPath;
	// tilde expansion: "~" -> home, "~/x" -> home + "/x"
	if (!homeDir.empty() && (p == "~" || (p.size() > 1 && p[0] == '~' &&
		(p[1] == '/' || p[1] == '\\'))))
	{
		p = homeDir + p.substr(1);
	}
	// an absolute path (or a Windows drive path) resolves only against itself
	if (p.front() == '/' || (p.size() > 1 && p[1] == ':'))
	{
		return exists(p) ? p : std::string();
	}
	auto join = [](std::string const& base, std::string const& rel) -> std::string
	{
		if (base.empty())
		{
			return rel;
		}
		const char back = base.back();
		return (back == '/' || back == '\\') ? base + rel : base + "/" + rel;
	};
	// project root first, then the session's working directory
	if (!projectRoot.empty())
	{
		const std::string candidate = join(projectRoot, p);
		if (exists(candidate))
		{
			return candidate;
		}
	}
	if (!sessionCwd.empty() && sessionCwd != projectRoot)
	{
		const std::string candidate = join(sessionCwd, p);
		if (exists(candidate))
		{
			return candidate;
		}
	}
	return std::string();
}

std::vector<std::string> readFileLinesAround(std::string const& path,
	int targetLine, int context, int& outFirstLine)
{
	outFirstLine = 0;
	std::vector<std::string> window;
	std::ifstream file(path);
	if (!file)
	{
		return window;
	}
	const int first = targetLine - context > 1 ? targetLine - context : 1;
	const int last = targetLine + context;
	int lineNo = 0;
	std::string line;
	while (std::getline(file, line))
	{
		++lineNo;
		if (lineNo > last)
		{
			break;
		}
		if (lineNo >= first)
		{
			// strip a trailing CR so a CRLF file does not show a stray glyph
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (window.empty())
			{
				outFirstLine = lineNo;
			}
			window.push_back(line);
		}
	}
	return window;
}

} // namespace Orkige
