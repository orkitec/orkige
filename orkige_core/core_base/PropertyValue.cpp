/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertyValue.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_base/PropertyValue.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace Orkige
{
	namespace
	{
		//! trim leading/trailing ASCII whitespace (the CVarManager dialect)
		String propTrimmed(String const & text)
		{
			std::size_t begin = 0;
			std::size_t end = text.size();
			while (begin < end &&
				std::isspace(static_cast<unsigned char>(text[begin])))
			{
				++begin;
			}
			while (end > begin &&
				std::isspace(static_cast<unsigned char>(text[end - 1])))
			{
				--end;
			}
			return text.substr(begin, end - begin);
		}
		//---------------------------------------------------------
		String propToLower(String const & text)
		{
			String out = text;
			for (char & c : out)
			{
				c = static_cast<char>(std::tolower(
					static_cast<unsigned char>(c)));
			}
			return out;
		}
		//---------------------------------------------------------
		String floatToString(float value)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.9g",
				static_cast<double>(value));
			return buffer;
		}
		//---------------------------------------------------------
		String doubleToString(double value)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.9g", value);
			return buffer;
		}
		//---------------------------------------------------------
		//! parse count whitespace/comma separated floats out of text; false when
		//! fewer than count numbers are present (extra numbers are ignored)
		bool parseFloats(String const & text, int count, float * out)
		{
			std::istringstream stream(text);
			for (int each = 0; each < count; ++each)
			{
				// treat commas as separators so "1,2,3" parses like "1 2 3"
				while (stream.good())
				{
					int peeked = stream.peek();
					if (peeked == ',' )
					{
						stream.get();
						continue;
					}
					break;
				}
				if (!(stream >> out[each]) || !std::isfinite(out[each]))
				{
					return false;
				}
			}
			return true;
		}
	}
	//---------------------------------------------------------
	PropertyValue::PropertyValue()
		: mKind(PropertyKind::Int)
		, mInt(0)
		, mFloat(0.0)
	{
		this->mVec[0] = this->mVec[1] = this->mVec[2] = this->mVec[3] = 0.0f;
	}
	//---------------------------------------------------------
	PropertyValue::PropertyValue(PropertyKind kind)
		: mKind(kind)
		, mInt(0)
		, mFloat(0.0)
	{
		this->mVec[0] = this->mVec[1] = this->mVec[2] = this->mVec[3] = 0.0f;
	}
	//---------------------------------------------------------
	//--- factories -------------------------------------------
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeInt(long long value)
	{
		PropertyValue result(PropertyKind::Int);
		result.mInt = value;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeFloat(double value)
	{
		PropertyValue result(PropertyKind::Float);
		result.mFloat = value;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeBool(bool value)
	{
		PropertyValue result(PropertyKind::Bool);
		result.mInt = value ? 1 : 0;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeString(String const & value)
	{
		PropertyValue result(PropertyKind::String);
		result.mText = value;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeEnum(String const & enumTypeName,
		long long value)
	{
		PropertyValue result(PropertyKind::Enum);
		result.mInt = value;
		result.mTypeName = enumTypeName;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeVec3(PropVec3 const & value)
	{
		PropertyValue result(PropertyKind::Vec3);
		result.mVec[0] = value.x;
		result.mVec[1] = value.y;
		result.mVec[2] = value.z;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeQuat(PropQuat const & value)
	{
		PropertyValue result(PropertyKind::Quat);
		result.mVec[0] = value.w;
		result.mVec[1] = value.x;
		result.mVec[2] = value.y;
		result.mVec[3] = value.z;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeColor(PropColor const & value)
	{
		PropertyValue result(PropertyKind::Color);
		result.mVec[0] = value.r;
		result.mVec[1] = value.g;
		result.mVec[2] = value.b;
		result.mVec[3] = value.a;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeAssetRef(String const & assetKind,
		String const & id)
	{
		PropertyValue result(PropertyKind::AssetRef);
		result.mText = id;
		result.mTypeName = assetKind;
		return result;
	}
	//---------------------------------------------------------
	PropertyValue PropertyValue::makeObjectRef(String const & targetType,
		String const & id)
	{
		PropertyValue result(PropertyKind::ObjectRef);
		result.mText = id;
		result.mTypeName = targetType;
		return result;
	}
	//---------------------------------------------------------
	//--- typed reads -----------------------------------------
	//---------------------------------------------------------
	long long PropertyValue::asInt() const
	{
		switch (this->mKind)
		{
		case PropertyKind::Int:
		case PropertyKind::Bool:
		case PropertyKind::Enum:
			return this->mInt;
		case PropertyKind::Float:
			return static_cast<long long>(this->mFloat);
		case PropertyKind::String:
		case PropertyKind::AssetRef:
		case PropertyKind::ObjectRef:
			return std::strtoll(this->mText.c_str(), NULL, 10);
		default:
			return 0;
		}
	}
	//---------------------------------------------------------
	double PropertyValue::asFloat() const
	{
		switch (this->mKind)
		{
		case PropertyKind::Float:
			return this->mFloat;
		case PropertyKind::Int:
		case PropertyKind::Bool:
		case PropertyKind::Enum:
			return static_cast<double>(this->mInt);
		case PropertyKind::String:
		case PropertyKind::AssetRef:
		case PropertyKind::ObjectRef:
			return std::strtod(this->mText.c_str(), NULL);
		default:
			return 0.0;
		}
	}
	//---------------------------------------------------------
	bool PropertyValue::asBool() const
	{
		switch (this->mKind)
		{
		case PropertyKind::Bool:
		case PropertyKind::Int:
		case PropertyKind::Enum:
			return this->mInt != 0;
		case PropertyKind::Float:
			return this->mFloat != 0.0;
		case PropertyKind::String:
		case PropertyKind::AssetRef:
		case PropertyKind::ObjectRef:
		{
			const String lowered = propToLower(propTrimmed(this->mText));
			return lowered == "1" || lowered == "true" || lowered == "on" ||
				lowered == "yes";
		}
		default:
			return false;
		}
	}
	//---------------------------------------------------------
	String PropertyValue::asString() const
	{
		return this->toString();
	}
	//---------------------------------------------------------
	PropVec3 PropertyValue::asVec3() const
	{
		PropVec3 result;
		if (this->mKind == PropertyKind::Vec3 ||
			this->mKind == PropertyKind::Quat ||
			this->mKind == PropertyKind::Color)
		{
			result.x = this->mVec[0];
			result.y = this->mVec[1];
			result.z = this->mVec[2];
		}
		return result;
	}
	//---------------------------------------------------------
	PropQuat PropertyValue::asQuat() const
	{
		PropQuat result;
		if (this->mKind == PropertyKind::Quat ||
			this->mKind == PropertyKind::Color)
		{
			result.w = this->mVec[0];
			result.x = this->mVec[1];
			result.y = this->mVec[2];
			result.z = this->mVec[3];
		}
		else if (this->mKind == PropertyKind::Vec3)
		{
			result.w = 0.0f;
			result.x = this->mVec[0];
			result.y = this->mVec[1];
			result.z = this->mVec[2];
		}
		return result;
	}
	//---------------------------------------------------------
	PropColor PropertyValue::asColor() const
	{
		PropColor result;
		if (this->mKind == PropertyKind::Color ||
			this->mKind == PropertyKind::Quat)
		{
			result.r = this->mVec[0];
			result.g = this->mVec[1];
			result.b = this->mVec[2];
			result.a = this->mVec[3];
		}
		else if (this->mKind == PropertyKind::Vec3)
		{
			result.r = this->mVec[0];
			result.g = this->mVec[1];
			result.b = this->mVec[2];
			result.a = 1.0f;
		}
		return result;
	}
	//---------------------------------------------------------
	String PropertyValue::toString() const
	{
		switch (this->mKind)
		{
		case PropertyKind::Int:
		case PropertyKind::Enum:
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%lld", this->mInt);
			return buffer;
		}
		case PropertyKind::Bool:
			return this->mInt != 0 ? "1" : "0";
		case PropertyKind::Float:
			return doubleToString(this->mFloat);
		case PropertyKind::Vec3:
			return floatToString(this->mVec[0]) + " " +
				floatToString(this->mVec[1]) + " " +
				floatToString(this->mVec[2]);
		case PropertyKind::Quat:
		case PropertyKind::Color:
			return floatToString(this->mVec[0]) + " " +
				floatToString(this->mVec[1]) + " " +
				floatToString(this->mVec[2]) + " " +
				floatToString(this->mVec[3]);
		case PropertyKind::String:
		case PropertyKind::AssetRef:
		case PropertyKind::ObjectRef:
		default:
			return this->mText;
		}
	}
	//---------------------------------------------------------
	bool PropertyValue::fromString(String const & text, String * outError)
	{
		switch (this->mKind)
		{
		case PropertyKind::Int:
		case PropertyKind::Enum:
		{
			const String trimmed = propTrimmed(text);
			char * end = NULL;
			const long long parsed = std::strtoll(trimmed.c_str(), &end, 10);
			if (trimmed.empty() || end != trimmed.c_str() + trimmed.size())
			{
				if (outError) { *outError = "'" + text + "' is not an integer"; }
				return false;
			}
			this->mInt = parsed;
			return true;
		}
		case PropertyKind::Float:
		{
			const String trimmed = propTrimmed(text);
			char * end = NULL;
			const double parsed = std::strtod(trimmed.c_str(), &end);
			if (trimmed.empty() || end != trimmed.c_str() + trimmed.size() ||
				!std::isfinite(parsed))
			{
				if (outError) { *outError = "'" + text + "' is not a number"; }
				return false;
			}
			this->mFloat = parsed;
			return true;
		}
		case PropertyKind::Bool:
		{
			const String lowered = propToLower(propTrimmed(text));
			if (lowered == "1" || lowered == "true" || lowered == "on" ||
				lowered == "yes")
			{
				this->mInt = 1;
				return true;
			}
			if (lowered == "0" || lowered == "false" || lowered == "off" ||
				lowered == "no")
			{
				this->mInt = 0;
				return true;
			}
			if (outError) { *outError = "'" + text + "' is not a boolean"; }
			return false;
		}
		case PropertyKind::Vec3:
		{
			float parsed[3] = { 0.0f, 0.0f, 0.0f };
			if (!parseFloats(text, 3, parsed))
			{
				if (outError) { *outError = "'" + text + "' is not a vec3"; }
				return false;
			}
			this->mVec[0] = parsed[0];
			this->mVec[1] = parsed[1];
			this->mVec[2] = parsed[2];
			return true;
		}
		case PropertyKind::Quat:
		case PropertyKind::Color:
		{
			float parsed[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (!parseFloats(text, 4, parsed))
			{
				if (outError)
				{
					*outError = "'" + text + "' needs four numbers";
				}
				return false;
			}
			this->mVec[0] = parsed[0];
			this->mVec[1] = parsed[1];
			this->mVec[2] = parsed[2];
			this->mVec[3] = parsed[3];
			return true;
		}
		case PropertyKind::String:
		case PropertyKind::AssetRef:
		case PropertyKind::ObjectRef:
		default:
			// text kinds never reject (kept verbatim, like a String cvar)
			this->mText = text;
			return true;
		}
	}
	//---------------------------------------------------------
}
