// ScriptCompletion - the Script panel's completion symbol collection.
// See ScriptCompletion.h for the design notes.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "ScriptCompletion.h"

#include <algorithm>
#include <cctype>

namespace Orkige
{
	namespace
	{
		bool isIdentifierChar(char character)
		{
			return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
				character == '_';
		}

		String toLower(String const& text)
		{
			String lower = text;
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			return lower;
		}

		void sortDedupe(std::vector<String>& names)
		{
			std::sort(names.begin(), names.end());
			names.erase(std::unique(names.begin(), names.end()),
				names.end());
		}
	}
	//---------------------------------------------------------
	void ScriptCompletionSymbols::addGlobal(String const& name)
	{
		if (!name.empty())
		{
			this->mGlobals.push_back(name);
		}
	}
	//---------------------------------------------------------
	void ScriptCompletionSymbols::addMember(String const& prefix,
		String const& name)
	{
		if (!prefix.empty() && !name.empty())
		{
			this->mMembers[prefix].push_back(name);
		}
	}
	//---------------------------------------------------------
	void ScriptCompletionSymbols::finalize()
	{
		sortDedupe(this->mGlobals);
		for (auto& entry : this->mMembers)
		{
			sortDedupe(entry.second);
		}
	}
	//---------------------------------------------------------
	std::vector<String> const* ScriptCompletionSymbols::membersFor(
		String const& prefix) const
	{
		const auto exact = this->mMembers.find(prefix);
		if (exact != this->mMembers.end())
		{
			return &exact->second;
		}
		// case-insensitive fallback: `local engine = Engine.getSingleton()`
		// should complete engine: from the Engine type's members - but only
		// when the lowered name is unambiguous
		const String lowered = toLower(prefix);
		std::vector<String> const* found = nullptr;
		for (auto const& entry : this->mMembers)
		{
			if (toLower(entry.first) == lowered)
			{
				if (found != nullptr)
				{
					return nullptr;	// ambiguous - offer nothing
				}
				found = &entry.second;
			}
		}
		return found;
	}
	//---------------------------------------------------------
	void addLuaKeywords(ScriptCompletionSymbols& symbols)
	{
		static char const* const kKeywords[] = {
			"and", "break", "do", "else", "elseif", "end", "false", "for",
			"function", "goto", "if", "in", "local", "nil", "not", "or",
			"repeat", "return", "then", "true", "until", "while",
			// the sandbox-kept base functions scripts lean on constantly
			"pairs", "ipairs", "print", "tostring", "tonumber", "type",
			"select", "pcall", "error", "assert",
		};
		for (char const* keyword : kKeywords)
		{
			symbols.addGlobal(keyword);
		}
	}
	//---------------------------------------------------------
	void addApiIndexSymbols(ScriptCompletionSymbols& symbols,
		String const& indexText)
	{
		// the index is line-oriented: a signature line starts with
		// "<identifier><. or : or (>..." - headers ("# ", "## ") and fenced
		// markers are skipped. "loc(key)" style global functions register as
		// plain globals.
		std::size_t lineStart = 0;
		while (lineStart <= indexText.size())
		{
			std::size_t lineEnd = indexText.find('\n', lineStart);
			if (lineEnd == String::npos)
			{
				lineEnd = indexText.size();
			}
			String line = indexText.substr(lineStart, lineEnd - lineStart);
			lineStart = lineEnd + 1;
			// strip leading whitespace
			std::size_t begin = 0;
			while (begin < line.size() &&
				(line[begin] == ' ' || line[begin] == '\t'))
			{
				++begin;
			}
			line.erase(0, begin);
			if (line.empty() || line[0] == '#' || line[0] == '`' ||
				!isIdentifierChar(line[0]))
			{
				continue;
			}
			std::size_t cursor = 0;
			while (cursor < line.size() && isIdentifierChar(line[cursor]))
			{
				++cursor;
			}
			const String first = line.substr(0, cursor);
			if (cursor >= line.size())
			{
				continue;
			}
			if (line[cursor] == '.' || line[cursor] == ':')
			{
				const std::size_t memberStart = cursor + 1;
				std::size_t memberEnd = memberStart;
				while (memberEnd < line.size() &&
					isIdentifierChar(line[memberEnd]))
				{
					++memberEnd;
				}
				if (memberEnd > memberStart)
				{
					symbols.addGlobal(first);
					symbols.addMember(first,
						line.substr(memberStart, memberEnd - memberStart));
				}
			}
			else if (line[cursor] == '(')
			{
				symbols.addGlobal(first);	// a global function like loc()
			}
		}
	}
	//---------------------------------------------------------
	void addReflectedKinds(ScriptCompletionSymbols& symbols,
		std::vector<ReflectedKindSymbols> const& kinds)
	{
		for (ReflectedKindSymbols const& kind : kinds)
		{
			if (!kind.selfField.empty())
			{
				symbols.addMember("self", kind.selfField);
			}
			if (!kind.worldAccessor.empty())
			{
				symbols.addMember("world", kind.worldAccessor);
			}
			for (String const& property : kind.properties)
			{
				if (!kind.selfField.empty())
				{
					symbols.addMember(kind.selfField, property);
				}
				if (!kind.kindName.empty())
				{
					symbols.addMember(kind.kindName, property);
				}
			}
		}
		// the universal self surface every script instance carries
		symbols.addMember("self", "id");
		symbols.addMember("self", "gameObject");
		symbols.addMember("self", "getComponent");
		symbols.addMember("world", "getComponent");
	}
	//---------------------------------------------------------
	void addRuntimeTable(ScriptCompletionSymbols& symbols,
		String const& tableName, std::vector<String> const& memberNames)
	{
		symbols.addGlobal(tableName);
		for (String const& member : memberNames)
		{
			// skip Lua-internal metatable entries ("__index", "new", ...)
			if (member.size() >= 2 && member[0] == '_' && member[1] == '_')
			{
				continue;
			}
			symbols.addMember(tableName, member);
		}
	}
	//---------------------------------------------------------
	std::vector<String> suggestCompletions(
		ScriptCompletionSymbols const& symbols,
		String const& lineBeforeTerm, String const& fragment,
		std::vector<String> const& documentIdentifiers, std::size_t limit)
	{
		// classify: does the text before the term end in "<identifier>." or
		// "<identifier>:"? Then only that table's members apply.
		String prefixTable;
		if (!lineBeforeTerm.empty())
		{
			const char accessor = lineBeforeTerm.back();
			if (accessor == '.' || accessor == ':')
			{
				std::size_t nameStart = lineBeforeTerm.size() - 1;
				while (nameStart > 0 &&
					isIdentifierChar(lineBeforeTerm[nameStart - 1]))
				{
					--nameStart;
				}
				prefixTable = lineBeforeTerm.substr(nameStart,
					lineBeforeTerm.size() - 1 - nameStart);
			}
		}
		// gather the candidate pool
		std::vector<String> pool;
		if (!prefixTable.empty())
		{
			if (std::vector<String> const* members =
				symbols.membersFor(prefixTable))
			{
				pool = *members;
			}
			// an unknown table still completes from the document's own
			// identifiers (a local table the API knows nothing about)
			pool.insert(pool.end(), documentIdentifiers.begin(),
				documentIdentifiers.end());
		}
		else
		{
			pool = symbols.globals();
			pool.insert(pool.end(), documentIdentifiers.begin(),
				documentIdentifiers.end());
		}
		sortDedupe(pool);
		// rank: case-sensitive prefix, then case-insensitive prefix, then
		// substring; the exact fragment itself is never suggested alone
		const String loweredFragment = toLower(fragment);
		std::vector<String> exactPrefix;
		std::vector<String> loosePrefix;
		std::vector<String> substring;
		for (String const& candidate : pool)
		{
			if (candidate == fragment)
			{
				continue;
			}
			if (fragment.empty())
			{
				exactPrefix.push_back(candidate);
				continue;
			}
			const String loweredCandidate = toLower(candidate);
			if (candidate.compare(0, fragment.size(), fragment) == 0)
			{
				exactPrefix.push_back(candidate);
			}
			else if (loweredCandidate.compare(0, loweredFragment.size(),
				loweredFragment) == 0)
			{
				loosePrefix.push_back(candidate);
			}
			else if (loweredCandidate.find(loweredFragment) != String::npos)
			{
				substring.push_back(candidate);
			}
		}
		std::vector<String> result;
		for (std::vector<String> const* bucket :
			{ &exactPrefix, &loosePrefix, &substring })
		{
			for (String const& candidate : *bucket)
			{
				if (result.size() >= limit)
				{
					return result;
				}
				result.push_back(candidate);
			}
		}
		return result;
	}
}
