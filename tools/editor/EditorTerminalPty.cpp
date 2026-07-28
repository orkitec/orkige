/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalPty.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalPty.cpp - the platform pseudo-terminal backends behind the
// TerminalPty seam. POSIX (openpty + fork/exec, child in its own session so a
// terminate() signals the whole process group) and Windows (ConPTY +
// CreateProcess with the pseudoconsole attribute, wrapped in a Job Object whose
// closure kills the child tree). Everything above this file - the VT screen
// model, the key encoder, the ImGui panel - is OS-agnostic, so this is the
// entire per-OS surface.
//
// The ConPTY APIs (CreatePseudoConsole/ResizePseudoConsole/ClosePseudoConsole)
// are gated in the Windows SDK behind NTDDI_VERSION >= NTDDI_WIN10_RS5, so these
// macros MUST be set before <windows.h> is first seen. This TU therefore opts
// OUT of the editor's precompiled header (which would pull windows.h at a lower
// version first) - see the SKIP_PRECOMPILE_HEADERS in tools/editor/CMakeLists.
#if defined(_WIN32)
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT 0x0A00			// Windows 10
	#endif
	#ifndef NTDDI_VERSION
		#define NTDDI_VERSION 0x0A000006	// NTDDI_WIN10_RS5 - ConPTY
	#endif
#endif

#include "EditorTerminalPty.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
	#include <windows.h>
	#include <cstdlib>
	#include <vector>
#else
	#include <cerrno>
	#include <cstdlib>
	#include <fcntl.h>
	#include <csignal>
	#include <sys/ioctl.h>
	#include <sys/wait.h>
	#include <termios.h>
	#include <unistd.h>
	#include <cstdio>
	#if defined(__APPLE__)
		#include <util.h>
		#include <libproc.h>
	#else
		#include <pty.h>
	#endif
	extern "C" char** environ;
#endif

namespace OrkigeEditor
{
	std::string defaultShell()
	{
#if defined(_WIN32)
		// powershell is the modern default; fall back to ComSpec (cmd.exe)
		return "powershell.exe";
#else
		if (char const* shellEnv = std::getenv("SHELL"))
		{
			if (shellEnv[0] != '\0')
			{
				return shellEnv;
			}
		}
		return "/bin/sh";
#endif
	}

#if !defined(_WIN32)
	namespace
	{
		//! basename of a path (for the login-shell argv[0] "-zsh" convention)
		std::string baseName(std::string const& path)
		{
			const std::size_t slash = path.find_last_of('/');
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		//! the POSIX pty backend: openpty + fork/exec, the child made a session
		//! leader so terminate() can signal the whole process group.
		class PosixPty final : public TerminalPty
		{
		public:
			~PosixPty() override { this->terminate(); }

			bool spawn(TermPtySpec const& spec) override
			{
				const std::string shell =
					spec.shell.empty() ? defaultShell() : spec.shell;
				std::string cwd = spec.cwd;
				if (cwd.empty())
				{
					if (char const* home = std::getenv("HOME"))
					{
						cwd = home;
					}
				}

				// build the child's environment in the PARENT (async-signal
				// safety: no setenv between fork and exec)
				std::vector<std::string> envStrings = this->buildEnv(spec.env);
				std::vector<char*> envp;
				envp.reserve(envStrings.size() + 1);
				for (std::string& entry : envStrings)
				{
					envp.push_back(entry.data());
				}
				envp.push_back(nullptr);

				// argv: login shell gets "-l" so a bundled .app's skinny PATH
				// still resolves user tools (git/claude/...)
				const std::string arg0 =
					spec.loginShell ? ("-" + baseName(shell)) : baseName(shell);
				std::vector<std::string> argStrings;
				argStrings.push_back(arg0);
				if (spec.loginShell)
				{
					argStrings.push_back("-l");
				}
				std::vector<char*> argv;
				for (std::string& a : argStrings)
				{
					argv.push_back(a.data());
				}
				argv.push_back(nullptr);

				struct winsize ws;
				std::memset(&ws, 0, sizeof(ws));
				ws.ws_col = static_cast<unsigned short>(spec.cols > 0 ? spec.cols : 80);
				ws.ws_row = static_cast<unsigned short>(spec.rows > 0 ? spec.rows : 24);

				int master = -1;
				int slave = -1;
				if (openpty(&master, &slave, nullptr, nullptr, &ws) != 0)
				{
					mError = std::string("openpty failed: ") +
						std::strerror(errno);
					return false;
				}

				const pid_t pid = fork();
				if (pid < 0)
				{
					mError = std::string("fork failed: ") + std::strerror(errno);
					::close(master);
					::close(slave);
					return false;
				}
				if (pid == 0)
				{
					// --- child ---
					::close(master);
					setsid();
					ioctl(slave, TIOCSCTTY, 0);
					dup2(slave, STDIN_FILENO);
					dup2(slave, STDOUT_FILENO);
					dup2(slave, STDERR_FILENO);
					if (slave > STDERR_FILENO)
					{
						::close(slave);
					}
					if (!cwd.empty())
					{
						if (chdir(cwd.c_str()) != 0)
						{
							// non-fatal: fall through and start in the inherited cwd
						}
					}
					execve(shell.c_str(), argv.data(), envp.data());
					// exec failed - end the child without running atexit handlers
					_exit(127);
				}

				// --- parent ---
				::close(slave);
				const int flags = fcntl(master, F_GETFL, 0);
				fcntl(master, F_SETFL, flags | O_NONBLOCK);
				mMaster = master;
				mPid = pid;
				mAlive = true;
				return true;
			}

			std::size_t read(char* buffer, std::size_t cap) override
			{
				if (mMaster < 0 || cap == 0)
				{
					return 0;
				}
				const ssize_t n = ::read(mMaster, buffer, cap);
				if (n > 0)
				{
					return static_cast<std::size_t>(n);
				}
				if (n == 0)
				{
					// EOF: the child closed its side
					mAlive = false;
					return 0;
				}
				// n < 0: EAGAIN/EWOULDBLOCK means "nothing pending right now"
				return 0;
			}

			bool write(char const* data, std::size_t len) override
			{
				if (mMaster < 0)
				{
					return false;
				}
				std::size_t off = 0;
				while (off < len)
				{
					const ssize_t n = ::write(mMaster, data + off, len - off);
					if (n > 0)
					{
						off += static_cast<std::size_t>(n);
						continue;
					}
					if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
					{
						// the pty buffer is momentarily full; a v1 terminal
						// drops the remainder rather than block the UI thread
						return true;
					}
					return false;
				}
				return true;
			}

			void resize(int cols, int rows) override
			{
				if (mMaster < 0)
				{
					return;
				}
				struct winsize ws;
				std::memset(&ws, 0, sizeof(ws));
				ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 1);
				ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 1);
				ioctl(mMaster, TIOCSWINSZ, &ws);
			}

