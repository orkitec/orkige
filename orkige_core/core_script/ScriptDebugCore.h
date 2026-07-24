/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	ScriptDebugCore.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptDebugCore_h__24_7_2026__10_00_00__
#define __ScriptDebugCore_h__24_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <set>
#include <vector>

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief one script breakpoint: a PROJECT-RELATIVE script path (forward
	//! slashes, e.g. "scripts/player.lua") plus a 1-based line number - the
	//! same key the editor persists per project and the debug protocol carries
	//! as "<file>:<line>". Backend-neutral.
	struct ScriptBreakpoint
	{
		String	file;		//!< project-relative script path
		int		line = 0;	//!< 1-based line number

		ScriptBreakpoint() {}
		ScriptBreakpoint(String const & breakFile, int breakLine)
			: file(breakFile), line(breakLine) {}
	};

	//! @brief how a paused script should advance on the next debug command.
	//! None = free run (until the next breakpoint); In = pause at the very next
	//! executed line, wherever it is; Over = pause at the next line at the SAME
	//! or a SHALLOWER call depth (calls run through); Out = pause once the
	//! current function returned (first line at a shallower depth).
	enum class ScriptStepMode
	{
		None = 0,
		In,
		Over,
		Out
	};

	//! @brief one captured call-stack frame at a script break. `source` is the
	//! normalized chunk path (project-relative for engine-loaded scripts),
	//! `line` the 1-based current line, `function` the best-effort function
	//! name ("" for the main chunk / anonymous functions) and `isScript`
	//! whether the frame executes script code (a host-call frame is kept for
	//! context but carries no line/locals).
	struct ScriptStackFrame
	{
		String	source;
		int		line = 0;
		String	function;
		bool	isScript = true;
	};

	//! @brief one variable row read from a paused frame: the declared name,
	//! its scope ("local"/"upvalue"/"field"), the value's type name, a bounded
	//! display string and whether it can be EXPANDED (a table - request its
	//! fields with an explicit expand path, never an unbounded dump).
	struct ScriptDebugVariable
	{
		String	name;
		String	scope;
		String	type;
		String	value;
		bool	expandable = false;
	};

	//! @brief the pure breakpoint/step decision logic behind the script
	//! debugger: chunk-name normalization, breakpoint matching and the
	//! step-mode state machine. Kept free of any scripting backend so the
	//! headless unit tests exercise exactly what the line hook runs.
	namespace ScriptDebugCore
	{
		//! @brief normalize a chunk name the way the debugger compares it:
		//! strip the leading '@' (Lua's file-chunk marker), turn backslashes
		//! into forward slashes. The engine loads every script instance with
		//! its project-relative path as the chunk name, so a normalized chunk
		//! usually IS the breakpoint key already.
		inline String normalizeChunk(String const & chunkName)
		{
			String result = chunkName;
			if (!result.empty() && result[0] == '@')
			{
				result.erase(result.begin());
			}
			for (char & character : result)
			{
				if (character == '\\')
				{
					character = '/';
				}
			}
			return result;
		}

		//! @brief does a (normalized) chunk name refer to the breakpoint's
		//! file? Exact match first; otherwise a whole-path-component suffix
		//! match either way round, so an absolute chunk name still hits a
		//! project-relative breakpoint (and vice versa) without ever matching
		//! a mere basename tail like "xplayer.lua" vs "player.lua".
		inline bool chunkMatchesFile(String const & normalizedChunk,
			String const & breakpointFile)
		{
			if (normalizedChunk.empty() || breakpointFile.empty())
			{
				return false;
			}
			if (normalizedChunk == breakpointFile)
			{
				return true;
			}
			if (normalizedChunk.size() > breakpointFile.size())
			{
				const std::size_t offset =
					normalizedChunk.size() - breakpointFile.size();
				return normalizedChunk[offset - 1] == '/' &&
					normalizedChunk.compare(offset, String::npos,
						breakpointFile) == 0;
			}
			if (breakpointFile.size() > normalizedChunk.size())
			{
				const std::size_t offset =
					breakpointFile.size() - normalizedChunk.size();
				return breakpointFile[offset - 1] == '/' &&
					breakpointFile.compare(offset, String::npos,
						normalizedChunk) == 0;
			}
			return false;
		}

		//! @brief the breakpoint set in hook-friendly form: a cheap line-number
		//! reject (the line hook fires constantly - almost every event must
		//! fail in O(log n) integer work before any string comparison runs)
		//! plus the full entries for the per-file match.
		class BreakpointIndex
		{
		public:
			//! replace the whole set (the protocol's full-list-replace shape)
			void assign(std::vector<ScriptBreakpoint> const & breakpoints)
			{
				this->mEntries = breakpoints;
				this->mLines.clear();
				for (ScriptBreakpoint const & breakpoint : this->mEntries)
				{
					this->mLines.insert(breakpoint.line);
				}
			}
			void clear()
			{
				this->mEntries.clear();
				this->mLines.clear();
			}
			bool empty() const { return this->mEntries.empty(); }
			std::vector<ScriptBreakpoint> const & entries() const
			{
				return this->mEntries;
			}
			//! does any breakpoint sit on this line at all (the fast reject)
			bool anyOnLine(int line) const
			{
				return this->mLines.find(line) != this->mLines.end();
			}
			//! full match: a breakpoint on this line whose file matches the
			//! normalized chunk name
			bool matches(String const & normalizedChunk, int line) const
			{
				if (!this->anyOnLine(line))
				{
					return false;
				}
				for (ScriptBreakpoint const & breakpoint : this->mEntries)
				{
					if (breakpoint.line == line &&
						chunkMatchesFile(normalizedChunk, breakpoint.file))
					{
						return true;
					}
				}
				return false;
			}
		private:
			std::vector<ScriptBreakpoint>	mEntries;
			std::multiset<int>				mLines;
		};

		//! @brief the step-mode decision: should a line event at call depth
		//! `currentDepth` pause execution, given the mode armed at depth
		//! `baseDepth` (the depth where the previous pause released)?
		inline bool stepShouldBreak(ScriptStepMode mode, int baseDepth,
			int currentDepth)
		{
			switch (mode)
			{
			case ScriptStepMode::In:
				return true;
			case ScriptStepMode::Over:
				return currentDepth <= baseDepth;
			case ScriptStepMode::Out:
				return currentDepth < baseDepth;
			case ScriptStepMode::None:
			default:
				return false;
			}
		}

		//! @brief parse one wire breakpoint entry "<file>:<line>" (the debug
		//! protocol's list element and the per-project persistence line).
		//! False on a missing/garbage line number or an empty file part.
		inline bool parseBreakpoint(String const & text, ScriptBreakpoint & out)
		{
			const std::size_t colon = text.rfind(':');
			if (colon == String::npos || colon == 0 ||
				colon + 1 >= text.size())
			{
				return false;
			}
			int line = 0;
			for (std::size_t i = colon + 1; i < text.size(); ++i)
			{
				if (text[i] < '0' || text[i] > '9')
				{
					return false;
				}
				line = line * 10 + (text[i] - '0');
			}
			if (line <= 0)
			{
				return false;
			}
			out.file = normalizeChunk(text.substr(0, colon));
			out.line = line;
			return true;
		}

		//! format the wire/persistence form "<file>:<line>"
		inline String formatBreakpoint(ScriptBreakpoint const & breakpoint)
		{
			return breakpoint.file + ":" + std::to_string(breakpoint.line);
		}
	}
	/** @} */
}

#endif //__ScriptDebugCore_h__24_7_2026__10_00_00__
