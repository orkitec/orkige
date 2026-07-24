// ScriptBreakpoints - the editor's per-project script breakpoint store.
// See ScriptBreakpoints.h for the design notes.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "ScriptBreakpoints.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace Orkige
{
	//---------------------------------------------------------
	void ScriptBreakpointStore::attachProject(String const& projectRoot)
	{
		this->mProjectRoot = projectRoot;
		this->mEntries.clear();
		++this->mRevision;
		if (!this->mProjectRoot.empty())
		{
			this->load();
		}
	}
	//---------------------------------------------------------
	bool ScriptBreakpointStore::set(String const& file, int line)
	{
		if (file.empty() || line <= 0 || this->has(file, line))
		{
			return false;
		}
		this->mEntries.push_back(ScriptBreakpoint(
			ScriptDebugCore::normalizeChunk(file), line));
		this->sortEntries();
		++this->mRevision;
		this->save();
		return true;
	}
	//---------------------------------------------------------
	bool ScriptBreakpointStore::clear(String const& file, int line)
	{
		const String normalized = ScriptDebugCore::normalizeChunk(file);
		const std::size_t before = this->mEntries.size();
		this->mEntries.erase(std::remove_if(this->mEntries.begin(),
			this->mEntries.end(),
			[&](ScriptBreakpoint const& breakpoint)
			{
				return breakpoint.line == line &&
					breakpoint.file == normalized;
			}), this->mEntries.end());
		if (this->mEntries.size() == before)
		{
			return false;
		}
		++this->mRevision;
		this->save();
		return true;
	}
	//---------------------------------------------------------
	bool ScriptBreakpointStore::toggle(String const& file, int line)
	{
		if (this->clear(file, line))
		{
			return false;
		}
		this->set(file, line);
		return true;
	}
	//---------------------------------------------------------
	bool ScriptBreakpointStore::clearAll()
	{
		if (this->mEntries.empty())
		{
			return false;
		}
		this->mEntries.clear();
		++this->mRevision;
		this->save();
		return true;
	}
	//---------------------------------------------------------
	bool ScriptBreakpointStore::has(String const& file, int line) const
	{
		const String normalized = ScriptDebugCore::normalizeChunk(file);
		for (ScriptBreakpoint const& breakpoint : this->mEntries)
		{
			if (breakpoint.line == line && breakpoint.file == normalized)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	std::vector<int> ScriptBreakpointStore::linesFor(String const& file) const
	{
		const String normalized = ScriptDebugCore::normalizeChunk(file);
		std::vector<int> lines;
		for (ScriptBreakpoint const& breakpoint : this->mEntries)
		{
			if (breakpoint.file == normalized)
			{
				lines.push_back(breakpoint.line);
			}
		}
		return lines;
	}
	//---------------------------------------------------------
	std::vector<String> ScriptBreakpointStore::wireList() const
	{
		std::vector<String> wire;
		wire.reserve(this->mEntries.size());
		for (ScriptBreakpoint const& breakpoint : this->mEntries)
		{
			wire.push_back(ScriptDebugCore::formatBreakpoint(breakpoint));
		}
		return wire;
	}
	//---------------------------------------------------------
	void ScriptBreakpointStore::sortEntries()
	{
		std::sort(this->mEntries.begin(), this->mEntries.end(),
			[](ScriptBreakpoint const& a, ScriptBreakpoint const& b)
			{
				return a.file != b.file ? a.file < b.file : a.line < b.line;
			});
	}
	//---------------------------------------------------------
	void ScriptBreakpointStore::save() const
	{
		if (this->mProjectRoot.empty())
		{
			return;
		}
		std::error_code ignored;
		const std::filesystem::path storePath =
			std::filesystem::path(this->mProjectRoot) / STORE_RELATIVE_PATH;
		std::filesystem::create_directories(storePath.parent_path(), ignored);
		std::ofstream file(storePath, std::ios::trunc);
		for (ScriptBreakpoint const& breakpoint : this->mEntries)
		{
			file << ScriptDebugCore::formatBreakpoint(breakpoint) << "\n";
		}
	}
	//---------------------------------------------------------
	void ScriptBreakpointStore::load()
	{
		const std::filesystem::path storePath =
			std::filesystem::path(this->mProjectRoot) / STORE_RELATIVE_PATH;
		std::ifstream file(storePath);
		String line;
		while (std::getline(file, line))
		{
			// tolerate CR residue and blank lines in a hand-edited file
			while (!line.empty() &&
				(line.back() == '\r' || line.back() == ' '))
			{
				line.pop_back();
			}
			ScriptBreakpoint breakpoint;
			if (ScriptDebugCore::parseBreakpoint(line, breakpoint))
			{
				this->mEntries.push_back(breakpoint);
			}
		}
		this->sortEntries();
	}
}
