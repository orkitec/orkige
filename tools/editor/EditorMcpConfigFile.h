/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorMcpConfigFile.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorMcpConfigFile_h__28_7_2026__12_00_00__
#define __EditorMcpConfigFile_h__28_7_2026__12_00_00__

#include <string>

// EditorMcpConfigFile.h - the file-IO reconciler that keeps
// `<projectRoot>/.mcp.json` (the project-scope MCP discovery file a `claude`
// session started in the project directory auto-loads) in line with the live
// editor MCP endpoint. It wraps the pure merge-or-skip decisions in
// EditorMcpConfig.h with the read/write/remove: it writes our marked server
// entry when the endpoint is live and a project is open, tears the entry back
// out of the PREVIOUS file on a project switch / port change, and removes it on
// clean shutdown - never touching a `.mcp.json` (or a foreign `orkige` server)
// the user authored. Editor-only file access, like the Claude-IDE lock writer.
namespace OrkigeEditor
{
	class McpProjectConfig
	{
	public:
		//! @brief bring the on-disk `.mcp.json` in line with the desired state,
		//! once per frame. When @p endpointLive and @p projectRoot is non-empty,
		//! ensure OUR marked server entry in that project's file names @p url /
		//! @p bearerToken; when the project closed, the endpoint went down, or
		//! the target changed, strip our entry from the previously-managed file
		//! first. Idempotent - it touches the disk only on a real change.
		void reconcile(bool endpointLive, std::string const& projectRoot,
			std::string const& url, std::string const& bearerToken);
		//! @brief strip our entry from the currently-managed file (clean shutdown
		//! / project close). No-op when we manage none.
		void clear();

		//! the `.mcp.json` path we currently manage ("" = none) - the selfcheck
		//! and shutdown path read it
		std::string const& managedPath() const { return mPath; }

	private:
		//! remove OUR entry from the file at @p path (delete the file when ours
		//! was the only server, rewrite when others remain, leave it otherwise)
		void stripFromFile(std::string const& path);

		std::string mPath;		//!< the file we currently manage ("" = none)
		std::string mUrl;		//!< the url our current entry names
		std::string mToken;		//!< the token our current entry names
		std::string mLastKey;	//!< the (path,url,token) we last reconciled
	};
}

#endif //__EditorMcpConfigFile_h__28_7_2026__12_00_00__
