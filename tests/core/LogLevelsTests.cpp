/**************************************************************
	created:	2026/07/13 at 09:00
	filename: 	LogLevelsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Unit coverage for the runtime log level table behind the tagged diagnostic
	macros (core_debug/DebugMacros + LogLevels.cpp): per-tag threshold gating,
	the zero-evaluation-when-disabled contract of the stream-style macros, and
	the log.<tag> cvar control seam. Pure core - no renderer, no boot.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <core_debug/DebugMacros.h>
#include <core_debug/CVarManager.h>
#include <core_debug/Breadcrumbs.h>

using Orkige::CVarManager;
using Orkige::CVarType;
using Orkige::String;
using namespace Orkige;

namespace
{
	//! restore a tag to inheriting the default after a case tweaks it, so the
	//! process-wide table does not leak between the singleton-persistent cases
	struct TagGuard
	{
		const char * tag;
		explicit TagGuard(const char * t) : tag(t) {}
		~TagGuard() { logClearTagLevel(tag); }
	};
}

TEST_CASE("log level names parse and round-trip", "[log][unit]")
{
	CHECK(logLevelFromName("error") == LL_ERROR);
	CHECK(logLevelFromName("WARN") == LL_WARN);		// case-insensitive
	CHECK(logLevelFromName("warning") == LL_WARN);	// alias
	CHECK(logLevelFromName("info") == LL_INFO);
	CHECK(logLevelFromName("debug") == LL_DEBUG);
	CHECK(logLevelFromName("off") == LL_OFF);
	CHECK(logLevelFromName("bogus") == -3);			// invalid sentinel
	CHECK(logLevelFromName(nullptr) == -3);

	CHECK(String(logLevelName(LL_ERROR)) == "error");
	CHECK(String(logLevelName(LL_WARN)) == "warn");
	CHECK(String(logLevelName(LL_INFO)) == "info");
	CHECK(String(logLevelName(LL_DEBUG)) == "debug");
}

TEST_CASE("default threshold passes error+warn but not verbose debug", "[log][unit]")
{
	TagGuard guard("t_log_default");
	// with no override a tag inherits the process default; error and warn are on
	// in every build config, the verbose debug detail is off until raised.
	CHECK(logTagEnabled("t_log_default", LL_ERROR));
	CHECK(logTagEnabled("t_log_default", LL_WARN));
	CHECK_FALSE(logTagEnabled("t_log_default", LL_DEBUG));
}

TEST_CASE("per-tag threshold gates independently", "[log][unit]")
{
	TagGuard loud("t_log_loud");
	TagGuard quiet("t_log_quiet");

	logSetTagLevel("t_log_loud", LL_DEBUG);
	logSetTagLevel("t_log_quiet", LL_OFF);

	// the loud tag now emits at every severity...
	CHECK(logTagEnabled("t_log_loud", LL_DEBUG));
	CHECK(logTagEnabled("t_log_loud", LL_ERROR));
	// ...while the silenced tag emits nothing, not even errors...
	CHECK_FALSE(logTagEnabled("t_log_quiet", LL_ERROR));
	// ...and a third, untouched tag still follows the default.
	CHECK(logTagEnabled("t_log_other", LL_ERROR));
	CHECK_FALSE(logTagEnabled("t_log_other", LL_DEBUG));

	// clearing the override restores the default behaviour
	logClearTagLevel("t_log_loud");
	CHECK_FALSE(logTagEnabled("t_log_loud", LL_DEBUG));
}

TEST_CASE("disabled macro does not evaluate its message arguments", "[log][unit]")
{
	TagGuard guard("t_log_sideeffect");

	// the tag is at the default, so an LL_DEBUG oDebugMsg is OFF: the stream
	// expression - which increments the probe - must never run.
	int probe = 0;
	oDebugMsg("t_log_sideeffect", 0, "value=" << (++probe));
	CHECK(probe == 0);	// argument NOT evaluated on the disabled path

	// raise the tag to debug: now the same call fires and evaluates the stream.
	logSetTagLevel("t_log_sideeffect", LL_DEBUG);
	oDebugMsg("t_log_sideeffect", 0, "value=" << (++probe));
	CHECK(probe == 1);	// argument evaluated exactly once on the enabled path
}

TEST_CASE("oDebugWarning only fires and evaluates when the condition is false", "[log][unit]")
{
	int probe = 0;
	// condition true -> no warning, message untouched (warn is on by default,
	// so only the condition guards the evaluation here).
	oDebugWarning(true, "should not build: " << (++probe));
	CHECK(probe == 0);
	// condition false -> warning fires (default threshold includes warn).
	oDebugWarning(false, "fires: " << (++probe));
	CHECK(probe == 1);
}

TEST_CASE("an emitted error drops a crash breadcrumb", "[log][unit]")
{
	Orkige::Breadcrumbs crumbs(8);	// live singleton for the scope of this case
	const size_t before = crumbs.count();

	// an error is on at the default threshold: emitting one records a breadcrumb
	oDebugError("t_log_bc", 0, "boom: " << 42);
	CHECK(crumbs.count() == before + 1);

	// a debug message (off by default) neither logs nor leaves a crumb
	oDebugMsg("t_log_bc", 0, "quiet");
	CHECK(crumbs.count() == before + 1);
}

TEST_CASE("log.<tag> cvar raises a tag's verbosity live", "[log][cvar][unit]")
{
	TagGuard guard("render");
	CVarManager & cvars = CVarManager::getSingleton();

	// the log.<tag> cvars are installed at startup (LogLevels bootstrap)
	REQUIRE(cvars.exists("log.render"));
	REQUIRE(cvars.exists("log.default"));

	// default: render inherits the process default -> debug is off
	CHECK_FALSE(logTagEnabled("render", LL_DEBUG));

	// set_cvar log.render debug -> the table's render threshold rises live
	String err;
	REQUIRE(cvars.setString("log.render", "debug", &err));
	CHECK(logTagEnabled("render", LL_DEBUG));

	// silence it entirely, then hand back to the default via the empty string
	REQUIRE(cvars.setString("log.render", "off", &err));
	CHECK_FALSE(logTagEnabled("render", LL_ERROR));
	REQUIRE(cvars.setString("log.render", "", &err));
	CHECK(logTagEnabled("render", LL_ERROR));		// inherits default again
	CHECK_FALSE(logTagEnabled("render", LL_DEBUG));
}

TEST_CASE("log extra sink receives gated lines and clears safely", "[log][unit]")
{
	const char * tag = "test.sink";
	TagGuard guard(tag);
	struct Captured { int level; std::string tag; std::string message; };
	std::vector<Captured> lines;
	logSetSink([&lines](int level, const char * t, const char * m)
	{
		lines.push_back({ level, t ? t : "", m ? m : "" });
	});

	// every level reaches the sink while the tag is fully verbose, tag+message
	// arrive intact, and the sink runs on the emitting call (no async)
	logSetTagLevel(tag, LL_DEBUG);
	oDebugError(tag, 0, "an error line");
	oDebugMsg(tag, 0, "a debug line");
	REQUIRE(lines.size() == 2);
	CHECK(lines[0].level == LL_ERROR);
	CHECK(lines[0].tag == String(tag));
	CHECK(lines[0].message == "an error line");
	CHECK(lines[1].level == LL_DEBUG);
	CHECK(lines[1].message == "a debug line");

	// the sink fires AFTER the gate: a line quieter than the threshold never
	// reaches it (same fast-reject contract as stderr)
	lines.clear();
	logSetTagLevel(tag, LL_ERROR);
	oDebugMsg(tag, 0, "a suppressed debug line");	// debug < error -> gated
	oDebugError(tag, 0, "a passing error line");
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].message == "a passing error line");

	// after clearing, no further line reaches the (now-gone) sink - the
	// unregister is explicit and complete, so a captured reference can never
	// dangle past the consumer's lifetime (the guard mutex also blocks a clear
	// until any in-flight call returns)
	logClearSink();
	lines.clear();
	logSetTagLevel(tag, LL_DEBUG);
	oDebugError(tag, 0, "after clear");
	CHECK(lines.empty());
}
