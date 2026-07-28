/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorMcpConfigFile.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "EditorMcpConfigFile.h"

#include "EditorMcpConfig.h"

#include <core_debug/DebugMacros.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace OrkigeEditor
{
	namespace
	{
		namespace fs = std::filesystem;

		//! read a whole file ("" when it does not exist / cannot be read)
		std::string readFile(std::string const& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				return std::string();
			}
			std::ostringstream buffer;
			buffer << in.rdbuf();
			return buffer.str();
		}

		//! the `.mcp.json` path for a project root
		std::string configPathFor(std::string const& projectRoot)
		{
			return (fs::path(projectRoot) / ".mcp.json").string();
		}
	}
	//---------------------------------------------------------
	void McpProjectConfig::reconcile(bool endpointLive,
		std::string const& projectRoot, std::string const& url,
		std::string const& bearerToken)
	{
		const std::string desiredPath =
			(endpointLive && !projectRoot.empty())
				? configPathFor(projectRoot) : std::string();

		// the managed target moved (project switch / endpoint down): tear our
		// entry out of the file we were managing before touching the new one
		if (!mPath.empty() && mPath != desiredPath)
		{
			this->stripFromFile(mPath);
			mPath.clear();
			mUrl.clear();
			mToken.clear();
		}
		if (desiredPath.empty())
		{
			mLastKey.clear();
			return;
		}
		// reconcile this exact (path,url,token) at most once - the per-frame call
		// then costs nothing until something actually changes
		const std::string key = desiredPath + "\n" + url + "\n" + bearerToken;
		if (key == mLastKey)
		{
			return;
		}
		mLastKey = key;

		const std::string existing = readFile(desiredPath);
		const McpWritePlan plan = planMcpWrite(existing, url, bearerToken);
		if (plan.action == McpWriteAction::Skip)
		{
			// a foreign .mcp.json / a user-authored `orkige` server: manage none
			mPath.clear();
			mUrl.clear();
			mToken.clear();
			oDebugMsg("editor.mcp", 0, "project .mcp.json: " << plan.reason);
			return;
		}
		std::error_code ec;
		{
			std::ofstream out(desiredPath, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				oDebugWarn("editor.mcp", 0, "could not write project .mcp.json at "
					<< desiredPath);
				mPath.clear();
				return;
			}
			out << plan.content << "\n";
		}
#ifndef _WIN32
		// the file carries the bearer token: keep it readable by its owner only
		fs::permissions(desiredPath,
			fs::perms::owner_read | fs::perms::owner_write,
			fs::perm_options::replace, ec);
#endif
		mPath = desiredPath;
		mUrl = url;
		mToken = bearerToken;
		oDebugMsg("editor.mcp", 0, "project .mcp.json: " << plan.reason <<
			" (" << desiredPath << ")");
	}
	//---------------------------------------------------------
	void McpProjectConfig::clear()
	{
		if (mPath.empty())
		{
			return;
		}
		this->stripFromFile(mPath);
		mPath.clear();
		mUrl.clear();
		mToken.clear();
		mLastKey.clear();
	}
	//---------------------------------------------------------
	void McpProjectConfig::stripFromFile(std::string const& path)
	{
		const std::string existing = readFile(path);
		const McpRemovePlan plan = planMcpRemove(existing);
		std::error_code ec;
		switch (plan.action)
		{
		case McpRemoveAction::RemoveFile:
			fs::remove(path, ec);
			oDebugMsg("editor.mcp", 0, "project .mcp.json: " << plan.reason);
			break;
		case McpRemoveAction::Rewrite:
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (out)
			{
				out << plan.content << "\n";
			}
			oDebugMsg("editor.mcp", 0, "project .mcp.json: " << plan.reason);
			break;
		}
		case McpRemoveAction::Leave:
		default:
			// not ours / unparseable: never touch it
			break;
		}
	}
}
