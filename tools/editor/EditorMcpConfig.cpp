/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorMcpConfig.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "EditorMcpConfig.h"

#include <cctype>

namespace OrkigeEditor
{
	using Orkige::JsonValue;
	using Orkige::String;

	const char* const MCP_SERVER_NAME = "orkige";
	const char* const MCP_MANAGED_MARKER = "x-orkige-managed";

	namespace
	{
		//! is a document body empty / all whitespace (i.e. no file yet)?
		bool isBlank(std::string const& text)
		{
			for (char c : text)
			{
				if (!std::isspace(static_cast<unsigned char>(c)))
				{
					return false;
				}
			}
			return true;
		}

		//! is @p entry an `orkige` server object WE wrote (carries our marker)?
		bool isOurEntry(JsonValue const& entry)
		{
			return entry.isObject() && entry.has(String(MCP_MANAGED_MARKER)) &&
				entry.get(String(MCP_MANAGED_MARKER)).asBool();
		}
	}
	//---------------------------------------------------------
	JsonValue buildOrkigeMcpServerEntry(std::string const& url,
		std::string const& bearerToken)
	{
		JsonValue entry = JsonValue::object();
		entry.set("type", JsonValue("http"));
		entry.set("url", JsonValue(String(url.c_str())));
		// an auth-off dev endpoint (no token file) carries no headers block
		if (!bearerToken.empty())
		{
			JsonValue headers = JsonValue::object();
			headers.set("Authorization",
				JsonValue(String(("Bearer " + bearerToken).c_str())));
			entry.set("headers", headers);
		}
		// the marker rides LAST so the leading fields match a hand-authored entry
		entry.set(String(MCP_MANAGED_MARKER), JsonValue(true));
		return entry;
	}
	//---------------------------------------------------------
	McpWritePlan planMcpWrite(std::string const& existingContent,
		std::string const& url, std::string const& bearerToken)
	{
		McpWritePlan plan;
		const JsonValue ourEntry = buildOrkigeMcpServerEntry(url, bearerToken);

		// no file yet: write a fresh document carrying only our entry
		if (isBlank(existingContent))
		{
			JsonValue servers = JsonValue::object();
			servers.set(String(MCP_SERVER_NAME), ourEntry);
			JsonValue root = JsonValue::object();
			root.set("mcpServers", servers);
			plan.action = McpWriteAction::Write;
			plan.content = std::string(root.serialize().c_str());
			plan.reason = "wrote a new .mcp.json with the orkige endpoint";
			return plan;
		}

		JsonValue root;
		if (!JsonValue::parse(String(existingContent.c_str()), root) ||
			!root.isObject())
		{
			plan.action = McpWriteAction::Skip;
			plan.reason = "existing .mcp.json is not parseable - left untouched";
			return plan;
		}

		// preserve every top-level key except mcpServers (which we rebuild)
		const JsonValue existingServers = root.get("mcpServers");
		if (root.has(String("mcpServers")) && !existingServers.isObject())
		{
			plan.action = McpWriteAction::Skip;
			plan.reason = "existing .mcp.json has a non-object mcpServers - "
				"left untouched";
			return plan;
		}
		JsonValue servers = JsonValue::object();
		if (existingServers.isObject())
		{
			for (auto const& member : existingServers.members())
			{
				if (member.first == String(MCP_SERVER_NAME))
				{
					if (!isOurEntry(member.second))
					{
						// a user-authored server owns our name: never clobber it
						plan.action = McpWriteAction::Skip;
						plan.reason = "a foreign 'orkige' server is present in "
							".mcp.json - left untouched";
						return plan;
					}
					continue;	// our stale entry: replaced below
				}
				servers.set(member.first, member.second);	// keep every other
			}
		}
		servers.set(String(MCP_SERVER_NAME), ourEntry);

		JsonValue rebuilt = JsonValue::object();
		for (auto const& member : root.members())
		{
			if (member.first == String("mcpServers"))
			{
				continue;
			}
			rebuilt.set(member.first, member.second);	// keep other top-level keys
		}
		rebuilt.set("mcpServers", servers);

		plan.action = McpWriteAction::Write;
		plan.content = std::string(rebuilt.serialize().c_str());
		plan.reason = "merged the orkige endpoint into the existing .mcp.json";
		return plan;
	}
	//---------------------------------------------------------
	McpRemovePlan planMcpRemove(std::string const& existingContent)
	{
		McpRemovePlan plan;	// defaults to Leave

		if (isBlank(existingContent))
		{
			plan.reason = "no .mcp.json to remove";
			return plan;
		}
		JsonValue root;
		if (!JsonValue::parse(String(existingContent.c_str()), root) ||
			!root.isObject())
		{
			plan.reason = "existing .mcp.json is not parseable - left untouched";
			return plan;
		}
		const JsonValue servers = root.get("mcpServers");
		if (!servers.isObject() || !servers.has(String(MCP_SERVER_NAME)) ||
			!isOurEntry(servers.get(String(MCP_SERVER_NAME))))
		{
			plan.reason = "no orkige-managed entry in .mcp.json - left untouched";
			return plan;
		}

		// our marked entry is present: strip it. Count what else remains.
		JsonValue kept = JsonValue::object();
		std::size_t others = 0;
		for (auto const& member : servers.members())
		{
			if (member.first == String(MCP_SERVER_NAME))
			{
				continue;
			}
			kept.set(member.first, member.second);
			++others;
		}
		// other top-level keys besides mcpServers keep the file alive too
		std::size_t otherTopLevel = 0;
		JsonValue rebuilt = JsonValue::object();
		for (auto const& member : root.members())
		{
			if (member.first == String("mcpServers"))
			{
				continue;
			}
			rebuilt.set(member.first, member.second);
			++otherTopLevel;
		}
		if (others == 0 && otherTopLevel == 0)
		{
			plan.action = McpRemoveAction::RemoveFile;
			plan.reason = "removed .mcp.json (orkige was its only server)";
			return plan;
		}
		rebuilt.set("mcpServers", kept);
		plan.action = McpRemoveAction::Rewrite;
		plan.content = std::string(rebuilt.serialize().c_str());
		plan.reason = "stripped the orkige entry from .mcp.json (kept others)";
		return plan;
	}
}
