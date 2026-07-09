/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugProtocol.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __DebugProtocol_h__7_7_2026__23_30_00__
#define __DebugProtocol_h__7_7_2026__23_30_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <map>

namespace Orkige
{
	//! @brief the editor <-> runtime remote debugging protocol: message type
	//! and field name constants shared by DebugServer (runtime side) and
	//! DebugClient (editor side).
	//! @remarks Wire format: one message per line ('\n' terminated), each line
	//! a FLAT JSON object: {"v":1,"type":"...","key":"value",...}. JSON was
	//! chosen over reusing the tinyxml2-backed XMLArchive because the messages
	//! are tiny flat key/value records where a one-line self-delimiting format
	//! wins - the hand-rolled reader/writer in DebugMessage supports exactly
	//! the subset the protocol needs (strings, numbers, bools, null and flat
	//! arrays of strings/numbers - no nested objects) and rejects everything
	//! else without crashing.
	namespace DebugProtocol
	{
		//! protocol version written into every message as "v"
		extern ORKIGE_CORE_DLL const int VERSION;
		//! transport line cap: longer lines are refused by the sender and
		//! discarded by the receiver (protects both ends from unbounded input)
		extern ORKIGE_CORE_DLL const unsigned int MAX_LINE_LENGTH;

		//--- editor -> runtime ---
		extern ORKIGE_CORE_DLL const String MSG_PAUSE;				//!< gate update/physics stepping
		extern ORKIGE_CORE_DLL const String MSG_RESUME;				//!< resume update/physics stepping
		extern ORKIGE_CORE_DLL const String MSG_STEP;				//!< advance exactly one update while paused
		extern ORKIGE_CORE_DLL const String MSG_QUIT;				//!< runtime replies bye and exits
		extern ORKIGE_CORE_DLL const String MSG_SELECT;				//!< FIELD_ID: object whose state gets streamed
		extern ORKIGE_CORE_DLL const String MSG_SET_PROPERTY;		//!< FIELD_ID + FIELD_COMPONENT + FIELD_PROPERTY + FIELD_VALUE
		extern ORKIGE_CORE_DLL const String MSG_REQUEST_HIERARCHY;	//!< ask for a hierarchy message now
		//! @brief FIELD_ID + FIELD_VALUE ("1"/"0"): set the GameObject's own
		//! active flag (GameObject::setActive). Additive since protocol v1:
		//! old runtimes answer with an "unknown command" error, never crash.
		extern ORKIGE_CORE_DLL const String MSG_SET_ACTIVE;
		//! @brief Lua hot-reload (WP #77): the editor tells the RUNNING player
		//! to recompile-and-swap its ScriptComponents (compile-before-swap, so
		//! a broken edit keeps the old instance ticking). Optional FIELD_ID
		//! targets a single GameObject; ABSENT = reload every ScriptComponent
		//! (v1 is reload-ALL). Player-directed by design: the editor never ticks
		//! components, it only sends this; the player performs the swap. This is
		//! the FIRST of the additive protocol-extension messages that ride the
		//! ONE debug protocol - #83 (cvars, MSG_SET_CVAR) and #80 (the MCP play-
		//! control verbs) extend the same processMessages else-if chain the same
		//! way. Additive since protocol v1: old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_RELOAD_SCRIPT;
		//! @brief cvar tuning (WP #83): the editor tells the RUNNING player to
		//! change a console variable live. FIELD_CVAR_NAME names the cvar,
		//! FIELD_VALUE carries the new value (a string, parsed per the cvar's
		//! registered type on the player). The player drives it through
		//! CVarManager::setString, so the cvar's onChange fires and the change
		//! takes effect immediately; a bad name/value answers with an error
		//! (never crashes). The SECOND additive protocol-extension message that
		//! rides the ONE debug protocol (after MSG_RELOAD_SCRIPT); #80 (the MCP
		//! play-control verbs) extends the same else-if chain the same way.
		//! Additive since protocol v1: old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_SET_CVAR;

