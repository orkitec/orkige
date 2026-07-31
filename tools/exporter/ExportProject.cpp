/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportProject.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportProject.h"

#include <tinyxml2.h>

#include <cctype>
#include <filesystem>
#include <system_error>

namespace OrkigeExport
{
	namespace
	{
		//! trim ASCII whitespace off both ends
		Orkige::String trimmed(Orkige::String const & text)
		{
			std::size_t first = 0;
			std::size_t last = text.size();
			while(first < last && std::isspace(
				static_cast<unsigned char>(text[first])) != 0)
			{
				++first;
			}
			while(last > first && std::isspace(
				static_cast<unsigned char>(text[last - 1])) != 0)
			{
				--last;
			}
			return text.substr(first, last - first);
		}
		//---------------------------------------------------------
		Orkige::String elementText(tinyxml2::XMLElement const * parent,
			const char * name)
		{
			if(parent == 0)
			{
				return "";
			}
			tinyxml2::XMLElement const * child = parent->FirstChildElement(name);
			if(child == 0 || child->GetText() == 0)
			{
				return "";
			}
			return trimmed(child->GetText());
		}
	}
	//---------------------------------------------------------
	Orkige::String ExportProject::setting(Orkige::String const & key,
		Orkige::String const & fallback) const
	{
		std::map<Orkige::String, Orkige::String>::const_iterator found =
			this->settings.find(key);
		return (found == this->settings.end()) ? fallback : found->second;
	}
	//---------------------------------------------------------
	Orkige::String ExportProject::exeName() const
	{
		Orkige::String out;
		for(char character : this->name)
		{
			if(std::isalnum(static_cast<unsigned char>(character)) != 0)
			{
				out += character;
			}
		}
		return out.empty() ? Orkige::String("OrkigeGame") : out;
	}
	//---------------------------------------------------------
	Orkige::String ExportProject::idSlug() const
	{
		Orkige::String out;
		for(char character : this->name)
		{
			const unsigned char raw = static_cast<unsigned char>(character);
			if(std::isalnum(raw) != 0)
			{
				out += static_cast<char>(std::tolower(raw));
			}
		}
		if(out.empty())
		{
			return "orkigegame";
		}
		// a reverse-DNS label may not start with a digit
		if(std::isdigit(static_cast<unsigned char>(out[0])) != 0)
		{
			out = "p" + out;
		}
		return out;
	}
	//---------------------------------------------------------
	Orkige::String ExportProject::nativeTarget() const
	{
		return trimmed(this->setting("native.target"));
	}
	//---------------------------------------------------------
	bool ExportProject::readManifest(Orkige::String const & path,
		ExportProject & project, Orkige::String * error)
	{
		auto fail = [error](Orkige::String const & message) -> bool
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		};
		std::error_code ignored;
		std::filesystem::path manifestPath(path);
		std::filesystem::path rootPath(path);
		if(std::filesystem::is_directory(manifestPath, ignored))
		{
			manifestPath /= "project.orkproj";
		}
		else
		{
			rootPath = manifestPath.parent_path();
		}
		if(!std::filesystem::is_regular_file(manifestPath, ignored))
		{
			return fail("no project.orkproj at '" + manifestPath.string() + "'");
		}
		tinyxml2::XMLDocument document;
		if(document.LoadFile(manifestPath.string().c_str()) !=
			tinyxml2::XML_SUCCESS)
		{
			return fail("unparseable manifest '" + manifestPath.string() +
				"': " + (document.ErrorStr() != 0 ? document.ErrorStr() : "?"));
		}
		tinyxml2::XMLElement const * root = document.RootElement();
		if(root == 0 || Orkige::String(root->Name()) != "OrkigeProject")
		{
			return fail("'" + manifestPath.string() +
				"' is not an OrkigeProject manifest");
		}
		ExportProject read;
		read.root = std::filesystem::absolute(rootPath, ignored)
			.lexically_normal().string();
		read.name = elementText(root, "Name");
		if(read.name.empty())
		{
			return fail("manifest '" + manifestPath.string() + "' has no Name");
		}
		read.mainScene = elementText(root, "MainScene");
		// Settings may sit under a <Settings> wrapper or (older hand-edited
		// manifests) loose at the root; walk every <Setting> either way, which
		// is what the manifest reader in core_project/Project does too.
		for(tinyxml2::XMLElement const * settings =
				root->FirstChildElement("Settings");
			settings != 0;
			settings = settings->NextSiblingElement("Settings"))
		{
			for(tinyxml2::XMLElement const * entry =
					settings->FirstChildElement("Setting");
				entry != 0; entry = entry->NextSiblingElement("Setting"))
			{
				const char * key = entry->Attribute("key");
				if(key != 0 && key[0] != '\0')
				{
					const char * value = entry->Attribute("value");
					read.settings[key] = (value != 0) ? value : "";
				}
			}
		}
		for(tinyxml2::XMLElement const * entry =
				root->FirstChildElement("Setting");
			entry != 0; entry = entry->NextSiblingElement("Setting"))
		{
			const char * key = entry->Attribute("key");
			if(key != 0 && key[0] != '\0')
			{
				const char * value = entry->Attribute("value");
				read.settings[key] = (value != 0) ? value : "";
			}
		}
		project = read;
		return true;
	}
}
