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

#include "EditorTerminalSession.h"	// TerminalInputQueue (the pure input queue)

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

		//! write `len` bytes to the child's input. Never blocks and never DROPS:
		//! a terminal accepts only about a kilobyte of pending input at a time,
		//! so whatever the child cannot take right now is queued in order and
		//! handed over by flushPendingWrites() as it drains. Returns false on a
		//! broken pipe or when the queue is at capacity (an honest refusal - a
		//! silently truncated paste strands the app mid-sequence, and a bracketed
		//! paste missing its closing marker swallows every later keystroke, the
		//! interrupt included).
		bool write(char const* data, std::size_t len);
		bool write(std::string const& data)
		{
			return this->write(data.data(), data.size());
		}

		//! hand any queued input the child could not take earlier over now.
		//! Called every frame beside the output drain. Returns false on a broken
		//! pipe.
		bool flushPendingWrites();

		//! how many written bytes are still waiting for the child (0 = all sent)
		std::size_t pendingWriteBytes() const { return mInput.pending(); }

		//! tell the pty the visible grid is now cols x rows (SIGWINCH to the app)
		virtual void resize(int cols, int rows) = 0;

		//! true while the child process is still running
		virtual bool isAlive() = 0;

		//! the name of the pty's FOREGROUND process - what the shell currently
		//! has in the foreground (the running `claude`/`vim`/`git`, else the
		//! shell itself). POSIX reads tcgetpgrp() then the group leader's process
		//! name (macOS libproc, Linux /proc/<pid>/comm); Windows returns empty
		//! (no dependency-free console-process query - the VT title covers the
		//! agent TUIs there). Empty when unavailable. A low-cadence poll input to
		//! the app-aware tab title, NOT a per-frame call.
		virtual std::string foregroundProcessName() { return std::string(); }

		//! signal and reap the child (its whole process tree) and close the pty.
		//! Idempotent - safe to call more than once and from the destructor.
		virtual void terminate() = 0;

		//! a human-readable reason a spawn/read failed (empty when fine)
		virtual std::string errorMessage() const = 0;

	protected:
		//! hand as many of `len` bytes as the child's input accepts RIGHT NOW,
		//! without blocking. Returns the number accepted (0 when full) or -1 on
		//! a broken pipe. The one per-OS write primitive; the queue above it is
		//! shared.
		virtual std::ptrdiff_t writeSome(char const* data, std::size_t len) = 0;

		//! forget queued input (a torn-down child will never read it)
		void discardPendingWrites() { mInput.clear(); }

	private:
		//! the queue's transport trampoline into writeSome() of this instance
		static std::ptrdiff_t queueSink(void* context, char const* data,
			std::size_t len);

		TerminalInputQueue	mInput;
	};

	//! construct a pty backend for this platform. Never null on desktop; the
	//! returned object is not yet spawned.
	std::unique_ptr<TerminalPty> createTerminalPty();

	//! the platform default interactive shell (respects $SHELL on POSIX; the
	//! ComSpec/powershell on Windows). Exposed so the panel can show it.
	std::string defaultShell();
}

#endif //__EditorTerminalPty_h__28_7_2026__12_00_00__
