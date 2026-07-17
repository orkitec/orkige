/**************************************************************
	created:	2026/07/12 at 18:00
	filename: 	MaterialAsset.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file MaterialAsset.cpp
//! @brief the `.omat` grammar parser (@see MaterialAsset.h)

#include "core_util/MaterialAsset.h"

#include <set>
#include <sstream>

namespace Orkige
{
	namespace
	{
		//! report "line N: what" into the optional error string
		bool fail(String * outError, int line, String const & what)
		{
			if(outError)
			{
				*outError = "line " + std::to_string(line) + ": " + what;
			}
			return false;
		}

		//! read one 0..1 float value; false on garbage or out of range
		bool readUnitFloat(std::istringstream & tokens, float & out)
		{
			if(!(tokens >> out))
			{
				return false;
			}
			return out >= 0.0f && out <= 1.0f;
		}

		//! a directive line must be spent - trailing tokens are an error
		bool lineDone(std::istringstream & tokens)
		{
			String extra;
			return !(tokens >> extra);
		}
	}
	//---------------------------------------------------------
	bool MaterialAsset::parse(String const & text, ParsedMaterial & out,
		String * outError)
	{
		out = ParsedMaterial();
		if(outError)
		{
			outError->clear();
		}

		ParsedMaterial parsed;
		std::set<String> seen;	//!< every directive may appear once
		bool anyDirective = false;
		int lineNumber = 0;

		std::istringstream lines(text);
		String rawLine;
		while(std::getline(lines, rawLine))
		{
			++lineNumber;
			// strip a trailing comment, then tokenize on whitespace
			const std::size_t hash = rawLine.find('#');
			if(hash != String::npos)
			{
				rawLine.erase(hash);
			}
			std::istringstream tokens(rawLine);
			String keyword;
			if(!(tokens >> keyword) || keyword.empty())
			{
				continue;	// blank / comment-only line
			}
			if(!seen.insert(keyword).second)
			{
				return fail(outError, lineNumber,
					"duplicate directive '" + keyword + "'");
			}

			if(keyword == "version")
			{
				int version = 0;
				if(!(tokens >> version) || !lineDone(tokens))
				{
					return fail(outError, lineNumber, "version takes one integer");
				}
				if(version != 1)
				{
					return fail(outError, lineNumber, "unsupported version " +
						std::to_string(version) + " (this engine reads version 1)");
				}
			}
			else if(keyword == "albedo")
			{
				float r, g, b, a;
				if(!readUnitFloat(tokens, r) || !readUnitFloat(tokens, g) ||
					!readUnitFloat(tokens, b) || !readUnitFloat(tokens, a) ||
					!lineDone(tokens))
				{
					return fail(outError, lineNumber,
						"albedo takes four 0..1 values (r g b a)");
				}
				parsed.albedo = Colour(r, g, b, a);
			}
			else if(keyword == "emissive")
			{
				float r, g, b;
				if(!readUnitFloat(tokens, r) || !readUnitFloat(tokens, g) ||
					!readUnitFloat(tokens, b) || !lineDone(tokens))
				{
					return fail(outError, lineNumber,
						"emissive takes three 0..1 values (r g b)");
				}
				parsed.emissive = Colour(r, g, b, 1.0f);
			}
			else if(keyword == "metalness" || keyword == "roughness" ||
				keyword == "alphaTest")
			{
				float value = 0.0f;
				if(!readUnitFloat(tokens, value) || !lineDone(tokens))
				{
					return fail(outError, lineNumber,
						keyword + " takes one 0..1 value");
				}
				(keyword == "metalness" ? parsed.metalness
					: keyword == "roughness" ? parsed.roughness
					: parsed.alphaTest) = value;
			}
			else if(keyword == "twoSided")
			{
				int value = 0;
				if(!(tokens >> value) || (value != 0 && value != 1) ||
					!lineDone(tokens))
				{
					return fail(outError, lineNumber, "twoSided takes 0 or 1");
				}
				parsed.twoSided = (value == 1);
			}
			else if(keyword == "albedoTexture" || keyword == "normalTexture" ||
				keyword == "emissiveTexture")
			{
				String name;
				if(!(tokens >> name) || name.empty() || !lineDone(tokens))
				{
					return fail(outError, lineNumber,
						keyword + " takes one resource name");
				}
				if(keyword == "albedoTexture")
				{
					parsed.albedoTexture = name;
				}
				else if(keyword == "normalTexture")
				{
					parsed.normalTexture = name;
				}
				else
				{
					parsed.emissiveTexture = name;
				}
			}
			else
			{
				// no reserved-word tolerance here (@see MaterialAsset.h): a
				// typo'd directive silently ignored would misrender silently
				return fail(outError, lineNumber,
					"unknown directive '" + keyword + "'");
			}
			anyDirective = true;
		}

		if(!anyDirective)
		{
			return fail(outError, lineNumber,
				"empty material (no directives found)");
		}
		out = parsed;
		return true;
	}
	//---------------------------------------------------------
	namespace
	{
		//! a short, round-trippable float form: fixed 4 decimals with trailing
		//! zeros (and a bare trailing dot) trimmed - "0.5", "1", "0.2500" -> "0.25"
		String shortFloat(float value)
		{
			std::ostringstream out;
			out.setf(std::ios::fixed);
			out.precision(4);
			out << value;
			String text = out.str();
			const std::size_t dot = text.find('.');
			if(dot != String::npos)
			{
				std::size_t last = text.find_last_not_of('0');
				if(last == dot)		// nothing but zeros after the point
				{
					--last;
				}
				text.erase(last + 1);
			}
			return text;
		}
	}
	//---------------------------------------------------------
	String MaterialAsset::serialize(ParsedMaterial const & material)
	{
		const ParsedMaterial defaults;
		std::ostringstream out;
		// the version line is always emitted so a hand read shows the grammar
		out << "version 1\n";
		if(material.albedo.r != defaults.albedo.r ||
			material.albedo.g != defaults.albedo.g ||
			material.albedo.b != defaults.albedo.b ||
			material.albedo.a != defaults.albedo.a)
		{
			out << "albedo " << shortFloat(material.albedo.r) << " "
				<< shortFloat(material.albedo.g) << " "
				<< shortFloat(material.albedo.b) << " "
				<< shortFloat(material.albedo.a) << "\n";
		}
		if(!material.albedoTexture.empty())
		{
			out << "albedoTexture " << material.albedoTexture << "\n";
		}
		if(material.metalness != defaults.metalness)
		{
			out << "metalness " << shortFloat(material.metalness) << "\n";
		}
		if(material.roughness != defaults.roughness)
		{
			out << "roughness " << shortFloat(material.roughness) << "\n";
		}
		if(!material.normalTexture.empty())
		{
			out << "normalTexture " << material.normalTexture << "\n";
		}
		if(material.emissive.r != defaults.emissive.r ||
			material.emissive.g != defaults.emissive.g ||
			material.emissive.b != defaults.emissive.b)
		{
			out << "emissive " << shortFloat(material.emissive.r) << " "
				<< shortFloat(material.emissive.g) << " "
				<< shortFloat(material.emissive.b) << "\n";
		}
		if(!material.emissiveTexture.empty())
		{
			out << "emissiveTexture " << material.emissiveTexture << "\n";
		}
		if(material.alphaTest != defaults.alphaTest)
		{
			out << "alphaTest " << shortFloat(material.alphaTest) << "\n";
		}
		if(material.twoSided != defaults.twoSided)
		{
			out << "twoSided " << (material.twoSided ? 1 : 0) << "\n";
		}
		return out.str();
	}
}
