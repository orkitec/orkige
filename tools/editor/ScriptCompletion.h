// ScriptCompletion - the Script panel's completion symbol collection.
//
// The suggestion sources merge here, all as PURE data so the editor-core unit
// suite pins the logic down without an engine or a scripting state:
//   * the generated Lua API signature index (GeneratedLuaApi.h text) - the
//     documented global tables and core types with their members
//   * the reflected script surface, fed as plain DTOs the editor shell builds
//     from the ONE truth (ScriptRuntime's component-access registry + the
//     property schemas): self.<field>, world.<accessor>, per-kind properties
//   * live scripting-state globals/members (ScriptRuntime::globalNames /
//     globalMemberNames), fed as plain name lists
//   * the Lua keywords
//   * the open document's own identifiers (fed by the widget per query)
// The query half classifies the text before the cursor ("world." / "self:" /
// bare identifier) and returns the ordered, bounded suggestion list.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_util/String.h>

#include <map>
#include <vector>

namespace Orkige
{
	//! @brief the merged completion symbol set: names completable at a bare
	//! identifier position, plus per-prefix member names ("world" -> exists,
	//! get, ...). Prefix lookup is case-insensitive on a miss, so the common
	//! `local engine = Engine.getSingleton()` idiom still completes
	//! `engine:` from the registered `Engine` type's members.
	class ScriptCompletionSymbols
	{
	public:
		void addGlobal(String const& name);
		void addMember(String const& prefix, String const& name);
		//! sort + dedupe every list (call once after collection)
		void finalize();
		std::vector<String> const& globals() const { return mGlobals; }
		//! the member list for a prefix: exact match first, else the single
		//! case-insensitive match (nullptr when unknown/ambiguous)
		std::vector<String> const* membersFor(String const& prefix) const;
		bool empty() const { return mGlobals.empty() && mMembers.empty(); }

	private:
		std::vector<String> mGlobals;
		std::map<String, std::vector<String>> mMembers;
	};

	//! @brief one reflected component KIND's completion surface, built by the
	//! editor shell from the scriptable-component registry + property schema
	//! (a DTO so the collector and its tests stay engine-free)
	struct ReflectedKindSymbols
	{
		String selfField;		//!< the self.<name> injection field ("" = none)
		String worldAccessor;	//!< the world.<accessor> name ("" = none)
		String kindName;		//!< the reflected kind ("TransformComponent")
		std::vector<String> properties;	//!< reflected property names
	};

	//! the Lua keywords (and the handful of ubiquitous base functions the
	//! sandbox keeps)
	void addLuaKeywords(ScriptCompletionSymbols& symbols);

	//! @brief parse the generated Lua API signature index (the
	//! GeneratedLuaApi.h text): every "table.member(...)" / "Type:method(...)"
	//! signature line contributes its table as a global and its member under
	//! that prefix; comment/header lines are skipped.
	void addApiIndexSymbols(ScriptCompletionSymbols& symbols,
		String const& indexText);

	//! @brief fold in the reflected component kinds: self.<field> +
	//! world.<accessor> entries, and each kind's properties as members under
	//! BOTH its self-field name ("transform.") and its kind name.
	void addReflectedKinds(ScriptCompletionSymbols& symbols,
		std::vector<ReflectedKindSymbols> const& kinds);

	//! @brief fold in one live scripting-state table: the global name plus
	//! its enumerated members (the ScriptRuntime::globalMemberNames feed)
	void addRuntimeTable(ScriptCompletionSymbols& symbols,
		String const& tableName, std::vector<String> const& memberNames);

	//! @brief the query: given the line text BEFORE the term being typed, the
	//! typed fragment and the open document's identifiers, produce the
	//! ordered suggestion list (prefix matches first, then substring matches;
	//! case-insensitive; bounded by limit). A "table."/"table:" context
	//! narrows to that table's members; a bare identifier offers globals +
	//! keywords + document identifiers.
	std::vector<String> suggestCompletions(
		ScriptCompletionSymbols const& symbols,
		String const& lineBeforeTerm, String const& fragment,
		std::vector<String> const& documentIdentifiers, std::size_t limit);
}
