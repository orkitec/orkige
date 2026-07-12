/**************************************************************
	created:	2026/07/12 at 14:00
	filename: 	ScriptEventPayload.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptEventPayload.h"

#include <cmath>
#include <cstdio>

namespace Orkige
{
	//---------------------------------------------------------
	String ScriptEventScalar::toDisplayString() const
	{
		switch (this->kind)
		{
		case Kind::Bool:
			return this->boolValue ? "true" : "false";
		case Kind::String:
			return this->stringValue;
		case Kind::Number:
		{
			// integral values print without a decimal tail (ids/counts read
			// cleanly in the trace); everything else keeps full precision
			const double value = this->numberValue;
			char buffer[32];
			if (value == std::floor(value) && std::abs(value) < 1.0e15)
			{
				std::snprintf(buffer, sizeof(buffer), "%lld",
					static_cast<long long>(value));
			}
			else
			{
				std::snprintf(buffer, sizeof(buffer), "%g", value);
			}
			return String(buffer);
		}
		case Kind::Nil:
		default:
			return String();
		}
	}
	//---------------------------------------------------------
	void ScriptEventPayload::setString(String const & key, String const & value)
	{
		ScriptEventField field;
		field.isTable = false;
		field.scalar = ScriptEventScalar::makeString(value);
		this->fields.push_back(std::make_pair(ScriptEventKey::named(key), field));
	}
	//---------------------------------------------------------
	void ScriptEventPayload::setNumber(String const & key, double value)
	{
		ScriptEventField field;
		field.isTable = false;
		field.scalar = ScriptEventScalar::makeNumber(value);
		this->fields.push_back(std::make_pair(ScriptEventKey::named(key), field));
	}
	//---------------------------------------------------------
	void ScriptEventPayload::setBool(String const & key, bool value)
	{
		ScriptEventField field;
		field.isTable = false;
		field.scalar = ScriptEventScalar::makeBool(value);
		this->fields.push_back(std::make_pair(ScriptEventKey::named(key), field));
	}
	//---------------------------------------------------------
	void ScriptEventPayload::flattenScalars(
		std::vector<std::pair<String, String> > & outFields) const
	{
		for (std::pair<ScriptEventKey, ScriptEventField> const & entry :
			this->fields)
		{
			if (entry.second.isTable)
			{
				continue;	// nested tables do not flatten into the trace
			}
			// name the field: a string key as is, an integer key stringified
			const String name = entry.first.isIndex
				? std::to_string(entry.first.index)
				: entry.first.name;
			outFields.push_back(
				std::make_pair(name, entry.second.scalar.toDisplayString()));
		}
	}
}
