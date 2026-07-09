/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	Json.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __Json_h__7_9_2026__12_00_00__
#define __Json_h__7_9_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <utility>
#include <vector>

namespace Orkige
{
	//! @brief a minimal, allocation-simple JSON value with FULL nesting
	//! (objects and arrays), unlike the flat DebugMessage codec next door.
	//! @remarks it exists for the editor's in-process MCP endpoint (WP #90),
	//! whose JSON-RPC 2.0 surface needs nested objects/arrays (params, result,
	//! the tool inputSchemas, the content array) that DebugMessage - a flat
	//! key/value line record - deliberately cannot carry. The parser is a
	//! recursive-descent reader over the same JSON subset the debug protocol
	//! already speaks (strings with \uXXXX + surrogate pairs, numbers, true/
	//! false/null) plus nesting; it NEVER throws and returns false on any
	//! malformed input. Object member order is preserved (a vector of pairs),
	//! which keeps serialized JSON-RPC readable and stable for tests. Numbers
	//! are held as double; integral values serialize without a decimal point.
	class ORKIGE_CORE_DLL JsonValue
	{
		//--- Types -------------------------------------------
	public:
		//! the JSON value kinds
		enum class Type
		{
			Null, Bool, Number, String, Array, Object
		};
		typedef std::vector<JsonValue> Array;					//!< array elements
		typedef std::vector<std::pair<String, JsonValue> > Object;	//!< ordered members
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		Type	mType;			//!< which kind this value holds
		bool	mBool;			//!< Bool payload
		double	mNumber;		//!< Number payload
		String	mString;		//!< String payload
		Array	mArray;			//!< Array payload
		Object	mObject;		//!< Object payload (insertion order preserved)
		//--- Methods -----------------------------------------
	public:
		//! construct a null value
		JsonValue();
		//! construct a value of the given kind (empty for the aggregates)
		explicit JsonValue(Type type);
		//! convenience scalar constructors
		explicit JsonValue(bool value);
		explicit JsonValue(double value);
		explicit JsonValue(int value);
		explicit JsonValue(String const & value);
		explicit JsonValue(const char* value);

		//! factory: an empty object / array (readability at call sites)
		static JsonValue object();
		static JsonValue array();

		Type getType() const { return this->mType; }
		bool isNull() const { return this->mType == Type::Null; }
		bool isBool() const { return this->mType == Type::Bool; }
		bool isNumber() const { return this->mType == Type::Number; }
		bool isString() const { return this->mType == Type::String; }
		bool isArray() const { return this->mType == Type::Array; }
		bool isObject() const { return this->mType == Type::Object; }

		//! typed accessors (defaultValue when the kind does not match)
		bool asBool(bool defaultValue = false) const;
		double asNumber(double defaultValue = 0.0) const;
		//! numbers are also readable as int / string for convenience
		int asInt(int defaultValue = 0) const;
		String const & asString() const;

		//--- array ---
		//! append a value (turns a non-array into an empty array first)
		void push(JsonValue value);
		//! element count (0 for non-arrays/objects)
		size_t size() const;
		//! array element access (a null value for out-of-range/non-array)
		JsonValue const & at(size_t index) const;

		//--- object ---
		//! set a member (turns a non-object into an empty object first);
		//! replaces an existing member in place, else appends
		void set(String const & key, JsonValue value);
		//! is the object member present
		bool has(String const & key) const;
		//! object member access (a null value when absent/non-object)
		JsonValue const & get(String const & key) const;
		//! the ordered members (empty for non-objects)
		Object const & members() const { return this->mObject; }

		//! serialize to a compact single-line JSON string
		String serialize() const;
		//! @brief parse a JSON document; returns false (leaving out untouched)
		//! on ANY malformed input - never throws. Trailing non-whitespace is
		//! rejected. Nesting depth is bounded so a pathological input cannot
		//! exhaust the stack.
		static bool parse(String const & text, JsonValue & out);
	protected:
	private:
	};
}

#endif //__Json_h__7_9_2026__12_00_00__
