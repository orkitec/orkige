/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorIdeProtocol.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "EditorIdeProtocol.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace OrkigeEditor
{
	using Orkige::JsonValue;
	using Orkige::String;

	namespace
	{
		//! the lower-case extension of a path including the dot (".lua"), ""
		//! when the path carries none
		std::string lowerExtension(std::string const& path)
		{
			std::string ext = std::filesystem::path(path).extension().string();
			for (char& c : ext)
			{
				c = static_cast<char>(
					std::tolower(static_cast<unsigned char>(c)));
			}
			return ext;
		}

		//! the base file name of a path (the getOpenEditors tab label)
		std::string fileName(std::string const& path)
		{
			return std::filesystem::path(path).filename().string();
		}

		//! wrap an inner JSON body as the single text block of a tool result -
		//! the IDE protocol's convention (a tool's structured payload rides as a
		//! JSON STRING inside content[0].text, not as structuredContent)
		JsonValue toolTextOf(JsonValue const& body)
		{
			return ideToolText(std::string(body.serialize().c_str()));
		}
	}
	//---------------------------------------------------------
	std::string serializeIdeLock(IdeLockInfo const& info)
	{
		JsonValue root = JsonValue::object();
		root.set("pid", JsonValue(static_cast<double>(info.pid)));
		JsonValue folders = JsonValue::array();
		for (std::string const& folder : info.workspaceFolders)
		{
			folders.push(JsonValue(String(folder.c_str())));
		}
		root.set("workspaceFolders", folders);
		root.set("ideName", JsonValue(String(info.ideName.c_str())));
		root.set("transport", JsonValue(String(info.transport.c_str())));
		root.set("authToken", JsonValue(String(info.authToken.c_str())));
		return std::string(root.serialize().c_str());
	}
	//---------------------------------------------------------
	bool parseIdeLock(std::string const& text, IdeLockInfo& out)
	{
		JsonValue root;
		if (!JsonValue::parse(String(text.c_str()), root) || !root.isObject())
		{
			return false;
		}
		IdeLockInfo parsed;
		parsed.pid = static_cast<long>(root.get("pid").asNumber(0.0));
		JsonValue const& folders = root.get("workspaceFolders");
		for (size_t i = 0; i < folders.size(); ++i)
		{
			parsed.workspaceFolders.push_back(
				std::string(folders.at(i).asString().c_str()));
		}
		if (root.get("ideName").isString())
		{
			parsed.ideName = std::string(root.get("ideName").asString().c_str());
		}
		if (root.get("transport").isString())
		{
			parsed.transport =
				std::string(root.get("transport").asString().c_str());
		}
		parsed.authToken = std::string(root.get("authToken").asString().c_str());
		out = parsed;
		return true;
	}
	//---------------------------------------------------------
	bool ideLockMayOverwrite(bool ownerPidAlive)
	{
		return !ownerPidAlive;
	}
	//---------------------------------------------------------
	std::string ideLanguageForPath(std::string const& path)
	{
		const std::string ext = lowerExtension(path);
		if (ext == ".lua") return "lua";
		if (ext == ".c" || ext == ".h") return "c";
		if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
			ext == ".hpp" || ext == ".hh" || ext == ".inl" || ext == ".mm")
		{
			return "cpp";
		}
		if (ext == ".py") return "python";
		if (ext == ".json" || ext == ".jsonl") return "json";
		if (ext == ".md" || ext == ".markdown") return "markdown";
		if (ext == ".glsl" || ext == ".vert" || ext == ".frag" ||
			ext == ".hlsl" || ext == ".metal")
		{
			return "glsl";
		}
		// the engine's XMLArchive carriers + XLIFF read as xml
		if (ext == ".xml" || ext == ".oscene" || ext == ".oprefab" ||
			ext == ".orkproj" || ext == ".orkmeta" || ext == ".olevels" ||
			ext == ".oactions" || ext == ".olayers" || ext == ".xlf")
		{
			return "xml";
		}
		// the line-based config-text family is a hint-free plaintext to claude
		return "plaintext";
	}
	//---------------------------------------------------------
	std::string idePathToFileUri(std::string const& absolutePath)
	{
		// file://<path> with the reserved bytes percent-encoded; the path
		// separators and a leading slash stay literal so the URI reads normally
		std::string encoded;
		encoded.reserve(absolutePath.size() + 8);
		for (unsigned char c : absolutePath)
		{
			const bool unreserved = (c >= 'A' && c <= 'Z') ||
				(c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
			if (unreserved)
			{
				encoded += static_cast<char>(c);
			}
			else
			{
				static const char* HEX = "0123456789ABCDEF";
				encoded += '%';
				encoded += HEX[(c >> 4) & 0xF];
				encoded += HEX[c & 0xF];
			}
		}
		if (!encoded.empty() && encoded[0] == '/')
		{
			return "file://" + encoded;
		}
		return "file:///" + encoded;
	}
	//---------------------------------------------------------
	std::string ideFileUriToPath(std::string const& uri)
	{
		const std::string prefix = "file://";
		if (uri.compare(0, prefix.size(), prefix) != 0)
		{
			return std::string();
		}
		std::string rest = uri.substr(prefix.size());
		// a file URI's authority is empty ("file:///path"): drop the extra host
		// slashes back to the single leading one
		std::string decoded;
		for (size_t i = 0; i < rest.size(); ++i)
		{
			if (rest[i] == '%' && i + 2 < rest.size())
			{
				auto hex = [](char c) -> int
				{
					if (c >= '0' && c <= '9') return c - '0';
					if (c >= 'a' && c <= 'f') return c - 'a' + 10;
					if (c >= 'A' && c <= 'F') return c - 'A' + 10;
					return -1;
				};
				const int hi = hex(rest[i + 1]);
				const int lo = hex(rest[i + 2]);
				if (hi >= 0 && lo >= 0)
				{
					decoded += static_cast<char>((hi << 4) | lo);
					i += 2;
					continue;
				}
			}
			decoded += rest[i];
		}
		return decoded;
	}
	//---------------------------------------------------------
	JsonValue ideJsonRpcResult(JsonValue const& id, JsonValue const& result)
	{
		JsonValue root = JsonValue::object();
		root.set("jsonrpc", JsonValue("2.0"));
		root.set("id", id);
		root.set("result", result);
		return root;
	}
	//---------------------------------------------------------
	JsonValue ideJsonRpcError(JsonValue const& id, int code,
		std::string const& message)
	{
		JsonValue error = JsonValue::object();
		error.set("code", JsonValue(static_cast<double>(code)));
		error.set("message", JsonValue(String(message.c_str())));
		JsonValue root = JsonValue::object();
		root.set("jsonrpc", JsonValue("2.0"));
		root.set("id", id);
		root.set("error", error);
		return root;
	}
	//---------------------------------------------------------
	JsonValue ideNotification(std::string const& method, JsonValue const& params)
	{
		JsonValue root = JsonValue::object();
		root.set("jsonrpc", JsonValue("2.0"));
		root.set("method", JsonValue(String(method.c_str())));
		root.set("params", params);
		return root;
	}
	//---------------------------------------------------------
	JsonValue ideInitializeResult(std::string const& protocolVersion,
		std::string const& serverName, std::string const& serverVersion)
	{
		JsonValue result = JsonValue::object();
		result.set("protocolVersion",
			JsonValue(String(protocolVersion.c_str())));
		JsonValue capabilities = JsonValue::object();
		JsonValue tools = JsonValue::object();
		tools.set("listChanged", JsonValue(false));
		capabilities.set("tools", tools);
		result.set("capabilities", capabilities);
		JsonValue serverInfo = JsonValue::object();
		serverInfo.set("name", JsonValue(String(serverName.c_str())));
		serverInfo.set("version", JsonValue(String(serverVersion.c_str())));
		result.set("serverInfo", serverInfo);
		return result;
	}
	//---------------------------------------------------------
	JsonValue ideToolList()
	{
		// each entry: {name, description, inputSchema:{type:object, properties}}.
		// The IDE tool surface claude drives; a bare schema is enough (claude
		// calls them, it never lists them to a user).
		auto tool = [](const char* name, const char* description,
			JsonValue properties) -> JsonValue
		{
			JsonValue schema = JsonValue::object();
			schema.set("type", JsonValue("object"));
			schema.set("properties", properties);
			JsonValue entry = JsonValue::object();
			entry.set("name", JsonValue(name));
			entry.set("description", JsonValue(description));
			entry.set("inputSchema", schema);
			return entry;
		};
		auto stringProp = [](const char* type, const char* description)
			-> JsonValue
		{
			JsonValue prop = JsonValue::object();
			prop.set("type", JsonValue(type));
			prop.set("description", JsonValue(description));
			return prop;
		};

		JsonValue tools = JsonValue::array();
		tools.push(tool("getWorkspaceFolders",
			"The open project root folder(s).", JsonValue::object()));
		tools.push(tool("getOpenEditors",
			"The documents open in the editor (path, active tab, dirty).",
			JsonValue::object()));
		tools.push(tool("getCurrentSelection",
			"The text selection in the focused document.", JsonValue::object()));
		{
			JsonValue props = JsonValue::object();
			props.set("uri", stringProp("string",
				"file:// URI to limit diagnostics to (omit for all)"));
			tools.push(tool("getDiagnostics",
				"Parse diagnostics for the open documents.", props));
		}
		{
			JsonValue props = JsonValue::object();
			props.set("filePath",
				stringProp("string", "the file to open in the editor"));
			props.set("preview", stringProp("boolean", "open as a preview tab"));
			props.set("startText", stringProp("string",
				"reveal the first line containing this text"));
			props.set("makeFrontmost",
				stringProp("boolean", "focus the opened document"));
			tools.push(tool("openFile",
				"Open a file in the embedded editor.", props));
		}
		{
			JsonValue props = JsonValue::object();
			props.set("tab_name", stringProp("string", "the tab to close"));
			tools.push(tool("close_tab", "Close an open document tab.", props));
		}
		{
			JsonValue props = JsonValue::object();
			props.set("old_file_path", stringProp("string", "the original file"));
			props.set("new_file_path", stringProp("string", "the modified file"));
			props.set("new_file_contents",
				stringProp("string", "the proposed contents"));
			props.set("tab_name", stringProp("string", "the diff tab caption"));
			tools.push(tool("openDiff",
				"Open a proposed-change diff (not supported in this editor).",
				props));
		}
		return tools;
	}
	//---------------------------------------------------------
	JsonValue ideToolText(std::string const& text)
	{
		JsonValue block = JsonValue::object();
		block.set("type", JsonValue("text"));
		block.set("text", JsonValue(String(text.c_str())));
		JsonValue content = JsonValue::array();
		content.push(block);
		JsonValue result = JsonValue::object();
		result.set("content", content);
		return result;
	}
	//---------------------------------------------------------
	JsonValue ideToolError(std::string const& message)
	{
		JsonValue result = ideToolText(message);
		result.set("isError", JsonValue(true));
		return result;
	}
	//---------------------------------------------------------
	JsonValue ideWorkspaceFoldersResult(std::vector<std::string> const& roots)
	{
		JsonValue folders = JsonValue::array();
		for (std::string const& root : roots)
		{
			JsonValue folder = JsonValue::object();
			folder.set("name", JsonValue(String(fileName(root).c_str())));
			folder.set("uri", JsonValue(String(idePathToFileUri(root).c_str())));
			folder.set("path", JsonValue(String(root.c_str())));
			folders.push(folder);
		}
		JsonValue body = JsonValue::object();
		body.set("success", JsonValue(true));
		body.set("folders", folders);
		if (!roots.empty())
		{
			body.set("rootPath", JsonValue(String(roots.front().c_str())));
		}
		return toolTextOf(body);
	}
	//---------------------------------------------------------
	JsonValue ideOpenEditorsResult(std::vector<IdeEditor> const& editors)
	{
		JsonValue tabs = JsonValue::array();
		for (IdeEditor const& editor : editors)
		{
			JsonValue tab = JsonValue::object();
			tab.set("uri", JsonValue(
				String(idePathToFileUri(editor.absolutePath).c_str())));
			tab.set("isActive", JsonValue(editor.active));
			tab.set("label",
				JsonValue(String(fileName(editor.absolutePath).c_str())));
			tab.set("languageId", JsonValue(String(editor.languageId.c_str())));
			tab.set("isDirty", JsonValue(editor.dirty));
			tabs.push(tab);
		}
		JsonValue body = JsonValue::object();
		body.set("tabs", tabs);
		return toolTextOf(body);
	}
	//---------------------------------------------------------
	JsonValue ideSelectionParams(IdeSelection const& selection)
	{
		JsonValue params = JsonValue::object();
		params.set("text", JsonValue(String(selection.text.c_str())));
		params.set("filePath",
			JsonValue(String(selection.absolutePath.c_str())));
		params.set("fileUrl", JsonValue(
			String(idePathToFileUri(selection.absolutePath).c_str())));
		JsonValue start = JsonValue::object();
		start.set("line", JsonValue(static_cast<double>(selection.startLine)));
		start.set("character",
			JsonValue(static_cast<double>(selection.startChar)));
		JsonValue end = JsonValue::object();
		end.set("line", JsonValue(static_cast<double>(selection.endLine)));
		end.set("character", JsonValue(static_cast<double>(selection.endChar)));
		JsonValue range = JsonValue::object();
		range.set("start", start);
		range.set("end", end);
		range.set("isEmpty", JsonValue(selection.text.empty()));
		params.set("selection", range);
		return params;
	}
	//---------------------------------------------------------
	JsonValue ideSelectionResult(IdeSelection const& selection)
	{
		if (!selection.active)
		{
			JsonValue body = JsonValue::object();
			body.set("success", JsonValue(false));
			body.set("message", JsonValue("No active editor selection"));
			return toolTextOf(body);
		}
		JsonValue body = ideSelectionParams(selection);
		body.set("success", JsonValue(true));
		return toolTextOf(body);
	}
	//---------------------------------------------------------
	JsonValue ideDiagnosticsResult(
		std::vector<IdeDiagnostic> const& diagnostics,
		std::string const& filterPath)
	{
		// group by file into the [{uri, diagnostics:[...]}] shape claude reads;
		// a diagnostic's 1-based line maps to a 0-based LSP range line. Files
		// are accumulated in first-seen order, then assembled once.
		std::vector<std::string> order;
		std::vector<JsonValue> perFile;	// one diagnostics array per file
		for (IdeDiagnostic const& diag : diagnostics)
		{
			if (!filterPath.empty() && diag.absolutePath != filterPath)
			{
				continue;
			}
			size_t index = order.size();
			for (size_t i = 0; i < order.size(); ++i)
			{
				if (order[i] == diag.absolutePath)
				{
					index = i;
					break;
				}
			}
			if (index == order.size())
			{
				order.push_back(diag.absolutePath);
				perFile.push_back(JsonValue::array());
			}
			const int zeroLine = diag.line > 0 ? diag.line - 1 : 0;
			JsonValue point = JsonValue::object();
			point.set("line", JsonValue(static_cast<double>(zeroLine)));
			point.set("character", JsonValue(0.0));
			JsonValue range = JsonValue::object();
			range.set("start", point);
			range.set("end", point);
			JsonValue entry = JsonValue::object();
			entry.set("message", JsonValue(String(diag.message.c_str())));
			entry.set("severity", JsonValue(String(diag.severity.c_str())));
			entry.set("range", range);
			entry.set("source", JsonValue("orkige"));
			perFile[index].push(entry);
		}
		JsonValue root = JsonValue::array();
		for (size_t i = 0; i < order.size(); ++i)
		{
			JsonValue fileEntry = JsonValue::object();
			fileEntry.set("uri",
				JsonValue(String(idePathToFileUri(order[i]).c_str())));
			fileEntry.set("diagnostics", perFile[i]);
			root.push(fileEntry);
		}
		return toolTextOf(root);
	}
	//---------------------------------------------------------
	bool ideSelectionChanged(IdeSelection const& a, IdeSelection const& b)
	{
		if (a.active != b.active)
		{
			return true;
		}
		if (!a.active)
		{
			return false;	// both inactive: nothing to report
		}
		return a.absolutePath != b.absolutePath || a.text != b.text ||
			a.startLine != b.startLine || a.startChar != b.startChar ||
			a.endLine != b.endLine || a.endChar != b.endChar;
	}
}