			bool isAlive() override
			{
				if (!mAlive || mPid <= 0)
				{
					return false;
				}
				int status = 0;
				const pid_t r = waitpid(mPid, &status, WNOHANG);
				if (r == mPid)
				{
					mAlive = false;
				}
				return mAlive;
			}

			std::string foregroundProcessName() override
			{
				if (mMaster < 0)
				{
					return std::string();
				}
				// the pty's foreground process group; its leader's pid == the
				// pgid, so we name that process
				const pid_t pgrp = tcgetpgrp(mMaster);
				if (pgrp <= 0)
				{
					return std::string();
				}
			#if defined(__APPLE__)
				char name[256];
				name[0] = '\0';
				if (proc_name(pgrp, name, sizeof(name)) > 0)
				{
					return std::string(name);
				}
				return std::string();
			#else
				// Linux: the command name in /proc/<pid>/comm (truncated to 15
				// chars by the kernel, which is fine for a prefix-matched label)
				char path[64];
				std::snprintf(path, sizeof(path), "/proc/%d/comm",
					static_cast<int>(pgrp));
				std::FILE* f = std::fopen(path, "r");
				if (f == nullptr)
				{
					return std::string();
				}
				char buf[256];
				std::string out;
				if (std::fgets(buf, sizeof(buf), f) != nullptr)
				{
					out = buf;
				}
				std::fclose(f);
				while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
				{
					out.pop_back();
				}
				return out;
			#endif
			}

			void terminate() override
			{
				if (mPid > 0)
				{
					// signal the whole process group (the child is its own
					// session leader, so -pid reaches its descendants too)
					::kill(-mPid, SIGHUP);
					::kill(-mPid, SIGKILL);
					int status = 0;
					for (int i = 0; i < 100; ++i)
					{
						const pid_t r = waitpid(mPid, &status, WNOHANG);
						if (r == mPid || (r < 0 && errno == ECHILD))
						{
							break;
						}
						usleep(1000);
					}
					mPid = -1;
				}
				if (mMaster >= 0)
				{
					::close(mMaster);
					mMaster = -1;
				}
				mAlive = false;
			}

			std::string errorMessage() const override { return mError; }

