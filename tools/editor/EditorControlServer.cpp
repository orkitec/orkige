// EditorControlServer.cpp - the editor's in-process MCP endpoint (WP #90).
//
// Two layers:
//   1. an HTTP/1.1 + JSON-RPC 2.0 transport (handleHttp / dispatchJsonRpc /
//      runToolCall / buildToolList) on top of the hand-rolled loopback
//      HttpServer - the MCP Streamable HTTP surface (initialize, tools/list,
//      tools/call, notifications). NEW in #90.
//   2. the WP #80 verb handler (handleMessage) REUSED wholesale - a thin
//      adapter over EditorCore + the EditorDocument free functions. Its verbs
//      still work on an internal DebugMessage request/reply pair; sendOk/sendErr
//      buffer the reply into mReply (they used to write a DebugServer socket).
// See EditorControlServer.h for the design.
#include "EditorControlServer.h"
#include "EditorApp.h"

#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>
#include <core_debugnet/DebugSocket.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_util/optr.h>

#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/RenderWorld.h>

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

namespace Orkige
{
	const String EditorControlServer::MSG_OK = "ok";
	const String EditorControlServer::MSG_ERR = "err";
	const String EditorControlServer::MCP_PROTOCOL_VERSION = "2025-03-26";

	namespace
	{
		//! read an optional space-separated float bundle; false when the field
		//! is absent or does not parse to exactly count floats (out untouched)
		bool readFloats(DebugMessage const& message, String const& key,
			float* out, int count)
		{
			if (!message.has(key))
			{
				return false;
			}
			return parsePlayFloats(message.get(key), out, count);
		}
		//! is this verb a pure read (allowed before authentication)?
		bool isReadVerb(String const& type)
		{
			return type == "hello" || type == "ping" || type == "get_state" ||
				type == "list_hierarchy" || type == "get_object" ||
				type == "get_component" || type == "list_assets" ||
				type == "console_tail" || type == "list_addable_components";
		}
		//---------------------------------------------------------
		//--- generic reflected-property helpers (task #94 P5a) ---
		//---------------------------------------------------------
		//! @brief the agent-facing name of a PropertyKind ("float"/"vec3"/...),
		//! so get_component's metadata is self-describing without the client
		//! needing the C++ enum ordinals.
		String kindName(PropertyKind kind)
		{
			switch (kind)
			{
			case PropertyKind::Int:			return "int";
			case PropertyKind::Float:		return "float";
			case PropertyKind::Bool:		return "bool";
			case PropertyKind::String:		return "string";
			case PropertyKind::Enum:		return "enum";
			case PropertyKind::Vec3:		return "vec3";
			case PropertyKind::Quat:		return "quat";
			case PropertyKind::Color:		return "color";
			case PropertyKind::AssetRef:	return "asset";
			case PropertyKind::ObjectRef:	return "object";
			}
			return "string";
		}
		//! @brief the discovery hint for a property: Enum -> its
		//! "label=value,label=value,..." table (from the enum registry),
		//! AssetRef/ObjectRef -> the asset-kind / object-type hint, else "".
		//! Mirrors PlayerRuntime::propertyHint / the inspector's local cousin.
		String propertyHint(PropertyDesc const& desc)
		{
			if (desc.kind == PropertyKind::Enum)
			{
				String options;
				if (EnumInfo const* enumInfo =
					TypeManager::getSingleton().findEnum(desc.enumTypeName))
				{
					for (auto const& labelled : enumInfo->values())
					{
						if (!options.empty())
						{
							options += ",";
						}
						options += labelled.first + "=" +
							std::to_string(labelled.second);
					}
				}
				return options;
			}
			if (desc.kind == PropertyKind::AssetRef ||
				desc.kind == PropertyKind::ObjectRef)
			{
				return desc.referenceHint;
			}
			return String();
		}
		//---------------------------------------------------------
		//--- MCP argument / reply conversion -----------------
		//---------------------------------------------------------
		//! render one JSON argument scalar as the flat string the DebugMessage
		//! verb handler expects (numbers without a needless decimal, bools as
		//! "1"/"0", strings verbatim)
		String argToString(JsonValue const& value)
		{
			switch (value.getType())
			{
			case JsonValue::Type::String:
				return value.asString();
			case JsonValue::Type::Bool:
				return value.asBool() ? "1" : "0";
			case JsonValue::Type::Number:
			{
				const double number = value.asNumber();
				char buffer[32];
				if (number == static_cast<long long>(number))
				{
					std::snprintf(buffer, sizeof(buffer), "%lld",
						static_cast<long long>(number));
				}
				else
				{
					std::snprintf(buffer, sizeof(buffer), "%.9g", number);
				}
				return buffer;
			}
			default:
				return String();
			}
		}
		//! copy an MCP tool call's `arguments` object into a DebugMessage: scalar
		//! members become fields, array members become list fields, and a nested
		//! object (e.g. set_component's `properties`) is flattened one level so
		//! its members land as fields too (both the nested and the flat shape
		//! work).
		void applyArguments(DebugMessage& out, JsonValue const& arguments)
		{
			if (!arguments.isObject())
			{
				return;
			}
			for (auto const& member : arguments.members())
			{
				JsonValue const& value = member.second;
				if (value.isArray())
				{
					StringVector list;
					for (size_t i = 0; i < value.size(); ++i)
					{
						list.push_back(argToString(value.at(i)));
					}
					out.setList(member.first, list);
				}
				else if (value.isObject())
				{
					for (auto const& sub : value.members())
					{
						if (sub.second.isArray())
						{
							StringVector list;
							for (size_t i = 0; i < sub.second.size(); ++i)
							{
								list.push_back(argToString(sub.second.at(i)));
							}
							out.setList(sub.first, list);
						}
						else if (!sub.second.isObject())
						{
							out.set(sub.first, argToString(sub.second));
						}
					}
				}
				else if (!value.isNull())
				{
					out.set(member.first, argToString(value));
				}
			}
		}
		//! turn a verb reply (DebugMessage) into a JSON object: scalar fields as
		//! strings, list fields as string arrays (the MCP structuredContent)
		JsonValue replyToJson(DebugMessage const& reply)
		{
			JsonValue out = JsonValue::object();
			for (auto const& field : reply.fields)
			{
				out.set(field.first, JsonValue(field.second));
			}
			for (auto const& list : reply.lists)
			{
				JsonValue array = JsonValue::array();
				for (String const& value : list.second)
				{
					array.push(JsonValue(value));
				}
				out.set(list.first, array);
			}
			return out;
		}
		//---------------------------------------------------------
		//--- JSON-RPC envelope helpers -----------------------
		//---------------------------------------------------------
		JsonValue jsonRpcResult(JsonValue const& id, JsonValue const& result)
		{
			JsonValue out = JsonValue::object();
			out.set("jsonrpc", JsonValue("2.0"));
			out.set("id", id);
			out.set("result", result);
			return out;
		}
		JsonValue jsonRpcError(JsonValue const& id, int code,
			String const& message)
		{
			JsonValue error = JsonValue::object();
			error.set("code", JsonValue(code));
			error.set("message", JsonValue(message));
			JsonValue out = JsonValue::object();
			out.set("jsonrpc", JsonValue("2.0"));
			out.set("id", id);
			out.set("error", error);
			return out;
		}
		//---------------------------------------------------------
		//--- tool-schema table -------------------------------
		//---------------------------------------------------------
		struct PropSpec
		{
			const char* name;
			const char* type;		//!< JSON-schema type ("string"/"number"/...)
			const char* description;
			bool required;
		};
		struct ToolSpec
		{
			const char* name;
			const char* description;
			std::vector<PropSpec> properties;
		};
		//! the advertised MCP tools. tools/call can run any verb the handler
		//! knows; these carry the JSON inputSchemas the client sees. Vector
		//! fields (position/orientation/scale/...) are space-separated float
		//! STRINGS, matching the player's set_property wire convention (e.g.
		//! position "1 2 3", orientation "w x y z").
		std::vector<ToolSpec> const& toolSpecs()
		{
			static const std::vector<ToolSpec> specs = {
				{ "get_state",
				  "Snapshot of the editor: project/scene paths, dirty flag, "
				  "selection, object count, undo/redo availability, play mode.",
				  {} },
				{ "list_hierarchy",
				  "List every GameObject id with its parent and active flags.",
				  {} },
				{ "get_object",
				  "One GameObject: parent, active flags and its component list.",
				  { { "id", "string", "GameObject id", true } } },
				{ "get_component",
				  "Read a component's reflected properties. Returns every "
				  "property as a name->value field plus parallel discovery "
				  "lists: 'properties' (names), 'kinds' (int/float/bool/string/"
				  "enum/vec3/quat/color/asset/object), 'hints' (enum options "
				  "'label=value,...' or asset-kind), 'readonly' and 'transient' "
				  "('1'/'0'). Values are canonical strings: vectors "
				  "space-separated (vec3 'x y z', quat 'w x y z', color "
				  "'r g b a'), bool '1'/'0', enum the integer value.",
				  { { "id", "string", "GameObject id", true },
				    { "component", "string", "component type name", true } } },
				{ "set_component",
				  "Write a component's reflected properties by NAME (undoable, "
				  "one merged undo step). Pass the properties to change either "
				  "at the top level or inside a 'properties' object; use the "
				  "property names get_component reports. Values are canonical "
				  "strings (vectors space-separated, bool '1'/'0', enum the "
				  "integer value). An unknown or read-only property, or a value "
				  "that does not parse, is refused without changing the object.",
				  { { "id", "string", "GameObject id", true },
				    { "component", "string", "component type name", true },
				    { "properties", "object",
				      "name->value map of properties to set", false } } },
				{ "create_object",
				  "Create a GameObject (undoable). Defaults to a cube mesh.",
				  { { "id", "string", "desired id (auto when omitted)", false },
				    { "mesh", "string", "mesh name ('cube' = default)", false },
				    { "position", "string", "e.g. '0 0 0'", false } } },
				{ "delete_object",
				  "Delete a GameObject (undoable).",
				  { { "id", "string", "GameObject id", true } } },
				{ "duplicate_object",
				  "Duplicate a GameObject (undoable).",
				  { { "id", "string", "GameObject id", true } } },
				{ "rename_object",
				  "Rename a GameObject.",
				  { { "id", "string", "current id", true },
				    { "new_id", "string", "new id", true } } },
				{ "reparent_object",
				  "Reparent a GameObject ('' parent = make it a root).",
				  { { "id", "string", "GameObject id", true },
				    { "parent", "string", "new parent id ('' for root)", false } } },
				{ "set_active",
				  "Set a GameObject's own active flag.",
				  { { "id", "string", "GameObject id", true },
				    { "value", "string", "'1' active, '0' inactive", true } } },
				{ "select",
				  "Select a GameObject ('' clears the selection).",
				  { { "id", "string", "GameObject id ('' clears)", false } } },
				{ "add_component",
				  "Add a component to a GameObject (undoable).",
				  { { "id", "string", "GameObject id", true },
				    { "component", "string", "component type name", true } } },
				{ "remove_component",
				  "Remove a component from a GameObject (undoable).",
				  { { "id", "string", "GameObject id", true },
				    { "component", "string", "component type name", true } } },
				{ "list_addable_components",
				  "Component type names that can be added to a GameObject.", {} },
				{ "undo", "Undo the last command.", {} },
				{ "redo", "Redo the last undone command.", {} },
				{ "new_scene",
				  "Start a new empty scene (refuses to clobber unsaved changes "
				  "unless force='1').",
				  { { "force", "string", "'1' to discard unsaved changes", false } } },
				{ "open_scene",
				  "Open a scene file (dirty-state policy applies).",
				  { { "scene", "string", "scene file path", true },
				    { "force", "string", "'1' to discard unsaved changes", false } } },
				{ "save_scene",
				  "Save the scene (to 'scene' path, or the current path).",
				  { { "scene", "string", "target path (optional)", false } } },
				{ "open_project",
				  "Open a project directory (dirty-state policy applies).",
				  { { "path", "string", "project root directory", true } } },
				{ "new_project",
				  "Create a new project at a directory.",
				  { { "path", "string", "project root directory", true } } },
				{ "close_project",
				  "Close the current project.",
				  { { "force", "string", "'1' to discard unsaved changes", false } } },
				{ "play",
				  "Enter play mode (async: returns accepted; poll get_state).",
				  {} },
				{ "stop", "Stop play mode.", {} },
				{ "pause", "Pause the running player.", {} },
				{ "resume", "Resume the running player.", {} },
				{ "step", "Advance the paused player one frame.", {} },
				{ "screenshot",
				  "Write a PNG: the chrome-free scene viewport, or the whole "
				  "editor window (window='1'). Returns the written path.",
				  { { "path", "string", "output PNG path", true },
				    { "window", "string", "'1' for the whole window", false } } },
				{ "list_assets",
				  "List the open project's assets and scenes.", {} },
				{ "console_tail",
				  "The last N editor Console lines (default 50, max 200).",
				  { { "count", "integer", "how many lines (1-200)", false } } },
			};
			return specs;
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	EditorControlServer::EditorControlServer()
	{
	}
	//---------------------------------------------------------
	EditorControlServer::~EditorControlServer()
	{
		this->stop();
	}
	//---------------------------------------------------------
	bool EditorControlServer::start(unsigned short port,
		std::string const& tokenFilePath)
	{
		if (!this->mServer.start(port))
		{
			return false;
		}
		// auth policy: a token is only meaningful when we can PUBLISH it for the
		// client to read. With a token-file path we mint a fresh secret (the
		// same 128-bit hex generator the asset database uses) and enforce it on
		// mutations; without one, auth is off (a hand-started dev port - a
		// loopback reader is harmless, and there is no secret to present).
		this->mTokenFilePath = tokenFilePath;
		if (!tokenFilePath.empty())
		{
			this->mToken = AssetDatabase::generateId();
			std::error_code ignored;
			std::filesystem::create_directories(
				std::filesystem::path(tokenFilePath).parent_path(), ignored);
			FILE* file = std::fopen(tokenFilePath.c_str(), "wb");
			if (!file)
			{
				this->mServer.stop();
				return false;
			}
			// the port too, so a client that started the editor on an ephemeral
			// port (0) can discover it: "<port>\n<token>\n"
			std::fprintf(file, "%u\n%s\n",
				static_cast<unsigned>(this->mServer.getPort()),
				this->mToken.c_str());
			std::fclose(file);
		}
		return true;
	}
	//---------------------------------------------------------
	void EditorControlServer::stop()
	{
		this->mServer.stop();
		if (!this->mTokenFilePath.empty())
		{
			std::error_code ignored;
			std::filesystem::remove(this->mTokenFilePath, ignored);
			this->mTokenFilePath.clear();
		}
		this->mToken.clear();
		this->mAuthenticated = false;
	}
	//---------------------------------------------------------
	void EditorControlServer::update(EditorControlContext const& context)
	{
		if (!this->mServer.isListening())
		{
			return;
		}
		this->mServer.update(
			[this, &context](HttpRequest const& request) -> HttpResponse
			{
				return this->handleHttp(request, context);
			});
	}
	//---------------------------------------------------------
	//--- HTTP + JSON-RPC transport ---------------------------
	//---------------------------------------------------------
	HttpResponse EditorControlServer::handleHttp(HttpRequest const& request,
		EditorControlContext const& context)
	{
		HttpResponse response;
		// the ONE MCP endpoint: POST /mcp. Everything else is refused.
		String path = request.target;
		const size_t query = path.find('?');
		if (query != String::npos)
		{
			path = path.substr(0, query);
		}
		if (path != "/mcp")
		{
			response.status = 404;
			response.reason = "Not Found";
			response.contentType = "text/plain";
			response.body = "not found (the MCP endpoint is POST /mcp)";
			return response;
		}
		if (request.method == "GET")
		{
			// the optional Streamable-HTTP GET-SSE stream is not implemented -
			// this endpoint is POST-only (request/response tool calls)
			response.status = 405;
			response.reason = "Method Not Allowed";
			response.contentType = "text/plain";
			response.extraHeaders.push_back({ "Allow", "POST" });
			response.body = "SSE streaming is not supported; use POST /mcp";
			return response;
		}
		if (request.method == "DELETE")
		{
			// MCP session teardown - stateless server, nothing to release
			response.status = 200;
			response.body = "{}";
			return response;
		}
		if (request.method != "POST")
		{
			response.status = 405;
			response.reason = "Method Not Allowed";
			response.contentType = "text/plain";
			response.extraHeaders.push_back({ "Allow", "POST" });
			response.body = "use POST /mcp";
			return response;
		}

		// auth is per-request: an Authorization: Bearer <token> header. With no
		// token configured, auth is off (every request counts as authenticated).
		bool authenticated = this->mToken.empty();
		if (!this->mToken.empty())
		{
			const String authorization = request.header("authorization");
			const String prefix = "Bearer ";
			if (authorization.size() > prefix.size() &&
				authorization.compare(0, prefix.size(), prefix) == 0 &&
				authorization.substr(prefix.size()) == this->mToken)
			{
				authenticated = true;
			}
		}

		JsonValue body;
		if (!JsonValue::parse(request.body, body))
		{
			// JSON parse error -> a JSON-RPC error with a null id
			response.body = jsonRpcError(JsonValue(), -32700,
				"parse error").serialize();
			return response;
		}

		if (body.isArray())
		{
			// a JSON-RPC batch: process each, drop notifications from the reply
			JsonValue batch = JsonValue::array();
			for (size_t i = 0; i < body.size(); ++i)
			{
				bool isNotification = false;
				JsonValue reply = this->dispatchJsonRpc(body.at(i),
					authenticated, context, isNotification);
				if (!isNotification)
				{
					batch.push(reply);
				}
			}
			if (batch.size() == 0)
			{
				response.status = 202;
				response.reason = "Accepted";
				response.contentType = "";	// no body for an all-notification batch
				return response;
			}
			response.body = batch.serialize();
			return response;
		}

		bool isNotification = false;
		JsonValue reply = this->dispatchJsonRpc(body, authenticated, context,
			isNotification);
		if (isNotification)
		{
			// notifications get no JSON-RPC response body (202 Accepted)
			response.status = 202;
			response.reason = "Accepted";
			response.contentType = "";
			return response;
		}
		response.body = reply.serialize();
		return response;
	}
	//---------------------------------------------------------
	JsonValue EditorControlServer::dispatchJsonRpc(JsonValue const& request,
		bool authenticated, EditorControlContext const& context,
		bool& isNotification)
	{
		isNotification = false;
		if (!request.isObject() || !request.get("method").isString())
		{
			return jsonRpcError(JsonValue(), -32600, "invalid request");
		}
		// a JSON-RPC notification carries no id
		const bool hasId = request.has("id");
		isNotification = !hasId;
		const JsonValue id = hasId ? request.get("id") : JsonValue();
		const String method = request.get("method").asString();
		const JsonValue params = request.get("params");

		// MCP lifecycle notifications (notifications/initialized, .../cancelled,
		// ...): acknowledged with no response body
		if (method.rfind("notifications/", 0) == 0)
		{
			isNotification = true;
			return JsonValue();
		}
		if (isNotification)
		{
			// any other id-less request: treat as a notification (no response)
			return JsonValue();
		}

		if (method == "initialize")
		{
			JsonValue result = JsonValue::object();
			// echo the client's requested protocol version when it sent one
			// (maximizes interop), else advertise ours
			String protocolVersion = MCP_PROTOCOL_VERSION;
			if (params.isObject() &&
				params.get("protocolVersion").isString() &&
				!params.get("protocolVersion").asString().empty())
			{
				protocolVersion = params.get("protocolVersion").asString();
			}
			result.set("protocolVersion", JsonValue(protocolVersion));
			JsonValue capabilities = JsonValue::object();
			JsonValue tools = JsonValue::object();
			tools.set("listChanged", JsonValue(false));
			capabilities.set("tools", tools);
			result.set("capabilities", capabilities);
			JsonValue serverInfo = JsonValue::object();
			serverInfo.set("name", JsonValue("orkige-editor"));
			serverInfo.set("version", JsonValue(String(ORKIGE_EDITOR_VERSION)));
			result.set("serverInfo", serverInfo);
			result.set("instructions", JsonValue(
				"Drive the running Orkige editor: inspect and mutate the "
				"GameObject hierarchy and components, manage scenes/projects, "
				"control play mode, take screenshots. Mutations need the "
				"Authorization: Bearer <token> header."));
			return jsonRpcResult(id, result);
		}
		if (method == "ping")
		{
			return jsonRpcResult(id, JsonValue::object());
		}
		if (method == "tools/list")
		{
			JsonValue result = JsonValue::object();
			result.set("tools", buildToolList());
			return jsonRpcResult(id, result);
		}
		if (method == "tools/call")
		{
			return jsonRpcResult(id,
				this->runToolCall(params, authenticated, context));
		}
		return jsonRpcError(id, -32601, "method not found: " + method);
	}
	//---------------------------------------------------------
	JsonValue EditorControlServer::runToolCall(JsonValue const& params,
		bool authenticated, EditorControlContext const& context)
	{
		// build the MCP tool result: content[] (+ structuredContent) / isError.
		// A refused/failed verb is reported as isError=true content (the MCP
		// convention that lets the model see and react to tool errors), NOT a
		// JSON-RPC protocol error.
		auto toolError = [](String const& message) -> JsonValue
		{
			JsonValue text = JsonValue::object();
			text.set("type", JsonValue("text"));
			text.set("text", JsonValue(message));
			JsonValue content = JsonValue::array();
			content.push(text);
			JsonValue result = JsonValue::object();
			result.set("content", content);
			result.set("isError", JsonValue(true));
			return result;
		};

		if (!params.isObject() || !params.get("name").isString() ||
			params.get("name").asString().empty())
		{
			return toolError("tools/call needs a 'name'");
		}
		const String name = params.get("name").asString();

		DebugMessage request(name);
		applyArguments(request, params.get("arguments"));

		// per-request auth: the verb handler's requireAuth reads mAuthenticated
		this->mAuthenticated = authenticated;
		this->runVerb(request, context);

		const JsonValue structured = replyToJson(this->mReply);
		JsonValue result = JsonValue::object();
		JsonValue content = JsonValue::array();
		JsonValue text = JsonValue::object();
		text.set("type", JsonValue("text"));
		if (this->mReplyIsError)
		{
			text.set("text", JsonValue(
				this->mReply.get(DebugProtocol::FIELD_MESSAGE)));
			content.push(text);
			result.set("content", content);
			result.set("isError", JsonValue(true));
		}
		else
		{
			text.set("text", JsonValue(structured.serialize()));
			content.push(text);
			result.set("content", content);
			result.set("structuredContent", structured);
			result.set("isError", JsonValue(false));
		}
		return result;
	}
	//---------------------------------------------------------
	JsonValue EditorControlServer::buildToolList()
	{
		JsonValue tools = JsonValue::array();
		for (ToolSpec const& spec : toolSpecs())
		{
			JsonValue schema = JsonValue::object();
			schema.set("type", JsonValue("object"));
			JsonValue properties = JsonValue::object();
			JsonValue required = JsonValue::array();
			for (PropSpec const& prop : spec.properties)
			{
				JsonValue property = JsonValue::object();
				property.set("type", JsonValue(String(prop.type)));
				property.set("description", JsonValue(String(prop.description)));
				properties.set(prop.name, property);
				if (prop.required)
				{
					required.push(JsonValue(String(prop.name)));
				}
			}
			schema.set("properties", properties);
			if (required.size() > 0)
			{
				schema.set("required", required);
			}
			JsonValue tool = JsonValue::object();
			tool.set("name", JsonValue(String(spec.name)));
			tool.set("description", JsonValue(String(spec.description)));
			tool.set("inputSchema", schema);
			tools.push(tool);
		}
		return tools;
	}
	//---------------------------------------------------------
	//--- verb reply buffering (were DebugServer sends) -------
	//---------------------------------------------------------
	void EditorControlServer::runVerb(DebugMessage const& request,
		EditorControlContext const& context)
	{
		this->mReply = DebugMessage(MSG_OK);
		this->mReplyIsError = false;
		this->handleMessage(request, context);
	}
	//---------------------------------------------------------
	void EditorControlServer::sendOk(String const& req, DebugMessage& payload)
	{
		(void)req;
		payload.type = MSG_OK;
		this->mReply = payload;
		this->mReplyIsError = false;
	}
	//---------------------------------------------------------
	void EditorControlServer::sendOk(String const& req)
	{
		(void)req;
		this->mReply = DebugMessage(MSG_OK);
		this->mReplyIsError = false;
	}
	//---------------------------------------------------------
	void EditorControlServer::sendErr(String const& req, String const& message)
	{
		(void)req;
		DebugMessage err(MSG_ERR);
		err.set(DebugProtocol::FIELD_MESSAGE, message);
		this->mReply = err;
		this->mReplyIsError = true;
	}
	//---------------------------------------------------------
	bool EditorControlServer::requireAuth(String const& req)
	{
		// no token configured => auth disabled (developer convenience for a
		// hand-started control port); with a token, a valid bearer is required
		if (this->mToken.empty() || this->mAuthenticated)
		{
			return true;
		}
		this->sendErr(req,
			"unauthenticated: present the Authorization: Bearer <token> header");
		return false;
	}
	//---------------------------------------------------------
	//--- the WP #80 verb handler (reused wholesale) ----------
	//---------------------------------------------------------
	void EditorControlServer::handleMessage(DebugMessage const& request,
		EditorControlContext const& context)
	{
		const String type = request.type;
		const String req = request.get(DebugProtocol::FIELD_REQ);
		EditorState& state = *context.state;
		EditorCore& core = *context.core;
		GameObjectManager& manager = *context.gameObjectManager;

		// auth gate: everything but the pure reads needs a prior valid token
		if (!isReadVerb(type) && !this->requireAuth(req))
		{
			return;
		}

		// destructive open/new/close verbs clobber the current world; honor the
		// dirty-state policy (refuse unless force=1 or the scene is clean)
		auto clobberRefused = [&](void) -> bool
		{
			if (core.isSceneDirty() && request.get("force") != "1")
			{
				this->sendErr(req, "scene has unsaved changes - save_scene "
					"first or pass force=1");
				return true;
			}
			return false;
		};

		//--- handshake / status ------------------------------
		if (type == "hello")
		{
			if (!this->mToken.empty() &&
				request.get(DebugProtocol::FIELD_TOKEN) != this->mToken)
			{
				this->sendErr(req, "auth failed: wrong or missing token");
				return;
			}
			this->mAuthenticated = true;
			DebugMessage ok(MSG_OK);
			ok.set("editor_version", ORKIGE_EDITOR_VERSION);
			ok.set("protocol_version",
				std::to_string(DebugProtocol::VERSION));
			ok.set("authenticated", "1");
			this->sendOk(req, ok);
			return;
		}
		if (type == "ping")
		{
			this->sendOk(req);
			return;
		}
		if (type == "get_state")
		{
			DebugMessage ok(MSG_OK);
			ok.set("project_loaded", state.project.isLoaded() ? "1" : "0");
			ok.set("project_name", state.project.getName());
			ok.set("project_root", state.project.getRootDirectory());
			ok.set("scene_path", state.currentScenePath);
			ok.set("scene_dirty", core.isSceneDirty() ? "1" : "0");
			ok.set("selected", core.getSelectedObjectId());
			ok.set("object_count",
				std::to_string(manager.getGameObjects().size()));
			ok.set("can_undo", core.canUndo() ? "1" : "0");
			ok.set("can_redo", core.canRedo() ? "1" : "0");
			ok.set("play_mode", playSessionModeName(*context.play));
			this->sendOk(req, ok);
			return;
		}

		//--- hierarchy read ----------------------------------
		if (type == "list_hierarchy")
		{
			StringVector ids;
			StringVector parents;
			StringVector activeSelf;
			StringVector activeHierarchy;
			for (auto const& [id, gameObject] : manager.getGameObjects())
			{
				ids.push_back(id);
				parents.push_back(gameObject->getParentId());
				activeSelf.push_back(gameObject->isActiveSelf() ? "1" : "0");
				activeHierarchy.push_back(
					gameObject->isActiveInHierarchy() ? "1" : "0");
			}
			DebugMessage ok(MSG_OK);
			ok.setList(DebugProtocol::LIST_IDS, ids);
			ok.setList(DebugProtocol::LIST_PARENTS, parents);
			ok.setList(DebugProtocol::LIST_ACTIVE, activeSelf);
			ok.setList("active_hierarchy", activeHierarchy);
			ok.set("selected", core.getSelectedObjectId());
			this->sendOk(req, ok);
			return;
		}
		if (type == "get_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			optr<GameObject> gameObject = manager.getGameObject(id).lock();
			if (!gameObject)
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			StringVector components;
			for (auto const& [componentType, component] :
				gameObject->getComponents())
			{
				components.push_back(componentType.getName());
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, id);
			ok.set("parent", gameObject->getParentId());
			ok.set("active_self", gameObject->isActiveSelf() ? "1" : "0");
			ok.set("active_hierarchy",
				gameObject->isActiveInHierarchy() ? "1" : "0");
			ok.setList(DebugProtocol::LIST_COMPONENTS, components);
			this->sendOk(req, ok);
			return;
		}

		//--- generic reflected property read (task #94 P5a) --
		// GENERIC over the property registry: every reflected property of the
		// named component crosses back as a field (name -> canonical string)
		// plus four parallel metadata lists (names/kinds/hints/read-only) so an
		// agent can DISCOVER the property set without a hardcoded allowlist. New
		// components and script exports appear here with zero server code.
		// TRANSIENT properties (runtime telemetry) ARE included and marked in
		// the "transient" list - an agent inspecting a running object may want
		// them; HIDDEN and getter-less properties are skipped.
		if (type == "get_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			optr<GameObject> gameObject = manager.getGameObject(id).lock();
			if (!gameObject)
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			const TypeInfo componentType(component);
			if (!gameObject->hasComponent(componentType))
			{
				this->sendErr(req, "no " + component + " on '" + id + "'");
				return;
			}
			PropertySchema const* schema = TypeManager::getSingleton()
				.getPropertySchema(componentType.getId());
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, id);
			ok.set(DebugProtocol::FIELD_COMPONENT, component);
			StringVector names;
			StringVector kinds;
			StringVector hints;
			StringVector readonly;
			StringVector transient;
			if (schema)
			{
				for (PropertyDesc const& desc : schema->properties())
				{
					if (desc.hasFlag(PROP_HIDDEN) || !desc.get)
					{
						continue; // never expose a hidden / unreadable property
					}
					String value;
					if (!core.getObjectProperty(id, component, desc.name, value))
					{
						continue;
					}
					ok.set(desc.name, value);
					names.push_back(desc.name);
					kinds.push_back(kindName(desc.kind));
					hints.push_back(propertyHint(desc));
					readonly.push_back(desc.isReadOnly() ? "1" : "0");
					transient.push_back(
						desc.hasFlag(PROP_TRANSIENT) ? "1" : "0");
				}
			}
			ok.setList("properties", names);
			ok.setList("kinds", kinds);
			ok.setList("hints", hints);
			ok.setList("readonly", readonly);
			ok.setList("transient", transient);
			this->sendOk(req, ok);
			return;
		}

		//--- generic reflected property write (task #94 P5a) -
		// GENERIC over the property registry, undoable: every request field
		// that names a writable reflected property is applied through
		// EditorCore::applyPropertyChange (the undoable PropertyChangeCommand /
		// reflected setter - the change takes effect live). Validated first, so
		// an unknown/read-only/unparseable property is reported as an isError
		// WITHOUT touching the object (atomic). Pass changes at the top level or
		// inside a 'properties' object (applyArguments flattens both).
		if (type == "set_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			optr<GameObject> gameObject = manager.getGameObject(id).lock();
			if (!gameObject)
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			const TypeInfo componentType(component);
			if (!gameObject->hasComponent(componentType))
			{
				this->sendErr(req, "no " + component + " on '" + id + "'");
				return;
			}
			PropertySchema const* schema = TypeManager::getSingleton()
				.getPropertySchema(componentType.getId());
			// the request fields that are NOT properties (routing/control)
			auto isReserved = [](String const& key) -> bool
			{
				return key == DebugProtocol::FIELD_ID ||
					key == DebugProtocol::FIELD_COMPONENT ||
					key == DebugProtocol::FIELD_REQ ||
					key == DebugProtocol::FIELD_TOKEN ||
					key == "force";
			};
			// validate every provided property before applying any (atomic)
			struct Pending { String name; String before; String after; };
			std::vector<Pending> pending;
			for (auto const& field : request.fields)
			{
				if (isReserved(field.first))
				{
					continue;
				}
				PropertyDesc const* desc =
					schema ? schema->find(field.first) : 0;
				if (!desc)
				{
					this->sendErr(req, "unknown property '" + field.first +
						"' on " + component);
					return;
				}
				if (desc->isReadOnly())
				{
					this->sendErr(req, "property '" + field.first +
						"' on " + component + " is read-only");
					return;
				}
				String before;
				core.getObjectProperty(id, component, field.first, before);
				pending.push_back({ field.first, before, field.second });
			}
			if (pending.empty())
			{
				this->sendErr(req, "set_component: no writable properties given "
					"for " + component);
				return;
			}
			// merge the whole set into ONE undo step
			const unsigned int session = core.beginMergeSession();
			bool applied = true;
			for (Pending const& change : pending)
			{
				if (!core.applyPropertyChange(id, component, change.name,
					change.before, change.after, session))
				{
					applied = false;
					this->sendErr(req, "set '" + change.name + "' on " +
						component + " was refused (bad value?)");
					return;
				}
			}
			if (!applied)
			{
				this->sendErr(req, "set_component '" + component +
					"' on '" + id + "' was refused (see the editor log)");
				return;
			}
			this->sendOk(req);
			return;
		}

		//--- object mutation (undoable) ----------------------
		if (type == "create_object")
		{
			String id = request.get(DebugProtocol::FIELD_ID);
			String mesh = request.get("mesh");
			if (mesh.empty() || mesh == "cube")
			{
				mesh = RenderWorld::CUBE_MESH_NAME;
				if (id.empty()) id = core.generateObjectId("Cube");
			}
			if (id.empty()) id = core.generateObjectId("Object");
			if (manager.objectExists(id))
			{
				this->sendErr(req, "object id '" + id + "' already exists");
				return;
			}
			float p[3] = { 0.0f, 0.0f, 0.0f };
			readFloats(request, "position", p, 3);
			if (!core.executeCommand(onew(new CreateObjectCommand(
				id, mesh, Vec3(p[0], p[1], p[2])))))
			{
				this->sendErr(req, "create_object failed - mesh '" + mesh +
					"' did not load");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, id);
			this->sendOk(req, ok);
			return;
		}
		if (type == "delete_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (!manager.objectExists(id))
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			if (!core.executeCommand(onew(new DeleteObjectCommand(id))))
			{
				this->sendErr(req, "delete_object '" + id + "' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "duplicate_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (!manager.objectExists(id))
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			const String newId = core.makeDuplicateId(id);
			if (!core.executeCommand(onew(
				new DuplicateObjectCommand(id, newId))))
			{
				this->sendErr(req, "duplicate_object '" + id + "' was refused");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, newId);
			this->sendOk(req, ok);
			return;
		}
		if (type == "rename_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String newId = request.get("new_id");
			if (!core.renameObject(id, newId))
			{
				this->sendErr(req, "rename '" + id + "' -> '" + newId +
					"' was refused (empty/duplicate/unknown)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, newId);
			this->sendOk(req, ok);
			return;
		}
		if (type == "reparent_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String parent = request.get("parent");	// "" = make a root
			if (!core.reparentObject(id, parent))
			{
				this->sendErr(req, "reparent '" + id + "' onto '" + parent +
					"' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "set_active")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (!core.setObjectActive(id,
				request.get(DebugProtocol::FIELD_VALUE) == "1"))
			{
				this->sendErr(req, "set_active on '" + id + "' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "group_objects")
		{
			if (!core.groupSelected())
			{
				this->sendErr(req, "group needs a selection");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, core.getSelectedObjectId());
			this->sendOk(req, ok);
			return;
		}

		//--- component add/remove ----------------------------
		if (type == "add_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			if (!core.addComponentToObject(id, component))
			{
				this->sendErr(req, "add_component '" + component + "' on '" +
					id + "' was refused (already present / unknown type)");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "remove_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			String blockedBy;
			if (!core.canRemoveComponent(id, component, &blockedBy))
			{
				this->sendErr(req, "remove_component '" + component +
					"' refused - '" + blockedBy + "' depends on it");
				return;
			}
			if (!core.removeComponentFromObject(id, component))
			{
				this->sendErr(req, "remove_component '" + component + "' on '" +
					id + "' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "list_addable_components")
		{
			DebugMessage ok(MSG_OK);
			ok.setList(DebugProtocol::LIST_COMPONENTS,
				core.getAddableComponentTypes());
			this->sendOk(req, ok);
			return;
		}

		//--- selection / undo-redo ---------------------------
		if (type == "select")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (id.empty())
			{
				core.clearSelection();
			}
			else if (!manager.objectExists(id))
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			else
			{
				core.selectObject(id);
			}
			this->sendOk(req);
			return;
		}
		if (type == "undo")
		{
			if (!core.undo())
			{
				this->sendErr(req, "nothing to undo");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "redo")
		{
			if (!core.redo())
			{
				this->sendErr(req, "nothing to redo");
				return;
			}
			this->sendOk(req);
			return;
		}

		//--- scene / project documents -----------------------
		if (type == "new_scene")
		{
			if (clobberRefused()) return;
			newScene(state, core);
			this->sendOk(req);
			return;
		}
		if (type == "open_scene")
		{
			if (clobberRefused()) return;
			if (!openSceneFromPath(state, core,
				request.get(DebugProtocol::FIELD_SCENE)))
			{
				this->sendErr(req, "open_scene failed (see the editor log)");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "save_scene")
		{
			String path = request.get(DebugProtocol::FIELD_SCENE);
			if (path.empty()) path = state.currentScenePath;
			if (path.empty())
			{
				this->sendErr(req, "save_scene needs a 'scene' path (the "
					"current scene is untitled)");
				return;
			}
			if (!saveSceneToPath(state, core, path))
			{
				this->sendErr(req, "save_scene failed (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_SCENE, path);
			this->sendOk(req, ok);
			return;
		}
		if (type == "open_project")
		{
			if (clobberRefused()) return;
			if (!openProjectFromPath(state, core,
				request.get("path")))
			{
				this->sendErr(req, "open_project failed (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("project_root", state.project.getRootDirectory());
			ok.set("scene_path", state.currentScenePath);
			this->sendOk(req, ok);
			return;
		}
		if (type == "new_project")
		{
			if (clobberRefused()) return;
			if (!newProjectAtPath(state, core, request.get("path")))
			{
				this->sendErr(req, "new_project failed (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("project_root", state.project.getRootDirectory());
			this->sendOk(req, ok);
			return;
		}
		if (type == "close_project")
		{
			if (clobberRefused()) return;
			closeProject(state, core);
			this->sendOk(req);
			return;
		}

		//--- play control (translated to the ONE player protocol) --
		if (type == "play")
		{
			if (context.play->isActive())
			{
				this->sendErr(req, "already playing");
				return;
			}
			if (!startPlay(*context.play, manager, state.project))
			{
				this->sendErr(req, "play could not start (see the editor log)");
				return;
			}
			// long op: the player boots over subsequent frames - report the
			// launch as accepted and let the client poll get_state for progress
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("play_mode", playSessionModeName(*context.play));
			this->sendOk(req, ok);
			return;
		}
		if (type == "stop")
		{
			if (!context.play->isActive())
			{
				this->sendErr(req, "not playing");
				return;
			}
			requestStopPlay(*context.play);
			this->sendOk(req);
			return;
		}
		if (type == "pause" || type == "resume" || type == "step")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player to " + type);
				return;
			}
			if (type == "pause")
			{
				context.play->client.send(DebugMessage(DebugProtocol::MSG_PAUSE));
				context.play->mode = PlaySession::Mode::Paused;
			}
			else if (type == "resume")
			{
				context.play->client.send(DebugMessage(DebugProtocol::MSG_RESUME));
				context.play->mode = PlaySession::Mode::Playing;
			}
			else // step
			{
				context.play->client.send(DebugMessage(DebugProtocol::MSG_STEP));
			}
			this->sendOk(req);
			return;
		}

		//--- screenshot (out of band; returns the written path) --
		if (type == "screenshot")
		{
			const String path = request.get("path");
			if (path.empty())
			{
				this->sendErr(req, "screenshot needs a 'path'");
				return;
			}
			if (request.get("window") == "1")
			{
				// the whole editor window (chrome included)
				RenderSystem::get()->saveWindowContents(path);
			}
			else
			{
				// the chrome-free scene viewport (the EditorSceneRT RTT)
				if (!context.sceneTarget || !context.sceneTarget->texture)
				{
					this->sendErr(req, "no scene render target to capture");
					return;
				}
				context.sceneTarget->texture->writeContentsToFile(path);
			}
			DebugMessage ok(MSG_OK);
			ok.set("path", path);
			this->sendOk(req, ok);
			return;
		}

		//--- asset listing -----------------------------------
		if (type == "list_assets")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("project_root", state.project.getRootDirectory());
			StringVector ids;
			StringVector paths;
			StringVector names;
			if (optr<AssetDatabase> const& database =
				state.project.getAssetDatabase())
			{
				// page defensively so a big project never blows the 64KiB frame
				const size_t maxEntries = 500;
				std::vector<AssetEntry> assets = database->listAssets();
				bool truncated = false;
				for (AssetEntry const& asset : assets)
				{
					if (ids.size() >= maxEntries)
					{
						truncated = true;
						break;
					}
					ids.push_back(asset.id);
					paths.push_back(asset.relativePath);
					names.push_back(asset.fileName);
				}
				ok.set("truncated", truncated ? "1" : "0");
			}
			ok.setList("asset_ids", ids);
			ok.setList("asset_paths", paths);
			ok.setList("asset_names", names);
			ok.setList("scenes", state.project.listScenes());
			this->sendOk(req, ok);
			return;
		}

		//--- console tail ------------------------------------
		if (type == "console_tail")
		{
			int count = 50;
			if (request.has("count"))
			{
				count = std::atoi(request.get("count").c_str());
			}
			if (count < 1) count = 1;
			if (count > 200) count = 200;	// respect the frame cap
			StringVector lines;
			StringVector levels;
			{
				std::lock_guard<std::mutex> lock(context.console->mutex);
				std::vector<ConsoleLine> const& all = context.console->lines;
				size_t start = all.size() >
					static_cast<size_t>(count)
					? all.size() - static_cast<size_t>(count) : 0;
				for (size_t i = start; i < all.size(); ++i)
				{
					lines.push_back(all[i].text);
					levels.push_back(all[i].level == ConsoleLevel::Error
						? "error" : all[i].level == ConsoleLevel::Warning
						? "warning" : "info");
				}
			}
			DebugMessage ok(MSG_OK);
			ok.setList("lines", lines);
			ok.setList("levels", levels);
			this->sendOk(req, ok);
			return;
		}

		//--- unknown -----------------------------------------
		this->sendErr(req, "unknown command '" + type + "'");
	}
	//---------------------------------------------------------
	//--- EditorControlSelfTest (threaded HTTP/MCP client) ----
	//---------------------------------------------------------
	namespace
	{
		//! lower-case an ASCII string (case-insensitive header search)
		String toLower(String const& value)
		{
			String out(value);
			for (char& c : out)
			{
				if (c >= 'A' && c <= 'Z')
				{
					c = static_cast<char>(c - 'A' + 'a');
				}
			}
			return out;
		}
		//! connect a BLOCKING TCP socket to 127.0.0.1:port (with a short retry so
		//! a not-quite-ready listener is tolerated); INVALID on failure. A recv
		//! timeout guards against a stuck read hanging the ctest.
		DebugSocketUtil::SocketHandle connectBlocking(unsigned short port)
		{
			DebugSocketUtil::initialise();
			for (int attempt = 0; attempt < 20; ++attempt)
			{
				DebugSocketUtil::SocketHandle handle =
					static_cast<DebugSocketUtil::SocketHandle>(
						::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
				if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
				{
					return DebugSocketUtil::INVALID_SOCKET_HANDLE;
				}
#ifdef _WIN32
				DWORD timeoutMs = 15000;
				::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
					reinterpret_cast<char*>(&timeoutMs), sizeof(timeoutMs));
#else
				struct timeval tv;
				tv.tv_sec = 15;
				tv.tv_usec = 0;
				::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
				struct sockaddr_in address;
				std::memset(&address, 0, sizeof(address));
				address.sin_family = AF_INET;
				address.sin_port = htons(port);
				::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
				if (::connect(handle,
					reinterpret_cast<struct sockaddr*>(&address),
					sizeof(address)) == 0)
				{
					return handle;
				}
				DebugSocketUtil::closeSocket(handle);
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			return DebugSocketUtil::INVALID_SOCKET_HANDLE;
		}
		//! send the whole buffer over a blocking socket
		bool sendAll(DebugSocketUtil::SocketHandle handle, String const& data)
		{
			size_t sent = 0;
			while (sent < data.size())
			{
				const long n = static_cast<long>(::send(handle,
					data.data() + sent, data.size() - sent, 0));
				if (n <= 0)
				{
					return false;
				}
				sent += static_cast<size_t>(n);
			}
			return true;
		}
		//! read one full HTTP response (status + Content-Length body) from a
		//! blocking socket; false on error/timeout
		bool readHttpResponse(DebugSocketUtil::SocketHandle handle,
			int& status, String& body)
		{
			String buffer;
			char chunk[4096];
			size_t headerEnd = String::npos;
			while ((headerEnd = buffer.find("\r\n\r\n")) == String::npos)
			{
				const long n = static_cast<long>(::recv(handle, chunk,
					sizeof(chunk), 0));
				if (n <= 0)
				{
					return false;
				}
				buffer.append(chunk, static_cast<size_t>(n));
				if (buffer.size() > 8 * 1024 * 1024)
				{
					return false;
				}
			}
			const size_t space = buffer.find(' ');
			if (space == String::npos)
			{
				return false;
			}
			status = std::atoi(buffer.c_str() + space + 1);
			const String headerSection = toLower(buffer.substr(0, headerEnd));
			size_t contentLength = 0;
			const size_t lengthAt = headerSection.find("content-length:");
			if (lengthAt != String::npos)
			{
				contentLength = static_cast<size_t>(std::strtoul(
					buffer.c_str() + lengthAt +
					std::strlen("content-length:"), NULL, 10));
			}
			const size_t bodyStart = headerEnd + 4;
			while (buffer.size() < bodyStart + contentLength)
			{
				const long n = static_cast<long>(::recv(handle, chunk,
					sizeof(chunk), 0));
				if (n <= 0)
				{
					return false;
				}
				buffer.append(chunk, static_cast<size_t>(n));
			}
			body = buffer.substr(bodyStart, contentLength);
			return true;
		}
	}
	//---------------------------------------------------------
	EditorControlSelfTest::~EditorControlSelfTest()
	{
		if (this->mThread.joinable())
		{
			this->mThread.join();
		}
	}
	//---------------------------------------------------------
	void EditorControlSelfTest::begin(unsigned short port,
		std::string const& token, std::string const& screenshotPath)
	{
		this->mToken = token;
		this->mScreenshotPath = screenshotPath;
		this->mActive.store(true);
		this->mDone.store(false);
		this->mPassed.store(false);
		this->mThread = std::thread(&EditorControlSelfTest::run, this, port);
	}
	//---------------------------------------------------------
	void EditorControlSelfTest::update(GameObjectManager& manager)
	{
		// the worker verifies the endpoint through its JSON-RPC responses; the
		// manager is unused here (kept for the historical call signature)
		(void)manager;
		if (this->mDone.load() && this->mThread.joinable())
		{
			this->mThread.join();
		}
	}
	//---------------------------------------------------------
	void EditorControlSelfTest::run(unsigned short port)
	{
		DebugSocketUtil::SocketHandle handle = connectBlocking(port);
		auto finish = [this, &handle](bool passed, String const& message)
		{
			if (passed)
			{
				SDL_Log("orkige_editor: control self-test PASSED");
			}
			else
			{
				SDL_Log("orkige_editor: FAILED %s", message.c_str());
			}
			if (handle != DebugSocketUtil::INVALID_SOCKET_HANDLE)
			{
				DebugSocketUtil::closeSocket(handle);
			}
			this->mPassed.store(passed);
			this->mActive.store(false);
			this->mDone.store(true);
		};
		if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			finish(false, "control self-test: could not connect to the MCP port");
			return;
		}

		int idCounter = 0;
		// POST one JSON-RPC message; returns the parsed response (empty for a
		// notification) and false on any transport/id-echo failure
		auto post = [&](String const& method, JsonValue const& params,
			bool withAuth, bool expectResponse, JsonValue& response) -> bool
		{
			++idCounter;
			JsonValue rpc = JsonValue::object();
			rpc.set("jsonrpc", JsonValue("2.0"));
			if (expectResponse)
			{
				rpc.set("id", JsonValue(idCounter));
			}
			rpc.set("method", JsonValue(method));
			if (!params.isNull())
			{
				rpc.set("params", params);
			}
			const String bodyStr = rpc.serialize();
			String http;
			http += "POST /mcp HTTP/1.1\r\n";
			http += "Host: 127.0.0.1\r\n";
			http += "Content-Type: application/json\r\n";
			http += "Accept: application/json, text/event-stream\r\n";
			if (withAuth)
			{
				http += "Authorization: Bearer ";
				http += this->mToken;
				http += "\r\n";
			}
			http += "Content-Length: " + std::to_string(bodyStr.size()) +
				"\r\n\r\n";
			http += bodyStr;
			if (!sendAll(handle, http))
			{
				return false;
			}
			int status = 0;
			String respBody;
			if (!readHttpResponse(handle, status, respBody))
			{
				return false;
			}
			if (!expectResponse)
			{
				response = JsonValue();
				return status == 202;	// notifications answer 202 Accepted
			}
			if (status != 200)
			{
				return false;
			}
			if (!JsonValue::parse(respBody, response))
			{
				return false;
			}
			return response.get("id").asInt(-1) == idCounter;	// id echo
		};

		JsonValue response;

		// (1) initialize handshake
		JsonValue initParams = JsonValue::object();
		initParams.set("protocolVersion",
			JsonValue(EditorControlServer::MCP_PROTOCOL_VERSION));
		initParams.set("capabilities", JsonValue::object());
		JsonValue clientInfo = JsonValue::object();
		clientInfo.set("name", JsonValue("orkige-selftest"));
		clientInfo.set("version", JsonValue("1"));
		initParams.set("clientInfo", clientInfo);
		if (!post("initialize", initParams, false, true, response))
		{
			finish(false, "control self-test: initialize failed");
			return;
		}
		{
			JsonValue const& result = response.get("result");
			if (!result.get("protocolVersion").isString() ||
				result.get("protocolVersion").asString().empty() ||
				result.get("serverInfo").get("name").asString().empty())
			{
				finish(false, "control self-test: initialize result malformed");
				return;
			}
			SDL_Log("orkige_editor: control self-test - initialize OK "
				"(protocol %s, server %s)",
				result.get("protocolVersion").asString().c_str(),
				result.get("serverInfo").get("name").asString().c_str());
		}

		// (2) notifications/initialized (no response body)
		if (!post("notifications/initialized", JsonValue(), false, false,
			response))
		{
			finish(false, "control self-test: initialized notification failed");
			return;
		}

		// (3) tools/list must advertise the core tools with schemas
		if (!post("tools/list", JsonValue(), false, true, response))
		{
			finish(false, "control self-test: tools/list failed");
			return;
		}
		{
			JsonValue const& tools = response.get("result").get("tools");
			bool hasCreate = false;
			bool hasList = false;
			bool schemasOk = true;
			for (size_t i = 0; i < tools.size(); ++i)
			{
				const String name = tools.at(i).get("name").asString();
				if (!tools.at(i).get("inputSchema").isObject())
				{
					schemasOk = false;
				}
				if (name == "create_object") hasCreate = true;
				if (name == "list_hierarchy") hasList = true;
			}
			if (!hasCreate || !hasList || !schemasOk || tools.size() < 10)
			{
				finish(false, "control self-test: tools/list missing tools/"
					"schemas");
				return;
			}
			SDL_Log("orkige_editor: control self-test - tools/list OK "
				"(%zu tools)", tools.size());
		}

		// (4) tools/call create_object (authed mutation)
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue("create_object"));
			JsonValue args = JsonValue::object();
			args.set("id", JsonValue("McpProbe"));
			args.set("position", JsonValue("1 2 3"));
			params.set("arguments", args);
			if (!post("tools/call", params, true, true, response))
			{
				finish(false, "control self-test: create_object call failed");
				return;
			}
			JsonValue const& result = response.get("result");
			if (result.get("isError").asBool(true) ||
				result.get("structuredContent").get("id").asString() !=
					"McpProbe")
			{
				finish(false, "control self-test: create_object did not report "
					"the created id");
				return;
			}
			SDL_Log("orkige_editor: control self-test - create_object OK "
				"('McpProbe')");
		}

		// (5) tools/call list_hierarchy (read, no auth) must include it
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue("list_hierarchy"));
			if (!post("tools/call", params, false, true, response))
			{
				finish(false, "control self-test: list_hierarchy call failed");
				return;
			}
			JsonValue const& ids =
				response.get("result").get("structuredContent").get("ids");
			bool found = false;
			for (size_t i = 0; i < ids.size(); ++i)
			{
				if (ids.at(i).asString() == "McpProbe")
				{
					found = true;
				}
			}
			if (!found)
			{
				finish(false, "control self-test: created object missing from "
					"list_hierarchy");
				return;
			}
			SDL_Log("orkige_editor: control self-test - list_hierarchy OK "
				"(%zu objects, McpProbe present)", ids.size());
		}

		// (6) AUTH REJECTION: a mutation WITHOUT the bearer token must be
		// refused (isError), and no object may be created
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue("create_object"));
			JsonValue args = JsonValue::object();
			args.set("id", JsonValue("McpProbeUnauthed"));
			params.set("arguments", args);
			if (!post("tools/call", params, false, true, response))
			{
				finish(false, "control self-test: unauthenticated call had a "
					"transport failure");
				return;
			}
			if (!response.get("result").get("isError").asBool(false))
			{
				finish(false, "control self-test: a mutation WITHOUT a token was "
					"NOT rejected");
				return;
			}
			SDL_Log("orkige_editor: control self-test - unauthenticated "
				"mutation correctly rejected");
		}

		// (7) tools/call screenshot (authed) - returns the path, file written
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue("screenshot"));
			JsonValue args = JsonValue::object();
			args.set("path", JsonValue(String(this->mScreenshotPath)));
			params.set("arguments", args);
			if (!post("tools/call", params, true, true, response))
			{
				finish(false, "control self-test: screenshot call failed");
				return;
			}
			JsonValue const& result = response.get("result");
			if (result.get("isError").asBool(true) ||
				result.get("structuredContent").get("path").asString() !=
					this->mScreenshotPath)
			{
				finish(false, "control self-test: screenshot did not return the "
					"path");
				return;
			}
			std::error_code ignored;
			if (!std::filesystem::exists(this->mScreenshotPath, ignored) ||
				std::filesystem::file_size(this->mScreenshotPath, ignored) == 0)
			{
				finish(false, "control self-test: screenshot file '" +
					this->mScreenshotPath + "' was not written");
				return;
			}
			SDL_Log("orkige_editor: control self-test - screenshot OK "
				"(wrote '%s')", this->mScreenshotPath.c_str());
		}

		// a tools/call helper returning the structuredContent (or isError)
		auto callTool = [&](String const& tool, JsonValue const& args,
			bool withAuth, JsonValue& structured, bool& isError) -> bool
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue(tool));
			params.set("arguments", args);
			JsonValue reply;
			if (!post("tools/call", params, withAuth, true, reply))
			{
				return false;
			}
			JsonValue const& result = reply.get("result");
			isError = result.get("isError").asBool(true);
			structured = result.get("structuredContent");
			return true;
		};

		// (8) GENERIC get_component (task #94 P5a): the reflected Transform
		// property set crosses back by name, with discovery metadata lists.
		// McpProbe was created at position "1 2 3".
		{
			JsonValue args = JsonValue::object();
			args.set("id", JsonValue("McpProbe"));
			args.set("component", JsonValue("TransformComponent"));
			JsonValue structured;
			bool isError = true;
			if (!callTool("get_component", args, false, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: generic get_component failed");
				return;
			}
			// the value crosses back by property name
			if (structured.get("position").asString() != "1 2 3")
			{
				finish(false, "control self-test: get_component TransformComponent "
					"did not report position '1 2 3'");
				return;
			}
			// the discovery list advertises the reflected property set
			JsonValue const& names = structured.get("properties");
			bool hasPosition = false;
			for (size_t i = 0; i < names.size(); ++i)
			{
				if (names.at(i).asString() == "position") hasPosition = true;
			}
			if (!hasPosition || !structured.get("kinds").isArray())
			{
				finish(false, "control self-test: get_component missing the "
					"property discovery lists");
				return;
			}
			SDL_Log("orkige_editor: control self-test - generic get_component OK "
				"(%zu properties, position='1 2 3')", names.size());
		}

		// (9) GENERIC set_component by NAME (undoable): move the Transform via a
		// 'properties' object, read it back through get_component.
		{
			JsonValue props = JsonValue::object();
			props.set("position", JsonValue("4 5 6"));
			JsonValue args = JsonValue::object();
			args.set("id", JsonValue("McpProbe"));
			args.set("component", JsonValue("TransformComponent"));
			args.set("properties", props);
			JsonValue structured;
			bool isError = true;
			if (!callTool("set_component", args, true, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: generic set_component "
					"(Transform position) failed");
				return;
			}
			JsonValue readArgs = JsonValue::object();
			readArgs.set("id", JsonValue("McpProbe"));
			readArgs.set("component", JsonValue("TransformComponent"));
			if (!callTool("get_component", readArgs, false, structured, isError) ||
				isError || structured.get("position").asString() != "4 5 6")
			{
				finish(false, "control self-test: set_component did not move the "
					"Transform to '4 5 6'");
				return;
			}
			SDL_Log("orkige_editor: control self-test - generic set_component "
				"(Transform position -> '4 5 6') OK");
		}

		// (10) a DIFFERENT component by name end to end: add a SpriteComponent,
		// set its integer zOrder property, read it back. Proves the generic
		// path is not Transform-specific.
		{
			JsonValue addArgs = JsonValue::object();
			addArgs.set("id", JsonValue("McpProbe"));
			addArgs.set("component", JsonValue("SpriteComponent"));
			JsonValue structured;
			bool isError = true;
			if (!callTool("add_component", addArgs, true, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: add SpriteComponent failed");
				return;
			}
			JsonValue props = JsonValue::object();
			props.set("zOrder", JsonValue("7"));
			JsonValue setArgs = JsonValue::object();
			setArgs.set("id", JsonValue("McpProbe"));
			setArgs.set("component", JsonValue("SpriteComponent"));
			setArgs.set("properties", props);
			if (!callTool("set_component", setArgs, true, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: set SpriteComponent zOrder "
					"failed");
				return;
			}
			JsonValue readArgs = JsonValue::object();
			readArgs.set("id", JsonValue("McpProbe"));
			readArgs.set("component", JsonValue("SpriteComponent"));
			if (!callTool("get_component", readArgs, false, structured, isError) ||
				isError || structured.get("zOrder").asString() != "7")
			{
				finish(false, "control self-test: SpriteComponent zOrder did not "
					"read back as '7'");
				return;
			}
			SDL_Log("orkige_editor: control self-test - generic Sprite property "
				"(zOrder -> 7) OK");
		}

		finish(true, "");
	}
}
