/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	StringTable.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/StringTable.h"

#include <fstream>
#include <sstream>

namespace Orkige
{
	IMPL_OSINGLETON(StringTable);
	//---------------------------------------------------------
	const String StringTable::LOCALISATION_SETTING_KEY = "localisation";
	//---------------------------------------------------------
	namespace
	{
		//! trim leading/trailing ASCII whitespace (in place copy)
		String trimmed(String const & text)
		{
			std::size_t begin = 0;
			std::size_t end = text.size();
			while(begin < end && (unsigned char)text[begin] <= ' ')
			{
				++begin;
			}
			while(end > begin && (unsigned char)text[end - 1] <= ' ')
			{
				--end;
			}
			return text.substr(begin, end - begin);
		}
	}
	//---------------------------------------------------------
	StringTable::StringTable()
	{
	}
	//---------------------------------------------------------
	StringTable::~StringTable()
	{
	}
	//---------------------------------------------------------
	unsigned int StringTable::parseInto(String const & language,
		std::istream & stream)
	{
		Table & table = this->mLanguages[language];
		unsigned int added = 0;
		String line;
		while(std::getline(stream, line))
		{
			// tolerate CR from CRLF sources
			if(!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			const String trimmedLine = trimmed(line);
			if(trimmedLine.empty() || trimmedLine[0] == '#' ||
				trimmedLine[0] == ';')
			{
				continue;
			}
			const std::size_t equals = trimmedLine.find('=');
			if(equals == String::npos)
			{
				continue;	// not a key=value line - skip, never throw
			}
			const String key = trimmed(trimmedLine.substr(0, equals));
			const String value = trimmed(trimmedLine.substr(equals + 1));
			if(key.empty())
			{
				continue;
			}
			table[key] = value;
			++added;
		}
		return added;
	}
	//---------------------------------------------------------
	bool StringTable::loadFile(String const & filePath)
	{
		std::ifstream file(filePath.c_str());
		if(!file.is_open())
		{
			return false;
		}
		// [lang] section headers switch the target language; lines before the
		// first header land under the active language (or "" when none is set)
		String currentLanguage = this->mLanguage;
		String line;
		std::ostringstream buffer;
		auto flush = [&]()
		{
			if(!buffer.str().empty())
			{
				std::istringstream section(buffer.str());
				this->parseInto(currentLanguage, section);
				buffer.str("");
				buffer.clear();
			}
		};
		while(std::getline(file, line))
		{
			String probe = line;
			if(!probe.empty() && probe.back() == '\r')
			{
				probe.pop_back();
			}
			probe = trimmed(probe);
			if(probe.size() >= 2 && probe.front() == '[' && probe.back() == ']')
			{
				flush();
				currentLanguage = trimmed(probe.substr(1, probe.size() - 2));
				if(this->mLanguage.empty())
				{
					this->mLanguage = currentLanguage;
				}
				continue;
			}
			buffer << line << '\n';
		}
		flush();
		return true;
	}
	//---------------------------------------------------------
	bool StringTable::loadLanguage(String const & language,
		String const & filePath)
	{
		std::ifstream file(filePath.c_str());
		if(!file.is_open())
		{
			return false;
		}
		this->parseInto(language, file);
		if(this->mLanguage.empty())
		{
			this->mLanguage = language;
		}
		return true;
	}
	//---------------------------------------------------------
	void StringTable::set(String const & language, String const & key,
		String const & value)
	{
		this->mLanguages[language][key] = value;
		if(this->mLanguage.empty())
		{
			this->mLanguage = language;
		}
	}
	//---------------------------------------------------------
	void StringTable::setLanguage(String const & language)
	{
		this->mLanguage = language;
	}
	//---------------------------------------------------------
	bool StringTable::hasLanguage(String const & language) const
	{
		return this->mLanguages.find(language) != this->mLanguages.end();
	}
	//---------------------------------------------------------
	String const & StringTable::get(String const & key) const
	{
		std::map<String, Table>::const_iterator languageIt =
			this->mLanguages.find(this->mLanguage);
		if(languageIt != this->mLanguages.end())
		{
			Table::const_iterator entryIt = languageIt->second.find(key);
			if(entryIt != languageIt->second.end())
			{
				return entryIt->second;
			}
		}
		return key;	// honest fallback: the key stays visible in the UI
	}
	//---------------------------------------------------------
	bool StringTable::has(String const & key) const
	{
		std::map<String, Table>::const_iterator languageIt =
			this->mLanguages.find(this->mLanguage);
		if(languageIt == this->mLanguages.end())
		{
			return false;
		}
		return languageIt->second.find(key) != languageIt->second.end();
	}
	//---------------------------------------------------------
	String StringTable::format(String const & key,
		StringVector const & args) const
	{
		String result = this->get(key);
		for(std::size_t i = 0; i < args.size(); ++i)
		{
			std::ostringstream placeholderStream;
			placeholderStream << "%%" << i << "%%";
			const String placeholder = placeholderStream.str();
			for(std::size_t pos = result.find(placeholder);
				pos != String::npos;
				pos = result.find(placeholder, pos + args[i].length()))
			{
				result.replace(pos, placeholder.length(), args[i]);
			}
		}
		return result;
	}
	//---------------------------------------------------------
	void StringTable::clear()
	{
		this->mLanguages.clear();
		this->mLanguage.clear();
	}
}
