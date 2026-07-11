/********************************************************************
	created:	Saturday 2026/07/11 at 16:00
	filename: 	GuiLayout.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the .oui declarative UI document model (@see GuiLayout.h).
*********************************************************************/

#include "engine_gui/GuiLayout.h"

#include <sstream>

namespace Orkige
{
	namespace
	{
		//! trim leading/trailing spaces + tabs + CR (LF already split off)
		String trim(String const & s)
		{
			const char* ws = " \t\r";
			const size_t begin = s.find_first_not_of(ws);
			if(begin == String::npos)
			{
				return String();
			}
			const size_t end = s.find_last_not_of(ws);
			return s.substr(begin, end - begin + 1);
		}
	}
	//---------------------------------------------------------
	String const * GuiLayoutSection::find(String const & key) const
	{
		for(GuiLayoutEntry const & entry : this->entries)
		{
			if(entry.key == key)
			{
				return &entry.value;
			}
		}
		return NULL;
	}
	//---------------------------------------------------------
	void GuiLayoutSection::set(String const & key, String const & value)
	{
		for(GuiLayoutEntry & entry : this->entries)
		{
			if(entry.key == key)
			{
				entry.value = value;
				return;
			}
		}
		GuiLayoutEntry entry;
		entry.key = key;
		entry.value = value;
		this->entries.push_back(entry);
	}
	//---------------------------------------------------------
	bool GuiLayoutDoc::parse(String const & text, GuiLayoutDoc & out, String & error)
	{
		out.sections.clear();
		error.clear();
		std::istringstream stream(text);
		String rawLine;
		int lineNumber = 0;
		bool haveSection = false;
		while(std::getline(stream, rawLine))
		{
			++lineNumber;
			const String line = trim(rawLine);
			if(line.empty() || line[0] == '#' || line[0] == ';')
			{
				continue;	// blank / comment
			}
			if(line[0] == '[')
			{
				const size_t close = line.find(']');
				if(close == String::npos)
				{
					std::ostringstream msg;
					msg << "unterminated section header on line " << lineNumber;
					error = msg.str();
					return false;
				}
				const String header = trim(line.substr(1, close - 1));
				if(header.empty())
				{
					std::ostringstream msg;
					msg << "empty section header on line " << lineNumber;
					error = msg.str();
					return false;
				}
				// split "Type id" on the first run of whitespace
				GuiLayoutSection section;
				const size_t space = header.find_first_of(" \t");
				if(space == String::npos)
				{
					section.type = header;
				}
				else
				{
					section.type = header.substr(0, space);
					section.id = trim(header.substr(space + 1));
				}
				out.sections.push_back(section);
				haveSection = true;
				continue;
			}
			// a key line: "key = value" / "key : value" / "key<tab>value"
			if(!haveSection)
			{
				std::ostringstream msg;
				msg << "key outside any section on line " << lineNumber;
				error = msg.str();
				return false;
			}
			size_t sep = line.find_first_of("=:\t");
			GuiLayoutEntry entry;
			if(sep == String::npos)
			{
				entry.key = trim(line);	// a bare flag key, empty value
			}
			else
			{
				entry.key = trim(line.substr(0, sep));
				entry.value = trim(line.substr(sep + 1));
			}
			if(entry.key.empty())
			{
				std::ostringstream msg;
				msg << "empty key on line " << lineNumber;
				error = msg.str();
				return false;
			}
			out.sections.back().entries.push_back(entry);
		}
		return true;
	}
	//---------------------------------------------------------
	String GuiLayoutDoc::serialize() const
	{
		std::ostringstream out;
		for(size_t s = 0; s < this->sections.size(); ++s)
		{
			GuiLayoutSection const & section = this->sections[s];
			out << '[' << section.type;
			if(!section.id.empty())
			{
				out << ' ' << section.id;
			}
			out << "]\n";
			for(GuiLayoutEntry const & entry : section.entries)
			{
				out << entry.key << " = " << entry.value << '\n';
			}
			if(s + 1 < this->sections.size())
			{
				out << '\n';	// blank line between sections (canonical form)
			}
		}
		return out.str();
	}
	//---------------------------------------------------------
	GuiLayoutSection const * GuiLayoutDoc::findSection(String const & type) const
	{
		for(GuiLayoutSection const & section : this->sections)
		{
			if(section.type == type)
			{
				return &section;
			}
		}
		return NULL;
	}
}
