// SyntaxHighlight.cpp - the pure per-line highlighter (see SyntaxHighlight.h).
#include "SyntaxHighlight.h"

#include <array>
#include <cctype>

namespace Orkige
{
namespace
{

bool isDigitCh(char c)
{
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isHexCh(char c)
{
	return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

bool isIdentStart(char c)
{
	const unsigned char uc = static_cast<unsigned char>(c);
	return std::isalpha(uc) != 0 || c == '_';
}

bool isIdentCh(char c)
{
	const unsigned char uc = static_cast<unsigned char>(c);
	return std::isalnum(uc) != 0 || c == '_';
}

bool isSpaceCh(char c)
{
	return std::isspace(static_cast<unsigned char>(c)) != 0;
}

//! the token immediately before `i` is a boundary (so "abc123" is not a number)
bool numberBoundary(std::string const& s, std::size_t i)
{
	return i == 0 || !isIdentCh(s[i - 1]);
}

//! scan a numeric literal starting at `i` (decimal, `0x` hex, optional
//! exponent); returns the one-past-the-end index
std::size_t scanNumber(std::string const& s, std::size_t i)
{
	const std::size_t n = s.size();
	std::size_t j = i;
	if (s[j] == '0' && j + 1 < n && (s[j + 1] == 'x' || s[j + 1] == 'X'))
	{
		j += 2;
		while (j < n && isHexCh(s[j]))
		{
			++j;
		}
		return j;
	}
	while (j < n && (isDigitCh(s[j]) || s[j] == '.'))
	{
		++j;
	}
	if (j < n && (s[j] == 'e' || s[j] == 'E'))
	{
		std::size_t k = j + 1;
		if (k < n && (s[k] == '+' || s[k] == '-'))
		{
			++k;
		}
		if (k < n && isDigitCh(s[k]))
		{
			j = k;
			while (j < n && isDigitCh(s[j]))
			{
				++j;
			}
		}
	}
	return j;
}

//! scan a quoted string starting at the opening quote `i`; honours a `\`
//! escape; an unterminated string runs to the end of the line
std::size_t scanString(std::string const& s, std::size_t i)
{
	const std::size_t n = s.size();
	const char quote = s[i];
	std::size_t j = i + 1;
	while (j < n)
	{
		if (s[j] == '\\' && j + 1 < n)
		{
			j += 2;
			continue;
		}
		if (s[j] == quote)
		{
			return j + 1;
		}
		++j;
	}
	return n;
}

//! scan an identifier run starting at `i`
std::size_t scanIdent(std::string const& s, std::size_t i)
{
	const std::size_t n = s.size();
	std::size_t j = i;
	while (j < n && isIdentCh(s[j]))
	{
		++j;
	}
	return j;
}

bool isLuaKeyword(std::string const& word)
{
	static const std::array<const char*, 22> kw = {
		"and", "break", "do", "else", "elseif", "end", "false", "for",
		"function", "goto", "if", "in", "local", "nil", "not", "or",
		"repeat", "return", "then", "true", "until", "while",
	};
	for (const char* k : kw)
	{
		if (word == k)
		{
			return true;
		}
	}
	return false;
}

//! accumulates gap-free, ordered spans: emit() fills the untouched run before
//! `begin` as Text, then appends the classified token
struct SpanBuilder
{
	std::vector<SyntaxSpan>&	out;
	std::size_t					covered = 0;

	explicit SpanBuilder(std::vector<SyntaxSpan>& o) : out(o) {}

	void emit(std::size_t begin, std::size_t end, SyntaxToken token)
	{
		if (begin > this->covered)
		{
			this->out.push_back({ this->covered, begin, SyntaxToken::Text });
		}
		if (end > begin)
		{
			this->out.push_back({ begin, end, token });
			this->covered = end;
		}
	}

	void finish(std::size_t n)
	{
		if (n > this->covered)
		{
			this->out.push_back({ this->covered, n, SyntaxToken::Text });
		}
	}
};

void highlightLua(std::string const& s, SpanBuilder& b)
{
	const std::size_t n = s.size();
	std::size_t i = 0;
	while (i < n)
	{
		const char c = s[i];
		if (c == '-' && i + 1 < n && s[i + 1] == '-')
		{
			b.emit(i, n, SyntaxToken::Comment);	// line comment to end
			return;
		}
		if (c == '"' || c == '\'')
		{
			const std::size_t end = scanString(s, i);
			b.emit(i, end, SyntaxToken::String);
			i = end;
			continue;
		}
		if (isDigitCh(c) && numberBoundary(s, i))
		{
			const std::size_t end = scanNumber(s, i);
			b.emit(i, end, SyntaxToken::Number);
			i = end;
			continue;
		}
		if (isIdentStart(c))
		{
			const std::size_t end = scanIdent(s, i);
			if (isLuaKeyword(s.substr(i, end - i)))
			{
				b.emit(i, end, SyntaxToken::Keyword);
			}
			i = end;	// a non-keyword identifier stays Text (gap fill)
			continue;
		}
		++i;
	}
}

void highlightIni(std::string const& s, SpanBuilder& b)
{
	const std::size_t n = s.size();
	// the leading key token: the first identifier run on the line (a directive
	// name / key) is highlighted, unless the line opens a section or a comment
	std::size_t lead = 0;
	while (lead < n && isSpaceCh(s[lead]))
	{
		++lead;
	}
	std::size_t keyBegin = std::string::npos;
	std::size_t keyEnd = 0;
	if (lead < n && isIdentStart(s[lead]))
	{
		keyBegin = lead;
		keyEnd = scanIdent(s, lead);
	}
	std::size_t i = 0;
	while (i < n)
	{
		if (i == keyBegin)
		{
			b.emit(keyBegin, keyEnd, SyntaxToken::Keyword);
			i = keyEnd;
			continue;
		}
		const char c = s[i];
		if (c == '#')
		{
			b.emit(i, n, SyntaxToken::Comment);
			return;
		}
		if (c == '/' && i + 1 < n && s[i + 1] == '/')
		{
			b.emit(i, n, SyntaxToken::Comment);
			return;
		}
		if (c == '[')
		{
			const std::size_t close = s.find(']', i);
			const std::size_t end = close == std::string::npos ? n : close + 1;
			b.emit(i, end, SyntaxToken::Section);
			i = end;
			continue;
		}
		if (c == '@' && i + 1 < n && isIdentStart(s[i + 1]))
		{
			// a localisation key may be dotted (e.g. @menu.play)
			std::size_t end = i + 1;
			while (end < n && (isIdentCh(s[end]) || s[end] == '.'))
			{
				++end;
			}
			b.emit(i, end, SyntaxToken::Reference);
			i = end;
			continue;
		}
		if (c == '"' || c == '\'')
		{
			const std::size_t end = scanString(s, i);
			b.emit(i, end, SyntaxToken::String);
			i = end;
			continue;
		}
		if (isDigitCh(c) && numberBoundary(s, i))
		{
			const std::size_t end = scanNumber(s, i);
			b.emit(i, end, SyntaxToken::Number);
			i = end;
			continue;
		}
		if (isIdentStart(c))
		{
			i = scanIdent(s, i);	// non-key identifier stays Text
			continue;
		}
		++i;
	}
}

void highlightMarkup(std::string const& s, SpanBuilder& b)
{
	const std::size_t n = s.size();
	std::size_t i = 0;
	while (i < n)
	{
		const char c = s[i];
		if (c == '<' && s.compare(i, 4, "<!--") == 0)
		{
			const std::size_t close = s.find("-->", i + 4);
			const std::size_t end = close == std::string::npos ? n : close + 3;
			b.emit(i, end, SyntaxToken::Comment);
			i = end;
			continue;
		}
		if (c == '/' && i + 1 < n && s[i + 1] == '/')
		{
			b.emit(i, n, SyntaxToken::Comment);
			return;
		}
		if (c == '<')
		{
			// the tag punctuation + element name (attributes are scanned after)
			std::size_t j = i + 1;
			if (j < n && (s[j] == '/' || s[j] == '?' || s[j] == '!'))
			{
				++j;
			}
			while (j < n && (isIdentCh(s[j]) || s[j] == ':' || s[j] == '-' ||
				s[j] == '.'))
			{
				++j;
			}
			b.emit(i, j, SyntaxToken::Section);
			i = j;
			continue;
		}
		if (c == '"' || c == '\'')
		{
			const std::size_t end = scanString(s, i);
			// a JSON key is a string immediately followed (after spaces) by ':'
			std::size_t k = end;
			while (k < n && isSpaceCh(s[k]))
			{
				++k;
			}
			const SyntaxToken token = (k < n && s[k] == ':')
				? SyntaxToken::Section : SyntaxToken::String;
			b.emit(i, end, token);
			i = end;
			continue;
		}
		if (isDigitCh(c) && numberBoundary(s, i))
		{
			const std::size_t end = scanNumber(s, i);
			b.emit(i, end, SyntaxToken::Number);
			i = end;
			continue;
		}
		++i;
	}
}

} // namespace

std::vector<SyntaxSpan> highlightLine(std::string const& line, SyntaxFormat format)
{
	std::vector<SyntaxSpan> spans;
	if (line.empty())
	{
		return spans;
	}
	SpanBuilder builder(spans);
	switch (format)
	{
	case SyntaxFormat::Lua:		highlightLua(line, builder); break;
	case SyntaxFormat::IniLike:	highlightIni(line, builder); break;
	case SyntaxFormat::Markup:	highlightMarkup(line, builder); break;
	case SyntaxFormat::PlainText:	break;	// one trailing Text span covers it
	}
	builder.finish(line.size());
	return spans;
}

SyntaxFormat syntaxFormatForExtension(std::string const& extension)
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
	if (ext == ".lua")
	{
		return SyntaxFormat::Lua;
	}
	if (ext == ".oui" || ext == ".ogui" || ext == ".omat" || ext == ".oshape" ||
		ext == ".oanim")
	{
		return SyntaxFormat::IniLike;
	}
	if (ext == ".oscene" || ext == ".oprefab" || ext == ".xlf" ||
		ext == ".json" || ext == ".oactions" || ext == ".olayers" ||
		ext == ".olevels" || ext == ".orkproj" || ext == ".orkmeta" ||
		ext == ".xml")
	{
		return SyntaxFormat::Markup;
	}
	return SyntaxFormat::PlainText;
}

unsigned int syntaxTokenColor(SyntaxToken token, bool dark)
{
	if (dark)
	{
		switch (token)
		{
		case SyntaxToken::Text:			return 0xD4D4D4FFu;
		case SyntaxToken::Comment:		return 0x7C8A74FFu;
		case SyntaxToken::String:		return 0xC9A97AFFu;
		case SyntaxToken::Number:		return 0x9FBBD8FFu;
		case SyntaxToken::Keyword:		return 0xB48EADFFu;
		case SyntaxToken::Section:		return 0x7FB0C4FFu;
		case SyntaxToken::Reference:	return 0x9AC48AFFu;
		}
		return 0xD4D4D4FFu;
	}
	switch (token)
	{
	case SyntaxToken::Text:			return 0x2C2C2CFFu;
	case SyntaxToken::Comment:		return 0x5D7150FFu;
	case SyntaxToken::String:		return 0x8A5A22FFu;
	case SyntaxToken::Number:		return 0x2F5C8AFFu;
	case SyntaxToken::Keyword:		return 0x7A3D6EFFu;
	case SyntaxToken::Section:		return 0x2C6E82FFu;
	case SyntaxToken::Reference:	return 0x3F7A2FFFu;
	}
	return 0x2C2C2CFFu;
}

} // namespace Orkige