		//--- runtime -> editor ---
		extern ORKIGE_CORE_DLL const String MSG_HELLO;				//!< first message after connect; FIELD_SCENE: loaded scene path
		extern ORKIGE_CORE_DLL const String MSG_HIERARCHY;			//!< LIST_IDS: all GameObject ids (sent on change or on request)
		extern ORKIGE_CORE_DLL const String MSG_OBJECT_STATE;		//!< FIELD_ID + LIST_COMPONENTS + "<Component>.<property>" fields
		extern ORKIGE_CORE_DLL const String MSG_LOG;				//!< FIELD_MESSAGE: a runtime log line
		extern ORKIGE_CORE_DLL const String MSG_ERROR;				//!< FIELD_MESSAGE: command failed (never fatal)
		//! @brief FIELD_ID + FIELD_MESSAGE: a ScriptComponent on the given
		//! GameObject failed and disabled itself - pushed ONCE per object per
		//! connection, independent of the object_state stream (which only
		//! covers the SELECTED object, so never-selected objects would fail
		//! silently otherwise). Additive since protocol v1: old editors ignore
		//! unknown message types.
		extern ORKIGE_CORE_DLL const String MSG_SCRIPT_ERROR;
		extern ORKIGE_CORE_DLL const String MSG_BYE;				//!< orderly shutdown notice

		//--- field names ---
		extern ORKIGE_CORE_DLL const String FIELD_ID;				//!< GameObject id
		extern ORKIGE_CORE_DLL const String FIELD_COMPONENT;		//!< component type name (e.g. "TransformComponent")
		extern ORKIGE_CORE_DLL const String FIELD_PROPERTY;		//!< property name (e.g. "position")
		extern ORKIGE_CORE_DLL const String FIELD_VALUE;			//!< property value (floats space-separated)
		extern ORKIGE_CORE_DLL const String FIELD_CVAR_NAME;		//!< MSG_SET_CVAR: the console variable's name
		extern ORKIGE_CORE_DLL const String FIELD_SCENE;			//!< scene file path
		extern ORKIGE_CORE_DLL const String FIELD_MESSAGE;			//!< human-readable log/error text
		extern ORKIGE_CORE_DLL const String FIELD_LEVEL;			//!< log severity: "info", "warning" or "error"
		extern ORKIGE_CORE_DLL const String LIST_IDS;				//!< hierarchy: GameObject id list
		extern ORKIGE_CORE_DLL const String LIST_COMPONENTS;		//!< object_state: component type name list
		//! @brief hierarchy: parent id per object ("" = root), parallel to
		//! LIST_IDS. Additive since protocol v1: absent in old runtimes'
		//! messages - the editor then renders the historical flat list.
		extern ORKIGE_CORE_DLL const String LIST_PARENTS;
		//! @brief hierarchy: activeSelf flag per object ("1"/"0"), parallel to
		//! LIST_IDS (effective activeInHierarchy is derived through the
		//! parents). Additive since protocol v1 like LIST_PARENTS.
		extern ORKIGE_CORE_DLL const String LIST_ACTIVE;
	}

	//! @brief one protocol message: a type plus flat string fields and flat
	//! string-list fields; encodes to / decodes from a single JSON line.
	//! @remarks all field values travel as JSON strings; the decoder also
	//! accepts bare numbers, true/false and null (stored as their literal
	//! text) so hand-written peers stay compatible. "v" and "type" map to
	//! the version/type members, every other key lands in fields/lists.
	class ORKIGE_CORE_DLL DebugMessage
	{
		//--- Types -------------------------------------------
	public:
		typedef std::map<String, String> FieldMap;			//!< scalar fields by name
		typedef std::map<String, StringVector> ListMap;		//!< string-list fields by name
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
		String		type;		//!< message type (one of DebugProtocol::MSG_*)
		int			version;	//!< protocol version ("v"), DebugProtocol::VERSION when built locally
		FieldMap	fields;		//!< scalar fields
		ListMap		lists;		//!< string-list fields
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! construct an empty (typeless) message at the current protocol version
		DebugMessage();
		//! construct a message of the given type at the current protocol version
		explicit DebugMessage(String const & messageType);
		//! set a scalar field
		void set(String const & key, String const & value);
		//! set a scalar field from a float (round-trip precision)
		void setFloat(String const & key, float value);
		//! get a scalar field ("" when missing)
		String const & get(String const & key) const;
		//! is the scalar field present
		bool has(String const & key) const;
		//! get a scalar field parsed as float (defaultValue when missing/garbage)
		float getFloat(String const & key, float defaultValue = 0.0f) const;
		//! set a string-list field
		void setList(String const & key, StringVector const & values);
		//! get a string-list field (empty vector when missing)
		StringVector const & getList(String const & key) const;
		//! encode as one JSON line (no trailing newline)
		String encode() const;
		//! @brief decode a JSON line; returns false (leaving out untouched on
		//! garbage) for anything that is not a flat JSON object - never throws
		static bool decode(String const & line, DebugMessage & out);
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__DebugProtocol_h__7_7_2026__23_30_00__
