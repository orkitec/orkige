// ExternalEditor.cpp - the pure open-at-line service (see ExternalEditor.h).
#include "ExternalEditor.h"

#include <cctype>
#include <cstddef>
#include <fstream>

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
