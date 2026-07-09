/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	CVarCommand.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debug/CVarCommand.h"
#include "core_debug/CVarManager.h"

#include <cctype>

namespace Orkige
{
	namespace CVarCommand
	{
		namespace
		{
			//! the leading token of a line ("" when the line is only whitespace)
			//! plus the offset just past it (where the remainder begins)
			String firstToken(String const & line, std::size_t & outAfter)
			{
				std::size_t begin = 0;
				while (begin < line.size() &&
					std::isspace(static_cast<unsigned char>(line[begin])))
				{
					++begin;
				}
				std::size_t end = begin;
				while (end < line.size() &&
					!std::isspace(static_cast<unsigned char>(line[end])))
				{
					++end;
				}
				outAfter = end;
				return line.substr(begin, end - begin);
			}
			//---------------------------------------------------------
			//! the remainder of the line after offset, with surrounding
			//! whitespace trimmed
			String remainder(String const & line, std::size_t after)
			{
				std::size_t begin = after;
				std::size_t end = line.size();
				while (begin < end &&
					std::isspace(static_cast<unsigned char>(line[begin])))
				{
					++begin;
				}
				while (end > begin &&
					std::isspace(static_cast<unsigned char>(line[end - 1])))
				{
					--end;
				}
				return line.substr(begin, end - begin);
			}
			//---------------------------------------------------------
			//! "<name> = <value>" for the human-readable output
			String describe(CVar const & cvar)
			{
				return cvar.name + " = " + cvar.value;
			}
		}
		//---------------------------------------------------------
		bool isCommand(String const & line)
		{
			std::size_t after = 0;
			const String verb = firstToken(line, after);
			return verb == "set" || verb == "get" || verb == "find" ||
				verb == "reset";
		}
		//---------------------------------------------------------
		bool parseSet(String const & line, String & outName, String & outValue)
		{
			std::size_t after = 0;
			if (firstToken(line, after) != "set")
			{
				return false;
			}
			std::size_t afterName = 0;
			const String rest = remainder(line, after);
			const String name = firstToken(rest, afterName);
			if (name.empty())
			{
				return false;
			}
			const String value = remainder(rest, afterName);
			if (value.empty())
			{
				return false;
			}
			outName = name;
			outValue = value;
			return true;
		}
		//---------------------------------------------------------
		String run(String const & line)
		{
			std::size_t after = 0;
			const String verb = firstToken(line, after);
			CVarManager & manager = CVarManager::getSingleton();

			if (verb == "get")
			{
				const String name = remainder(line, after);
				if (name.empty())
				{
					return "error: usage: get <name>";
				}
				CVar const * cvar = manager.find(name);
				if (!cvar)
				{
					return "error: unknown cvar '" + name + "'";
				}
				return describe(*cvar);
			}
			if (verb == "set")
			{
				String name;
				String value;
				if (!parseSet(line, name, value))
				{
					return "error: usage: set <name> <value>";
				}
				String error;
				if (!manager.setString(name, value, &error))
				{
					return "error: " + error;
				}
				return describe(*manager.find(name));
			}
			if (verb == "reset")
			{
				const String name = remainder(line, after);
				if (name.empty())
				{
					return "error: usage: reset <name>";
				}
				String error;
				if (!manager.reset(name, &error))
				{
					return "error: " + error;
				}
				return describe(*manager.find(name));
			}
			if (verb == "find")
			{
				const String prefix = remainder(line, after); // "" = all
				const StringVector names = manager.findByPrefix(prefix);
				if (names.empty())
				{
					return prefix.empty()
						? String("no cvars registered")
						: "no cvars matching '" + prefix + "'";
				}
				String out;
				for (std::size_t i = 0; i < names.size(); ++i)
				{
					if (i > 0)
					{
						out += "\n";
					}
					out += describe(*manager.find(names[i]));
				}
				return out;
			}
			return "error: unknown command '" + verb + "'";
		}
	}
}
