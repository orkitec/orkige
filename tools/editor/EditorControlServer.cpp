// EditorControlServer.cpp - the editor's in-process MCP endpoint.
//
// Two layers:
//   1. an HTTP/1.1 + JSON-RPC 2.0 transport (handleHttp / dispatchJsonRpc /
//      runToolCall / buildToolList) on top of the hand-rolled loopback
//      HttpServer - the MCP Streamable HTTP surface (initialize, tools/list,
//      tools/call, notifications).
//   2. the verb handler (handleMessage) REUSED wholesale - a thin
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
#include <core_game/GameObjectComponent.h>
#include <core_game/GameObjectManager.h>
#include <core_game/PrefabSerializer.h>
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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <vector>

namespace Orkige
{
	const String EditorControlServer::MSG_OK = "ok";
	const String EditorControlServer::MSG_ERR = "err";
	const String EditorControlServer::MCP_PROTOCOL_VERSION = "2025-03-26";

	//---------------------------------------------------------
	//--- the async test-runner state ---
	//---------------------------------------------------------
	//! one failing test, evidence-shaped: the name, how long it ran and the tail
	//! of its captured output (the agent's "why did it fail" material)
	struct TestFailure
	{
		String name;
		String durationSec;
		String logTail;		//!< last ~40 lines of the test's captured output
	};
	//! the structured outcome of one run_tests job. Filled by the worker thread,
	//! read once by get_test_results. Build failures short-circuit the ctest run
	//! (an agent's first question is "did it compile"), so a run reports EITHER a
	//! build failure OR a test tally, never a half-parsed mix.
	struct TestRunResult
	{
		String preset;			//!< the caller's preset selector (echoed back)
		String filter;			//!< the ctest -R regex (echoed back)
		String label;			//!< the ctest -L label (echoed back)
		String buildDir;		//!< the resolved build tree the run drove
		String error;			//!< non-empty: the run could not be carried out
		bool buildRequested = false;
		bool buildFailed = false;
		String buildErrors;		//!< compiler output tail when buildFailed
		int total = 0;
		int passed = 0;
		int failed = 0;
		std::vector<TestFailure> failures;
	};
	//! one asynchronous test run. The worker thread computes result then flips
	//! done; get_test_results reports "running" until then, and the structured
	//! result after. The mutex guards result against the worker/main handoff.
	struct EditorTestJob
	{
		std::string id;
		std::thread worker;
		std::atomic<bool> done{ false };
		std::mutex mutex;
		TestRunResult result;
	};

