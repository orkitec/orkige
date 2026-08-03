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
	//!    `t.fail(message)`.
	//!  - `__orkige_run(shouldRun)` - the run pass, returning one record per
	//!    executed test (name / status / message / ms).
	//!
	//! FILE:LINE COMES FREE WITHOUT THE `debug` LIBRARY: script chunks load
	//! under their project-relative names, so an assertion raising with
	//! `error(message, level)` gets `chunkname:line:` prepended by Lua itself,
	//! naming the line in the TEST BODY that failed. That is core Lua, not the
	//! denied reflection surface.
	//!
	//! A test declaring a `scene` option is REFUSED honestly (recorded as an
	//! error naming what is missing) rather than silently passing: running a
	//! test against a live scene means yielding across frames, which needs a
	//! coroutine in the sandbox - a security-posture decision that has not been
	//! taken.
	char const * scriptTestPrelude();

	/** @} */
}

#endif //__ScriptTestPrelude_h__3_8_2026__16_00_00__
