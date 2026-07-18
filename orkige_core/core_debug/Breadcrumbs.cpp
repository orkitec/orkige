/**************************************************************
	created:	2026/07/11 at 11:00
	filename: 	Breadcrumbs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debug/Breadcrumbs.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#	include <fcntl.h>
#	include <io.h>
#else
#	include <fcntl.h>
#	include <unistd.h>
#endif

// AddressSanitizer owns the fatal-signal handlers (SIGSEGV etc.) and produces
// its own reports; installing over them would clobber that, so the crash marker
// stands down under ASan. clang exposes __has_feature(address_sanitizer); GCC
// and MSVC define __SANITIZE_ADDRESS__.
#if defined(__has_feature)
#	if __has_feature(address_sanitizer)
#		define ORKIGE_ASAN_BUILD 1
#	endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#	define ORKIGE_ASAN_BUILD 1
#endif
#ifndef ORKIGE_ASAN_BUILD
#	define ORKIGE_ASAN_BUILD 0
#endif

namespace Orkige
{
	IMPL_OSINGLETON(Breadcrumbs);

	namespace
	{
		//! The crash-marker state lives at file scope because a signal handler
		//! can only reach static/global storage. Everything here is prepared at
		//! install time and never mutated afterwards, so the handler reads it
		//! without a lock. Each entry is a WHOLE pre-formatted breadcrumb line
		//! (fixed POD buffer, no std::string), so the handler patches nothing -
		//! it selects the line for the raised signal and write(2)s it.
		constexpr unsigned kMaxCrashSignals = 8;
		struct CrashSignalLine
		{
			int			signo = 0;		//!< the signal this line marks
			char		text[128] = {};	//!< the whole "crash" breadcrumb line
			unsigned	len = 0;		//!< bytes of text to write
		};
		CrashSignalLine			gCrashLines[kMaxCrashSignals];
		unsigned				gCrashLineCount = 0;
		//! the dedicated append fd the handler write(2)s to (-1 = disarmed); a
		//! sig_atomic_t so a store from teardown is seen safely by the handler
		volatile sig_atomic_t	gCrashFd = -1;
		bool					gCrashInstalled = false;

		//! the fatal-signal handler: async-signal-safe throughout. Write the one
		//! pre-formatted line for the raised signal, then restore the default
		//! disposition and re-raise so the OS still generates its crash report /
		//! core dump - the crash is marked, never swallowed.
		extern "C" void orkige_crash_signal_handler(int signo)
		{
			const int fd = static_cast<int>(gCrashFd);
			if (fd >= 0)
			{
				for (unsigned i = 0; i < gCrashLineCount; ++i)
				{
					if (gCrashLines[i].signo == signo)
					{
#if defined(_WIN32)
						(void)::_write(fd, gCrashLines[i].text,
							gCrashLines[i].len);
#else
						const ssize_t written = ::write(fd,
							gCrashLines[i].text, gCrashLines[i].len);
						(void)written;
#endif
						break;
					}
				}
			}
			std::signal(signo, SIG_DFL);
			std::raise(signo);
		}
	}

	const size_t Breadcrumbs::DEFAULT_MAX_ENTRIES = 256;
	const size_t Breadcrumbs::DEFAULT_MAX_MESSAGE = 512;
	const size_t Breadcrumbs::DEFAULT_MAX_FILE_BYTES = 128 * 1024;

	namespace
	{
		//! monotonic seconds since some fixed epoch (steady clock; only deltas
		//! matter, so the epoch itself is irrelevant)
		double monotonicSeconds()
		{
			return std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}
		//---------------------------------------------------------
		//! append a JSON string literal (quoted, escaped) - breadcrumb text may
		//! hold quotes/newlines (log lines, script error messages)
		void appendJsonString(String & out, String const & value)
		{
			out += '"';
			for (char const c : value)
			{
				switch (c)
				{
				case '"':	out += "\\\"";	break;
				case '\\':	out += "\\\\";	break;
				case '\b':	out += "\\b";	break;
				case '\f':	out += "\\f";	break;
				case '\n':	out += "\\n";	break;
				case '\r':	out += "\\r";	break;
				case '\t':	out += "\\t";	break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x",
							static_cast<unsigned>(static_cast<unsigned char>(c)));
						out += buffer;
					}
					else
					{
						out += c;	// UTF-8 bytes pass through
					}
					break;
				}
			}
			out += '"';
		}
	}
	//---------------------------------------------------------
	Breadcrumbs::Breadcrumbs(size_t maxEntries, size_t maxFileBytes)
		: mMaxEntries(maxEntries == 0 ? 1 : maxEntries)
		, mMaxMessage(DEFAULT_MAX_MESSAGE)
		, mMaxFileBytes(maxFileBytes == 0 ? 1 : maxFileBytes)
		, mFileBytes(0)
		, mStartSeconds(monotonicSeconds())
	{
	}
	//---------------------------------------------------------
	Breadcrumbs::~Breadcrumbs()
	{
		// disarm the crash marker before the file goes away: a later crash must
		// not write into a closed (possibly reused) fd. The process normally
		// exits right after this, but a test that constructs/destroys the trail
		// keeps the handler harmless.
		if (gCrashInstalled)
		{
			const int fd = static_cast<int>(gCrashFd);
			gCrashFd = -1;
			for (unsigned i = 0; i < gCrashLineCount; ++i)
			{
				std::signal(gCrashLines[i].signo, SIG_DFL);
			}
			gCrashLineCount = 0;
			gCrashInstalled = false;
			if (fd >= 0)
			{
#if defined(_WIN32)
				::_close(fd);
#else
				::close(fd);
#endif
			}
		}
		if (this->mStream.is_open())
		{
			this->mStream.flush();
			this->mStream.close();
		}
	}
	//---------------------------------------------------------
	void Breadcrumbs::setFile(String const & path)
	{
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		this->mFile = path;
		this->mFileBytes = 0;
		// derive the rotated (previous-session) path: insert ".prev" before the
		// ".jsonl" suffix, else append ".prev"
		const std::string::size_type dot = path.rfind(".jsonl");
		if (dot != std::string::npos && dot + 6 == path.size())
		{
			this->mPrevFile = path.substr(0, dot) + ".prev.jsonl";
		}
		else if (!path.empty())
		{
			this->mPrevFile = path + ".prev";
		}
		else
		{
			this->mPrevFile.clear();
		}
	}
	//---------------------------------------------------------
	void Breadcrumbs::rotate()
	{
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		this->mRing.clear();
		this->mFileBytes = 0;
		if (this->mFile.empty())
		{
			return;
		}
		// move the live file to the previous slot (replacing any older one) so a
		// fresh session's trail never clobbers the crash trail from the last run
		std::remove(this->mPrevFile.c_str());
		std::rename(this->mFile.c_str(), this->mPrevFile.c_str());
		this->openStream();
	}
	//---------------------------------------------------------
	void Breadcrumbs::openStream()
	{
		if (this->mFile.empty())
		{
			return;
		}
		this->mStream.open(this->mFile.c_str(),
			std::ios::binary | std::ios::app);
	}
	//---------------------------------------------------------
	void Breadcrumbs::rewriteFromRing()
	{
		if (this->mFile.empty())
		{
			return;
		}
		if (this->mStream.is_open())
		{
			this->mStream.close();
		}
		// enforce the hard byte cap: drop oldest entries until the ring fits
		// (the in-memory ring shrinks with the file so both stay bounded)
		size_t ringBytes = 0;
		for (String const & line : this->mRing)
		{
			ringBytes += line.size() + 1;
		}
		while (ringBytes > this->mMaxFileBytes && this->mRing.size() > 1)
		{
			ringBytes -= this->mRing.front().size() + 1;
			this->mRing.pop_front();
		}
		std::ofstream fresh(this->mFile.c_str(),
			std::ios::binary | std::ios::trunc);
		size_t bytes = 0;
		if (fresh)
		{
			for (String const & line : this->mRing)
			{
				fresh.write(line.data(),
					static_cast<std::streamsize>(line.size()));
				fresh.put('\n');
				bytes += line.size() + 1;
			}
			fresh.flush();
		}
		this->mFileBytes = bytes;
		// reopen the append stream for subsequent records
		this->openStream();
	}
	//---------------------------------------------------------
	void Breadcrumbs::record(String const & kind, String const & msg,
		std::vector<std::pair<String, String>> const & fields)
	{
		String text = msg;
		if (text.size() > this->mMaxMessage)
		{
			text.resize(this->mMaxMessage);
			text += "...";
		}
		String line = "{\"t\":";
		char timeBuffer[32];
		std::snprintf(timeBuffer, sizeof(timeBuffer), "%.3f",
			monotonicSeconds() - this->mStartSeconds);
		line += timeBuffer;
		line += ",\"kind\":";
		appendJsonString(line, kind);
		line += ",\"msg\":";
		appendJsonString(line, text);
		for (std::pair<String, String> const & field : fields)
		{
			line += ',';
			appendJsonString(line, field.first);
			line += ':';
			appendJsonString(line, field.second);
		}
		line += '}';

		this->mRing.push_back(line);
		while (this->mRing.size() > this->mMaxEntries)
		{
			this->mRing.pop_front();
		}

		if (this->mFile.empty())
		{
			return;	// memory-only ring (editor / no file set)
		}
		if (!this->mStream.is_open())
		{
			this->openStream();
		}
		if (this->mStream.is_open())
		{
			this->mStream.write(line.data(),
				static_cast<std::streamsize>(line.size()));
			this->mStream.put('\n');
			this->mStream.flush();	// crash survivability: on disk before we return
			this->mFileBytes += line.size() + 1;
		}
		// enforce the byte cap by rewriting from the (already bounded) ring - the
		// file then holds exactly the last N entries within the cap
		if (this->mFileBytes > this->mMaxFileBytes)
		{
			this->rewriteFromRing();
		}
	}
	//---------------------------------------------------------
	String Breadcrumbs::contents() const
	{
		String out;
		for (String const & line : this->mRing)
		{
			out += line;
			out += '\n';
		}
		return out;
	}
	//---------------------------------------------------------
	bool Breadcrumbs::loadFile(String const & path, String & outText)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			return false;
		}
		std::ostringstream contents;
		contents << file.rdbuf();
		outText = contents.str();
		return true;
	}
	//---------------------------------------------------------
	bool Breadcrumbs::installCrashHandler()
	{
#if ORKIGE_ASAN_BUILD
		// ASan owns the fatal handlers; leave its reports intact.
		return false;
#else
		if (this->mFile.empty() || gCrashInstalled)
		{
			return false;
		}
		// a dedicated append fd for the handler's write(2). The normal path keeps
		// its ofstream; both append to the same file, and a single write() of one
		// whole line under O_APPEND is atomic, so the two appenders never corrupt
		// each other's lines.
#if defined(_WIN32)
		int fd = -1;
		::_sopen_s(&fd, this->mFile.c_str(),
			_O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY, _SH_DENYNO,
			_S_IREAD | _S_IWRITE);
#else
		const int fd = ::open(this->mFile.c_str(),
			O_WRONLY | O_APPEND | O_CREAT, 0644);
#endif
		if (fd < 0)
		{
			return false;
		}

		// the handled signals. Windows' CRT raises only a subset (no SIGBUS).
		struct SignalName { int signo; const char* name; };
		static const SignalName kSignals[] =
		{
			{ SIGSEGV, "SIGSEGV" },
#if !defined(_WIN32)
			{ SIGBUS,  "SIGBUS"  },
#endif
			{ SIGILL,  "SIGILL"  },
			{ SIGFPE,  "SIGFPE"  },
			{ SIGABRT, "SIGABRT" },
		};

		// pre-format ONE whole crash breadcrumb line per signal, matching the
		// record() JSON-line shape so the same reader/parser handles it. The "t"
		// field is the install-time (boot) elapsed second - async-signal-safety
		// forbids formatting the clock inside the handler, and the ACCURATE
		// moment of death is the last ordinary crumb before this one; this line
		// only marks THAT the run died and to which signal.
		const double elapsed = monotonicSeconds() - this->mStartSeconds;
		gCrashLineCount = 0;
		for (SignalName const & entry : kSignals)
		{
			if (gCrashLineCount >= kMaxCrashSignals)
			{
				break;
			}
			CrashSignalLine & line = gCrashLines[gCrashLineCount];
			const int written = std::snprintf(line.text, sizeof(line.text),
				"{\"t\":%.3f,\"kind\":\"crash\",\"msg\":\"%s\",\"signal\":%d}\n",
				elapsed, entry.name, entry.signo);
			if (written <= 0)
			{
				continue;
			}
			line.signo = entry.signo;
			line.len = static_cast<unsigned>(
				written < static_cast<int>(sizeof(line.text))
					? written : sizeof(line.text) - 1);
			++gCrashLineCount;
		}

		gCrashFd = fd;

#if defined(_WIN32)
		for (unsigned i = 0; i < gCrashLineCount; ++i)
		{
			std::signal(gCrashLines[i].signo, &orkige_crash_signal_handler);
		}
#else
		// sigaction (no SA_RESETHAND): the handler restores the default itself
		// and re-raises. A full sigfillset mask keeps the handler from being
		// interrupted by another fatal signal mid-write.
		struct sigaction action;
		std::memset(&action, 0, sizeof(action));
		action.sa_handler = &orkige_crash_signal_handler;
		sigfillset(&action.sa_mask);
		action.sa_flags = 0;
		for (unsigned i = 0; i < gCrashLineCount; ++i)
		{
			::sigaction(gCrashLines[i].signo, &action, nullptr);
		}
#endif
		gCrashInstalled = true;
		return true;
#endif // ORKIGE_ASAN_BUILD
	}
	//---------------------------------------------------------
	bool Breadcrumbs::lastEntryIsCrash(String const & fileText,
		String & outSignalName)
	{
		// isolate the last non-empty line (skip a trailing newline / whitespace)
		const String::size_type end = fileText.find_last_not_of("\r\n \t");
		if (end == String::npos)
		{
			return false;
		}
		const String::size_type newline = fileText.find_last_of('\n', end);
		const String::size_type start =
			(newline == String::npos) ? 0 : newline + 1;
		const String line = fileText.substr(start, end - start + 1);

		// our own crash line carries the exact "kind":"crash" token
		if (line.find("\"kind\":\"crash\"") == String::npos)
		{
			return false;
		}
		// pull the signal name out of the "msg" field for the caller
		outSignalName.clear();
		const String key = "\"msg\":\"";
		const String::size_type keyPos = line.find(key);
		if (keyPos != String::npos)
		{
			const String::size_type valueStart = keyPos + key.size();
			const String::size_type valueEnd = line.find('"', valueStart);
			if (valueEnd != String::npos)
			{
				outSignalName = line.substr(valueStart, valueEnd - valueStart);
			}
		}
		return true;
	}
}