		private:
			//! copy the inherited environment, override TERM/COLORTERM and
			//! apply the caller's extra entries (replacing an existing key)
			std::vector<std::string> buildEnv(
				std::vector<std::pair<std::string, std::string>> const& extra)
			{
				std::vector<std::string> out;
				auto keyOf = [](std::string const& kv)
				{
					const std::size_t eq = kv.find('=');
					return eq == std::string::npos ? kv : kv.substr(0, eq);
				};
				auto setKv = [&](std::string const& key, std::string const& value)
				{
					const std::string entry = key + "=" + value;
					for (std::string& existing : out)
					{
						if (keyOf(existing) == key)
						{
							existing = entry;
							return;
						}
					}
					out.push_back(entry);
				};
				if (environ != nullptr)
				{
					for (char** e = environ; *e != nullptr; ++e)
					{
						out.emplace_back(*e);
					}
				}
				setKv("TERM", "xterm-256color");
				setKv("COLORTERM", "truecolor");
				for (auto const& kv : extra)
				{
					setKv(kv.first, kv.second);
				}
				return out;
			}

			int		mMaster = -1;
			pid_t	mPid = -1;
			bool	mAlive = false;
			std::string	mError;
		};
	}

	std::unique_ptr<TerminalPty> createTerminalPty()
	{
		return std::make_unique<PosixPty>();
	}

