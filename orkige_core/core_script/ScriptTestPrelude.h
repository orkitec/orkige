/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestPrelude.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptTestPrelude_h__3_8_2026__16_00_00__
#define __ScriptTestPrelude_h__3_8_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief the engine-owned TEST VOCABULARY, as Lua source.
	//!
	//! It is a C++ STRING CONSTANT rather than a shipped `.lua` file on
	//! purpose: there is no file to install, no path to resolve, no way for a
	//! project to shadow it, a distributed editor carries it inside the binary,
	//! and an `ORKIGE_SCRIPTING=OFF` build simply never loads it (the constant
	//! costs a few kilobytes of rodata and nothing else).
	//!
	//! Loaded into a `*.test.lua` file's OWN sandbox before the file's chunk
	//! runs, it defines:
	//!  - `test(name [, options], fn)` - declares one test; the file's chunk
	//!    running IS the declaration pass.
	//!  - the assertion table handed to each test body as its one argument:
	//!    `t.eq` (recursive deep-equal), `t.near` (float tolerance),
	//!    `t.truthy`, `t.falsy`, `t.isnil`, `t.errors(fn [, contains])`,
	//!    `t.fail(message)`, plus the waits a play-mode body suspends on
	//!    (`t.wait`, `t.waitFrames`, `t.waitUntil`) - the SAME engine-owned
	//!    waits a game script uses.
	//!  - `__orkige_plan(shouldRun)` - the selection pass: which tests the
	//!    filter picked and which of them name a scene.
	//!  - `__orkige_case(index)` - one test as a callable. The host calls it
	//!    directly (no scene, no frames needed) or starts it as a script TASK
	//!    (a play-mode test); either way the body runs under pcall and lands
	//!    the same record, which is what makes the two tiers ONE vocabulary.
	//!  - `__orkige_record(index)` - that record (name / status / message /
	//!    ms), or nil for a test that never got that far.
	//!
	//! FILE:LINE COMES FREE WITHOUT THE `debug` LIBRARY: script chunks load
	//! under their project-relative names, so an assertion raising with
	//! `error(message, level)` gets `chunkname:line:` prepended by Lua itself,
	//! naming the line in the TEST BODY that failed. That is core Lua, not the
	//! denied reflection surface.
	//!
	//! A test declaring a `scene` option is a PLAY-MODE test: it needs a live
	//! world and frames, so it runs as a script task under a runner that has
	//! both. A caller that has neither (the frameless
	//! ScriptRuntime::runTestFile road) refuses it honestly, per test, rather
	//! than silently passing it.
	char const * scriptTestPrelude();

	/** @} */
}

#endif //__ScriptTestPrelude_h__3_8_2026__16_00_00__
