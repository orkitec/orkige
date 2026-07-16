// PythonToolchain - a lazy preflight for the editor's python3 subprocess use.
//
// The editor delegates a handful of asset/build transforms to the stdlib-only
// Util/*.py scripts (the SVG import cook, project export, per-project icon
// generation the exporter runs). Every one of those needs a python3 on PATH
// that meets the toolchain floor (3.10). Instead of letting a spawn fail with
// an opaque OS error, the spawn sites route a PREFLIGHT through here first: it
// finds the interpreter, runs `--version`, parses it and compares it to the
// floor, and hands back one honest message naming what was probed, what was
// found (missing / too old with the actual version) and what is required.
//
// The probe is cached per editor run (the interpreter does not change under a
// running editor). The interpreter is `python3` on PATH by default, overridable
// with the ORKIGE_PYTHON environment variable - which is also the injectable
// seam the tests point at a bogus / real interpreter to exercise both paths.
//
// The version parse + compare are pure free functions so they are unit-tested
// without spawning anything. This TU has no UI dependency, so it lives in the
// UI-independent orkige_editor_core library (the EditorScriptTools precedent).
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! the toolchain floor: python3 >= 3.10 (match statements in the Util
	//! scripts, sys.stdlib_module_names in the stdlib lint)
	constexpr int PYTHON_FLOOR_MAJOR = 3;
	constexpr int PYTHON_FLOOR_MINOR = 10;

	//! @brief a parsed interpreter version. `valid` is false when the
	//! `--version` text could not be parsed into major.minor(.patch).
	struct PythonVersion
	{
		int major = 0;
		int minor = 0;
		int patch = 0;
		bool valid = false;
	};

	//! @brief parse a `python3 --version` line ("Python 3.10.4") into a
	//! PythonVersion. Tolerant of surrounding whitespace, a missing patch
	//! component ("Python 3.11") and the historical stderr routing; yields an
	//! invalid PythonVersion for anything without a recognisable major.minor.
	PythonVersion parsePythonVersion(String const& versionText);

	//! @brief does `v` meet the major.minor floor? An invalid version never does.
	bool pythonVersionAtLeast(PythonVersion const& v, int floorMajor,
		int floorMinor);

	//! @brief the interpreter to probe/spawn: the ORKIGE_PYTHON environment
	//! variable when set to a non-empty value, else "python3" (found on PATH).
	//! This is the injectable seam - a test points ORKIGE_PYTHON at a bogus or a
	//! real interpreter to drive the failure / success path.
	String pythonExecutable();

	//! @brief the outcome of a toolchain probe.
	struct PythonProbeResult
	{
		bool ok = false;			//!< interpreter found AND meets the floor
		String executable;			//!< the interpreter that was probed
		PythonVersion version;		//!< parsed version (valid only when found)
		//! a Console-ready message on failure (empty on success): names what was
		//! probed, what was found and what is required
		String error;
	};

	//! @brief run the probe against `executable` WITHOUT the per-run cache:
	//! spawn `<executable> --version`, parse the output and compare to the
	//! major.minor floor. Used by the cached entry point and directly by the
	//! unit tests (which pass a real / bogus interpreter path).
	PythonProbeResult probePythonToolchainUncached(String const& executable,
		int floorMajor = PYTHON_FLOOR_MAJOR, int floorMinor = PYTHON_FLOOR_MINOR);

	//! @brief the platform's well-known interpreter locations tried when the
	//! PATH python3 misses the floor and no ORKIGE_PYTHON override exists (a
	//! Finder/Dock-launched app inherits the bare system PATH on macOS).
	std::vector<String> pythonFallbackCandidates();

	//! @brief the lazy, per-run-cached probe the spawn sites call before their
	//! first python3 use. An explicit ORKIGE_PYTHON is probed verbatim; else
	//! the PATH python3, then pythonFallbackCandidates() - the first
	//! floor-meeting interpreter wins, and total failure reports the PATH
	//! probe's message. Cached for the rest of the run.
	PythonProbeResult const& probePythonToolchain();
}
