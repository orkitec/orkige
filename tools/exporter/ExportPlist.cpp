/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportPlist.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportPlist.h"

#include <tinyxml2.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace OrkigeExport
{
	namespace
	{
		const char * const PLIST_HEADER =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" "
			"\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
			"<plist version=\"1.0\">\n";
		//---------------------------------------------------------
		void appendIndent(Orkige::String & out, int depth)
		{
			for(int level = 0; level < depth; ++level)
			{
				out += '\t';
			}
		}
		//---------------------------------------------------------
		//! XML text escaping: the five predefined entities, which is all a
		//! plist body needs (attribute values are ours alone)
		Orkige::String escaped(Orkige::String const & text)
		{
			Orkige::String out;
			out.reserve(text.size());
			for(char character : text)
			{
				switch(character)
				{
				case '&':	out += "&amp;";		break;
				case '<':	out += "&lt;";		break;
				case '>':	out += "&gt;";		break;
				case '"':	out += "&quot;";	break;
				case '\'':	out += "&apos;";	break;
				default:	out += character;	break;
				}
			}
			return out;
		}
		//---------------------------------------------------------
		Orkige::String numberText(double value)
		{
			char buffer[64];
			// an integral number is an <integer> - the plist type every count
			// and version an export writes actually is
			if(value == std::floor(value) && std::fabs(value) < 1e15)
			{
				std::snprintf(buffer, sizeof(buffer), "%lld",
					static_cast<long long>(value));
			}
			else
			{
				std::snprintf(buffer, sizeof(buffer), "%.17g", value);
			}
			return buffer;
		}
		//---------------------------------------------------------
		bool appendValue(Orkige::JsonValue const & value, int depth,
			Orkige::String & out, Orkige::String * error);
		//---------------------------------------------------------
		bool appendDict(Orkige::JsonValue const & value, int depth,
			Orkige::String & out, Orkige::String * error)
		{
			if(value.members().empty())
			{
				appendIndent(out, depth);
				out += "<dict/>\n";
				return true;
			}
			appendIndent(out, depth);
			out += "<dict>\n";
			for(std::pair<Orkige::String, Orkige::JsonValue> const & member :
				value.members())
			{
				appendIndent(out, depth + 1);
				out += "<key>" + escaped(member.first) + "</key>\n";
				if(!appendValue(member.second, depth + 1, out, error))
				{
					return false;
				}
			}
			appendIndent(out, depth);
			out += "</dict>\n";
			return true;
		}
		//---------------------------------------------------------
		bool appendValue(Orkige::JsonValue const & value, int depth,
			Orkige::String & out, Orkige::String * error)
		{
			switch(value.getType())
			{
			case Orkige::JsonValue::Type::Bool:
				appendIndent(out, depth);
				out += value.asBool() ? "<true/>\n" : "<false/>\n";
				return true;
			case Orkige::JsonValue::Type::Number:
			{
				const double number = value.asNumber();
				const bool integral = (number == std::floor(number) &&
					std::fabs(number) < 1e15);
				const Orkige::String tag = integral ? "integer" : "real";
				appendIndent(out, depth);
				out += "<" + tag + ">" + numberText(number) + "</" + tag +
					">\n";
				return true;
			}
			case Orkige::JsonValue::Type::String:
				appendIndent(out, depth);
				out += "<string>" + escaped(value.asString()) + "</string>\n";
				return true;
			case Orkige::JsonValue::Type::Array:
				if(value.size() == 0)
				{
					appendIndent(out, depth);
					out += "<array/>\n";
					return true;
				}
				appendIndent(out, depth);
				out += "<array>\n";
				for(std::size_t index = 0; index < value.size(); ++index)
				{
					if(!appendValue(value.at(index), depth + 1, out, error))
					{
						return false;
					}
				}
				appendIndent(out, depth);
				out += "</array>\n";
				return true;
			case Orkige::JsonValue::Type::Object:
				return appendDict(value, depth, out, error);
			case Orkige::JsonValue::Type::Null:
			default:
				if(error != 0)
				{
					*error = "a property list cannot carry a null value";
				}
				return false;
			}
		}
		//---------------------------------------------------------
		//! read one plist value element into a JsonValue
		Orkige::JsonValue readValue(tinyxml2::XMLElement const * element)
		{
			const Orkige::String tag = element->Name();
			if(tag == "true")
			{
				return Orkige::JsonValue(true);
			}
			if(tag == "false")
			{
				return Orkige::JsonValue(false);
			}
			if(tag == "integer" || tag == "real")
			{
				const char * text = element->GetText();
				return Orkige::JsonValue(
					text != 0 ? std::strtod(text, 0) : 0.0);
			}
			if(tag == "array")
			{
				Orkige::JsonValue array = Orkige::JsonValue::array();
				for(tinyxml2::XMLElement const * child =
						element->FirstChildElement();
					child != 0; child = child->NextSiblingElement())
				{
					array.push(readValue(child));
				}
				return array;
			}
			if(tag == "dict")
			{
				Orkige::JsonValue dict = Orkige::JsonValue::object();
				Orkige::String pendingKey;
				bool haveKey = false;
				for(tinyxml2::XMLElement const * child =
						element->FirstChildElement();
					child != 0; child = child->NextSiblingElement())
				{
					if(Orkige::String(child->Name()) == "key")
					{
						pendingKey =
							(child->GetText() != 0) ? child->GetText() : "";
						haveKey = true;
						continue;
					}
					if(haveKey)
					{
						dict.set(pendingKey, readValue(child));
						haveKey = false;
					}
				}
				return dict;
			}
			// <string>, and the <data>/<date> shapes an export never authors:
			// their inner text, which reports but does not round-trip
			const char * text = element->GetText();
			return Orkige::JsonValue(
				Orkige::String(text != 0 ? text : ""));
		}
	}
	//---------------------------------------------------------
	bool ExportPlist::serialize(Orkige::JsonValue const & root,
		Orkige::String & out, Orkige::String * error)
	{
		if(!root.isObject())
		{
			if(error != 0)
			{
				*error = "a property list root must be a dictionary";
			}
			return false;
		}
		Orkige::String body;
		if(!appendDict(root, 0, body, error))
		{
			return false;
		}
		out = PLIST_HEADER + body + "</plist>\n";
		return true;
	}
	//---------------------------------------------------------
	bool ExportPlist::write(Orkige::JsonValue const & root,
		Orkige::String const & path, Orkige::String * error)
	{
		Orkige::String text;
		if(!serialize(root, text, error))
		{
			return false;
		}
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if(!file)
		{
			if(error != 0)
			{
				*error = "cannot write the property list '" + path + "'";
			}
			return false;
		}
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.close();
		if(!file)
		{
			if(error != 0)
			{
				*error = "write failed for the property list '" + path + "'";
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportPlist::read(Orkige::String const & path, Orkige::JsonValue & out,
		Orkige::String * error)
	{
		tinyxml2::XMLDocument document;
		if(document.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
		{
			if(error != 0)
			{
				*error = "cannot read the property list '" + path + "': " +
					(document.ErrorStr() != 0 ? document.ErrorStr() : "?");
			}
			return false;
		}
		tinyxml2::XMLElement const * plist = document.RootElement();
		tinyxml2::XMLElement const * dict =
			(plist != 0) ? plist->FirstChildElement("dict") : 0;
		if(dict == 0)
		{
			if(error != 0)
			{
				*error = "'" + path + "' is not an XML property list with a "
					"dictionary root";
			}
			return false;
		}
		out = readValue(dict);
		return true;
	}
	//---------------------------------------------------------
	bool ExportPlist::setKeys(Orkige::String const & path,
		Orkige::JsonValue const & keys, Orkige::String * error)
	{
		if(!keys.isObject())
		{
			if(error != 0)
			{
				*error = "the keys to set must be a dictionary";
			}
			return false;
		}
		tinyxml2::XMLDocument document;
		if(document.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
		{
			if(error != 0)
			{
				*error = "cannot read the property list '" + path + "': " +
					(document.ErrorStr() != 0 ? document.ErrorStr() : "?");
			}
			return false;
		}
		tinyxml2::XMLElement * plist = document.RootElement();
		tinyxml2::XMLElement * dict =
			(plist != 0) ? plist->FirstChildElement("dict") : 0;
		if(dict == 0)
		{
			if(error != 0)
			{
				*error = "'" + path + "' is not an XML property list with a "
					"dictionary root";
			}
			return false;
		}
		for(std::pair<Orkige::String, Orkige::JsonValue> const & member :
			keys.members())
		{
			// the replacement value, serialized on its own and re-parsed into
			// this document - one code path for every value shape, and the
			// document keeps ownership of the nodes
			Orkige::String fragment;
			if(!appendValue(member.second, 0, fragment, error))
			{
				return false;
			}
			tinyxml2::XMLDocument parsed;
			if(parsed.Parse(fragment.c_str()) != tinyxml2::XML_SUCCESS ||
				parsed.RootElement() == 0)
			{
				if(error != 0)
				{
					*error = "cannot compose the value for key '" +
						member.first + "'";
				}
				return false;
			}
			tinyxml2::XMLNode * value =
				parsed.RootElement()->DeepClone(&document);
			// find the existing <key> and replace the element after it
			tinyxml2::XMLElement * existingKey = 0;
			for(tinyxml2::XMLElement * child = dict->FirstChildElement("key");
				child != 0; child = child->NextSiblingElement("key"))
			{
				if(child->GetText() != 0 &&
					Orkige::String(child->GetText()) == member.first)
				{
					existingKey = child;
					break;
				}
			}
			if(existingKey != 0)
			{
				tinyxml2::XMLElement * oldValue =
					existingKey->NextSiblingElement();
				if(oldValue != 0)
				{
					dict->InsertAfterChild(oldValue, value);
					dict->DeleteChild(oldValue);
				}
				else
				{
					dict->InsertAfterChild(existingKey, value);
				}
				continue;
			}
			tinyxml2::XMLElement * newKey = document.NewElement("key");
			newKey->SetText(member.first.c_str());
			dict->InsertEndChild(newKey);
			dict->InsertEndChild(value);
		}
		if(document.SaveFile(path.c_str()) != tinyxml2::XML_SUCCESS)
		{
			if(error != 0)
			{
				*error = "cannot write the property list '" + path + "'";
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	Orkige::JsonValue appTransportSecurity()
	{
		Orkige::JsonValue security = Orkige::JsonValue::object();
		security.set("NSAllowsLocalNetworking", Orkige::JsonValue(true));
		return security;
	}
	//---------------------------------------------------------
	Orkige::JsonValue privacyManifest()
	{
		Orkige::JsonValue accessed = Orkige::JsonValue::array();
		const char * const CATEGORIES[][2] = {
			{ "NSPrivacyAccessedAPICategoryFileTimestamp", "C617.1" },
			{ "NSPrivacyAccessedAPICategorySystemBootTime", "35F9.1" },
		};
		for(auto const & category : CATEGORIES)
		{
			Orkige::JsonValue reasons = Orkige::JsonValue::array();
			reasons.push(Orkige::JsonValue(category[1]));
			Orkige::JsonValue entry = Orkige::JsonValue::object();
			entry.set("NSPrivacyAccessedAPIType",
				Orkige::JsonValue(category[0]));
			entry.set("NSPrivacyAccessedAPITypeReasons", reasons);
			accessed.push(entry);
		}
		Orkige::JsonValue manifest = Orkige::JsonValue::object();
		manifest.set("NSPrivacyTracking", Orkige::JsonValue(false));
		manifest.set("NSPrivacyTrackingDomains", Orkige::JsonValue::array());
		manifest.set("NSPrivacyCollectedDataTypes", Orkige::JsonValue::array());
		manifest.set("NSPrivacyAccessedAPITypes", accessed);
		return manifest;
	}
	//---------------------------------------------------------
	Orkige::JsonValue iosEntitlements(Orkige::String const & teamId,
		Orkige::String const & bundleId, bool forDistribution)
	{
		const Orkige::String applicationIdentifier =
			teamId.empty() ? bundleId : (teamId + "." + bundleId);
		Orkige::JsonValue entitlements = Orkige::JsonValue::object();
		entitlements.set("application-identifier",
			Orkige::JsonValue(applicationIdentifier));
		entitlements.set("com.apple.developer.team-identifier",
			Orkige::JsonValue(teamId));
		entitlements.set("get-task-allow",
			Orkige::JsonValue(!forDistribution));
		return entitlements;
	}
}
