/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalPty.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTerminalPty_h__28_7_2026__12_00_00__
#define __EditorTerminalPty_h__28_7_2026__12_00_00__

//! @file EditorTerminalPty.h
//! @brief the OS pseudo-terminal seam behind the embedded terminal. A single
//! abstract interface (spawn a shell in a pty, non-blocking read/write, resize,
//! liveness, terminate-the-child-tree) with a POSIX implementation (openpty +
//! fork/exec, the child in its own session so terminate() signals the whole
//! process group) and a Windows implementation (ConPTY: CreatePseudoConsole +
//! a Job Object so closing the panel kills the child tree). Desktop/editor-only.
//! The shared panel/screen logic sits ABOVE this seam so the per-OS surface
//! stays as small as the two files can make it.

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace OrkigeEditor
{
	//! what to launch and how. Empty `shell` selects the platform default (the
	//! user's login shell on POSIX, powershell on Windows); empty `cwd` falls
	//! back to the home directory. `env` are EXTRA variables layered onto the
	//! inherited environment (the MCP connection material rides here).
	struct TermPtySpec
	{
		std::string	shell;
		std::string	cwd;
		std::vector<std::pair<std::string, std::string>>	env;
		int			cols = 80;
		int			rows = 24;
		bool		loginShell = true;	//!< POSIX: pass -l so PATH is complete
	};

	//! a live pseudo-terminal hosting one child process.
	class TerminalPty
	{
	public:
		virtual ~TerminalPty() = default;

		//! launch the child in a fresh pty. Returns false (with a reason in
		//! errorMessage()) if the pty or the child could not be created.
		virtual bool spawn(TermPtySpec const& spec) = 0;

		//! read up to `cap` bytes of child output into `buffer`. Non-blocking:
		//! returns the number of bytes read this call (0 when nothing is pending
		//! or the child has closed its end - check isAlive() to disambiguate).
		virtual std::size_t read(char* buffer, std::size_t cap) = 0;

		//! write `len` bytes to the child's input. Returns false on a broken pipe.
		virtual bool write(char const* data, std::size_t len) = 0;
		bool write(std::string const& data)
		{
			return this->write(data.data(), data.size());
		}

		//! tell the pty the visible grid is now cols x rows (SIGWINCH to the app)
		virtual void resize(int cols, int rows) = 0;

		//! true while the child process is still running
		virtual bool isAlive() = 0;

		//! signal and reap the child (its whole process tree) and close the pty.
		//! Idempotent - safe to call more than once and from the destructor.
		virtual void terminate() = 0;

		//! a human-readable reason a spawn/read failed (empty when fine)
		virtual std::string errorMessage() const = 0;
	};

	//! construct a pty backend for this platform. Never null on desktop; the
	//! returned object is not yet spawned.
	std::unique_ptr<TerminalPty> createTerminalPty();

	//! the platform default interactive shell (respects $SHELL on POSIX; the
	//! ComSpec/powershell on Windows). Exposed so the panel can show it.
	std::string defaultShell();
}

#endif //__EditorTerminalPty_h__28_7_2026__12_00_00__