	namespace
	{
		//---------------------------------------------------------
		//--- test-runner helpers (run_tests / list_tests) ----
		//---------------------------------------------------------
		//! @brief spawn a child process, capture its combined stdout+stderr and
		//! wait for it to exit. Blocks (only ever called off the main thread, on
		//! a run_tests worker or the self-test worker). false when the process
		//! could not be spawned (output then carries the spawn error).
		bool runProcessCapture(std::vector<std::string> const& args,
			std::string& output, int& exitCode)
		{
			output.clear();
			exitCode = -1;
			std::vector<const char*> argv;
			argv.reserve(args.size() + 1);
			for (std::string const& arg : args)
			{
				argv.push_back(arg.c_str());
			}
			argv.push_back(nullptr);
			SDL_PropertiesID props = SDL_CreateProperties();
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
				const_cast<char**>(argv.data()));
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
				SDL_PROCESS_STDIO_APP);
			SDL_SetBooleanProperty(props,
				SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
			SDL_Process* process = SDL_CreateProcessWithProperties(props);
			SDL_DestroyProperties(props);
			if (!process)
			{
				const char* error = SDL_GetError();
				output = error ? error : "process spawn failed";
				return false;
			}
			size_t dataSize = 0;
			void* data = SDL_ReadProcess(process, &dataSize, &exitCode);
			if (data)
			{
				output.assign(static_cast<char*>(data), dataSize);
				SDL_free(data);
			}
			SDL_DestroyProcess(process);
			return true;
		}
		//! keep the last @a maxLines lines of @a text (its trailing content -
		//! where compiler errors and a failing test's death cry live)
		String lastLines(String const& text, size_t maxLines)
		{
			size_t lineCount = 0;
			size_t start = text.size();
			// walk back over trailing newline noise, then count line breaks
			while (start > 0)
			{
				const size_t newline = text.rfind('\n', start - 1);
				if (newline == String::npos)
				{
					start = 0;
					break;
				}
				++lineCount;
				if (lineCount > maxLines)
				{
					start = newline + 1;
					return text.substr(start);
				}
				if (newline == 0)
				{
					start = 0;
					break;
				}
				start = newline;
			}
			return text.substr(start);
		}
		//! decode the five standard XML entities a CTest JUnit writer emits in
		//! text/attribute content (&amp; is unescaped LAST so "&amp;lt;" round-
		//! trips to "&lt;", not "<")
		String xmlUnescape(String const& in)
		{
			String out = in;
			auto replaceAll = [&out](String const& from, String const& to)
			{
				size_t at = 0;
				while ((at = out.find(from, at)) != String::npos)
				{
					out.replace(at, from.size(), to);
					at += to.size();
				}
			};
			replaceAll("&lt;", "<");
			replaceAll("&gt;", ">");
			replaceAll("&quot;", "\"");
			replaceAll("&apos;", "'");
			replaceAll("&amp;", "&");
			return out;
		}
		//! read an attribute value ('key="..."') out of an XML open tag, unescaped
		String xmlAttribute(String const& tag, String const& key)
		{
			const String needle = key + "=\"";
			const size_t at = tag.find(needle);
			if (at == String::npos)
			{
				return String();
			}
			const size_t valueStart = at + needle.size();
			const size_t valueEnd = tag.find('"', valueStart);
			if (valueEnd == String::npos)
			{
				return String();
			}
			return xmlUnescape(tag.substr(valueStart, valueEnd - valueStart));
		}
		//! @brief parse a CTest --output-junit file into the run tally. Each
		//! <testcase> carries name/time/status and a <system-out> body; a failing
		//! case (status != "run") contributes to the failures list with the tail
		//! of that body as its logTail. Deterministic - the file is machine-
		//! written, so a narrow hand parser beats pulling in an XML dependency.
		void parseJUnit(String const& xml, TestRunResult& result)
		{
			size_t pos = 0;
			while ((pos = xml.find("<testcase", pos)) != String::npos)
			{
				const size_t tagEnd = xml.find('>', pos);
				if (tagEnd == String::npos)
				{
					break;
				}
				const String openTag = xml.substr(pos, tagEnd - pos + 1);
				const bool selfClosed = tagEnd > pos && xml[tagEnd - 1] == '/';
				const String name = xmlAttribute(openTag, "name");
				const String status = xmlAttribute(openTag, "status");
				const String time = xmlAttribute(openTag, "time");
				String body;
				const size_t close = xml.find("</testcase>", tagEnd + 1);
				if (!selfClosed && close != String::npos)
				{
					body = xml.substr(tagEnd + 1, close - (tagEnd + 1));
				}
				++result.total;
				if (status == "run")
				{
					++result.passed;
				}
				else if (status == "fail")
				{
					++result.failed;
					String captured;
					const String openMarker = "<system-out>";
					const size_t soAt = body.find(openMarker);
					if (soAt != String::npos)
					{
						const size_t soEnd = body.find("</system-out>", soAt);
						if (soEnd != String::npos)
						{
							const size_t textAt = soAt + openMarker.size();
							captured = xmlUnescape(
								body.substr(textAt, soEnd - textAt));
						}
					}
					TestFailure failure;
					failure.name = name;
					failure.durationSec = time;
					failure.logTail = lastLines(captured, 40);
					result.failures.push_back(failure);
				}
				// any other status (notrun/disabled) counts toward total only
				pos = close != String::npos ? close + 11 : tagEnd + 1;
			}
		}
		//! parse `ctest -N` output ("  Test #NN: <name>") into the test names
		StringVector parseCtestList(String const& output)
		{
			StringVector names;
			size_t lineStart = 0;
			while (lineStart <= output.size())
			{
				size_t newline = output.find('\n', lineStart);
				const String line = output.substr(lineStart,
					newline == String::npos ? String::npos : newline - lineStart);
				const size_t hash = line.find('#');
				const size_t colon = hash == String::npos
					? String::npos : line.find(':', hash);
				if (colon != String::npos)
				{
					size_t nameStart = colon + 1;
					while (nameStart < line.size() && line[nameStart] == ' ')
					{
						++nameStart;
					}
					if (nameStart < line.size())
					{
						names.push_back(line.substr(nameStart));
					}
				}
				if (newline == String::npos)
				{
					break;
				}
				lineStart = newline + 1;
			}
			return names;
		}
		//! @brief resolve a run_tests/list_tests 'preset' selector to a concrete
		//! build tree. Empty / a current-flavour alias -> THIS editor build's own
		//! tree (the common case: the agent edits and rebuilds the tree the editor
		//! runs in). A "*classic" alias or a bare directory name -> the sibling
		//! tree under build/. An absolute path is used verbatim. false (with a
		//! reason) when the resolved tree has no CMake cache.
		bool resolveBuildDir(String const& preset, std::string& outDir,
			String& outError)
		{
			namespace fs = std::filesystem;
			const fs::path self(ORKIGE_EDITOR_ENGINE_BUILD_DIR);
			auto hasCache = [](fs::path const& dir) -> bool
			{
				std::error_code ec;
				return fs::exists(dir / "CMakeCache.txt", ec);
			};
			if (preset.empty() || preset == "current" || preset == "default" ||
				preset == "next" || preset == "desktop" || preset == "unit" ||
				preset == self.filename().string())
			{
				outDir = self.string();
				return true;
			}
			const fs::path direct(preset);
			if (direct.is_absolute())
			{
				if (hasCache(direct))
				{
					outDir = direct.string();
					return true;
				}
				outError = "build tree '" + preset + "' has no CMakeCache.txt";
				return false;
			}
			// friendly ctest-preset aliases -> the classic configure-preset dir
			static const std::map<std::string, std::string> presetDir = {
				{ "desktop-classic", "macos-debug-classic" },
				{ "classic", "macos-debug-classic" },
				{ "all", "macos-debug-classic" },
			};
			std::string dirName = preset;
			const auto mapped = presetDir.find(preset);
			if (mapped != presetDir.end())
			{
				dirName = mapped->second;
			}
			const fs::path candidate = self.parent_path() / dirName;
			if (hasCache(candidate))
			{
				outDir = candidate.string();
				return true;
			}
			outError = "no build tree for preset '" + preset + "' (looked in '" +
				candidate.string() + "')";
			return false;
		}
		//! @brief the run_tests worker body: an optional incremental build (whose
		//! failure short-circuits with the compiler output), then a filtered ctest
		//! whose JUnit output is parsed into the structured tally. device-labelled
		//! tests are always excluded so a test run never boots a simulator/
		//! emulator. Runs on its own thread; parks the verdict under job->mutex
		//! and flips job->done.
		void runTestJobWorker(EditorTestJob* job, TestRunResult params,
			std::vector<std::string> buildTargets, bool doBuild)
		{
			namespace fs = std::filesystem;
			TestRunResult result = params;
			const std::string cmakeExe = ORKIGE_EDITOR_CMAKE;
			const std::string ctestExe = ORKIGE_EDITOR_CTEST;

			if (doBuild)
			{
				std::vector<std::string> build = { cmakeExe, "--build",
					result.buildDir };
				if (!buildTargets.empty())
				{
					build.push_back("--target");
					for (std::string const& target : buildTargets)
					{
						build.push_back(target);
					}
				}
				std::string output;
				int exitCode = 0;
				const bool spawned = runProcessCapture(build, output, exitCode);
				if (!spawned || exitCode != 0)
				{
					result.buildFailed = true;
					result.buildErrors = spawned
						? lastLines(output, 120)
						: ("could not start the build: " + output);
					std::lock_guard<std::mutex> lock(job->mutex);
					job->result = std::move(result);
					job->done.store(true);
					return;
				}
			}

			const fs::path junitPath = fs::temp_directory_path() /
				("orkige_run_tests_" + job->id + ".xml");
			std::error_code ignored;
			fs::remove(junitPath, ignored);
			std::vector<std::string> ctest = { ctestExe, "--test-dir",
				result.buildDir, "--output-on-failure", "--output-junit",
				junitPath.string(), "-LE", "device" };
			if (!result.label.empty())
			{
				ctest.push_back("-L");
				ctest.push_back(result.label);
			}
			if (!result.filter.empty())
			{
				ctest.push_back("-R");
				ctest.push_back(result.filter);
			}
			std::string output;
			int exitCode = 0;
			if (!runProcessCapture(ctest, output, exitCode))
			{
				result.error = "could not start ctest: " + output;
				std::lock_guard<std::mutex> lock(job->mutex);
				job->result = std::move(result);
				job->done.store(true);
				return;
			}
			// ctest exits non-zero when tests fail - that is not an error here,
			// the JUnit file is the source of truth for the tally
			std::ifstream junitFile(junitPath, std::ios::binary);
			if (junitFile)
			{
				std::string xml((std::istreambuf_iterator<char>(junitFile)),
					std::istreambuf_iterator<char>());
				junitFile.close();
				parseJUnit(xml, result);
			}
			if (result.total == 0 && result.error.empty())
			{
				// no results parsed: surface ctest's own words (typically "No
				// tests were found" for a filter that matched none)
				result.error = lastLines(output, 20);
			}
			fs::remove(junitPath, ignored);

			std::lock_guard<std::mutex> lock(job->mutex);
			job->result = std::move(result);
			job->done.store(true);
		}
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
				type == "console_tail" || type == "list_addable_components" ||
				type == "list_tests" || type == "get_test_results" ||
				type == "runtime_hierarchy" || type == "runtime_state" ||
				type == "read_project_file" || type == "list_project_files";
		}
		//---------------------------------------------------------
		//--- project-root path jail (write/read/list authoring) --
		//---------------------------------------------------------
		//! @brief jail a caller-supplied path inside the open project's root. The
		//! authoring verbs (write/read/list_project_file(s)) never touch anything
		//! outside the project. Mirrors AssetDatabase::resolveInsideRoot's lexical
		//! containment (an outside path relativizes to a "../"-led one) and adds
		//! two guards it does not need: ABSOLUTE paths are refused outright (an
		//! authoring path is always project-relative), and a symlink escape is
		//! caught by re-checking the containment on the weakly-canonical forms
		//! (which resolve symlinks over the path's existing prefix). false (+ a
		//! reason in outError) when refused; on success outAbsolute is the
		//! lexically-normal absolute path and outRelative its generic
		//! project-relative form.
		bool jailProjectPath(String const& root, String const& rel,
			std::filesystem::path& outAbsolute, String& outRelative,
			String& outError)
		{
			namespace fs = std::filesystem;
			if (root.empty())
			{
				outError = "no project open";
				return false;
			}
			if (rel.empty())
			{
				outError = "path must not be empty";
				return false;
			}
			const fs::path requested(rel);
			if (requested.is_absolute() || requested.has_root_name())
			{
				outError = "path must be project-relative (absolute paths are "
					"refused)";
				return false;
			}
			auto escapes = [](fs::path const& base, fs::path const& target) -> bool
			{
				const fs::path relative = target.lexically_relative(base);
				return relative.empty() ||
					(relative.begin() != relative.end() &&
						*relative.begin() == fs::path(".."));
			};
			const fs::path rootPath(root);
			const fs::path absolute = (rootPath / requested).lexically_normal();
			// lexical containment (the "../" escape) - matches resolveInsideRoot
			if (escapes(rootPath.lexically_normal(), absolute))
			{
				outError = "path escapes the project root";
				return false;
			}
			// symlink containment: resolve symlinks over the existing prefix of
			// both paths and re-check (a component symlinked outside the root
			// would pass the lexical test but fail here)
			std::error_code ec;
			const fs::path canonicalRoot = fs::weakly_canonical(rootPath, ec);
			const fs::path canonicalTarget = fs::weakly_canonical(absolute, ec);
			if (!canonicalRoot.empty() && !canonicalTarget.empty() &&
				escapes(canonicalRoot, canonicalTarget))
			{
				outError = "path escapes the project root (symlink)";
				return false;
			}
			outAbsolute = absolute;
			outRelative = absolute.lexically_relative(rootPath).generic_string();
			return true;
		}
		//! @brief glob one path segment: '*' matches any run (including empty),
		//! '?' any single character, everything else literally. Used by
		//! list_project_files to filter entries by name without pulling in a
		//! regex. Matches the WHOLE string (anchored both ends).
		bool globMatch(String const& pattern, String const& text)
		{
			// classic two-pointer wildcard match with backtracking on '*'
			size_t p = 0, t = 0, star = String::npos, mark = 0;
			while (t < text.size())
			{
				if (p < pattern.size() &&
					(pattern[p] == '?' || pattern[p] == text[t]))
				{
					++p;
					++t;
				}
				else if (p < pattern.size() && pattern[p] == '*')
				{
					star = p++;
					mark = t;
				}
				else if (star != String::npos)
				{
					p = star + 1;
					t = ++mark;
				}
				else
				{
					return false;
				}
			}
			while (p < pattern.size() && pattern[p] == '*')
			{
				++p;
			}
			return p == pattern.size();
		}
		//---------------------------------------------------------
		//--- generic reflected-property helpers ---
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
				  "Write a PNG of the EDITOR: the chrome-free scene viewport, or "
				  "the whole editor window (window='1'). Returns the written "
				  "path. For the RUNNING game's frame use screenshot_game.",
				  { { "path", "string", "output PNG path", true },
				    { "window", "string", "'1' for the whole window", false } } },
				{ "runtime_hierarchy",
				  "The RUNNING game's live GameObject hierarchy (ids/parents/"
				  "active), streamed from the player during Play. Distinct from "
				  "list_hierarchy, which always reads the EDIT world. Errors when "
				  "no player is connected.",
				  {} },
				{ "runtime_state",
				  "The component state streamed for the running game's currently "
				  "SELECTED object: 'object' (the streamed id), 'ready' ('1' when "
				  "it matches the selection), and the parallel lists 'properties' "
				  "(the '<Component>.<property>' keys), 'values', 'kinds', 'hints' "
				  "and 'readonly'. Call runtime_select first, then poll this until "
				  "ready='1' (the stream is asynchronous). Errors when no player "
				  "is connected.",
				  {} },
				{ "runtime_select",
				  "Choose which running-game object streams its component state "
				  "(''=stop streaming). Then poll runtime_state. Errors when no "
				  "player is connected.",
				  { { "id", "string", "GameObject id ('' clears)", false } } },
				{ "set_runtime_property",
				  "Write ONE reflected property on the RUNNING game live (the "
				  "player's reflected setter - takes effect immediately, not "
				  "undoable, NOT an edit-world change; use set_component for the "
				  "edit world). A bad value/name surfaces as a [remote] error in "
				  "console_tail. Errors when no player is connected.",
				  { { "id", "string", "GameObject id", true },
				    { "component", "string", "component type name", true },
				    { "property", "string", "property name", true },
				    { "value", "string", "canonical value string", true } } },
				{ "set_cvar",
				  "Change a console variable on the RUNNING game live "
				  "(CVarManager). An unknown name or bad value surfaces as a "
				  "[remote] error in console_tail. Errors when no player is "
				  "connected.",
				  { { "name", "string", "cvar name", true },
				    { "value", "string", "new value (parsed per the cvar type)",
				      true } } },
				{ "reload_script",
				  "Hot-reload Lua on the RUNNING game (compile-before-swap): one "
				  "object's ScriptComponent (id) or ALL of them (id omitted). A "
				  "reload that fails to compile keeps the old instance and "
				  "surfaces a [remote] SCRIPT ERROR. Errors when no player is "
				  "connected.",
				  { { "id", "string", "GameObject id (omit = reload all)",
				      false } } },
				{ "screenshot_game",
				  "Screenshot the RUNNING game's next rendered frame to 'path' "
				  "(desktop play only; the path is on the player's filesystem, "
				  "shared with the editor). ASYNC: returns accepted + "
				  "prev_screenshot_seq; poll get_state until screenshot_seq "
				  "exceeds it, then screenshot_path/screenshot_ok carry the "
				  "result. Errors when no player is connected.",
				  { { "path", "string", "output PNG path", true } } },
				{ "list_assets",
				  "List the open project's assets and scenes.", {} },
				{ "write_project_file",
				  "Author a TEXT file under the OPEN project's root (create the "
				  "parent dirs, LF endings) - the DEVELOP primitive for an "
				  "MCP-only agent: write scripts/<name>.lua, a config asset "
				  "(input.oactions / physics.olayers / levels.olevels), etc. The "
				  "path is project-root-relative and JAILED (absolute paths, '..' "
				  "escapes and symlink escapes are refused). Writing a "
				  "scripts/*.lua while a Play session is live is picked up by the "
				  "editor's scripts/ watcher, which hot-reloads the running game. "
				  "Returns the written 'path' and byte count.",
				  { { "path", "string",
				      "project-relative file path (e.g. 'scripts/player.lua')",
				      true },
				    { "content", "string", "the file's full text content",
				      true } } },
				{ "read_project_file",
				  "Read a TEXT file under the open project's root (same jail as "
				  "write_project_file; 1 MiB cap, honest error beyond). Returns "
				  "'content' and the byte count.",
				  { { "path", "string", "project-relative file path", true } } },
				{ "list_project_files",
				  "List one directory level under the open project's root (jailed) "
				  "so an agent can discover scripts and config files. Returns "
				  "parallel lists 'names', 'paths' (project-relative) and 'types' "
				  "('file'/'dir'). Optional 'dir' (default the root) and 'glob' (a "
				  "'*'/'?' filter on the entry name, e.g. '*.lua').",
				  { { "dir", "string",
				      "project-relative directory (default: the root)", false },
				    { "glob", "string", "name filter ('*'/'?', e.g. '*.lua')",
				      false } } },
				{ "import_asset",
				  "Import an OUTSIDE file into the open project (copy into "
				  "assets/, mint its stable .orkmeta id, refresh the resource "
				  "locations) - the MCP equivalent of dragging a file into the "
				  "editor (same trust model; needs auth). 'sourcePath' is an "
				  "absolute path on the editor's filesystem. Optional 'targetDir' "
				  "(project-relative, jailed) relocates the import within the "
				  "project, id preserved. Returns the project-relative 'path' and "
				  "the minted 'assetId'.",
				  { { "sourcePath", "string",
				      "absolute path of the file to import", true },
				    { "targetDir", "string",
				      "project-relative destination dir (default: assets/)",
				      false } } },
				{ "create_prefab",
				  "Write a GameObject's subtree as a .oprefab asset and convert "
				  "the live subtree into a prefab INSTANCE (undoable). 'path' is "
				  "project-relative and jailed, must end in .oprefab and must not "
				  "already exist. Returns the instance-root 'id', the 'path' and "
				  "the minted 'assetId'.",
				  { { "objectId", "string",
				      "the subtree-root GameObject to capture", true },
				    { "path", "string",
				      "project-relative .oprefab path (e.g. 'assets/Foo.oprefab')",
				      true } } },
				{ "instantiate_prefab",
				  "Instantiate a .oprefab asset into the current scene as a NEW "
				  "prefab instance (undoable). 'path' is the project-relative, "
				  "jailed .oprefab file. Optional 'parent' reparents the new "
				  "instance root under an existing object. Returns the new "
				  "instance-root 'id'.",
				  { { "path", "string",
				      "project-relative .oprefab path", true },
				    { "parent", "string",
				      "parent GameObject id (default: a scene root)", false } } },
				{ "console_tail",
				  "The last N editor Console lines (default 50, max 200).",
				  { { "count", "integer", "how many lines (1-200)", false } } },
				{ "list_tests",
				  "Discover the tests in a build tree (ctest -N). Returns 'tests' "
				  "(the names) so an agent can find, e.g., the selfcheck for the "
				  "project it is editing. Optional 'filter' is a ctest -R name "
				  "regex, 'label' a ctest -L label ('unit'/'integration'). "
				  "device-labelled (simulator/emulator) tests are never listed. "
				  "'preset' selects the tree (default: this editor's own build).",
				  { { "preset", "string",
				      "build tree selector: '' (this editor's build), "
				      "'desktop-classic', a build/ dir name or an absolute path",
				      false },
				    { "filter", "string", "ctest -R name regex", false },
				    { "label", "string", "ctest -L label", false } } },
				{ "run_tests",
				  "Run a scoped test suite and get STRUCTURED evidence back - the "
				  "close-the-loop primitive: edit, run the relevant test, read the "
				  "verdict, iterate. ASYNC: returns { accepted, jobId } immediately; "
				  "poll get_test_results for the result. With build=true (default) "
				  "the build tree is (incrementally) built first; a BUILD failure "
				  "short-circuits (no ctest) and is reported as buildFailed + the "
				  "compiler output. Then ctest runs, filtered by 'filter' (-R name "
				  "regex) and/or 'label' (-L). device-labelled tests are always "
				  "excluded (a run never boots a simulator/emulator). 'preset' "
				  "selects the tree (default: this editor's own build); 'targets' "
				  "scopes the build.",
				  { { "filter", "string", "ctest -R name regex", false },
				    { "label", "string", "ctest -L label (e.g. 'unit')", false },
				    { "preset", "string",
				      "build tree selector (default: this editor's build)", false },
				    { "build", "string",
				      "'0' to skip the build and test the tree as-is (default "
				      "builds first)", false },
				    { "targets", "array",
				      "build only these targets (default: the whole tree)",
				      false } } },
				{ "get_test_results",
				  "Poll a run_tests job. Returns status 'running' until the run "
				  "finishes, then 'done' with the structured verdict: total / "
				  "passed / failed counts, buildFailed + buildErrors on a build "
				  "failure, and the parallel failure lists 'failed_names', "
				  "'failed_durations', 'failed_logtails' (index-aligned; each "
				  "logtail is the tail of that test's captured output).",
				  { { "jobId", "string", "the id run_tests returned", true } } },
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
		this->joinTestJobs();
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
	void EditorControlServer::joinTestJobs()
	{
		// join every worker (each runs to completion on its own; this just waits
		// for the last build/ctest to exit) before dropping the jobs
		for (auto const& job : this->mTestJobs)
		{
			if (job->worker.joinable())
			{
				job->worker.join();
			}
		}
		this->mTestJobs.clear();
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
	//--- the verb handler (reused wholesale) ----------
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
			// live-player snapshot (present while a play session is up): the
			// debug tools poll these - remote connection, the streamed selection
			// and the last confirmed running-game screenshot
			PlaySession const& play = *context.play;
			ok.set("remote_connected", play.client.isConnected() ? "1" : "0");
			ok.set("remote_scene", play.remoteScenePath);
			ok.set("remote_selected", play.remoteSelectedId);
			ok.set("remote_object_count",
				std::to_string(play.remoteHierarchy.size()));
			ok.set("screenshot_path", play.lastScreenshotPath);
			ok.set("screenshot_ok", play.lastScreenshotOk ? "1" : "0");
			ok.set("screenshot_seq", std::to_string(play.screenshotSeq));
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

		//--- generic reflected property read --
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
			GameObjectComponent* instance =
				gameObject->getComponentPtr(componentType);
			if (!instance)
			{
				this->sendErr(req, "no " + component + " on '" + id + "'");
				return;
			}
			// the union schema (static per-type + dynamic per-instance) so a
			// ScriptComponent's exported script properties are discovered here
			// too - MCP shows them with zero server code
			const PropertySchema unionSchema = getComponentSchema(*instance);
			PropertySchema const* schema = &unionSchema;
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

		//--- generic reflected property write -
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
			GameObjectComponent* instance =
				gameObject->getComponentPtr(componentType);
			if (!instance)
			{
				this->sendErr(req, "no " + component + " on '" + id + "'");
				return;
			}
			// the union schema (static per-type + dynamic per-instance) so a
			// ScriptComponent's exported script properties validate/apply here
			// too
			const PropertySchema unionSchema = getComponentSchema(*instance);
			PropertySchema const* schema = &unionSchema;
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

		//--- runtime debug: inspect + drive the RUNNING game ------
		// These bridge the editor-side play link onto the ONE player debug
		// protocol (never a second player port). They serve the LIVE player -
		// distinct from list_hierarchy/get_component, which always read the
		// EDIT world (even during Play). A read answers honestly when no player
		// is connected.
		if (type == "runtime_hierarchy")
		{
			PlaySession const& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("scene", play.remoteScenePath);
			ok.set("selected", play.remoteSelectedId);
			ok.set("play_mode", playSessionModeName(play));
			ok.setList(DebugProtocol::LIST_IDS, play.remoteHierarchy);
			ok.setList(DebugProtocol::LIST_PARENTS, play.remoteParents);
			ok.setList(DebugProtocol::LIST_ACTIVE, play.remoteActive);
			this->sendOk(req, ok);
			return;
		}
		// runtime_state: the component state streamed for the running game's
		// currently-selected object. object_state only covers the SELECTED
		// object, so runtime_select the object first, then poll this until
		// 'object' matches (the stream arrives asynchronously at ~15Hz).
		if (type == "runtime_state")
		{
			PlaySession const& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("object", play.stateObjectId);
			ok.set("selected", play.remoteSelectedId);
			ok.set("ready", (!play.stateObjectId.empty() &&
				play.stateObjectId == play.remoteSelectedId) ? "1" : "0");
			ok.setList(DebugProtocol::LIST_COMPONENTS, play.stateComponents);
			// the streamed "<Component>.<property>" values by key, plus the
			// parallel metadata lists the player sends (kind/hint/read-only)
			StringVector keys;
			StringVector values;
			StringVector kinds;
			StringVector hints;
			StringVector readonly;
			for (String const& key : play.statePropKeys)
			{
				keys.push_back(key);
				auto valueIt = play.stateProperties.find(key);
				values.push_back(valueIt != play.stateProperties.end()
					? valueIt->second : String());
				auto kindIt = play.statePropKind.find(key);
				kinds.push_back(kindIt != play.statePropKind.end()
					? kindName(static_cast<PropertyKind>(kindIt->second))
					: String());
				auto hintIt = play.statePropHint.find(key);
				hints.push_back(hintIt != play.statePropHint.end()
					? hintIt->second : String());
				readonly.push_back(
					play.statePropReadonly.count(key) != 0 ? "1" : "0");
			}
			ok.setList("properties", keys);
			ok.setList("values", values);
			ok.setList("kinds", kinds);
			ok.setList("hints", hints);
			ok.setList("readonly", readonly);
			this->sendOk(req, ok);
			return;
		}
		if (type == "runtime_select")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			const String id = request.get(DebugProtocol::FIELD_ID);
			// "" is a legal clear (stop streaming); a non-empty id is validated
			// by the player, which errors over the log if it is unknown
			selectRemoteObject(*context.play, id);
			DebugMessage ok(MSG_OK);
			ok.set("selected", id);
			this->sendOk(req, ok);
			return;
		}
		if (type == "set_runtime_property")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			const String property = request.get(DebugProtocol::FIELD_PROPERTY);
			const String value = request.get(DebugProtocol::FIELD_VALUE);
			if (id.empty() || component.empty() || property.empty())
			{
				this->sendErr(req, "set_runtime_property needs id, component and "
					"property");
				return;
			}
			// fire-and-forget onto the player's reflected setter; a bad value/
			// name surfaces as a [remote] error line (console_tail), matching the
			// live protocol's one-way write model
			setRemoteObjectProperty(*context.play, id, component, property,
				value);
			this->sendOk(req);
			return;
		}
		if (type == "set_cvar")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			const String name = request.get(DebugProtocol::FIELD_CVAR_NAME);
			if (name.empty())
			{
				this->sendErr(req, "set_cvar needs a 'name'");
				return;
			}
			setRemoteCvar(*context.play, name,
				request.get(DebugProtocol::FIELD_VALUE));
			this->sendOk(req);
			return;
		}
		if (type == "reload_script")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			// reload one object's scripts (FIELD_ID) or ALL of them (absent).
			// reloadRemoteScripts sends reload-ALL; a per-object reload sends
			// MSG_RELOAD_SCRIPT with the id directly.
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (id.empty())
			{
				reloadRemoteScripts(*context.play, *context.console);
			}
			else
			{
				DebugMessage reload(DebugProtocol::MSG_RELOAD_SCRIPT);
				reload.set(DebugProtocol::FIELD_ID, id);
				context.play->client.send(reload);
			}
			this->sendOk(req);
			return;
		}
		if (type == "screenshot_game")
		{
			PlaySession& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			if (play.onSimulator || play.onAndroid)
			{
				this->sendErr(req, "screenshot_game is desktop-play only (the "
					"path lives on the player's filesystem)");
				return;
			}
			const String path = request.get("path");
			if (path.empty())
			{
				this->sendErr(req, "screenshot_game needs a 'path'");
				return;
			}
			// async: the player captures its NEXT frame and answers with
			// screenshot_saved; poll get_state (screenshot_path/screenshot_ok/
			// screenshot_seq) for the confirmation. Report the request accepted
			// plus the sequence value BEFORE the request so a poller knows a
			// fresh confirmation is one with a higher screenshot_seq.
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("path", path);
			ok.set("prev_screenshot_seq", std::to_string(play.screenshotSeq));
			requestRemoteScreenshot(play, path);
			this->sendOk(req, ok);
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

		//--- project file authoring --------------------------
		// The DEVELOP surface: an MCP-only agent authors project files (scripts,
		// config assets) without a filesystem tool. Every path is JAILED to the
		// open project's root (jailProjectPath - relative only, no "../" or
		// symlink escape). Writing scripts/*.lua during a live Play session is
		// picked up by the editor's existing scripts/ watcher, which triggers the
		// hot-reload - no separate reload hook here (see Docs/mcp.md).
		if (type == "write_project_file")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - write_project_file needs an "
					"open project to jail the path to");
				return;
			}
			std::filesystem::path absolute;
			String relative;
			String error;
			if (!jailProjectPath(state.project.getRootDirectory(),
				request.get("path"), absolute, relative, error))
			{
				this->sendErr(req, "write_project_file refused: " + error);
				return;
			}
			// text file, LF endings: normalize any CRLF the client sent, write
			// binary so the bytes land verbatim (no platform newline translation)
			String content = request.get("content");
			String normalized;
			normalized.reserve(content.size());
			for (size_t i = 0; i < content.size(); ++i)
			{
				if (content[i] == '\r' &&
					(i + 1 >= content.size() || content[i + 1] != '\n'))
				{
					normalized += '\n';	// a lone CR -> LF
				}
				else if (content[i] != '\r')
				{
					normalized += content[i];
				}
			}
			std::error_code ec;
			std::filesystem::create_directories(absolute.parent_path(), ec);
			std::ofstream out(absolute, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				this->sendErr(req, "write_project_file could not open '" +
					relative + "' for writing");
				return;
			}
			out.write(normalized.data(),
				static_cast<std::streamsize>(normalized.size()));
			out.close();
			if (!out)
			{
				this->sendErr(req, "write_project_file failed writing '" +
					relative + "'");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("path", relative);
			ok.set("bytes", std::to_string(normalized.size()));
			this->sendOk(req, ok);
			return;
		}
		if (type == "read_project_file")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open");
				return;
			}
			std::filesystem::path absolute;
			String relative;
			String error;
			if (!jailProjectPath(state.project.getRootDirectory(),
				request.get("path"), absolute, relative, error))
			{
				this->sendErr(req, "read_project_file refused: " + error);
				return;
			}
			std::error_code ec;
			if (!std::filesystem::is_regular_file(absolute, ec))
			{
				this->sendErr(req, "read_project_file: no such file '" +
					relative + "'");
				return;
			}
			const std::uintmax_t size = std::filesystem::file_size(absolute, ec);
			const std::uintmax_t maxSize = 1024 * 1024;	// 1 MiB text cap
			if (size > maxSize)
			{
				this->sendErr(req, "read_project_file: '" + relative + "' is " +
					std::to_string(size) + " bytes (over the " +
					std::to_string(maxSize) + "-byte text cap)");
				return;
			}
			std::ifstream in(absolute, std::ios::binary);
			if (!in)
			{
				this->sendErr(req, "read_project_file could not open '" +
					relative + "'");
				return;
			}
			String content((std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			DebugMessage ok(MSG_OK);
			ok.set("path", relative);
			ok.set("bytes", std::to_string(content.size()));
			ok.set("content", content);
			this->sendOk(req, ok);
			return;
		}
		if (type == "list_project_files")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open");
				return;
			}
			// default dir is the project root; a glob (optional) filters entry
			// NAMES. Listing is one directory level (like `ls`) so an agent
			// navigates predictably.
			String dirArg = request.get("dir");
			if (dirArg.empty())
			{
				dirArg = ".";
			}
			std::filesystem::path absolute;
			String relative;
			String error;
			if (!jailProjectPath(state.project.getRootDirectory(), dirArg,
				absolute, relative, error))
			{
				this->sendErr(req, "list_project_files refused: " + error);
				return;
			}
			std::error_code ec;
			if (!std::filesystem::is_directory(absolute, ec))
			{
				this->sendErr(req, "list_project_files: '" + relative +
					"' is not a directory");
				return;
			}
			const String glob = request.get("glob");
			StringVector names;
			StringVector paths;
			StringVector types;
			const std::filesystem::path rootPath(
				state.project.getRootDirectory());
			const size_t maxEntries = 1000;
			bool truncated = false;
			std::vector<std::filesystem::directory_entry> entries;
			for (std::filesystem::directory_iterator it(absolute, ec), end;
				!ec && it != end; it.increment(ec))
			{
				entries.push_back(*it);
			}
			std::sort(entries.begin(), entries.end(),
				[](std::filesystem::directory_entry const& a,
					std::filesystem::directory_entry const& b)
				{
					return a.path().filename().string() <
						b.path().filename().string();
				});
			for (std::filesystem::directory_entry const& entry : entries)
			{
				const String name = entry.path().filename().string();
				if (!glob.empty() && !globMatch(glob, name))
				{
					continue;
				}
				if (names.size() >= maxEntries)
				{
					truncated = true;
					break;
				}
				names.push_back(name);
				paths.push_back(entry.path().lexically_relative(rootPath)
					.generic_string());
				types.push_back(entry.is_directory(ec) ? "dir" : "file");
			}
			DebugMessage ok(MSG_OK);
			ok.set("dir", relative);
			ok.set("truncated", truncated ? "1" : "0");
			ok.setList("names", names);
			ok.setList("paths", paths);
			ok.setList("types", types);
			this->sendOk(req, ok);
			return;
		}
		// import_asset: copy an OUTSIDE file into the project (sidecar minted, id
		// returned). The source is by nature outside the jail - this is the MCP
		// equivalent of the human dragging a file into the editor, so it needs
		// auth (the trust boundary the drag has by sitting at the machine).
		if (type == "import_asset")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - import_asset needs a project "
					"to copy the file into");
				return;
			}
			const String source = request.get("sourcePath");
			if (source.empty())
			{
				this->sendErr(req, "import_asset needs a 'sourcePath'");
				return;
			}
			std::error_code ec;
			if (!std::filesystem::is_regular_file(source, ec))
			{
				this->sendErr(req, "import_asset: source '" + source +
					"' is not a file");
				return;
			}
			// the existing import path (copy into assets/ + resource-location
			// refresh + sidecar mint), reused wholesale
			String importError;
			const std::string destPath =
				importAssetFile(state, source, &importError);
			if (destPath.empty())
			{
				this->sendErr(req, "import_asset failed: " +
					(importError.empty() ? "see the editor log" : importError));
				return;
			}
			String relative = state.project.makeProjectRelative(destPath);
			optr<AssetDatabase> const& database = state.project.getAssetDatabase();
			String assetId = database ? database->idForPath(relative) : String();
			// an optional targetDir relocates the import within the project (id
			// survives - AssetDatabase::moveAsset carries the sidecar). The dir
			// is jailed like any authoring path.
			const String targetDir = request.get("targetDir");
			if (!targetDir.empty() && database)
			{
				std::filesystem::path targetAbsolute;
				String targetRelative;
				String jailError;
				if (!jailProjectPath(state.project.getRootDirectory(), targetDir,
					targetAbsolute, targetRelative, jailError))
				{
					this->sendErr(req, "import_asset targetDir refused: " +
						jailError);
					return;
				}
				const String fileName =
					std::filesystem::path(destPath).filename().string();
				const String moved = targetRelative.empty()
					? fileName : (targetRelative + "/" + fileName);
				if (moved != relative && database->moveAsset(relative, moved))
				{
					relative = moved;
					registerProjectAssetLocations(targetAbsolute.string());
				}
			}
			DebugMessage ok(MSG_OK);
			ok.set("path", relative);
			ok.set("assetId", assetId);
			this->sendOk(req, ok);
			return;
		}
		// create_prefab: write objectId's subtree as a .oprefab and convert the
		// live subtree into an instance - the exact seams GameObject > Create
		// Prefab uses (PrefabSerializer::savePrefab + AssetDatabase::importAsset +
		// EditorCore::makePrefabInstance), but at a caller-chosen jailed path.
		if (type == "create_prefab")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - prefabs live in the "
					"project's assets/");
				return;
			}
			const String objectId = request.get("objectId");
			std::filesystem::path absolute;
			String relative;
			String error;
			if (!jailProjectPath(state.project.getRootDirectory(),
				request.get("path"), absolute, relative, error))
			{
				this->sendErr(req, "create_prefab refused: " + error);
				return;
			}
			if (absolute.extension() != ".oprefab")
			{
				this->sendErr(req, "create_prefab: 'path' must end in .oprefab");
				return;
			}
			String reason;
			if (!core.canMakePrefab(objectId, &reason))
			{
				this->sendErr(req, "create_prefab refused for '" + objectId +
					"': " + reason);
				return;
			}
			std::error_code ec;
			if (std::filesystem::exists(absolute, ec))
			{
				this->sendErr(req, "create_prefab: '" + relative +
					"' already exists");
				return;
			}
			std::filesystem::create_directories(absolute.parent_path(), ec);
			if (!PrefabSerializer::savePrefab(absolute.string(), manager,
				objectId))
			{
				this->sendErr(req, "create_prefab could not write '" + relative +
					"' (see the editor log)");
				return;
			}
			String assetId;
			if (optr<AssetDatabase> const& database =
				state.project.getAssetDatabase())
			{
				assetId = database->importAsset(absolute.string());
			}
			if (!core.makePrefabInstance(objectId, absolute.string(), relative,
				assetId))
			{
				this->sendErr(req, "create_prefab: wrote '" + relative +
					"' but could not convert '" + objectId + "' into an instance");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, objectId);
			ok.set("path", relative);
			ok.set("assetId", assetId);
			this->sendOk(req, ok);
			return;
		}
		// instantiate_prefab: create a NEW instance of a .oprefab into the scene
		// (the Asset browser's instantiate flow: CreatePrefabInstanceCommand,
		// undoable), optionally reparented under parentId.
		if (type == "instantiate_prefab")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open");
				return;
			}
			std::filesystem::path absolute;
			String relative;
			String error;
			if (!jailProjectPath(state.project.getRootDirectory(),
				request.get("path"), absolute, relative, error))
			{
				this->sendErr(req, "instantiate_prefab refused: " + error);
				return;
			}
			std::error_code ec;
			if (!std::filesystem::is_regular_file(absolute, ec) ||
				absolute.extension() != ".oprefab")
			{
				this->sendErr(req, "instantiate_prefab: '" + relative +
					"' is not a .oprefab file");
				return;
			}
			const String parentId = request.get("parent");
			if (!parentId.empty() && !manager.objectExists(parentId))
			{
				this->sendErr(req, "instantiate_prefab: no such parent '" +
					parentId + "'");
				return;
			}
			optr<AssetDatabase> const& database = state.project.getAssetDatabase();
			const String assetId = database ? database->idForPath(relative)
				: String();
			String stem = absolute.stem().string();
			if (stem.empty())
			{
				stem = "Prefab";
			}
			const String rootId = core.generateObjectId(stem);
			if (!core.executeCommand(onew(new CreatePrefabInstanceCommand(
				rootId, absolute.string(), relative, assetId, Vec3::ZERO))))
			{
				this->sendErr(req, "instantiate_prefab: could not instantiate '" +
					relative + "' (see the editor log)");
				return;
			}
			if (!parentId.empty() && !core.reparentObject(rootId, parentId))
			{
				this->sendErr(req, "instantiate_prefab: instantiated '" + rootId +
					"' but could not reparent it under '" + parentId + "'");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, rootId);
			ok.set("path", relative);
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

		//--- test runner (structured, evidence-shaped) -------
		// list_tests: synchronous (ctest -N is fast) test discovery
		if (type == "list_tests")
		{
			std::string buildDir;
			String error;
			if (!resolveBuildDir(request.get("preset"), buildDir, error))
			{
				this->sendErr(req, error);
				return;
			}
			std::vector<std::string> args = { std::string(ORKIGE_EDITOR_CTEST),
				"--test-dir", buildDir, "-N", "-LE", "device" };
			if (!request.get("label").empty())
			{
				args.push_back("-L");
				args.push_back(request.get("label"));
			}
			if (!request.get("filter").empty())
			{
				args.push_back("-R");
				args.push_back(request.get("filter"));
			}
			std::string output;
			int exitCode = 0;
			if (!runProcessCapture(args, output, exitCode))
			{
				this->sendErr(req, "could not run ctest -N: " + output);
				return;
			}
			StringVector names = parseCtestList(output);
			DebugMessage ok(MSG_OK);
			ok.set("buildDir", buildDir);
			ok.set("count", std::to_string(names.size()));
			ok.setList("tests", names);
			this->sendOk(req, ok);
			return;
		}
		// run_tests: async - spawn a worker, return an accepted { jobId }
		if (type == "run_tests")
		{
			std::string buildDir;
			String error;
			if (!resolveBuildDir(request.get("preset"), buildDir, error))
			{
				this->sendErr(req, error);
				return;
			}
			const bool doBuild = request.get("build") != "0";	// default true
			auto job = std::make_unique<EditorTestJob>();
			job->id = AssetDatabase::generateId();
			TestRunResult params;
			params.preset = request.get("preset");
			params.filter = request.get("filter");
			params.label = request.get("label");
			params.buildDir = buildDir;
			params.buildRequested = doBuild;
			EditorTestJob* jobPtr = job.get();
			StringVector const& targetList = request.getList("targets");
			std::vector<std::string> targets(targetList.begin(),
				targetList.end());
			job->worker = std::thread(runTestJobWorker, jobPtr, params, targets,
				doBuild);
			this->mTestJobs.push_back(std::move(job));
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("jobId", jobPtr->id);
			ok.set("build", doBuild ? "1" : "0");
			ok.set("buildDir", buildDir);
			this->sendOk(req, ok);
			return;
		}
		// get_test_results: poll a run_tests job for the structured verdict
		if (type == "get_test_results")
		{
			const String jobId = request.get("jobId");
			EditorTestJob* job = nullptr;
			for (auto const& candidate : this->mTestJobs)
			{
				if (candidate->id == jobId)
				{
					job = candidate.get();
					break;
				}
			}
			if (!job)
			{
				this->sendErr(req, "no such test job '" + jobId + "'");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("jobId", jobId);
			if (!job->done.load())
			{
				ok.set("status", "running");
				this->sendOk(req, ok);
				return;
			}
			std::lock_guard<std::mutex> lock(job->mutex);
			TestRunResult const& result = job->result;
			ok.set("status", "done");
			ok.set("preset", result.preset);
			ok.set("filter", result.filter);
			ok.set("label", result.label);
			ok.set("buildDir", result.buildDir);
			ok.set("buildRequested", result.buildRequested ? "1" : "0");
			ok.set("buildFailed", result.buildFailed ? "1" : "0");
			if (result.buildFailed)
			{
				ok.set("buildErrors", result.buildErrors);
			}
			if (!result.error.empty())
			{
				ok.set("error", result.error);
			}
			ok.set("total", std::to_string(result.total));
			ok.set("passed", std::to_string(result.passed));
			ok.set("failed", std::to_string(result.failed));
			StringVector failedNames;
			StringVector failedDurations;
			StringVector failedLogTails;
			for (TestFailure const& failure : result.failures)
			{
				failedNames.push_back(failure.name);
				failedDurations.push_back(failure.durationSec);
				failedLogTails.push_back(failure.logTail);
			}
			ok.setList("failed_names", failedNames);
			ok.setList("failed_durations", failedDurations);
			ok.setList("failed_logtails", failedLogTails);
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
		std::string const& token, std::string const& screenshotPath,
		bool runtimeDebug)
	{
		this->mToken = token;
		this->mScreenshotPath = screenshotPath;
		this->mRuntimeDebug = runtimeDebug;
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
			// the runtime debug tools must be advertised too
			bool hasRuntimeHierarchy = false;
			bool hasSetRuntimeProperty = false;
			bool hasScreenshotGame = false;
			for (size_t i = 0; i < tools.size(); ++i)
			{
				const String name = tools.at(i).get("name").asString();
				if (!tools.at(i).get("inputSchema").isObject())
				{
					schemasOk = false;
				}
				if (name == "create_object") hasCreate = true;
				if (name == "list_hierarchy") hasList = true;
				if (name == "runtime_hierarchy") hasRuntimeHierarchy = true;
				if (name == "set_runtime_property") hasSetRuntimeProperty = true;
				if (name == "screenshot_game") hasScreenshotGame = true;
			}
			if (!hasCreate || !hasList || !schemasOk || tools.size() < 10 ||
				!hasRuntimeHierarchy || !hasSetRuntimeProperty ||
				!hasScreenshotGame)
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

		// a get_state fetch (read, no auth) returning the structuredContent
		auto getState = [&](JsonValue& state) -> bool
		{
			bool isError = true;
			return callTool("get_state", JsonValue::object(), false, state,
				isError) && !isError;
		};

		// --- the RUNTIME DEBUG conversation (a separate ctest, needs the
		// built player): boot Play over MCP, then pause/step/inspect/mutate/
		// screenshot the RUNNING game and stop - all through the MCP tools,
		// exactly as an agent would drive them. Runs instead of the edit-world
		// conversation below when the runtime-debug env selected this mode.
		if (this->mRuntimeDebug)
		{
			JsonValue structured;
			bool isError = true;
			// poll get_state until a predicate holds (or a timeout); returns the
			// final state. 0.1s between polls; the verbs run on the editor's main
			// thread, so a live player boots over these frames.
			auto pollState = [&](auto&& ready,
				int maxAttempts, JsonValue& outState) -> bool
			{
				for (int attempt = 0; attempt < maxAttempts; ++attempt)
				{
					if (getState(outState) && ready(outState))
					{
						return true;
					}
					std::this_thread::sleep_for(
						std::chrono::milliseconds(100));
				}
				return false;
			};

			// (R1) play (authed) -> accepted
			if (!callTool("play", JsonValue::object(), true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: runtime - play was not "
					"accepted");
				return;
			}
			// (R2) wait for the player to boot + connect (fat window - it spawns
			// a process and boots the engine)
			JsonValue state;
			if (!pollState([](JsonValue const& s)
				{
					return s.get("play_mode").asString() == "playing" &&
						s.get("remote_connected").asString() == "1";
				}, 600, state))
			{
				finish(false, "control self-test: runtime - player never "
					"reached the playing state");
				return;
			}
			SDL_Log("orkige_editor: control self-test - runtime play up "
				"(%s remote objects)",
				state.get("remote_object_count").asString().c_str());

			// (R3) runtime_hierarchy (read) must list the running objects
			if (!callTool("runtime_hierarchy", JsonValue::object(), false,
					structured, isError) || isError)
			{
				finish(false, "control self-test: runtime_hierarchy failed");
				return;
			}
			JsonValue const& ids = structured.get("ids");
			if (ids.size() == 0)
			{
				finish(false, "control self-test: runtime_hierarchy returned no "
					"objects");
				return;
			}
			const String firstId = ids.at(0).asString();

			// (R4) runtime_select (authed), then poll runtime_state until it
			// streams that object with its Transform
			JsonValue selectArgs = JsonValue::object();
			selectArgs.set("id", JsonValue(firstId));
			if (!callTool("runtime_select", selectArgs, true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: runtime_select failed");
				return;
			}
			bool stateReady = false;
			JsonValue runtimeState;
			for (int attempt = 0; attempt < 100 && !stateReady; ++attempt)
			{
				bool stateError = true;
				if (callTool("runtime_state", JsonValue::object(), false,
						runtimeState, stateError) && !stateError &&
					runtimeState.get("ready").asString() == "1" &&
					runtimeState.get("object").asString() == firstId)
				{
					stateReady = true;
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (!stateReady)
			{
				finish(false, "control self-test: runtime_state never streamed "
					"the selected object");
				return;
			}
			// the streamed keys must include the Transform position
			bool hasPosition = false;
			JsonValue const& keys = runtimeState.get("properties");
			for (size_t i = 0; i < keys.size(); ++i)
			{
				if (keys.at(i).asString() == "TransformComponent.position")
				{
					hasPosition = true;
				}
			}
			if (!hasPosition)
			{
				finish(false, "control self-test: runtime_state missing the "
					"Transform position of the running object");
				return;
			}
			SDL_Log("orkige_editor: control self-test - runtime_state streams "
				"'%s'", firstId.c_str());

			// (R5) pause (authed) -> paused, then step (authed)
			if (!callTool("pause", JsonValue::object(), true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: pause failed");
				return;
			}
			if (!pollState([](JsonValue const& s)
				{
					return s.get("play_mode").asString() == "paused";
				}, 100, state))
			{
				finish(false, "control self-test: running game did not pause");
				return;
			}
			if (!callTool("step", JsonValue::object(), true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: step failed");
				return;
			}
			SDL_Log("orkige_editor: control self-test - runtime pause + step OK");

			// (R6) set_runtime_property (authed): move the Transform live, then
			// read it back through runtime_state
			JsonValue setArgs = JsonValue::object();
			setArgs.set("id", JsonValue(firstId));
			setArgs.set("component", JsonValue("TransformComponent"));
			setArgs.set("property", JsonValue("position"));
			setArgs.set("value", JsonValue("2 3 4"));
			if (!callTool("set_runtime_property", setArgs, true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: set_runtime_property failed");
				return;
			}
			bool moved = false;
			for (int attempt = 0; attempt < 100 && !moved; ++attempt)
			{
				bool stateError = true;
				if (callTool("runtime_state", JsonValue::object(), false,
						runtimeState, stateError) && !stateError)
				{
					JsonValue const& pk = runtimeState.get("properties");
					JsonValue const& pv = runtimeState.get("values");
					for (size_t i = 0; i < pk.size() && i < pv.size(); ++i)
					{
						if (pk.at(i).asString() ==
								"TransformComponent.position" &&
							pv.at(i).asString() == "2 3 4")
						{
							moved = true;
						}
					}
				}
				if (!moved)
				{
					std::this_thread::sleep_for(
						std::chrono::milliseconds(100));
				}
			}
			if (!moved)
			{
				finish(false, "control self-test: set_runtime_property did not "
					"move the running object to '2 3 4'");
				return;
			}
			SDL_Log("orkige_editor: control self-test - set_runtime_property "
				"moved the running object");

			// (R7) screenshot_game (authed): capture the running frame, poll
			// get_state until a FRESH confirmation (screenshot_seq advanced),
			// then verify the file
			const int prevSeq = std::atoi(state.get("screenshot_seq")
				.asString().c_str());
			JsonValue shotArgs = JsonValue::object();
			shotArgs.set("path", JsonValue(String(this->mScreenshotPath)));
			if (!callTool("screenshot_game", shotArgs, true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: screenshot_game was not "
					"accepted");
				return;
			}
			if (!pollState([prevSeq](JsonValue const& s)
				{
					return std::atoi(s.get("screenshot_seq").asString()
						.c_str()) > prevSeq;
				}, 100, state))
			{
				finish(false, "control self-test: running game never confirmed "
					"the screenshot");
				return;
			}
			std::error_code ignored;
			if (state.get("screenshot_ok").asString() != "1" ||
				state.get("screenshot_path").asString() !=
					this->mScreenshotPath ||
				!std::filesystem::exists(this->mScreenshotPath, ignored) ||
				std::filesystem::file_size(this->mScreenshotPath, ignored) == 0)
			{
				finish(false, "control self-test: screenshot_game did not write "
					"the running-game frame");
				return;
			}
			SDL_Log("orkige_editor: control self-test - screenshot_game wrote "
				"'%s'", this->mScreenshotPath.c_str());

			// (R8) reload_script (authed): a smoke over the running player (the
			// fixtures carry no scripts, so this reloads zero but must succeed)
			if (!callTool("reload_script", JsonValue::object(), true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: reload_script failed");
				return;
			}
			// an unauthenticated mutation on the LIVE player must still be
			// rejected (the auth gate holds during Play)
			if (!callTool("pause", JsonValue::object(), false, structured,
					isError) || !isError)
			{
				finish(false, "control self-test: unauthenticated pause on the "
					"live player was NOT rejected");
				return;
			}

			// (R9) stop (authed) -> back to edit mode, session torn down
			if (!callTool("stop", JsonValue::object(), true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: stop failed");
				return;
			}
			if (!pollState([](JsonValue const& s)
				{
					return s.get("play_mode").asString() == "edit";
				}, 300, state))
			{
				finish(false, "control self-test: play did not revert to edit "
					"mode after stop");
				return;
			}
			SDL_Log("orkige_editor: control self-test - runtime debug loop "
				"PASSED (play/inspect/pause/step/set/screenshot/stop)");
			finish(true, "");
			return;
		}

		// --- runtime debug tools, NEGATIVE paths (headless, no live player):
		// the runtime READS error cleanly when nothing is playing, and every
		// runtime MUTATION is rejected without the bearer token (the auth gate
		// fires before the play-state check).
		{
			JsonValue structured;
			bool isError = false;
			// runtime_hierarchy (read) with no player must error, not crash
			if (!callTool("runtime_hierarchy", JsonValue::object(), false,
					structured, isError) || !isError)
			{
				finish(false, "control self-test: runtime_hierarchy did not "
					"error with no live player");
				return;
			}
			isError = false;
			if (!callTool("runtime_state", JsonValue::object(), false,
					structured, isError) || !isError)
			{
				finish(false, "control self-test: runtime_state did not error "
					"with no live player");
				return;
			}
			// each runtime mutation WITHOUT auth must be rejected
			const char* mutations[] = { "runtime_select", "set_runtime_property",
				"set_cvar", "reload_script", "screenshot_game" };
			for (const char* mutation : mutations)
			{
				isError = false;
				if (!callTool(mutation, JsonValue::object(), false, structured,
						isError) || !isError)
				{
					finish(false, String("control self-test: unauthenticated ") +
						mutation + " was NOT rejected");
					return;
				}
			}
			SDL_Log("orkige_editor: control self-test - runtime debug tools "
				"error cleanly with no player + reject unauthenticated "
				"mutations");
		}

		// (8) GENERIC get_component: the reflected Transform
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

		// a get_test_results poller: spins on the job (100ms between polls, hard
		// cap) until it reports 'done', returning the structured verdict
		auto waitForTestJob = [&](String const& jobId, JsonValue& structured)
			-> bool
		{
			for (int attempt = 0; attempt < 300; ++attempt)
			{
				JsonValue args = JsonValue::object();
				args.set("jobId", JsonValue(jobId));
				bool isError = true;
				if (!callTool("get_test_results", args, false, structured,
					isError) || isError)
				{
					return false;
				}
				if (structured.get("status").asString() == "done")
				{
					return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			return false;
		};

		// (11) list_tests: discover the tree's tests; a known core unit test
		// must be present (this proves ctest -N discovery end to end)
		{
			JsonValue args = JsonValue::object();
			args.set("filter", JsonValue("JsonValue"));
			JsonValue structured;
			bool isError = true;
			if (!callTool("list_tests", args, false, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: list_tests failed");
				return;
			}
			JsonValue const& tests = structured.get("tests");
			bool foundKnown = false;
			for (size_t i = 0; i < tests.size(); ++i)
			{
				if (tests.at(i).asString() ==
					"JsonValue parses scalars, numbers and escapes")
				{
					foundKnown = true;
				}
			}
			if (!foundKnown)
			{
				finish(false, "control self-test: list_tests did not report the "
					"known JsonValue unit test");
				return;
			}
			SDL_Log("orkige_editor: control self-test - list_tests OK "
				"(%zu tests, known unit test present)", tests.size());
		}

		// (12) run_tests HAPPY PATH: run ONE fast, already-built unit test
		// (build='0' keeps the ctest quick - no multi-minute build in the test),
		// poll get_test_results, assert the structured tally reports it passed
		{
			JsonValue args = JsonValue::object();
			args.set("filter", JsonValue("JsonValue parses scalars"));
			args.set("build", JsonValue("0"));
			JsonValue accepted;
			bool isError = true;
			if (!callTool("run_tests", args, true, accepted, isError) || isError ||
				accepted.get("jobId").asString().empty())
			{
				finish(false, "control self-test: run_tests (happy path) was not "
					"accepted");
				return;
			}
			JsonValue result;
			if (!waitForTestJob(accepted.get("jobId").asString(), result))
			{
				finish(false, "control self-test: run_tests (happy path) never "
					"finished");
				return;
			}
			// the tally fields cross as strings (the DebugMessage convention)
			const int total = std::atoi(result.get("total").asString().c_str());
			const int passed = std::atoi(result.get("passed").asString().c_str());
			const int failed = std::atoi(result.get("failed").asString().c_str());
			if (result.get("buildFailed").asString() != "0" || total < 1 ||
				passed < 1 || failed != 0)
			{
				finish(false, "control self-test: run_tests (happy path) did not "
					"report the unit test as passed");
				return;
			}
			SDL_Log("orkige_editor: control self-test - run_tests happy path OK "
				"(total %d, passed %d, failed %d)", total, passed, failed);
		}

		// (13) run_tests FAILURE PATH: build a throwaway CTest tree (one passing
		// + one failing test) and run it, so the failed[] + logTail parse is
		// exercised on a real failure without breaking the real suite. The
		// failing test writes to stderr, so its logTail must be non-empty.
		{
			namespace fs = std::filesystem;
			const fs::path probeRoot = fs::temp_directory_path() /
				("orkige_rtprobe_" + std::to_string(port));
			const fs::path probeSrc = probeRoot / "src";
			const fs::path probeBuild = probeRoot / "build";
			std::error_code ignored;
			fs::remove_all(probeRoot, ignored);
			fs::create_directories(probeSrc, ignored);
			{
				std::ofstream lists(probeSrc / "CMakeLists.txt");
				lists <<
					"cmake_minimum_required(VERSION 3.20)\n"
					"project(rtprobe NONE)\n"
					"enable_testing()\n"
					"add_test(NAME rtprobe_pass COMMAND \"${CMAKE_COMMAND}\" "
					"-E true)\n"
					"add_test(NAME rtprobe_fail COMMAND \"${CMAKE_COMMAND}\" "
					"-E cat /orkige_no_such_file_probe)\n";
			}
			std::vector<std::string> configure = { std::string(ORKIGE_EDITOR_CMAKE),
				"-S", probeSrc.string(), "-B", probeBuild.string() };
			std::string configureOutput;
			int configureExit = 0;
			if (!runProcessCapture(configure, configureOutput, configureExit) ||
				configureExit != 0)
			{
				fs::remove_all(probeRoot, ignored);
				finish(false, "control self-test: could not configure the "
					"throwaway CTest tree");
				return;
			}
			JsonValue args = JsonValue::object();
			args.set("preset", JsonValue(probeBuild.string()));
			args.set("build", JsonValue("0"));
			JsonValue accepted;
			bool isError = true;
			if (!callTool("run_tests", args, true, accepted, isError) || isError ||
				accepted.get("jobId").asString().empty())
			{
				fs::remove_all(probeRoot, ignored);
				finish(false, "control self-test: run_tests (failure path) was "
					"not accepted");
				return;
			}
			JsonValue result;
			const bool finished =
				waitForTestJob(accepted.get("jobId").asString(), result);
			bool ok = finished &&
				std::atoi(result.get("total").asString().c_str()) == 2 &&
				std::atoi(result.get("passed").asString().c_str()) == 1 &&
				std::atoi(result.get("failed").asString().c_str()) == 1;
			bool failureReported = false;
			if (ok)
			{
				JsonValue const& names = result.get("failed_names");
				JsonValue const& tails = result.get("failed_logtails");
				for (size_t i = 0; i < names.size(); ++i)
				{
					if (names.at(i).asString() == "rtprobe_fail" &&
						!tails.at(i).asString().empty())
					{
						failureReported = true;
					}
				}
			}
			fs::remove_all(probeRoot, ignored);
			if (!ok || !failureReported)
			{
				finish(false, "control self-test: run_tests (failure path) did "
					"not report the failing test with a log tail");
				return;
			}
			SDL_Log("orkige_editor: control self-test - run_tests failure path OK "
				"(1 pass, 1 fail with a captured log tail)");
		}

		// --- the AUTHORING (DEVELOP) tools: an MCP-only agent writes/reads/lists
		// project files, imports an outside asset and round-trips a prefab. These
		// need an open project (the path jail is rooted at it), so open a throwaway
		// one first. Runs in the edit-world conversation (no live player needed).
		namespace fs = std::filesystem;
		const fs::path authRoot = fs::temp_directory_path() /
			("orkige_mcp_authoring_" + std::to_string(port));
		std::error_code authIgnored;
		fs::remove_all(authRoot, authIgnored);

		// (14) new_project (authed, force past the dirty McpProbe scene)
		{
			JsonValue args = JsonValue::object();
			args.set("path", JsonValue(authRoot.string()));
			args.set("force", JsonValue("1"));
			JsonValue structured;
			bool isError = true;
			if (!callTool("new_project", args, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: new_project for the authoring "
					"tools failed");
				return;
			}
			SDL_Log("orkige_editor: control self-test - authoring project opened "
				"('%s')", authRoot.string().c_str());
		}

		// (15) write_project_file -> read_project_file round-trip. The content
		// carries a CR the server must normalize to LF (the repo's LF rule).
		const String scriptRel = "scripts/mcp_probe.lua";
		const String scriptContent =
			"-- mcp authoring probe\r\nfunction update() end\n";
		const String scriptExpected =
			"-- mcp authoring probe\nfunction update() end\n";
		{
			JsonValue args = JsonValue::object();
			args.set("path", JsonValue(scriptRel));
			args.set("content", JsonValue(scriptContent));
			JsonValue structured;
			bool isError = true;
			if (!callTool("write_project_file", args, true, structured,
					isError) || isError ||
				structured.get("path").asString() != scriptRel)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: write_project_file did not "
					"report the written path");
				return;
			}
			JsonValue readArgs = JsonValue::object();
			readArgs.set("path", JsonValue(scriptRel));
			if (!callTool("read_project_file", readArgs, false, structured,
					isError) || isError ||
				structured.get("content").asString() != scriptExpected)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: read_project_file did not "
					"round-trip the written (LF-normalized) content");
				return;
			}
			SDL_Log("orkige_editor: control self-test - write/read_project_file "
				"round-trip OK (CRLF normalized to LF)");
		}

		// (16) JAIL VIOLATIONS: an absolute path and a '..' escape must BOTH be
		// refused (isError), without writing anything
		{
			const char* badPaths[] = {
				"/etc/orkige_mcp_probe_escape", "../orkige_mcp_probe_escape" };
			for (const char* bad : badPaths)
			{
				JsonValue args = JsonValue::object();
				args.set("path", JsonValue(String(bad)));
				args.set("content", JsonValue("nope"));
				JsonValue structured;
				bool isError = false;
				if (!callTool("write_project_file", args, true, structured,
						isError) || !isError)
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, String("control self-test: write_project_file "
						"did NOT refuse the jailed path '") + bad + "'");
					return;
				}
			}
			// neither escape wrote its file
			if (fs::exists("/etc/orkige_mcp_probe_escape", authIgnored) ||
				fs::exists(authRoot.parent_path() / "orkige_mcp_probe_escape",
					authIgnored))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: a jailed write_project_file "
					"nonetheless wrote outside the project");
				return;
			}
			SDL_Log("orkige_editor: control self-test - write_project_file jail "
				"refuses absolute + '..' paths");
		}

		// (17) AUTH REJECTION on an authoring mutation: no token -> refused
		{
			JsonValue args = JsonValue::object();
			args.set("path", JsonValue(String("scripts/mcp_unauthed.lua")));
			args.set("content", JsonValue("nope"));
			JsonValue structured;
			bool isError = false;
			if (!callTool("write_project_file", args, false, structured,
					isError) || !isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated "
					"write_project_file was NOT rejected");
				return;
			}
			SDL_Log("orkige_editor: control self-test - unauthenticated "
				"write_project_file correctly rejected");
		}

		// (18) list_project_files: the scripts/ dir, filtered to *.lua, must
		// include the file we just wrote
		{
			JsonValue args = JsonValue::object();
			args.set("dir", JsonValue(String("scripts")));
			args.set("glob", JsonValue(String("*.lua")));
			JsonValue structured;
			bool isError = true;
			if (!callTool("list_project_files", args, false, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_project_files failed");
				return;
			}
			JsonValue const& names = structured.get("names");
			bool found = false;
			for (size_t i = 0; i < names.size(); ++i)
			{
				if (names.at(i).asString() == "mcp_probe.lua")
				{
					found = true;
				}
			}
			if (!found)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_project_files did not list "
					"the written script");
				return;
			}
			SDL_Log("orkige_editor: control self-test - list_project_files OK "
				"(scripts/*.lua lists the written file)");
		}

		// (19) import_asset: copy an OUTSIDE temp file into the project, assert a
		// stable id is minted and the file lands under assets/
		{
			const fs::path outside = fs::temp_directory_path() /
				("orkige_mcp_import_" + std::to_string(port) + ".txt");
			{
				std::ofstream f(outside, std::ios::binary);
				f << "imported by mcp";
			}
			JsonValue args = JsonValue::object();
			args.set("sourcePath", JsonValue(outside.string()));
			JsonValue structured;
			bool isError = true;
			const bool ok = callTool("import_asset", args, true, structured,
				isError) && !isError;
			const String importedPath = structured.get("path").asString();
			const String importedId = structured.get("assetId").asString();
			fs::remove(outside, authIgnored);
			if (!ok || importedId.empty() ||
				importedPath.rfind("assets/", 0) != 0)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: import_asset did not mint an id "
					"for the copied-in file");
				return;
			}
			// AUTH: the same import without a token must be rejected
			bool unauthError = false;
			if (!callTool("import_asset", args, false, structured, unauthError) ||
				!unauthError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated import_asset was "
					"NOT rejected");
				return;
			}
			SDL_Log("orkige_editor: control self-test - import_asset OK "
				"(minted id %s at '%s')", importedId.c_str(),
				importedPath.c_str());
		}

		// (20) create_prefab -> instantiate_prefab round-trip: make an object, of
		// it a prefab, then instantiate a fresh instance of that prefab
		{
			JsonValue createArgs = JsonValue::object();
			createArgs.set("name", JsonValue("create_object"));
			JsonValue objArgs = JsonValue::object();
			objArgs.set("id", JsonValue("PrefabSource"));
			createArgs.set("arguments", objArgs);
			JsonValue reply;
			if (!post("tools/call", createArgs, true, true, reply) ||
				reply.get("result").get("isError").asBool(true))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: could not create the prefab "
					"source object");
				return;
			}
			// create_prefab (authed): write the .oprefab and convert the subtree
			JsonValue prefabArgs = JsonValue::object();
			prefabArgs.set("objectId", JsonValue("PrefabSource"));
			prefabArgs.set("path", JsonValue(String("assets/McpProbe.oprefab")));
			JsonValue structured;
			bool isError = true;
			if (!callTool("create_prefab", prefabArgs, true, structured,
					isError) || isError ||
				structured.get("path").asString() != "assets/McpProbe.oprefab")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_prefab did not write the "
					".oprefab and convert the object");
				return;
			}
			// the .oprefab actually exists on disk
			if (!fs::exists(authRoot / "assets" / "McpProbe.oprefab",
				authIgnored))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_prefab reported success "
					"but no .oprefab file was written");
				return;
			}
			// instantiate_prefab (authed): a fresh instance root appears
			JsonValue instArgs = JsonValue::object();
			instArgs.set("path", JsonValue(String("assets/McpProbe.oprefab")));
			if (!callTool("instantiate_prefab", instArgs, true, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: instantiate_prefab failed");
				return;
			}
			const String instanceId = structured.get("id").asString();
			if (instanceId.empty())
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: instantiate_prefab did not "
					"report a new instance id");
				return;
			}
			// the new instance is present in the hierarchy
			JsonValue listReply;
			JsonValue listArgs = JsonValue::object();
			bool listError = true;
			if (!callTool("list_hierarchy", listArgs, false, listReply,
					listError) || listError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_hierarchy after "
					"instantiate_prefab failed");
				return;
			}
			JsonValue const& ids = listReply.get("ids");
			bool found = false;
			for (size_t i = 0; i < ids.size(); ++i)
			{
				if (ids.at(i).asString() == instanceId)
				{
					found = true;
				}
			}
			if (!found)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the instantiated prefab is "
					"missing from the hierarchy");
				return;
			}
			SDL_Log("orkige_editor: control self-test - create/instantiate_prefab "
				"round-trip OK (instance '%s')", instanceId.c_str());
		}

		fs::remove_all(authRoot, authIgnored);
		finish(true, "");
	}
}
