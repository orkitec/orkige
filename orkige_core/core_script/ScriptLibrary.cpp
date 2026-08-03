/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptLibrary.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptLibrary.h"
#include "core_util/PathJail.h"

namespace Orkige
{
	namespace
	{
		//! the ONE extension a library name may carry
		char const * const kLibraryExtension = ".lua";
	}
	//---------------------------------------------------------
	char const * ScriptLibrary::extension()
	{
		return kLibraryExtension;
	}
	//---------------------------------------------------------
	bool ScriptLibrary::checkName(String const & name, String & outError)
	{
		if(name.empty())
		{
			outError = "script.require: the library name is empty";
			return false;
		}
		// the ONE containment primitive: absolute paths, drive/UNC roots and any
		// ".." segment are refused, so a name can only ever address a script the
		// project itself ships
		if(!PathJail::isSafeRelativeEntry(name))
		{
			outError = "script.require('" + name + "'): not a project-relative "
				"path (absolute paths and '..' are refused)";
			return false;
		}
		// a LIBRARY is Lua source. Pinning the extension keeps the reachable set
		// exactly the set a path-bound ScriptComponent could already run - the
		// argument that makes this loader no new capability - and stops a data
		// file from being mistaken for code.
		const String suffix = kLibraryExtension;
		if(name.size() <= suffix.size() ||
			name.compare(name.size() - suffix.size(), suffix.size(),
				suffix) != 0)
		{
			outError = "script.require('" + name + "'): a library must be a '" +
				suffix + "' file";
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	String ScriptLibrary::cycleError(StringVector const & loadingChain,
		String const & name)
	{
		String chain;
		bool reached = false;
		for(String const & entry : loadingChain)
		{
			// the chain is rendered from the point the loop CLOSES, so the
			// message names the cycle itself and not the whole load history
			if(!reached && entry != name)
			{
				continue;
			}
			reached = true;
			if(!chain.empty())
			{
				chain += " -> ";
			}
			chain += entry;
		}
		if(chain.empty())
		{
			chain = name;
		}
		chain += " -> ";
		chain += name;
		return "script.require('" + name + "'): circular library dependency (" +
			chain + ")";
	}
	//---------------------------------------------------------
}
