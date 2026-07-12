/**************************************************************
	created:	2026/07/13 at 09:00
	filename: 	LogLevels.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The runtime log level table behind the tagged diagnostic macros
	(oDebugMsg/oDebugWarning/oDebugError). A tiny, singleton-independent,
	thread-safe registry of per-tag severity thresholds with a lock-light
	fast-reject gate, plus the emit path (stderr + the LogManager file sink +
	a crash breadcrumb on errors) and the log.<tag> cvar control seam.
***************************************************************/

#include "core_debug/DebugMacros.h"
#include "core_debug/LogManager.h"
#include "core_debug/Breadcrumbs.h"
#include "core_debug/CVarManager.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace Orkige
{
	namespace
	{
		//! the process default: error+warn everywhere, info additionally in a
		//! Debug build; the verbose LL_DEBUG detail stays off until raised.
#if defined(NDEBUG)
		const int kBuildDefaultLevel = LL_WARN;
#else
		const int kBuildDefaultLevel = LL_INFO;
#endif

		std::mutex									gLogMutex;
		int											gLogDefault = kBuildDefaultLevel;
		std::unordered_map<std::string, int>		gLogTags;	//!< per-tag overrides
		//! the loudest active threshold across the default and every override -
		//! a message strictly quieter than this is rejected without taking a lock
		std::atomic<int>							gLogMax{ kBuildDefaultLevel };

		//! recompute the fast-reject ceiling (call under gLogMutex)
		void recomputeMax()
		{
			int m = gLogDefault;
			for (auto const & kv : gLogTags)
			{
				m = std::max(m, kv.second);
			}
			gLogMax.store(m, std::memory_order_relaxed);
		}

		//! the file-name tail (drops directory prefixes for a compact log line)
		const char * baseName(const char * path)
		{
			if (!path)
			{
				return "";
			}
			const char * best = path;
			for (const char * p = path; *p; ++p)
			{
				if (*p == '/' || *p == '\\')
				{
					best = p + 1;
				}
			}
			return best;
		}
	}

	//---------------------------------------------------------------
	bool logTagEnabled(const char* tag, int level)
	{
		// fast reject: nothing is louder than gLogMax, so a message quieter than
		// it can never pass any threshold - bail before locking (the hot path for
		// the verbose LL_DEBUG calls, which are off by default).
		if (level > gLogMax.load(std::memory_order_relaxed))
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(gLogMutex);
		int threshold = gLogDefault;
		auto it = gLogTags.find(tag ? tag : "");
		if (it != gLogTags.end())
		{
			threshold = it->second;
		}
		return level <= threshold;
	}
	//---------------------------------------------------------------
	void logEmit(const char* tag, int level, std::string const & message,
		const char* fileName, int lineNumber)
	{
		const char * levelName = logLevelName(level);
		const char * safeTag = tag ? tag : "";

		// stderr - the developer channel the tests grep
		std::fprintf(stderr, "[%s][%s] %s (%s:%d)\n",
			levelName, safeTag, message.c_str(),
			baseName(fileName), lineNumber);

		// the configured LogManager file sink (dormant until a file is set)
		if (LogManager::getSingletonPtr())
		{
			LogManager::getSingleton().appendFileLine(
				safeTag, level, message.c_str(), fileName, lineNumber);
		}

		// crash visibility: an error joins the always-flushed breadcrumb trail
		if (level == LL_ERROR && Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("error",
				String(safeTag) + ": " + message);
		}
	}
	//---------------------------------------------------------------
	void logSetTagLevel(const char* tag, int level)
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		gLogTags[tag ? tag : ""] = level;
		recomputeMax();
	}
	//---------------------------------------------------------------
	void logClearTagLevel(const char* tag)
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		gLogTags.erase(tag ? tag : "");
		recomputeMax();
	}
	//---------------------------------------------------------------
	int logGetTagLevel(const char* tag)
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		auto it = gLogTags.find(tag ? tag : "");
		return it != gLogTags.end() ? it->second : -2;	//!< -2 = inherits default
	}
	//---------------------------------------------------------------
	void logSetDefaultLevel(int level)
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		gLogDefault = level;
		recomputeMax();
	}
	//---------------------------------------------------------------
	int logGetDefaultLevel()
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		return gLogDefault;
	}
	//---------------------------------------------------------------
	int logLevelFromName(const char* name)
	{
		if (!name)
		{
			return -3;
		}
		// case-insensitive compare against the canonical names
		std::string lower(name);
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower == "error") return LL_ERROR;
		if (lower == "warn" || lower == "warning") return LL_WARN;
		if (lower == "info") return LL_INFO;
		if (lower == "debug") return LL_DEBUG;
		if (lower == "off" || lower == "none") return LL_OFF;
		return -3;	//!< invalid
	}
	//---------------------------------------------------------------
	const char* logLevelName(int level)
	{
		switch (level)
		{
		case LL_ERROR:	return "error";
		case LL_WARN:	return "warn";
		case LL_INFO:	return "info";
		case LL_DEBUG:	return "debug";
		case LL_OFF:	return "off";
		default:		return "?";
		}
	}
	//---------------------------------------------------------------
	namespace
	{
		//! the tags the engine emits under today - one log.<tag> cvar each, plus a
		//! superset of near-future tags so `set_cvar log.render debug` just works.
		const char * const kKnownLogTags[] = {
			"engine", "render", "sound", "physic", "scene", "core", "game",
			"gameobject", "editor", "serialize", "script", "resource",
			"filesystem", "eventmanager", "loc", "gui", "asset"
		};

		//! apply a log cvar's string value ("" = inherit) to the table
		void applyTagCVar(const char* tag, String const & value)
		{
			if (value.empty())
			{
				logClearTagLevel(tag);
				return;
			}
			int level = logLevelFromName(value.c_str());
			if (level != -3)
			{
				logSetTagLevel(tag, level);
			}
		}
	}
	//---------------------------------------------------------------
	void logInstallCVars()
	{
		CVarManager & cvars = CVarManager::getSingleton();

		// log.default - the base threshold every tag without an override inherits
		cvars.registerCVar("log.default", CVarType::String,
			logLevelName(logGetDefaultLevel()), CVAR_PERSIST,
			"default log threshold (error/warn/info/debug/off)",
			[](CVar const & cv)
			{
				int level = logLevelFromName(cv.asString().c_str());
				if (level != -3 && level != LL_OFF)
				{
					logSetDefaultLevel(level);
				}
			});

		// log.<tag> - a per-tag override; empty string means "inherit the default"
		for (const char * tag : kKnownLogTags)
		{
			cvars.registerCVar(String("log.") + tag, CVarType::String,
				"", CVAR_PERSIST,
				String("log threshold for the '") + tag +
					"' tag (error/warn/info/debug/off; empty = inherit)",
				[tag](CVar const & cv)
				{
					applyTagCVar(tag, cv.asString());
				});
		}
	}
	//---------------------------------------------------------------
	namespace
	{
		//! install the cvars at startup so set_cvar/manifest overrides resolve
		//! them without any explicit boot wiring (like the OCVAR_* static inits)
		struct LogCVarBootstrap
		{
			LogCVarBootstrap() { logInstallCVars(); }
		};
		LogCVarBootstrap gLogCVarBootstrap;
	}
	//---------------------------------------------------------------
}
