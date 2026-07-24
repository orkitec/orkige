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
		//! @brief gui hot-reload: the editor tells the RUNNING player to
		//! reload one declarative `.oui` screen live. FIELD_PATH carries the
		//! .oui name the game passed to GuiFactory::loadLayout (a basename, e.g.
		//! "hud.oui"). The player re-reads the fresh file and, on a clean parse,
		//! DESTROYS that screen's widgets and rebuilds them (clean cutover, no
		//! state merge); a parse failure keeps the OLD screen and answers with an
		//! error. On a successful rebuild the player emits the `ui.reloaded`
		//! script-event so scripts re-acquire their widget handles. Player-
		//! directed like MSG_RELOAD_SCRIPT (the editor's .oui watcher only sends
		//! this; the swap happens on the player at its message-drain point, never
		//! mid-frame). An additive protocol-extension message that rides the ONE
		//! debug protocol; old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_RELOAD_UI;
		//! @brief vector-animation hot-reload: the editor tells the RUNNING
		//! player to re-read one `.oanim` rig live. FIELD_PATH carries the rig's
		//! resource name (a basename, e.g. "hero.oanim" - the value a
		//! VectorAnimationComponent's `animation` reference holds). The player
		//! parses the fresh file FIRST and, on a clean parse, rebuilds every
		//! VectorAnimationComponent playing that rig (clean cutover - playback
		//! restarts at each component's reflected clip, no state merge); a parse
		//! failure keeps every OLD rig and answers with an error carrying the
		//! line and reason. On a successful rebuild the player emits the
		//! `animation.reloaded` script-event so scripts re-drive their clips.
		//! Player-directed like MSG_RELOAD_UI (the editor's animation watcher -
		//! which re-cooks a changed Lottie source before sending - and the MCP
		//! reload_anim verb only send this; the swap happens on the player at
		//! its message-drain point, never mid-frame). An additive
		//! protocol-extension message that rides the ONE debug protocol; old
		//! players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_RELOAD_ANIM;
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
		//! @brief replace the runtime's script-breakpoint set (the editor's
		//! script debugger): LIST_BREAKPOINTS carries the WHOLE set as
		//! "<file>:<line>" entries (project-relative script path, 1-based
		//! line) - a full-list replace, so clearing one breakpoint just sends
		//! the remaining set and an empty list clears everything. The player
		//! applies it through the ScriptRuntime debug seam, which installs a
		//! script line hook only while the set is non-empty (zero overhead
		//! otherwise). Refused honestly (an error reply) where the runtime
		//! cannot block at a break: the browser player and scripting-disabled
		//! builds. An additive protocol-extension message riding the ONE debug
		//! protocol; old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_BREAKPOINTS;
		//! @brief release a held script break and continue freely (@see
		//! MSG_DEBUG_BREAK). Only meaningful while the runtime is paused at a
		//! break; ignored otherwise. The player answers MSG_DEBUG_RESUMED.
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_RESUME;
		//! release a held break and pause again at the very next executed
		//! script line, stepping INTO calls (@see MSG_DEBUG_RESUME)
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_STEP_IN;
		//! release a held break and pause at the next line in the SAME (or a
		//! shallower) function - calls run through (@see MSG_DEBUG_RESUME)
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_STEP_OUT;
		//! release a held break and pause once the current function returned
		//! (@see MSG_DEBUG_RESUME)
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_STEP_OVER;
		//! @brief read variables at a frame of the held break: FIELD_FRAME is
		//! the stack-frame index (0 = innermost, matching MSG_DEBUG_BREAK's
		//! stack lists); an optional LIST_EXPAND_PATH names a root variable
		//! plus a chain of table keys to list ONE nested table (bounded - the
		//! explicit-expand contract, never a whole-state dump). The player
		//! echoes the request fields and answers with the same message type
		//! carrying the parallel LIST_VAR_* lists; outside a break it answers
		//! an error. Editor->player it is the request, player->editor the
		//! reply.
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_LOCALS;
		//! @brief ask the runtime to DESCRIBE runtime-spawned objects: LIST_IDS
		//! names the GameObject ids the editor's scene mirror could not match
		//! against its authored document (they exist only in the running game -
		//! script/native-spawned). The runtime answers with MSG_SCENE_SPAWNS
		//! descriptors for every requested id it still holds (an id that
		//! already died again is simply omitted; the hierarchy stream has
		//! dropped it anyway). The EDITOR asks (rather than the player pushing
		//! unrequested) because only the matcher knows which ids its document
		//! cannot resolve - the runtime has no notion of what the editor's
		//! authored world holds. Idempotent and self-limiting: the editor asks
		//! once per unmatched id. An additive protocol-extension message riding
		//! the ONE debug protocol; old players answer "unknown command".
		extern ORKIGE_CORE_DLL const String MSG_QUERY_SPAWNS;

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
		//! @brief periodic FULL-SCENE transform stream: every GameObject's
		//! LOCAL transform (position/orientation/scale) as two parallel lists
		//! LIST_IDS / LIST_TRANSFORMS (each transform a flat
		//! "px py pz qw qx qy qz sx sy sz" string). Unlike MSG_OBJECT_STATE
		//! (which streams only the SELECTED object's reflected properties), this
		//! covers the WHOLE scene so an editor can MIRROR the running game's
		//! object motion into its own Scene view live and restore the authored
		//! poses on stop. Streamed as a DELTA - only the objects whose local
		//! transform changed since the last send ride each message (a full set
		//! goes out on the first send after a client attaches / a mid-play scene
		//! switch), so a settled or paused scene sends nothing. Fire-and-forget
		//! on the ~15Hz object-state cadence; a paused game freezes it (nothing
		//! moves), a step advances it by exactly the tick's motion. Additive
		//! since protocol v1: old editors ignore the unknown type. The active/
		//! visibility half of a mirror rides the EXISTING MSG_HIERARCHY
		//! LIST_ACTIVE + LIST_PARENTS (no new field).
		//! @remarks No new MCP verb: this stream feeds the editor's own Scene
		//! view (a visual mirror), and an agent already reads any object's live
		//! runtime transform through MSG_SELECT + MSG_OBJECT_STATE (the
		//! runtime_* / get_state surface). The whole-scene delta exists only so
		//! the editor can move ALL its authored nodes at once, cheaply.
		extern ORKIGE_CORE_DLL const String MSG_SCENE_TRANSFORMS;
		//! @brief a MID-PLAY SCENE SWITCH happened (the deferred level load:
		//! world.loadScene / LevelManager): the previous world was torn down
		//! and FIELD_SCENE now runs (project-relative when the runtime plays a
		//! project, so an editor resolves it against ITS copy of the same
		//! project; the load path otherwise). Sent from the reload point, so it
		//! always PRECEDES the new scene's hierarchy/transform streams - an
		//! editor mirroring the running game swaps its Scene view to a
		//! view-only load of that scene file before the new ids arrive. A
		//! client that attaches AFTER a switch learns the current scene from
		//! MSG_HELLO's FIELD_SCENE instead (which always carries the scene the
		//! runtime is on). Additive since protocol v1: old editors ignore the
		//! unknown type.
		extern ORKIGE_CORE_DLL const String MSG_SCENE_LOADED;
		//! @brief the answer to MSG_QUERY_SPAWNS: enough VISUAL identity per
		//! runtime-spawned object for an editor to instantiate a lightweight
		//! mirror stand-in. Parallel per-object lists LIST_IDS / LIST_PARENTS /
		//! LIST_COMPONENTS (per id: its space-separated component type names -
		//! type/kind names never contain spaces), plus a flat per-PROPERTY
		//! record across four parallel lists: LIST_SPAWN_OBJECTS (the owning
		//! object as a decimal INDEX into LIST_IDS - ids may contain spaces,
		//! an index cannot), LIST_PROP_KEYS ("<Component>.<property>"),
		//! LIST_SPAWN_KINDS (PropertyKind as int), LIST_SPAWN_VALUES (the
		//! canonical value string, isolated in its own list so no value ever
		//! needs escaping) and LIST_SPAWN_REFS (the AssetRef resolving id, ""
		//! otherwise). The records are exactly what the reflected
		//! property-capture used for prefab baselines produces, so both ends
		//! speak the ONE property registry's dialect. Batched: a reply may
		//! split across several messages (each internally consistent) to stay
		//! under the transport line cap. Additive since protocol v1.
		extern ORKIGE_CORE_DLL const String MSG_SCENE_SPAWNS;
		//! @brief script execution PAUSED at a breakpoint / step landing: the
		//! player is blocked inside the script hook (mid-frame - distinct from
		//! the frame-boundary MSG_PAUSE state) and keeps servicing the debug
		//! link from a bounded nested pump. FIELD_PATH + FIELD_LINE carry the
		//! paused location (project-relative script path, 1-based line);
		//! LIST_STACK_SOURCES / LIST_STACK_LINES / LIST_STACK_FUNCTIONS the
		//! call stack innermost-first (a host-call frame reads "[host]").
		//! Released by MSG_DEBUG_RESUME / the step commands; a vanished client
		//! auto-resumes (the runtime must never stay wedged in a break).
		//! Additive since protocol v1: old editors ignore unknown types.
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_BREAK;
		//! @brief the break released (a resume or step command was applied and
		//! execution continues; a step that lands raises a fresh
		//! MSG_DEBUG_BREAK). Additive since protocol v1.
		extern ORKIGE_CORE_DLL const String MSG_DEBUG_RESUMED;
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
		//! @brief MSG_STATS: the game's current named state (core_game/GameState,
		//! Lua `game.setState`); "" / omitted when the game never set one.
		extern ORKIGE_CORE_DLL const String FIELD_GAME_STATE;
		//! @brief MSG_PROFILE_DATA: the flattened scope tree - parallel lists,
		//! LIST_PROFILE_NAMES the scope names (depth-first), LIST_PROFILE_INFO
		//! one flat "depth calls milliseconds maxMilliseconds" per name.
		extern ORKIGE_CORE_DLL const String LIST_PROFILE_NAMES;
		extern ORKIGE_CORE_DLL const String LIST_PROFILE_INFO;
		//! @brief MSG_SCENE_TRANSFORMS: one flat "px py pz qw qx qy qz sx sy sz"
		//! local-transform string per id in LIST_IDS (10 space-separated floats:
		//! position, orientation quaternion w/x/y/z, scale). Kept a parallel
		//! string list beside LIST_IDS so the message stays flat (no nested
		//! objects), like the ui-rect / profile-info lists.
		extern ORKIGE_CORE_DLL const String LIST_TRANSFORMS;
		//! @brief MSG_SCENE_SPAWNS per-property record lists (parallel to each
		//! other, NOT to LIST_IDS): the owning object as a decimal index into
		//! LIST_IDS, the PropertyKind int, the canonical value string and the
		//! AssetRef resolving id ("" for every other kind). The property KEY
		//! rides the existing LIST_PROP_KEYS ("<Component>.<property>").
		extern ORKIGE_CORE_DLL const String LIST_SPAWN_OBJECTS;
		extern ORKIGE_CORE_DLL const String LIST_SPAWN_KINDS;
		extern ORKIGE_CORE_DLL const String LIST_SPAWN_VALUES;
		extern ORKIGE_CORE_DLL const String LIST_SPAWN_REFS;
		//! MSG_DEBUG_BREAK: the paused 1-based line (FIELD_PATH is the file)
		extern ORKIGE_CORE_DLL const String FIELD_LINE;
		//! MSG_DEBUG_LOCALS: the stack-frame index (0 = innermost)
		extern ORKIGE_CORE_DLL const String FIELD_FRAME;
		//! MSG_DEBUG_BREAKPOINTS: the whole breakpoint set, one
		//! "<file>:<line>" entry per element (full-list replace)
		extern ORKIGE_CORE_DLL const String LIST_BREAKPOINTS;
		//! @brief MSG_DEBUG_BREAK: the paused call stack as three parallel
		//! lists, innermost first - the normalized chunk path per frame (or
		//! "[host]" for a host-call frame), the 1-based current line per frame
		//! and the best-effort function name per frame ("" = anonymous).
		extern ORKIGE_CORE_DLL const String LIST_STACK_SOURCES;
		extern ORKIGE_CORE_DLL const String LIST_STACK_LINES;
		extern ORKIGE_CORE_DLL const String LIST_STACK_FUNCTIONS;
		//! MSG_DEBUG_LOCALS request: root variable name + table-key chain
		//! ("[3]" bracket form for non-string keys) selecting ONE nested table
		extern ORKIGE_CORE_DLL const String LIST_EXPAND_PATH;
		//! @brief MSG_DEBUG_LOCALS reply: the variable rows as five parallel
		//! lists - name, scope ("local"/"upvalue"/"field"), type name, bounded
		//! display value and the "1"/"0" may-expand flag (a table whose fields
		//! a follow-up LIST_EXPAND_PATH request can list).
		extern ORKIGE_CORE_DLL const String LIST_VAR_NAMES;
		extern ORKIGE_CORE_DLL const String LIST_VAR_SCOPES;
		extern ORKIGE_CORE_DLL const String LIST_VAR_TYPES;
		extern ORKIGE_CORE_DLL const String LIST_VAR_VALUES;
		extern ORKIGE_CORE_DLL const String LIST_VAR_EXPANDABLE;
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