#else	// _WIN32

	namespace
	{
		//! the Windows ConPTY backend. CreatePseudoConsole drives a hidden
		//! console the child renders into; we read its output pipe and write its
		//! input pipe. A Job Object with KILL_ON_JOB_CLOSE guarantees the child
		//! tree dies when the panel closes.
		class ConPty final : public TerminalPty
		{
		public:
			~ConPty() override { this->terminate(); }

			bool spawn(TermPtySpec const& spec) override
			{
				// two anonymous pipes: {inRead -> child stdin}, {child stdout ->
				// outRead}. ConPTY owns inRead + outWrite; we keep inWrite +
				// outRead.
				HANDLE inRead = nullptr;
				HANDLE inWrite = nullptr;
				HANDLE outRead = nullptr;
				HANDLE outWrite = nullptr;
				if (!CreatePipe(&inRead, &inWrite, nullptr, 0) ||
					!CreatePipe(&outRead, &outWrite, nullptr, 0))
				{
					mError = "CreatePipe failed";
					return false;
				}

				COORD size;
				size.X = static_cast<SHORT>(spec.cols > 0 ? spec.cols : 80);
				size.Y = static_cast<SHORT>(spec.rows > 0 ? spec.rows : 24);
				HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &mPc);
				// the child inherits its console ends; our copies are no longer
				// needed once handed to ConPTY
				CloseHandle(inRead);
				CloseHandle(outWrite);
				if (FAILED(hr))
				{
					mError = "CreatePseudoConsole failed";
					CloseHandle(inWrite);
					CloseHandle(outRead);
					return false;
				}
				mInWrite = inWrite;
				mOutRead = outRead;

				// a Job Object so closing it kills the whole child tree
				mJob = CreateJobObjectW(nullptr, nullptr);
				if (mJob != nullptr)
				{
					JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
					std::memset(&info, 0, sizeof(info));
					info.BasicLimitInformation.LimitFlags =
						JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
					SetInformationJobObject(mJob,
						JobObjectExtendedLimitInformation, &info, sizeof(info));
				}

				// STARTUPINFOEX carrying the pseudoconsole attribute
				STARTUPINFOEXW si;
				std::memset(&si, 0, sizeof(si));
				si.StartupInfo.cb = sizeof(si);
				SIZE_T attrSize = 0;
				InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
				mAttrBuffer.resize(attrSize);
				si.lpAttributeList =
					reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
						mAttrBuffer.data());
				if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0,
					&attrSize))
				{
					mError = "InitializeProcThreadAttributeList failed";
					return false;
				}
				if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
					PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, mPc, sizeof(mPc),
					nullptr, nullptr))
				{
					mError = "UpdateProcThreadAttribute failed";
					return false;
				}

				// STARTF_USESTDHANDLES with NULL handles: without it, a parent
				// whose OWN std handles are redirected (a CI runner's log pipes,
				// a spawned .app) leaks its raw std handle VALUES into the child
				// - the documented CreateProcess std-handle quirk - and the
				// child's stdio bypasses the pseudoconsole entirely (the shell's
				// banner landed in the parent's log while our ConPTY pipe stayed
				// silent after conhost's 85-byte preamble). Explicit null std
				// handles make the console subsystem bind the child's stdio to
				// its console: the pseudoconsole.
				si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
				si.StartupInfo.hStdInput = nullptr;
				si.StartupInfo.hStdOutput = nullptr;
				si.StartupInfo.hStdError = nullptr;

				const std::string shell =
					spec.shell.empty() ? defaultShell() : spec.shell;
				std::wstring cmdLine = toWide(shell);
				std::wstring cwd = toWide(spec.cwd);
				std::wstring envBlock = buildEnvBlock(spec.env);

				PROCESS_INFORMATION pi;
				std::memset(&pi, 0, sizeof(pi));
				// a mutable command-line buffer for CreateProcessW
				std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
				cmdBuffer.push_back(L'\0');
				const BOOL ok = CreateProcessW(nullptr, cmdBuffer.data(),
					nullptr, nullptr, FALSE,
					EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
					envBlock.empty() ? nullptr :
						static_cast<LPVOID>(&envBlock[0]),
					cwd.empty() ? nullptr : cwd.c_str(),
					&si.StartupInfo, &pi);
				if (!ok)
				{
					mError = "CreateProcess failed";
					return false;
				}
				if (mJob != nullptr)
				{
					AssignProcessToJobObject(mJob, pi.hProcess);
				}
				mProcess = pi.hProcess;
				CloseHandle(pi.hThread);
				mAlive = true;
				return true;
			}

			std::size_t read(char* buffer, std::size_t cap) override
			{
				if (mOutRead == nullptr || cap == 0)
				{
					return 0;
				}
				// non-blocking: only ReadFile when bytes are already queued
				DWORD avail = 0;
				if (!PeekNamedPipe(mOutRead, nullptr, 0, nullptr, &avail,
					nullptr))
				{
					mAlive = false;	// the child closed its console output
					return 0;
				}
				if (avail == 0)
				{
					return 0;
				}
				DWORD want = static_cast<DWORD>(
					cap < static_cast<std::size_t>(avail) ? cap : avail);
				DWORD got = 0;
				if (!ReadFile(mOutRead, buffer, want, &got, nullptr))
				{
					return 0;
				}
				return static_cast<std::size_t>(got);
			}

			bool write(char const* data, std::size_t len) override
			{
				if (mInWrite == nullptr)
				{
					return false;
				}
				DWORD written = 0;
				return WriteFile(mInWrite, data, static_cast<DWORD>(len),
					&written, nullptr) != 0;
			}

			void resize(int cols, int rows) override
			{
				if (mPc != nullptr)
				{
					COORD size;
					size.X = static_cast<SHORT>(cols > 0 ? cols : 1);
					size.Y = static_cast<SHORT>(rows > 0 ? rows : 1);
					ResizePseudoConsole(mPc, size);
				}
			}

			bool isAlive() override
			{
				if (!mAlive || mProcess == nullptr)
				{
					return false;
				}
				if (WaitForSingleObject(mProcess, 0) == WAIT_OBJECT_0)
				{
					mAlive = false;
				}
				return mAlive;
			}

			void terminate() override
			{
				if (mPc != nullptr)
				{
					ClosePseudoConsole(mPc);
					mPc = nullptr;
				}
				if (mJob != nullptr)
				{
					// closing the job kills every process assigned to it
					CloseHandle(mJob);
					mJob = nullptr;
				}
				if (mProcess != nullptr)
				{
					CloseHandle(mProcess);
					mProcess = nullptr;
				}
				if (mInWrite != nullptr)
				{
					CloseHandle(mInWrite);
					mInWrite = nullptr;
				}
				if (mOutRead != nullptr)
				{
					CloseHandle(mOutRead);
					mOutRead = nullptr;
				}
				mAlive = false;
			}

			std::string errorMessage() const override { return mError; }

		private:
			static std::wstring toWide(std::string const& utf8)
			{
				if (utf8.empty())
				{
					return std::wstring();
				}
				const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
					static_cast<int>(utf8.size()), nullptr, 0);
				std::wstring out(static_cast<std::size_t>(n), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
					static_cast<int>(utf8.size()), &out[0], n);
				return out;
			}

			//! a CREATE_UNICODE_ENVIRONMENT block: the inherited environment plus
			//! the caller's extras + TERM, as a double-NUL-terminated KEY=VALUE
			//! list. Empty result => inherit the parent block unchanged.
			static std::wstring buildEnvBlock(
				std::vector<std::pair<std::string, std::string>> const& extra)
			{
				if (extra.empty())
				{
					return std::wstring();
				}
				std::wstring block;
				if (wchar_t* env = GetEnvironmentStringsW())
				{
					for (wchar_t* p = env; *p != L'\0';)
					{
						const std::wstring entry(p);
						block += entry;
						block.push_back(L'\0');
						p += entry.size() + 1;
					}
					FreeEnvironmentStringsW(env);
				}
				for (auto const& kv : extra)
				{
					block += toWide(kv.first) + L"=" + toWide(kv.second);
					block.push_back(L'\0');
				}
				block.push_back(L'\0');	// the terminating empty string
				return block;
			}

			HPCON	mPc = nullptr;
			HANDLE	mJob = nullptr;
			HANDLE	mProcess = nullptr;
			HANDLE	mInWrite = nullptr;
			HANDLE	mOutRead = nullptr;
			std::vector<char>	mAttrBuffer;
			bool	mAlive = false;
			std::string	mError;
		};
	}

	std::unique_ptr<TerminalPty> createTerminalPty()
	{
		return std::make_unique<ConPty>();
	}

#endif	// _WIN32
}
