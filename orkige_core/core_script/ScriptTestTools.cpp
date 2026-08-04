/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestTools.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptTestTools.h"

#include <algorithm>
#include <map>

namespace Orkige
{
	namespace
	{
		//! the marker suffix: a project script is a test file iff its file name
		//! ends in this
		char const * const kTestFileSuffix = ".test.lua";
		//! the project directory test files live in
		char const * const kTestsDirectoryName = "tests";

		//! the base name of a path, split on BOTH separators so a
		//! Windows-spelled path derives the same name as a POSIX one (no
		//! std::filesystem: this stays pure string logic)
		String baseNameOf(String const & path)
		{
			const std::size_t slash = path.find_last_of("/\\");
			return (slash == String::npos) ? path : path.substr(slash + 1);
		}
	}
	//---------------------------------------------------------
	char const * ScriptTestTools::testFileSuffix()
	{
		return kTestFileSuffix;
	}
	//---------------------------------------------------------
	char const * ScriptTestTools::testsDirectoryName()
	{
		return kTestsDirectoryName;
	}
	//---------------------------------------------------------
	String ScriptTestTools::testNameForFile(String const & fileName)
	{
		const String base = baseNameOf(fileName);
		const String suffix = kTestFileSuffix;
		if(base.size() <= suffix.size())
		{
			return "";
		}
		if(base.compare(base.size() - suffix.size(), suffix.size(),
			suffix) != 0)
		{
			return "";	// a plain .lua library, a *.component.lua, or other file
		}
		return base.substr(0, base.size() - suffix.size());
	}
	//---------------------------------------------------------
	std::vector<ScriptTestFile> ScriptTestTools::collectTestFiles(
		StringVector const & projectRelativePaths, StringVector * outDuplicates)
	{
		// (stable name, path) pairs, sorted so the run order - and therefore
		// which file wins a name clash - is reproducible on every machine
		std::vector<std::pair<String, String> > candidates;
		for(String const & path : projectRelativePaths)
		{
			const String name = ScriptTestTools::testNameForFile(path);
			if(name.empty())
			{
				continue;
			}
			candidates.push_back(std::make_pair(name, path));
		}
		std::sort(candidates.begin(), candidates.end());

		std::vector<ScriptTestFile> files;
		std::map<String, String> seen;	//!< stable name -> the file that won
		for(std::pair<String, String> const & candidate : candidates)
		{
			const std::map<String, String>::const_iterator prior =
				seen.find(candidate.first);
			if(prior != seen.end())
			{
				// keep the name space honest: the first of two files deriving
				// one test name wins, both are reported, neither aborts the run
				if(outDuplicates != 0)
				{
					outDuplicates->push_back(candidate.first + ": kept '" +
						prior->second + "', ignored '" + candidate.second +
						"'");
				}
				continue;
			}
			seen[candidate.first] = candidate.second;
			ScriptTestFile file;
			file.name = candidate.first;
			file.resourceName = candidate.second;
			files.push_back(file);
		}
		return files;
	}
	//---------------------------------------------------------
	bool ScriptTestTools::filterMatches(String const & filter,
		String const & fileName, String const & testName)
	{
		if(filter.empty())
		{
			return true;
		}
		const String subject = fileName + "::" + testName;
		return subject.find(filter) != String::npos;
	}
	//---------------------------------------------------------
}
