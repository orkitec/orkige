/**************************************************************
	created:	2026/07/12 at 14:00
	filename: 	ScriptEventPayload.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptEventPayload_h__12_7_2026__14_00_00__
#define __ScriptEventPayload_h__12_7_2026__14_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <vector>
#include <utility>

namespace Orkige
{
	//! @brief the BOUNDED value model of a script-event payload
	//! (core_script/ScriptEventBus). A payload is a flat table whose values are
	//! scalars (bool / number / string) or a single level of nested scalar
	//! tables - nothing deeper, no functions/userdata. The conversion FROM a Lua
	//! table (at emit) enforces the bound and reports an honest error past it;
	//! this backend-neutral form is what the queue stores and what C++ event
	//! producers (gui, physics, app lifecycle) build directly, so the bus itself
	//! never touches a scripting backend.

	//! one scalar value in a payload (the leaf of the bounded model)
	struct ScriptEventScalar
	{
		//! scoped so the `String` enumerator does not shadow the String type
		enum class Kind { Nil, Bool, Number, String };
		Kind	kind = Kind::Nil;	//!< which member below is meaningful
		bool	boolValue = false;
		double	numberValue = 0.0;
		String	stringValue;

		static ScriptEventScalar makeBool(bool value)
		{
			ScriptEventScalar s; s.kind = Kind::Bool; s.boolValue = value; return s;
		}
		static ScriptEventScalar makeNumber(double value)
		{
			ScriptEventScalar s; s.kind = Kind::Number; s.numberValue = value; return s;
		}
		static ScriptEventScalar makeString(String const & value)
		{
			ScriptEventScalar s; s.kind = Kind::String; s.stringValue = value; return s;
		}
		//! the canonical string form (for the trace flatten): a number without a
		//! trailing ".0" when integral, "true"/"false" for a bool, the string as
		//! is, "" for nil
		String toDisplayString() const;
	};

	//! a payload key: a string name OR an array index (a Lua table can be keyed
	//! either way; both round-trip so `{1,2,3}` and `{id="x"}` survive)
	struct ScriptEventKey
	{
		bool		isIndex = false;	//!< true: integer key; false: string key
		long long	index = 0;			//!< the integer key (when isIndex)
		String		name;				//!< the string key (when !isIndex)

		static ScriptEventKey named(String const & n)
		{
			ScriptEventKey k; k.isIndex = false; k.name = n; return k;
		}
		static ScriptEventKey indexed(long long i)
		{
			ScriptEventKey k; k.isIndex = true; k.index = i; return k;
		}
	};

	//! a payload FIELD: either a scalar or a one-level table of scalar entries
	struct ScriptEventField
	{
		bool				isTable = false;	//!< true: `table` holds the value; false: `scalar`
		ScriptEventScalar	scalar;				//!< the scalar value (when !isTable)
		std::vector<std::pair<ScriptEventKey, ScriptEventScalar> >	table;	//!< the nested scalars (when isTable)
	};

	//! @brief a bounded, backend-neutral event payload: an ordered list of
	//! (key, field) entries. Ordered so a producer's field order is stable and
	//! the trace reads predictably; keyed so a handler reads `e.id` / `e[1]`.
	struct ScriptEventPayload
	{
		std::vector<std::pair<ScriptEventKey, ScriptEventField> >	fields;

		//! is this payload empty (the event carried no data)
		bool empty() const { return this->fields.empty(); }

		//--- top-level scalar builders for C++ producers -----------------
		//! set a string field by name (append; a producer sets each field once)
		void setString(String const & key, String const & value);
		//! set a number field by name
		void setNumber(String const & key, double value);
		//! set a bool field by name
		void setBool(String const & key, bool value);

		//! @brief flatten the TOP-LEVEL scalar fields to (name, string) pairs -
		//! the trace representation (nested tables are skipped; a trace event
		//! carries a flat field set, like the existing contact events)
		void flattenScalars(
			std::vector<std::pair<String, String> > & outFields) const;
	};
}

#endif //__ScriptEventPayload_h__12_7_2026__14_00_00__
