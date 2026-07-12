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
		//! @brief protocol version written into every message as "v".
		//! @remarks the version handshake is deliberately additive-friendly:
		//! DebugMessage::decode fills DebugMessage::version from the wire "v",
		//! and BOTH ends carry DebugProtocol::VERSION when they build a message
		//! locally. A peer never REFUSES a message purely on a version
		//! difference (every extension so far - LIST_PARENTS, MSG_SET_ACTIVE,
		//! MSG_RELOAD_SCRIPT, MSG_SET_CVAR, the MCP control verbs - stayed at
		//! v1 and old peers answer "unknown command" for a type they do not
		//! know). A handler that WANTS to gate a feature compares versions
		//! itself (the MCP control server echoes its VERSION in the hello ack so
		//! the host can adapt); the transport imposes no version policy.
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
		//! @brief Lua hot-reload: the editor tells the RUNNING player
		//! to recompile-and-swap its ScriptComponents (compile-before-swap, so
		//! a broken edit keeps the old instance ticking). Optional FIELD_ID
		//! targets a single GameObject; ABSENT = reload every ScriptComponent
		//! (v1 is reload-ALL). Player-directed by design: the editor never ticks
		//! components, it only sends this; the player performs the swap. This is
		//! the FIRST of the additive protocol-extension messages that ride the
		//! ONE debug protocol - cvars (MSG_SET_CVAR) and the MCP play-
		//! control verbs extend the same processMessages else-if chain the same
		//! way. Additive since protocol v1: old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_RELOAD_SCRIPT;
		//! @brief cvar tuning: the editor tells the RUNNING player to
		//! change a console variable live. FIELD_CVAR_NAME names the cvar,
		//! FIELD_VALUE carries the new value (a string, parsed per the cvar's
		//! registered type on the player). The player drives it through
		//! CVarManager::setString, so the cvar's onChange fires and the change
		//! takes effect immediately; a bad name/value answers with an error
		//! (never crashes). The SECOND additive protocol-extension message that
		//! rides the ONE debug protocol (after MSG_RELOAD_SCRIPT); the MCP
		//! play-control verbs extend the same else-if chain the same way.
		//! Additive since protocol v1: old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_SET_CVAR;
		//! @brief screenshot the RUNNING game: the editor asks the player to
		//! capture its next rendered frame to FIELD_PATH (a path on the player's
		//! filesystem - desktop play shares it with the editor). The player saves
		//! the window contents after the frame renders and answers with
		//! MSG_SCREENSHOT_SAVED. The FOURTH additive protocol-extension message
		//! that rides the ONE debug protocol (after MSG_RELOAD_SCRIPT / cvars and
		//! the MCP play-control verbs); old players answer "unknown command".
		//! Renderer-agnostic on the wire - the capture stays in the player's main
		//! loop (renderer containment), the link only carries the request/ack.
		extern ORKIGE_CORE_DLL const String MSG_SCREENSHOT;
		//! @brief start a TRACE recording of the RUNNING game: the editor asks
		//! the player to sample the world every FIELD_EVERY-th rendered frame
		//! for up to FIELD_SECONDS wall-clock seconds and write a JSON-lines
		//! (.jsonl) flight recorder to FIELD_PATH (a path on the player's
		//! filesystem, shared with the editor on desktop play). Each sample
		//! line carries the frame's dt and the named objects' positions /
		//! velocities / active+visible flags; event lines (contacts, scene
		//! loads, script errors, warnings) interleave as they occur. Optional
		//! FIELD_FILTER is a comma-separated id/name allowlist. The player
		//! auto-stops at the time budget and answers with MSG_RECORD_SAVED;
		//! MSG_RECORD_STOP ends it early. The sampling stays in the player (it
		//! observes the world); the link only carries the request/ack.
		//! Additive since protocol v1: old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_RECORD_START;
		//! @brief stop an in-progress trace NOW and write what was captured
		//! (@see MSG_RECORD_START). A no-op when nothing is recording.
		extern ORKIGE_CORE_DLL const String MSG_RECORD_STOP;

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
		//! @brief the answer to MSG_SCREENSHOT: FIELD_PATH echoes the requested
		//! path, FIELD_VALUE is "1" when the capture was written / "0" on failure
		//! (FIELD_MESSAGE then carries the reason). Additive since protocol v1:
		//! old editors ignore unknown message types.
		extern ORKIGE_CORE_DLL const String MSG_SCREENSHOT_SAVED;
		//! @brief the answer to MSG_RECORD_START/MSG_RECORD_STOP: FIELD_PATH
		//! echoes the written .jsonl trace, FIELD_VALUE is "1" on success / "0"
		//! on failure (FIELD_MESSAGE then carries the reason). Additive since
		//! protocol v1: old editors ignore unknown message types.
		extern ORKIGE_CORE_DLL const String MSG_RECORD_SAVED;
		//! @brief periodic runtime METRICS from the running game: the process
		//! memory footprint (FIELD_MEM_RSS current, FIELD_MEM_RSS_PEAK the
		//! session high-water mark, both resident-set-size bytes) streamed a few
		//! times a second so the editor Stats panel and the MCP get_state
		//! surface can answer "is the game growing?". Fire-and-forget like the
		//! object_state stream (no request, no ack). Additive since protocol v1:
		//! old editors ignore unknown message types, and a runtime whose
		//! platform cannot query memory simply omits the fields.
		extern ORKIGE_CORE_DLL const String MSG_STATS;
		//! @brief periodic gui LAYOUT readback from the running game: the
		//! id + on-screen pixel rect + visibility of every widget, so an agent
		//! (and the safe-area device test) can assert "every visible HUD widget
		//! lies inside the safe box". Parallel lists LIST_UI_IDS / LIST_UI_RECTS
		//! (each rect entry "left top width height visible"). Streamed alongside
		//! MSG_STATS when the game has a UI system; fire-and-forget like it.
		//! Additive since protocol v1: old editors ignore unknown message types.
		extern ORKIGE_CORE_DLL const String MSG_UI_LAYOUT;
		//! @brief synthesize a press on a gui widget in the RUNNING game: the
		//! editor asks the player to press-and-release the widget named by
		//! FIELD_ID at its centre, routed through the REAL input path so modal
		//! and disabled semantics apply (a press bound for a widget under a modal
		//! scrim is eaten, a disabled widget stays inert). An additive
		//! protocol-extension message; old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_GUI_PRESS;
		//! @brief dismiss a modal in the RUNNING game: FIELD_ID names the modal
		//! to close, or (empty) the topmost one. Additive since protocol v1.
		extern ORKIGE_CORE_DLL const String MSG_GUI_DISMISS_MODAL;
		//! @brief control the runtime CPU profiler: FIELD_VALUE "1" enables
		//! scope timing (the Release-build default is off; Debug players are
		//! already on), "0" disables it. Once enabled the player streams
		//! MSG_PROFILE_DATA alongside MSG_STATS. An additive protocol-extension
		//! message; old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_PROFILE;
		//! @brief periodic hierarchical CPU frame profile from the running
		//! game: the last completed frame's scope tree flattened depth-first
		//! into the parallel lists LIST_PROFILE_NAMES / LIST_PROFILE_INFO (each
		//! info entry "depth calls milliseconds maxMilliseconds"), plus
		//! FIELD_FRAME_MS (the whole frame's wall time). Streamed on the
		//! MSG_STATS cadence while the profiler is enabled; fire-and-forget.
		//! Additive since protocol v1: old editors ignore unknown types.
		extern ORKIGE_CORE_DLL const String MSG_PROFILE_DATA;
		extern ORKIGE_CORE_DLL const String MSG_BYE;				//!< orderly shutdown notice

		//--- field names ---
		extern ORKIGE_CORE_DLL const String FIELD_ID;				//!< GameObject id
		extern ORKIGE_CORE_DLL const String FIELD_COMPONENT;		//!< component type name (e.g. "TransformComponent")
		extern ORKIGE_CORE_DLL const String FIELD_PROPERTY;		//!< property name (e.g. "position")
		extern ORKIGE_CORE_DLL const String FIELD_VALUE;			//!< property value (floats space-separated)
		extern ORKIGE_CORE_DLL const String FIELD_CVAR_NAME;		//!< MSG_SET_CVAR: the console variable's name
		extern ORKIGE_CORE_DLL const String FIELD_PATH;				//!< MSG_SCREENSHOT/MSG_RECORD_START: output file path
		extern ORKIGE_CORE_DLL const String FIELD_SECONDS;			//!< MSG_RECORD_START: max wall-clock seconds to record
		extern ORKIGE_CORE_DLL const String FIELD_EVERY;			//!< MSG_RECORD_START: sample every Nth frame
		extern ORKIGE_CORE_DLL const String FIELD_FILTER;			//!< MSG_RECORD_START: comma-separated id/name allowlist ("" = all)
		extern ORKIGE_CORE_DLL const String FIELD_SCENE;			//!< scene file path
		extern ORKIGE_CORE_DLL const String FIELD_MESSAGE;			//!< human-readable log/error text
		extern ORKIGE_CORE_DLL const String FIELD_LEVEL;			//!< log severity: "info", "warning" or "error"
		extern ORKIGE_CORE_DLL const String FIELD_MEM_RSS;			//!< MSG_STATS: current resident set size in bytes
		extern ORKIGE_CORE_DLL const String FIELD_MEM_RSS_PEAK;		//!< MSG_STATS: peak resident set size this session (bytes)
		//! @brief MSG_STATS: the drawable window size and the safe-area insets
		//! (all in PIXELS), so the editor/MCP can answer "is the HUD inside the
		//! notch-safe box?" without a widget dump. Additive since protocol v1:
		//! a runtime that cannot report them simply omits the fields.
		extern ORKIGE_CORE_DLL const String FIELD_WINDOW_W;			//!< MSG_STATS: drawable width in pixels
		extern ORKIGE_CORE_DLL const String FIELD_WINDOW_H;			//!< MSG_STATS: drawable height in pixels
		extern ORKIGE_CORE_DLL const String FIELD_SAFE_LEFT;		//!< MSG_STATS: left safe-area inset (pixels)
		extern ORKIGE_CORE_DLL const String FIELD_SAFE_TOP;			//!< MSG_STATS: top safe-area inset (pixels)
		extern ORKIGE_CORE_DLL const String FIELD_SAFE_RIGHT;		//!< MSG_STATS: right safe-area inset (pixels)
		extern ORKIGE_CORE_DLL const String FIELD_SAFE_BOTTOM;		//!< MSG_STATS: bottom safe-area inset (pixels)
		//! @brief request-correlation id: a REQUEST/RESPONSE protocol
		//! (the MCP editor control port) echoes the request's "req" value back
		//! in its ok/err reply so an async caller can match answers to
		//! outstanding requests. Additive since protocol v1: absent on the
		//! fire-and-forget player messages, which never carry or expect it.
		extern ORKIGE_CORE_DLL const String FIELD_REQ;
		//! @brief auth token: a control port that guards mutation
		//! reads a secret the editor wrote to a file; the host presents it here
		//! in a "hello". Absent on the intra-machine player link (loopback +
		//! single-client is its trust model). Additive since protocol v1.
		extern ORKIGE_CORE_DLL const String FIELD_TOKEN;
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
		//! @brief object_state reflection metadata: four parallel
		//! lists describing the streamed properties so the editor picks a TYPED
		//! widget without a local schema. LIST_PROP_KEYS holds the ordered
		//! "<Component>.<property>" keys (the same keys the flat value fields use);
		//! LIST_PROP_KINDS the PropertyKind int per key; LIST_PROP_HINTS the
		//! widget hint (Enum: "label=value,label=value,..." option table; AssetRef/
		//! ObjectRef: the asset-kind/object-type hint; "" otherwise);
		//! LIST_PROP_FLAGS "1" when the property is read-only, "0" otherwise.
		//! Additive since protocol v1: an editor without them falls back to the
		//! historical untyped read-only value dump, and the values themselves
		//! still cross as the flat "<Component>.<property>" fields.
		extern ORKIGE_CORE_DLL const String LIST_PROP_KEYS;
		extern ORKIGE_CORE_DLL const String LIST_PROP_KINDS;
		extern ORKIGE_CORE_DLL const String LIST_PROP_HINTS;
		extern ORKIGE_CORE_DLL const String LIST_PROP_FLAGS;
		//! @brief MSG_UI_LAYOUT: the widget ids and, parallel to them, one
		//! "left top width height visible" rect string per id (pixels; visible
		//! is "1"/"0"). Two lists keep the message flat (no nested objects).
		extern ORKIGE_CORE_DLL const String LIST_UI_IDS;
		extern ORKIGE_CORE_DLL const String LIST_UI_RECTS;
		//! @brief MSG_UI_LAYOUT: the screen router's state - FIELD_UI_SCREEN is the
		//! current (top) screen's name, FIELD_UI_SCREEN_STACK the space-joined
		//! bottom-to-top screen path. Both empty when no screen stack is in use.
		//! Additive fields (an older player omits them; the reader keeps "").
		extern ORKIGE_CORE_DLL const String FIELD_UI_SCREEN;
		extern ORKIGE_CORE_DLL const String FIELD_UI_SCREEN_STACK;
		//! @brief MSG_STATS: the streamed-music snapshot as three parallel lists
		//! (kept flat, no nested objects, like LIST_UI_IDS/RECTS). LIST_MUSIC_IDS
		//! holds the track ids; LIST_MUSIC_FILES the resource-relative file per id;
		//! LIST_MUSIC_INFO one flat "<playing> <positionSec> <durationSec>
		//! <baseGain> <groupVolume> <effectiveGain> <loop>" string per id (playing
		//! and loop are "1"/"0"). Streamed alongside the memory/safe-area fields;
		//! absent (empty) when no track is registered. Additive since protocol v1.
		extern ORKIGE_CORE_DLL const String LIST_MUSIC_IDS;
		extern ORKIGE_CORE_DLL const String LIST_MUSIC_FILES;
		extern ORKIGE_CORE_DLL const String LIST_MUSIC_INFO;
		//! @brief MSG_STATS: the engine-level allocation counters (see
		//! core_debug/MemoryManager.h - tracked allocation events at the
		//! engine's own seams, not libc totals). FIELD_ALLOC_PER_FRAME is the
		//! last frame's total, FIELD_ALLOC_PEAK the worst frame of the session;
		//! LIST_ALLOC_TAGS / LIST_ALLOC_COUNTS break the last frame down per
		//! subsystem tag. Omitted before the first frame boundary. Additive.
		extern ORKIGE_CORE_DLL const String FIELD_ALLOC_PER_FRAME;
		extern ORKIGE_CORE_DLL const String FIELD_ALLOC_PEAK;
		extern ORKIGE_CORE_DLL const String LIST_ALLOC_TAGS;
		extern ORKIGE_CORE_DLL const String LIST_ALLOC_COUNTS;
		//! @brief MSG_STATS + MSG_PROFILE_DATA: the last frame's wall-clock
		//! duration in milliseconds (measured at the player's frame boundary,
		//! available even when scope timing is disabled). Additive.
		extern ORKIGE_CORE_DLL const String FIELD_FRAME_MS;
		//! @brief MSG_PROFILE_DATA: the flattened scope tree - parallel lists,
		//! LIST_PROFILE_NAMES the scope names (depth-first), LIST_PROFILE_INFO
		//! one flat "depth calls milliseconds maxMilliseconds" per name.
		extern ORKIGE_CORE_DLL const String LIST_PROFILE_NAMES;
		extern ORKIGE_CORE_DLL const String LIST_PROFILE_INFO;
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
