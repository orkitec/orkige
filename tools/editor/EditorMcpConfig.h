/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorMcpConfig.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorMcpConfig_h__28_7_2026__12_00_00__
#define __EditorMcpConfig_h__28_7_2026__12_00_00__

#include <core_debugnet/Json.h>

#include <string>

// EditorMcpConfig.h - the PURE half of the project-scoped MCP discovery file
// (@see EditorMcpConfigFile.h for the file-IO reconciler that drives it).
//
// When the editor's in-process MCP control endpoint is live and a project is
// open, the editor writes `<projectRoot>/.mcp.json` - the project-scope MCP
// config a `claude` session started in that project's directory (the embedded
// terminal starts there) auto-loads. Its shape is exactly what `claude mcp add
// --transport http -s project` produces:
//
//   { "mcpServers": { "orkige": {
//       "type": "http",
//       "url": "http://127.0.0.1:<port>/mcp",
//       "headers": { "Authorization": "Bearer <token>" } } } }
//
// Our entry additionally carries a MARKER field (an entry field `claude`
// ignores) so the reconciler only ever manages the entry IT created: a
// user-authored `orkige` server without the marker is left untouched, and every
// OTHER server the user authored is preserved verbatim across our write and our
// shutdown removal. Everything here is data -> JSON with no file IO, so it is
// exhaustively unit-tested (EditorMcpConfigTests).
namespace OrkigeEditor
{
	//! the server key we register under (the MCP server name a `claude` session
	//! sees for this editor's endpoint)
	extern const char* const MCP_SERVER_NAME;
	//! the marker FIELD written inside our server entry so a rewrite/removal only
	//! ever touches an entry we created (`claude` ignores unknown entry fields)
	extern const char* const MCP_MANAGED_MARKER;

	//! @brief build our server entry object: {type:"http", url, headers:{
	//! Authorization:"Bearer <token>"}, <marker>:true}. When @p bearerToken is
	//! empty (auth-off dev endpoint) the headers block is omitted.
	Orkige::JsonValue buildOrkigeMcpServerEntry(std::string const& url,
		std::string const& bearerToken);

	//--- write plan ------------------------------------------
	enum class McpWriteAction
	{
		Write,	//!< write `content` (our marked entry merged in)
		Skip	//!< leave the existing file untouched
	};
	struct McpWritePlan
	{
		McpWriteAction action = McpWriteAction::Write;
		std::string content;	//!< the whole-file body to write (Write only)
		std::string reason;		//!< a one-line honest note (why skipped / merged)
	};
	//! @brief decide what `<projectRoot>/.mcp.json` should become given its
	//! current @p existingContent (empty/whitespace = no file yet). Merge-or-skip:
	//!   - no file, or a valid file WITHOUT a foreign `orkige` server: add or
	//!     update OUR marked entry and preserve every other server -> Write;
	//!   - a file whose `orkige` entry is FOREIGN (present but not our marker):
	//!     Skip (we never clobber a user-authored server of our name);
	//!   - an unparseable existing file: Skip (never destroy a file we cannot
	//!     safely round-trip).
	McpWritePlan planMcpWrite(std::string const& existingContent,
		std::string const& url, std::string const& bearerToken);

	//--- removal plan (project switch / port change / shutdown) ---
	enum class McpRemoveAction
	{
		RemoveFile,	//!< delete the file (our entry was the only server)
		Rewrite,	//!< write `content` (our entry stripped, others kept)
		Leave		//!< leave the file untouched (not ours / nothing to do)
	};
	struct McpRemovePlan
	{
		McpRemoveAction action = McpRemoveAction::Leave;
		std::string content;	//!< the whole-file body to write (Rewrite only)
		std::string reason;		//!< a one-line honest note
	};
	//! @brief decide the removal given @p existingContent:
	//!   - our marked `orkige` is the ONLY server -> RemoveFile;
	//!   - our marked `orkige` plus other servers -> Rewrite (drop only ours);
	//!   - a foreign/absent `orkige`, or an unparseable file -> Leave.
	McpRemovePlan planMcpRemove(std::string const& existingContent);
}

#endif //__EditorMcpConfig_h__28_7_2026__12_00_00__
