/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	StringTable.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/StringTable.h"

#include "core_debug/LogManager.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace Orkige
{
	IMPL_OSINGLETON(StringTable);
	//---------------------------------------------------------
	const String StringTable::LOCALISATION_SETTING_KEY = "localisation";
	//---------------------------------------------------------
	namespace
	{
		//! log a localisation message when a LogManager exists (the player loads
		//! a project's loc/ before the engine's singleton set is up; without a
		//! LogManager the message is honestly dropped, as in AssetDatabase)
		void locLog(String const & text)
		{
			(void)text;
			if(LogManager::getSingletonPtr())
			{
				oDebugMsg("loc", 0, text);
			}
		}
		//! the local part of an element name (the suffix after any ':' prefix)
		//! so a prefixed document (xlf:xliff) matches like a default-namespace
		//! one - tinyxml2 is not namespace-aware
		const char * localName(const char * name)
		{
			if(name == nullptr)
			{
				return "";
			}
			const char * colon = std::strrchr(name, ':');
			return colon != nullptr ? colon + 1 : name;
		}
		//! does an element's local name match?
		bool isLocal(tinyxml2::XMLElement const * element, const char * name)
		{
			return element != nullptr &&
				std::strcmp(localName(element->Name()), name) == 0;
		}
		//! first child element whose local name matches (prefix-tolerant)
		tinyxml2::XMLElement const * firstChildLocal(
			tinyxml2::XMLElement const * parent, const char * name)
		{
			if(parent == nullptr)
			{
				return nullptr;
			}
			for(tinyxml2::XMLElement const * child = parent->FirstChildElement();
				child != nullptr;
				child = child->NextSiblingElement())
			{
				if(isLocal(child, name))
				{
					return child;
				}
			}
			return nullptr;
		}
		//! reconstruct the runtime string from a <source>/<target> element:
		//! text nodes append verbatim, an inline <x id="N"/> becomes %%N%%, any
		//! other inline element contributes its text content (flagged, not
		//! dropped silently). xml:space="preserve" keeps every space, which the
		//! PRESERVE_WHITESPACE parser (tinyxml2's default) already honors.
		String reconstruct(tinyxml2::XMLElement const * element,
			bool & sawForeignInline)
		{
			String out;
			if(element == nullptr)
			{
				return out;
			}
			for(tinyxml2::XMLNode const * node = element->FirstChild();
				node != nullptr;
				node = node->NextSibling())
			{
				if(tinyxml2::XMLText const * text = node->ToText())
				{
					const char * value = text->Value();
					if(value != nullptr)
					{
						out += value;
					}
					continue;
				}
				tinyxml2::XMLElement const * inline_ = node->ToElement();
				if(inline_ == nullptr)
				{
					continue;	// comments and the like carry no string content
				}
				if(std::strcmp(localName(inline_->Name()), "x") == 0)
				{
					const char * id = inline_->Attribute("id");
					if(id != nullptr)
					{
						out += "%%";
						out += id;
						out += "%%";
					}
					continue;
				}
				// an inline code our own emitter does not produce (<ph>, <g>,
				// <bpt>...): keep whatever text it wraps, flag the file once
				sawForeignInline = true;
				out += reconstruct(inline_, sawForeignInline);
			}
			return out;
		}
		//! a target is USABLE (its text overrides the source) iff its state is
		//! absent or one of the translated/reviewed family; the not-yet /
		//! needs-work states - and any unrecognized state - fall back to source
		bool isUsableState(String const & state)
		{
			if(state.empty())
			{
				return true;	// many tools omit state on a done target
			}
			static const char * const usable[] = {
				"translated",
				"reviewed",
				"needs-review-translation",
				"needs-review-l10n",
				"needs-review-adaptation",
				"final",
				"signed-off"
			};
			for(const char * candidate : usable)
			{
				if(state == candidate)
				{
					return true;
				}
			}
			return false;
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
	bool StringTable::loadXliffFile(String const & filePath)
	{
		// parse the WHOLE document first - nothing touches the live tables until
		// the file is proven well-formed and self-consistent, so a broken file
		// never leaves a partial load
		tinyxml2::XMLDocument document;	// PRESERVE_WHITESPACE by default
		if(document.LoadFile(filePath.c_str()) != tinyxml2::XML_SUCCESS)
		{
			std::ostringstream message;
			message << "localisation: cannot parse '" << filePath << "' (line "
				<< document.ErrorLineNum() << ": "
				<< (document.ErrorStr() != nullptr ? document.ErrorStr() : "")
				<< ")";
			locLog(message.str());
			return false;
		}

		tinyxml2::XMLElement const * root = document.RootElement();
		if(!isLocal(root, "xliff"))
		{
			locLog("localisation: '" + filePath +
				"' is not an <xliff> document");
			return false;
		}
		tinyxml2::XMLElement const * fileElement = firstChildLocal(root, "file");
		if(fileElement == nullptr)
		{
			locLog("localisation: '" + filePath + "' has no <file> element");
			return false;
		}
		tinyxml2::XMLElement const * body = firstChildLocal(fileElement, "body");
		if(body == nullptr)
		{
			locLog("localisation: '" + filePath + "' has no <body> element");
			return false;
		}

		// the file's source language must agree with what other files declared;
		// its target language names the language it translates INTO (absent =>
		// this is the source-only registry file, e.g. en.xlf)
		const char * sourceAttr = fileElement->Attribute("source-language");
		const char * targetAttr = fileElement->Attribute("target-language");
		const String sourceLanguage = sourceAttr != nullptr ? sourceAttr : "";
		const String targetLanguage = targetAttr != nullptr ? targetAttr : "";
		if(sourceLanguage.empty())
		{
			locLog("localisation: '" + filePath +
				"' has no source-language");
			return false;
		}
		if(!this->mSourceLanguage.empty() &&
			this->mSourceLanguage != sourceLanguage)
		{
			locLog("localisation: '" + filePath + "' source-language '" +
				sourceLanguage + "' disagrees with '" + this->mSourceLanguage +
				"' - skipped");
			return false;
		}
		const bool isTargetFile = !targetLanguage.empty();

		// build into locals; commit only once the file is fully validated
		std::vector<std::pair<String, String> > sources;	// key -> source text
		std::vector<std::pair<String, String> > targets;	// key -> usable target
		std::set<String> seenKeys;
		bool sawForeignInline = false;

		for(tinyxml2::XMLElement const * unit = body->FirstChildElement();
			unit != nullptr;
			unit = unit->NextSiblingElement())
		{
			if(!isLocal(unit, "trans-unit"))
			{
				continue;	// <bin-unit>, <group> and the like are tolerated
			}
			// resname is the stable resource key CAT tools preserve; id is the
			// hand-authored duplicate we fall back to
			const char * resname = unit->Attribute("resname");
			const char * id = unit->Attribute("id");
			const String key = resname != nullptr ? resname
				: (id != nullptr ? id : "");
			if(key.empty())
			{
				continue;	// a keyless unit carries no addressable string
			}
			if(!seenKeys.insert(key).second)
			{
				locLog("localisation: '" + filePath +
					"' has a duplicate key '" + key + "'");
				return false;	// duplicate keys => reject the whole file
			}

			tinyxml2::XMLElement const * sourceElement =
				firstChildLocal(unit, "source");
			const String sourceText = reconstruct(sourceElement, sawForeignInline);
			sources.push_back(std::make_pair(key, sourceText));

			if(!isTargetFile)
			{
				continue;	// the registry file has no targets
			}
			tinyxml2::XMLElement const * targetElement =
				firstChildLocal(unit, "target");
			if(targetElement == nullptr)
			{
				continue;	// no target yet => get() falls back to source
			}
			const char * stateAttr = targetElement->Attribute("state");
			const String state = stateAttr != nullptr ? stateAttr : "";
			const String targetText =
				reconstruct(targetElement, sawForeignInline);
			if(targetText.empty() || !isUsableState(state))
			{
				continue;	// empty or not-yet-usable => fall back to source
			}
			targets.push_back(std::make_pair(key, targetText));
		}

		if(sawForeignInline)
		{
			locLog("localisation: '" + filePath + "' carries inline codes other "
				"than <x/>; their text content was kept");
		}

		// commit -----------------------------------------------------------
		this->mSourceLanguage = sourceLanguage;
		Table & sourceTable = this->mLanguages[sourceLanguage];
		for(std::pair<String, String> const & entry : sources)
		{
			// the source-only registry (en.xlf) is authoritative and overwrites;
			// a target file only fills a gap (first writer wins) so the registry
			// text is never clobbered by a translated file's stale <source>
			if(isTargetFile)
			{
				sourceTable.insert(entry);
			}
			else
			{
				sourceTable[entry.first] = entry.second;
			}
		}
		if(isTargetFile)
		{
			Table & targetTable = this->mLanguages[targetLanguage];
			for(std::pair<String, String> const & entry : targets)
			{
				targetTable[entry.first] = entry.second;
			}
		}

		// default the active language to the source language until a caller
		// (device-locale pick / settings menu) selects another - I/O-free
		if(this->mLanguage.empty())
		{
			this->mLanguage = sourceLanguage;
		}
		return true;
	}
	//---------------------------------------------------------
	bool StringTable::loadXliffDirectory(String const & directory)
	{
		std::error_code error;
		std::filesystem::directory_iterator iterator(directory, error);
		if(error)
		{
			locLog("localisation: cannot open directory '" + directory + "'");
			return false;
		}
		// deterministic order so the source-only registry's first-writer-wins
		// and the active-language default are stable across runs
		std::vector<String> files;
		for(std::filesystem::directory_entry const & entry : iterator)
		{
			if(entry.is_regular_file() &&
				entry.path().extension() == ".xlf")
			{
				files.push_back(entry.path().string());
			}
		}
		std::sort(files.begin(), files.end());
		bool loadedAny = false;
		for(String const & file : files)
		{
			loadedAny = this->loadXliffFile(file) || loadedAny;
		}
		return loadedAny;
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
	StringVector StringTable::getLanguages() const
	{
		StringVector languages;	// the map keeps keys sorted for us
		languages.reserve(this->mLanguages.size());
		for(std::map<String, Table>::const_iterator it =
			this->mLanguages.begin(); it != this->mLanguages.end(); ++it)
		{
			languages.push_back(it->first);
		}
		return languages;
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
				return entryIt->second;	// the active-language translation
			}
		}

		// the active language is missing the key: warn once per (language, key)
		// then fall back to the source text, then to the key itself
		if(this->mWarnedKeys.insert(this->mLanguage + "/" + key).second)
		{
			locLog("localisation: no '" + this->mLanguage +
				"' translation for '" + key + "'");
		}

		if(!this->mSourceLanguage.empty() &&
			this->mSourceLanguage != this->mLanguage)
		{
			std::map<String, Table>::const_iterator sourceIt =
				this->mLanguages.find(this->mSourceLanguage);
			if(sourceIt != this->mLanguages.end())
			{
				Table::const_iterator entryIt = sourceIt->second.find(key);
				if(entryIt != sourceIt->second.end())
				{
					return entryIt->second;	// the untranslated source text
				}
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
		this->mSourceLanguage.clear();
		this->mWarnedKeys.clear();
	}
}
