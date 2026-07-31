/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportProcess.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportProcess_h__31_7_2026__12_00_00__
#define __ExportProcess_h__31_7_2026__12_00_00__

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file ExportProcess.h
//! @brief the platform tools an export cannot avoid shelling out to, behind
//! ONE injectable seam.
//!
//! An export legitimately drives `codesign`, `install_name_tool`, `iconutil`,
//! `cmake` and the two Android packaging scripts - they are the platform's own
//! toolchain, not something the engine can carry. Routing every one of them
//! through a `ProcessRunner` (the `EditorGit::GitRunner` pattern) means the
//! ARGUMENT COMPOSITION - which is where the bugs live, and which is what a
//! wrong signature or a missing entitlement flag actually is - can be asserted
//! headlessly on a machine that holds no certificate and runs no Xcode.
//!
//! The default runner spawns the process for real, merging stderr into stdout
//! so a tool's own complaint reaches the console verbatim.

namespace OrkigeExport
{
	//! @brief the outcome of one spawned command
	struct ProcessResult
	{
		bool			launched = false;	//!< did the process start at all
		int				exitCode = -1;
		Orkige::String	output;				//!< stdout + stderr, merged
	};

	//! @brief run @p arguments (argv[0] is the executable) and capture its
	//! merged output. The seam every platform-tool call goes through.
	typedef std::function<ProcessResult(std::vector<Orkige::String> const &)>
		ProcessRunner;

	//! @brief the real runner: spawn, capture, wait. Never throws; a process
	//! that cannot be spawned comes back with `launched == false`.
	ProcessResult runProcess(std::vector<Orkige::String> const & arguments);

	//! @brief the default `ProcessRunner` (a `runProcess` wrapper) - what
	//! every export uses unless a test injects its own.
	ProcessRunner defaultProcessRunner();

	//! @brief @p arguments rendered as a shell-ish command line, for the
	//! echoed "$ ..." log line. Display only - never re-parsed.
	Orkige::String commandLine(std::vector<Orkige::String> const & arguments);

	//! @brief find @p name on the PATH, or "" when it is not there (the
	//! `which` an option resolver takes)
	Orkige::String findOnPath(Orkige::String const & name);
}

#endif //__ExportProcess_h__31_7_2026__12_00_00__
