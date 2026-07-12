/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	PythonToolchainTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the editor's python3 toolchain preflight
	(PythonToolchain, in the UI-independent editor_core layer):
	- the pure version PARSE (parsePythonVersion) across the shapes a
	  `python3 --version` line takes;
	- the pure floor COMPARE (pythonVersionAtLeast);
	- the injectable interpreter seam (pythonExecutable / ORKIGE_PYTHON);
	- the PROBE failure/success paths driven through that seam: a bogus
	  interpreter path yields a clear, non-empty error naming what was probed;
	  a real python3 (present on every build machine, the documented floor)
	  probes OK.
***************************************************************/

#include "PythonToolchain.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace Orkige;

TEST_CASE("parsePythonVersion reads major.minor.patch", "[python]")
{
	// the canonical CPython banner
	PythonVersion v = parsePythonVersion("Python 3.10.4\n");
	REQUIRE(v.valid);
	CHECK(v.major == 3);
	CHECK(v.minor == 10);
	CHECK(v.patch == 4);
}

TEST_CASE("parsePythonVersion tolerates a missing patch", "[python]")
{
	PythonVersion v = parsePythonVersion("Python 3.11");
	REQUIRE(v.valid);
	CHECK(v.major == 3);
	CHECK(v.minor == 11);
	CHECK(v.patch == 0);
}

TEST_CASE("parsePythonVersion tolerates a bare version and whitespace",
	"[python]")
{
	PythonVersion v = parsePythonVersion("  3.12.1  ");
	REQUIRE(v.valid);
	CHECK(v.major == 3);
	CHECK(v.minor == 12);
	CHECK(v.patch == 1);
}

TEST_CASE("parsePythonVersion rejects garbage", "[python]")
{
	CHECK_FALSE(parsePythonVersion("").valid);
	CHECK_FALSE(parsePythonVersion("not a version").valid);
	// a lone integer is not a major.minor
	CHECK_FALSE(parsePythonVersion("Python 3").valid);
}

TEST_CASE("pythonVersionAtLeast compares against the floor", "[python]")
{
	auto v = [](int major, int minor)
	{
		PythonVersion pv;
		pv.major = major;
		pv.minor = minor;
		pv.valid = true;
		return pv;
	};
	// meets / exceeds
	CHECK(pythonVersionAtLeast(v(3, 10), 3, 10));
	CHECK(pythonVersionAtLeast(v(3, 11), 3, 10));
	CHECK(pythonVersionAtLeast(v(4, 0), 3, 10));
	// below the floor
	CHECK_FALSE(pythonVersionAtLeast(v(3, 9), 3, 10));
	CHECK_FALSE(pythonVersionAtLeast(v(2, 7), 3, 10));
	// an invalid version never meets the floor
	CHECK_FALSE(pythonVersionAtLeast(PythonVersion{}, 3, 10));
}

TEST_CASE("pythonExecutable honours the ORKIGE_PYTHON override", "[python]")
{
	// default: python3 on PATH
	::unsetenv("ORKIGE_PYTHON");
	CHECK(pythonExecutable() == String("python3"));

	// a set, non-empty value overrides
	::setenv("ORKIGE_PYTHON", "/opt/custom/python3", 1);
	CHECK(pythonExecutable() == String("/opt/custom/python3"));

	// an empty value falls back to python3 (not the empty string)
	::setenv("ORKIGE_PYTHON", "", 1);
	CHECK(pythonExecutable() == String("python3"));

	::unsetenv("ORKIGE_PYTHON");
}

TEST_CASE("probe fails honestly for a missing interpreter", "[python]")
{
	// the injectable seam: a bogus interpreter path exercises the failure path
	const String bogus = "/nonexistent/orkige/python3-does-not-exist";
	PythonProbeResult result = probePythonToolchainUncached(bogus);
	CHECK_FALSE(result.ok);
	CHECK(result.executable == bogus);
	REQUIRE_FALSE(result.error.empty());
	// the message names what was probed and what is required
	CHECK(result.error.find(bogus) != String::npos);
	CHECK(result.error.find("python3 >= 3.10") != String::npos);
}

TEST_CASE("probe refuses an interpreter with an unparseable version",
	"[python]")
{
	// /bin/echo stands in for a program that runs but does not report a
	// python version: `echo --version` prints "--version", which has no
	// major.minor - the probe refuses it with the honest requirement message.
	// (The too-old numeric branch is covered by the pure pythonVersionAtLeast
	// test above, since a real interpreter cannot be made to report an old
	// version on demand.)
	PythonProbeResult result = probePythonToolchainUncached("/bin/echo");
	CHECK_FALSE(result.ok);
	REQUIRE_FALSE(result.error.empty());
	CHECK(result.error.find("python3 >= 3.10") != String::npos);
}

TEST_CASE("probe succeeds for a real python3 meeting the floor", "[python]")
{
	// every build/CI machine has python3 >= 3.10 (the documented floor); the
	// probe finds it, parses its version and passes
	PythonProbeResult result = probePythonToolchainUncached("python3");
	REQUIRE(result.ok);
	CHECK(result.error.empty());
	REQUIRE(result.version.valid);
	CHECK(pythonVersionAtLeast(result.version, PYTHON_FLOOR_MAJOR,
		PYTHON_FLOOR_MINOR));
}
