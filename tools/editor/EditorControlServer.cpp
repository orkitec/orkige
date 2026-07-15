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
#include "EditorScriptHost.h"
#include "PythonToolchain.h"
#include "AnimationPreviewStage.h"
#include "GuiPreviewStage.h"
#include "GeneratedLuaApi.h"

#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>
#include <core_debugnet/DebugSocket.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectComponent.h>
#include <core_game/GameObjectManager.h>
#include <core_game/PrefabSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_debug/Breadcrumbs.h>
#include <core_util/optr.h>
#include <core_util/PlatformUtil.h>

#include <engine_gocomponent/ScriptComponentRegistry.h>
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
#include <future>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
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
	//! the structured outcome of one export_project job. Filled by the worker,
	//! read by get_export_results. Either error (the run could not even start /
	//! the exporter failed) OR ok + artifactPath.
	struct ExportRunResult
	{
		String platform;		//!< the requested platform (echoed back)
		String engineBuild;		//!< the build tree the exporter packaged from
		String error;			//!< non-empty: the export failed / was refused
		bool ok = false;
		String artifactPath;	//!< from the exporter's "OK <path>" line
		String outputTail;		//!< tail of the exporter's combined output
	};
	//! one asynchronous export. Same lifecycle as EditorTestJob: a worker thread
	//! runs Util/orkige_export.py, parks the verdict under mutex and flips done;
	//! get_export_results reports "running" until then, the result after.
	struct EditorExportJob
	{
		std::string id;
		std::thread worker;
		std::atomic<bool> done{ false };
		std::mutex mutex;
		ExportRunResult result;
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
				// a failed case is EITHER status="fail" (one CTest JUnit
				// convention) OR a <failure> child element under status="run"
				// (the other) - accept both, the writer varies by CTest build
				const bool hasFailureElement =
					body.find("<failure") != String::npos;
				if (status == "fail" || hasFailureElement)
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
					if (captured.empty() && hasFailureElement)
					{
						// no captured output: the <failure message="..."> is
						// the only trace this writer left - better than blank
						const size_t failAt = body.find("<failure");
						const String message = xmlAttribute(
							body.substr(failAt, body.find('>', failAt) -
								failAt + 1), "message");
						captured = xmlUnescape(message);
					}
					TestFailure failure;
					failure.name = name;
					failure.durationSec = time;
					failure.logTail = lastLines(captured, 40);
					result.failures.push_back(failure);
				}
				else if (status == "run")
				{
					++result.passed;
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
		//---------------------------------------------------------
		//--- export helpers (export_project / get_export_results) --
		//---------------------------------------------------------
		//! read a variable's value out of a build tree's CMakeCache.txt (the
		//! "KEY:TYPE=value" lines), "" when the cache or key is absent. Used to
		//! check an export tree's render flavor before spawning the exporter.
		String readCmakeCacheVar(String const& buildDir, String const& key)
		{
			std::ifstream cache(
				(std::filesystem::path(buildDir) / "CMakeCache.txt").string(),
				std::ios::binary);
			if (!cache)
			{
				return String();
			}
			String line;
			const String needle = key + ":";
			while (std::getline(cache, line))
			{
				if (line.rfind(needle, 0) == 0)
				{
					const size_t eq = line.find('=');
					if (eq != String::npos)
					{
						String value = line.substr(eq + 1);
						if (!value.empty() && value.back() == '\r')
						{
							value.pop_back();
						}
						return value;
					}
				}
			}
			return String();
		}
		//! @brief resolve the engine build tree the exporter packages from for a
		//! platform - mirrors EditorExport.cpp's startExport: THIS editor's tree
		//! for macOS, the mobile preset trees for the device targets.
		String resolveExportTree(String const& platform)
		{
			namespace fs = std::filesystem;
			if (platform == "ios-simulator")
			{
				return (fs::path(ORKIGE_EDITOR_ENGINE_ROOT) / "build" /
					"ios-simulator-debug").string();
			}
			if (platform == "android")
			{
				return (fs::path(ORKIGE_EDITOR_ENGINE_ROOT) / "build" /
					"android-debug").string();
			}
			return ORKIGE_EDITOR_ENGINE_BUILD_DIR;	// macos: this editor's tree
		}
		//! @brief point a play session at a target the picker enumerates, by the
		//! id list_play_targets reports: ''/'desktop' = the local player, an iOS
		//! simulator UDID or an adb serial = that device. Sets the same session
		//! fields the toolbar picker does; false (+ reason) for an unknown id.
		bool applyPlayTarget(PlaySession& session, String const& target,
			String& outError)
		{
			// reset to the desktop player first (a fresh pick each time)
			session.simulatorUdid.clear();
			session.simulatorLabel.clear();
			session.androidSerial.clear();
			session.androidLabel.clear();
			if (target.empty() || target == "desktop")
			{
				return true;
			}
#ifdef __APPLE__
			for (SimulatorDevice const& device : listSimulators())
			{
				if (device.udid == target)
				{
					session.simulatorUdid = device.udid;
					session.simulatorLabel = device.name;
					return true;
				}
			}
#endif
			for (AndroidDevice const& device : listAdbDevices())
			{
				if (device.serial == target)
				{
					session.androidSerial = device.serial;
					session.androidLabel = device.label;
					return true;
				}
			}
			outError = "unknown play target '" + target +
				"' (call list_play_targets for the available ids)";
			return false;
		}
		//! @brief the export_project worker body: run Util/orkige_export.py for
		//! the project and parse its "orkige_export: OK <path>" line. Runs on its
		//! own thread; parks the verdict under job->mutex and flips job->done.
		//! The command is built on the main thread (Project is not thread-safe)
		//! and handed in whole.
		void runExportJobWorker(EditorExportJob* job, ExportRunResult params,
			std::vector<std::string> command)
		{
			ExportRunResult result = params;
			std::string output;
			int exitCode = 0;
			const bool spawned = runProcessCapture(command, output, exitCode);
			result.outputTail = lastLines(output, 60);
			if (!spawned)
			{
				result.error = "could not start the exporter: " + output;
			}
			else if (exitCode != 0)
			{
				result.error = "export failed (exit " +
					std::to_string(exitCode) + ")";
			}
			else
			{
				// the exporter prints "orkige_export: OK <path>" on success
				const String marker = "orkige_export: OK ";
				const size_t at = output.rfind(marker);
				if (at != String::npos)
				{
					size_t end = output.find('\n', at);
					String path = output.substr(at + marker.size(),
						end == String::npos ? String::npos
							: end - (at + marker.size()));
					while (!path.empty() &&
						(path.back() == '\r' || path.back() == '\n'))
					{
						path.pop_back();
					}
					result.artifactPath = path;
					result.ok = true;
				}
				else
				{
					result.error = "exporter reported success but printed no "
						"artifact path";
				}
			}
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
		//! @brief resolve the grid-paint verbs' target cell CENTER from the
		//! request: either an explicit cell (col/row integer fields, flattened
		//! from the 'cell' object) or a world 'position' ("x y"/"x y z") snapped
		//! to the nearest cell (the same paintCellCoord snap the paint tool uses).
		//! false (+ a reason in outError) when neither is present or a supplied
		//! one does not parse.
		bool resolvePaintCell(DebugMessage const& request,
			EditorPaintGrid const& grid, float& centerX, float& centerY,
			String& outError)
		{
			if (request.has("col") && request.has("row"))
			{
				const int col = std::atoi(request.get("col").c_str());
				const int row = std::atoi(request.get("row").c_str());
				centerX = paintCellCenter(col, grid.originX, grid.cellSize);
				centerY = paintCellCenter(row, grid.originY, grid.cellSize);
				return true;
			}
			if (request.has("position"))
			{
				float xy[2] = { 0.0f, 0.0f };
				if (!parsePlayFloats(request.get("position"), xy, 2))
				{
					outError = "'position' must be a world 'x y' (or 'x y z')";
					return false;
				}
				const int col = paintCellCoord(xy[0], grid.originX, grid.cellSize);
				const int row = paintCellCoord(xy[1], grid.originY, grid.cellSize);
				centerX = paintCellCenter(col, grid.originX, grid.cellSize);
				centerY = paintCellCenter(row, grid.originY, grid.cellSize);
				return true;
			}
			outError = "needs a 'cell' {col,row} or a world 'position'";
			return false;
		}
		//---------------------------------------------------------
		//--- inline-image support (MCP image content blocks) -----
		//---------------------------------------------------------
		//! standard base64 (RFC 4648) of a raw byte buffer. Hand-rolled to keep the
		//! server dependency-free - the only consumer is the inline PNG block below.
		std::string base64Encode(const unsigned char* data, size_t length)
		{
			static const char kTable[] =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			out.reserve(((length + 2) / 3) * 4);
			size_t i = 0;
			for (; i + 3 <= length; i += 3)
			{
				const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
					(static_cast<unsigned int>(data[i + 1]) << 8) |
					static_cast<unsigned int>(data[i + 2]);
				out.push_back(kTable[(n >> 18) & 0x3F]);
				out.push_back(kTable[(n >> 12) & 0x3F]);
				out.push_back(kTable[(n >> 6) & 0x3F]);
				out.push_back(kTable[n & 0x3F]);
			}
			const size_t rem = length - i;
			if (rem == 1)
			{
				const unsigned int n = static_cast<unsigned int>(data[i]) << 16;
				out.push_back(kTable[(n >> 18) & 0x3F]);
				out.push_back(kTable[(n >> 12) & 0x3F]);
				out.push_back('=');
				out.push_back('=');
			}
			else if (rem == 2)
			{
				const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
					(static_cast<unsigned int>(data[i + 1]) << 8);
				out.push_back(kTable[(n >> 18) & 0x3F]);
				out.push_back(kTable[(n >> 12) & 0x3F]);
				out.push_back(kTable[(n >> 6) & 0x3F]);
				out.push_back('=');
			}
			return out;
		}
		//! largest PNG we inline into a tool result before it hurts the transport:
		//! above this we skip the block and note "inline_skipped" (the path stays).
		const size_t kMaxInlineImageBytes = 4u * 1024u * 1024u;	// 4 MiB
		//! the PNG path a synchronously-capturing verb wrote, or "" when the verb
		//! has no image at reply time (screenshot_game is async - see the docs).
		//! For a preview_ui sweep only the FIRST context's image is inlined (the
		//! reply has no scalar "path", so fall back to the head of "paths").
		std::string inlineImagePathForVerb(String const& verb,
			DebugMessage const& reply)
		{
			if (verb != "screenshot" && verb != "preview_ui" &&
				verb != "preview_animation")
			{
				return std::string();
			}
			if (reply.has("path") && !reply.get("path").empty())
			{
				return reply.get("path");
			}
			const StringVector& paths = reply.getList("paths");
			return paths.empty() ? std::string() : paths.front();
		}
		//---------------------------------------------------------
		//! is this verb a pure read (allowed before authentication)?
		bool isReadVerb(String const& type)
		{
			return type == "hello" || type == "ping" || type == "get_state" ||
				type == "list_hierarchy" || type == "get_object" ||
				type == "get_component" || type == "list_assets" ||
				type == "console_tail" || type == "list_addable_components" ||
				type == "list_tests" || type == "get_test_results" ||
				type == "runtime_hierarchy" || type == "runtime_state" ||
				type == "read_project_file" || type == "list_project_files" ||
				type == "list_play_targets" || type == "get_export_results" ||
				type == "list_paint_prefabs" ||
				type == "list_paintable_assets" || type == "get_safe_area" ||
				type == "get_ui_layout" || type == "get_breadcrumbs" ||
				type == "get_benchmark_results" ||
				type == "get_profile" || type == "get_lua_api" ||
				type == "get_project_setting";
		}
		//---------------------------------------------------------
		//! @brief verbs refused while a prefab edit stage is open: they address
		//! the SCENE/PROJECT document, Play, the paint grid or prefab instancing
		//! - none of which fit the single-root isolation stage (the editor's
		//! prefabEditBlocks guard refuses the same actions in the UI, so an MCP
		//! agent gets the same honest error instead of a generic document-level
		//! refusal). The prefab lifecycle verbs (open/save/close_prefab) and
		//! every pure editing/read verb are allowed through - they operate on
		//! "the world" the stage swapped the prefab subtree into.
		bool isBlockedInPrefabEdit(String const& type)
		{
			return type == "new_scene" || type == "open_scene" ||
				type == "save_scene" || type == "open_project" ||
				type == "new_project" || type == "close_project" ||
				type == "play" || type == "add_scene_to_levels" ||
				type == "create_prefab" || type == "instantiate_prefab" ||
				type == "paint_asset" || type == "paint_prefab" ||
				type == "erase_cell";
		}
		//---------------------------------------------------------
		//! @brief does this verb switch the edit document or history out from under
		//! an open MCP transaction? Such a verb clobbers/stashes the world (or
		//! rewinds the history) the transaction's commands were folding, so the
		//! transaction is auto-aborted BEFORE the verb runs. The scene/project/
		//! prefab lifecycle plus Play, plus undo/redo (which step outside the
		//! uncommitted fold).
		bool abortsOpenTransaction(String const& type)
		{
			return type == "new_scene" || type == "open_scene" ||
				type == "open_project" || type == "new_project" ||
				type == "close_project" || type == "play" ||
				type == "open_prefab" || type == "close_prefab" ||
				type == "undo" || type == "redo";
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
				  "selection, object count, undo/redo availability, play mode. "
				  "While a play session runs it also carries the streamed-music "
				  "snapshot (parallel 'music_ids'/'music_files' arrays plus a "
				  "'music_info' string per track: 'playing pos dur base group eff "
				  "loop'). While/after a compile-on-Play native build it also carries "
				  "'build_status' (none/building/ok/failed), 'build_target' and, "
				  "on a failure, the 'build_errors' compiler tail (kept after the "
				  "session reverts to edit mode).",
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
				{ "begin_transaction",
				  "Open an atomic-edit transaction: every mutating verb run until "
				  "end_transaction folds into ONE undo step on commit (or "
				  "unexecutes wholesale on abort) - the same one-undo atomicity an "
				  "editor script gets, for any MCP client. get_state reports "
				  "'transaction_open'. Refused (isError) when one is already open. "
				  "KEEP IT SHORT-LIVED: manual editor edits the owner makes in "
				  "between are folded in too, and the transaction auto-aborts if the "
				  "editor switches scene/project/prefab, starts Play, or shuts down.",
				  {} },
				{ "end_transaction",
				  "Close the open transaction. commit=true folds every verb run "
				  "since begin_transaction into ONE undo step (a single later undo "
				  "reverts them all); commit=false unexecutes them all, leaving no "
				  "partial edits. Refused (isError) when no transaction is open. "
				  "Returns 'committed' and the folded/rolled-back 'command_count'.",
				  { { "commit", "boolean",
				      "true to fold the edits into one undo step, false to roll "
				      "them all back", true } } },
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
				  "Enter play mode (async: returns accepted; poll get_state). "
				  "Optional 'scene' opens that scene into the editor first, then "
				  "plays it (project-relative and jailed when a project is open; "
				  "honors the unsaved-changes policy, force with force='1'). "
				  "Optional 'target' picks WHERE to run - '' or 'desktop' for the "
				  "local player, or an id from list_play_targets (an iOS simulator "
				  "UDID / an adb serial); a shutdown simulator boots async (poll "
				  "get_state).",
				  { { "scene", "string",
				      "scene to open+play (project-relative; default: the current "
				      "scene)", false },
				    { "target", "string",
				      "'desktop' (default) or a list_play_targets id", false },
				    { "force", "string",
				      "'1' to discard unsaved changes when opening 'scene'",
				      false } } },
				{ "list_play_targets",
				  "Enumerate the Play targets the editor's target picker shows: "
				  "the desktop player, iOS simulators (booted/shutdown), "
				  "enumerated iOS hardware (gated) and adb devices/emulators. "
				  "Returns 'target_count' and the parallel lists 'target_kinds' "
				  "(desktop/ios-simulator/ios-device/android), 'target_ids' (the "
				  "id to pass to play's 'target'), 'target_names' and "
				  "'target_states' (ready/booted/shutdown/gated/device).",
				  {} },
				{ "stop", "Stop play mode.", {} },
				{ "pause", "Pause the running player.", {} },
				{ "resume", "Resume the running player.", {} },
				{ "step", "Advance the paused player one frame.", {} },
				{ "screenshot",
				  "Write a PNG of the EDITOR: the chrome-free scene viewport, or "
				  "the whole editor window (window='1'). Returns the written "
				  "path AND inlines the PNG as an image content block (unless "
				  "inline=false or it exceeds 4 MiB). For the RUNNING game's "
				  "frame use screenshot_game.",
				  { { "path", "string", "output PNG path", true },
				    { "window", "string", "'1' for the whole window", false },
				    { "inline", "boolean", "inline the PNG as an image content "
				      "block (default true)", false } } },
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
				{ "get_safe_area",
				  "The RUNNING game's window size and safe-area insets (notch / "
				  "rounded corners / home indicator), in pixels: 'window_w', "
				  "'window_h' and 'safe_left','safe_top','safe_right', "
				  "'safe_bottom'. -1 until the player streams them; desktop "
				  "insets are 0. Read-only - assert the HUD lies inside "
				  "[safe_left, window_w-safe_right] x [safe_top, "
				  "window_h-safe_bottom].",
				  {} },
				{ "get_ui_layout",
				  "The RUNNING game's gui widget rects: parallel lists "
				  "'ids' and 'rects' (each rect a flat 'left top width height "
				  "visible enabled modal' string, pixels; the three flags are "
				  "1/0 - enabled=interactive, modal=part of an active modal "
				  "dialog). Streamed during Play; empty until the game builds its "
				  "UI. Combine with get_safe_area to check every visible widget "
				  "sits inside the safe box, or read the modal flag to assert a "
				  "dialog is up.",
				  {} },
				{ "gui_press",
				  "Synthesize a press on a gui widget in the RUNNING game by id, "
				  "routed through the REAL input path so modal and disabled "
				  "semantics apply: pressing a button under a modal scrim does "
				  "NOT fire, and a disabled widget stays inert. Use get_ui_layout "
				  "to find widget ids/flags. Errors when no player is connected.",
				  { { "id", "string", "gui widget id to press", true } } },
				{ "dismiss_modal",
				  "Close a modal dialog in the RUNNING game: 'id' names the modal "
				  "to close, or omit it to close the topmost one. Errors when no "
				  "player is connected.",
				  { { "id", "string", "modal id (omit = topmost)", false } } },
				{ "get_breadcrumbs",
				  "The crash breadcrumb trail the player leaves on disk: an "
				  "always-on, flush-per-entry ring of engine events (scene "
				  "loads, script errors, warnings, boot/shutdown), so after an "
				  "ABNORMAL exit (crash / OOM / device kill) you can still read "
				  "what happened up to the instant of death - the player is dead, "
				  "so this is pure file I/O, not the live debug link. Returns "
				  "'live' (this/most-recent session's breadcrumbs.jsonl text) and "
				  "'previous' (the prior session's, rotated aside at boot - the "
				  "one to read after a crash), each one JSON object per line, plus "
				  "the resolved 'dir'. Empty strings when a file is absent. "
				  "Read-only.",
				  {} },
				{ "get_benchmark_results",
				  "The per-scene performance results the player captured to disk "
				  "when armed with a benchmark run (ORKIGE_BENCHMARK): a JSONL "
				  "artifact with one 'meta' line (device/OS/GPU, flavor, render "
				  "system, build sha, scenario), one 'scene' record per scene "
				  "(frame-ms min/avg/p50/p95/p99/max, per-phase means, alloc "
				  "mean+peak, RSS peak, triangle/batch/texture means) and a "
				  "closing 'summary'. Like get_breadcrumbs this is pure file I/O "
				  "from the player's writable app dir (its project jail cannot "
				  "reach it). Picks the newest benchmark-*.jsonl, or the named "
				  "'file'. Returns the raw 'text', the parsed 'meta'/'summary' "
				  "lines, a 'scenes' array (one JSON object per scene), "
				  "'scene_count', 'aborted' and the resolved 'dir'/'file'. Empty "
				  "when no artifact exists. Read-only.",
				  { { "file", "string",
				      "artifact file name to read (omit = newest)", false } } },
				{ "get_profile",
				  "The RUNNING game's CPU frame profile: the last streamed "
				  "hierarchical scope snapshot as parallel lists 'names' and "
				  "'info' (each info a flat 'depth calls milliseconds "
				  "maxMilliseconds' string; depth 0 rows are the canonical tick "
				  "phases - input scripts events tweens physics load audio "
				  "present debug render), plus 'frame_ms' (whole-frame wall "
				  "time) and 'profile_seq' (snapshot counter - poll until it "
				  "advances for a fresh frame). Debug players stream this "
				  "automatically; on a Release player the first call arms the "
				  "profiler, so call again shortly after. Answers 'where does "
				  "the frame go?' - pair with get_state's alloc_per_frame/"
				  "alloc_tags for 'what allocates?'. Errors when no player is "
				  "connected.",
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
				{ "reload_ui",
				  "Hot-reload one declarative .oui screen on the RUNNING game: "
				  "destroy-and-rebuild that screen's widgets from the fresh file "
				  "(clean cutover). 'file' is the .oui name the game passed to "
				  "loadLayout (e.g. 'hud.oui'). A .oui that fails to parse keeps "
				  "the OLD screen and surfaces a [remote] error; a successful "
				  "rebuild emits the 'ui.reloaded' script event. Author the .oui "
				  "with write_project_file, then trigger it here (the editor's "
				  ".oui watcher also fires this on a file save). Errors when no "
				  "player is connected.",
				  { { "file", "string", "the .oui name (as passed to loadLayout)",
				      true } } },
				{ "screenshot_game",
				  "Screenshot the RUNNING game's next rendered frame to 'path' "
				  "(desktop play only; the path is on the player's filesystem, "
				  "shared with the editor). ASYNC: returns accepted + "
				  "prev_screenshot_seq; poll get_state until screenshot_seq "
				  "exceeds it, then screenshot_path/screenshot_ok carry the "
				  "result. The file does not exist yet at this reply, so (unlike "
				  "screenshot/preview_ui/preview_animation) this verb cannot "
				  "inline the image - read the confirmed path off the filesystem. "
				  "Errors when no player is connected.",
				  { { "path", "string", "output PNG path", true } } },
				{ "record_trace",
				  "Record a TEMPORAL TRACE of the RUNNING game to a .jsonl file "
				  "at 'path' - the flight-recorder evidence an agent READS (one "
				  "JSON object per line): per-frame samples of the named objects "
				  "(world pos, velocity when a rigid body exists, active/visible, "
				  "plus the frame dt) every 'everyNth' frame, with contact / "
				  "scene-load / script-error / warning events interleaved. "
				  "Desktop play only; the path is on the player's filesystem, "
				  "shared with the editor. Records for up to 'seconds' wall-clock "
				  "(default 5, capped at 60); 'objects' narrows to a "
				  "comma-separated id/name allowlist. ASYNC: returns accepted + "
				  "prev_record_seq; poll get_state until record_seq exceeds it, "
				  "then record_path/record_ok carry the result. Use stop_recording "
				  "to end early. Errors when no player is connected.",
				  { { "path", "string", "output .jsonl path", true },
				    { "seconds", "number", "max wall-clock seconds (default 5)",
				      false },
				    { "everyNth", "number", "sample every Nth frame (default 2)",
				      false },
				    { "objects", "string",
				      "comma-separated id/name allowlist (default: all)",
				      false } } },
				{ "stop_recording",
				  "Stop an in-progress record_trace early; the player writes what "
				  "it captured and confirms via get_state (record_seq advances, "
				  "record_path/record_ok carry the trace). Errors when no player "
				  "is connected.",
				  {} },
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
				{ "preview_ui",
				  "Render a project GUI screen (.oui) at a SIMULATED device "
				  "context into an offscreen target and return a screenshot + the "
				  "resolved widget rects - no running player needed. The centerpiece "
				  "of the collaborative UI loop: author a screen with "
				  "write_project_file, preview_ui it, iterate. Renders through the "
				  "REAL gui stack (the same one the game uses), isolated from any "
				  "running game. 'file' is a project-relative .oui. A single context "
				  "comes from 'width'/'height' (device pixels), 'scale' (content "
				  "scale 1/2/3) and 'insets' ('l t r b' safe-area pixels); OR pass "
				  "'contexts' for a device-matrix sweep: ';'-separated 'WxH[@scale]"
				  "[/l,t,r,b]' entries (e.g. '1179x2556@3/0,141,0,102; 2048x1536@2'). "
				  "Returns 'path' (single) or 'paths' + 'context_labels' (sweep), the "
				  "resolved 'width'/'height', and parallel 'ids'/'rects' (each rect "
				  "'left top width height visible enabled modal', pixels; the first "
				  "context for a sweep). Ogre-Next only (the classic editor reports "
				  "an honest error). Optional 'language' resolves the screen's "
				  "'@key' captions in that target language (from the project's loc/ "
				  "directory); omit for the source language. The result carries the "
				  "applied 'language' and the available 'languages'; a project with "
				  "no loc/ directory ignores 'language' with a 'language_note'. "
				  "The screenshot is also inlined as an image content block (the "
				  "FIRST context for a sweep) unless inline=false or it exceeds "
				  "4 MiB. Does not disturb the human's GUI Preview tab.",
				  { { "file", "string",
				      "project-relative .oui path (e.g. 'screens/title.oui')", true },
				    { "language", "string",
				      "preview language (a target from the result's 'languages'); "
				      "omit for the source language. Ignored with a note when the "
				      "project has no loc/ directory", false },
				    { "width", "number", "device width in pixels (default 1179)",
				      false },
				    { "height", "number", "device height in pixels (default 2556)",
				      false },
				    { "scale", "number", "content scale 1/2/3 (default 3)", false },
				    { "insets", "string",
				      "safe-area insets 'l t r b' in pixels (default '0 0 0 0')",
				      false },
				    { "contexts", "string",
				      "device-matrix sweep: ';'-separated 'WxH[@scale][/l,t,r,b]'",
				      false },
				    { "path", "string",
				      "output PNG path (default a temp file; a sweep appends an index)",
				      false },
				    { "inline", "boolean", "inline the screenshot (the first "
				      "sweep context) as an image content block (default true)",
				      false } } },
				{ "preview_animation",
				  "Render a vector-animation asset (.oanim) at a chosen clip and "
				  "time into a PNG and return the pose readback - no running player "
				  "needed. The animation twin of preview_ui: evaluate the pure rig "
				  "on the editor's own clock and CPU-rasterize the flat-colour pose "
				  "(the same raster that draws .oanim thumbnails), so an agent can "
				  "scrub a cycle (t=0 / mid / end screenshots) and try a blend "
				  "without a play session. 'asset' is a project-relative .oanim "
				  "path. 'clip' selects a clip by name (default the first); 'time' "
				  "is seconds into that clip (loop wraps, once clamps). Optional "
				  "'blendClip' + 'blendWeight' (0..1) mix a SECOND clip of the same "
				  "rig into the pose at the same time (the pose-level blend). Works "
				  "on both render flavors. Returns 'path' (the PNG), the resolved "
				  "'clip'/'frame'/'time', the rig 'duration' (frames)/'fps', "
				  "'layerCount'/'shapeCount'/'vertexCount', 'atEnd', and the "
				  "available 'clips'. The PNG is also inlined as an image content "
				  "block unless inline=false or it exceeds 4 MiB. Does not disturb "
				  "the human's Inspector animation preview.",
				  { { "asset", "string",
				      "project-relative .oanim path (e.g. 'assets/hero.oanim')",
				      true },
				    { "clip", "string",
				      "clip name to evaluate (default: the first clip)", false },
				    { "time", "number",
				      "seconds into the clip (default 0)", false },
				    { "blendClip", "string",
				      "a second clip of the same rig to blend in (optional)",
				      false },
				    { "blendWeight", "number",
				      "blend mix 0..1 (default 0.5 when a blendClip is given)",
				      false },
				    { "size", "number",
				      "square render size in pixels (default 256)", false },
				    { "path", "string",
				      "output PNG path (default a temp file)", false },
				    { "inline", "boolean", "inline the PNG as an image content "
				      "block (default true)", false } } },
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
				{ "open_prefab",
				  "Open a .oprefab ASSET for editing in the isolation stage (the "
				  "asset browser's double-click / hierarchy 'Open Prefab' over MCP; "
				  "needs auth). The live scene is snapshotted aside and the prefab "
				  "subtree is swapped into the ONE edit world, so EVERY editing verb "
				  "(create/delete/rename/reparent, get/set_component, undo/redo, "
				  "screenshot, run_editor_script) then operates on the prefab "
				  "unchanged. Give the prefab by 'path' (project-relative or "
				  "absolute) OR by stable 'asset' id. While staged the scene/project "
				  "lifecycle, play and the paint/instance verbs are refused (edit the "
				  "prefab, then close_prefab). Returns the stage 'root_id' (the file "
				  "stem) and 'prefab_path'. get_state reports edit_context='prefab'.",
				  { { "path", "string",
				      "project-relative or absolute .oprefab path", false },
				    { "asset", "string",
				      "stable asset id of the .oprefab (alternative to 'path')",
				      false } } },
				{ "save_prefab",
				  "Write the open prefab stage back to its .oprefab asset (Cmd/Ctrl+S "
				  "in prefab mode; needs auth). Refused with an honest error when the "
				  "stage root was deleted or objects exist OUTSIDE the single root "
				  "(savePrefab writes ONE subtree - strays would be silently lost; "
				  "parent them under the root or delete them). The open scene's "
				  "instances refresh from the rewritten file with their per-instance "
				  "overrides re-applied at close. Returns 'prefab_path'.",
				  {} },
				{ "close_prefab",
				  "Close the prefab isolation stage and restore the scene (needs "
				  "auth). 'policy' is REQUIRED (a headless caller never sees the UI "
				  "confirm modal): 'save' writes the prefab first (a refused save "
				  "cancels the close), 'discard' drops the unsaved stage edits (the "
				  ".oprefab keeps its last save). The reopened scene's prefab "
				  "instances rebuild from the (possibly edited) file with their "
				  "overrides re-applied. Returns the restored 'scene_path'; get_state "
				  "reports edit_context='scene' again.",
				  { { "policy", "string",
				      "'save' | 'discard' (required)", true } } },
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
				{ "export_project",
				  "Package the open project as a distributable via the export "
				  "pipeline (Util/orkige_export.py). ASYNC: returns { accepted, "
				  "jobId }; poll get_export_results. 'platform' is macos, "
				  "ios-simulator or android. The pipeline is pinned to the CLASSIC "
				  "render flavor - a missing or next-flavored engine tree is "
				  "refused up front with an honest error (build the matching "
				  "classic preset first). Native-module projects export desktop "
				  "only.",
				  { { "platform", "string",
				      "macos | ios-simulator | android", true } } },
				{ "get_export_results",
				  "Poll an export_project job. Returns status 'running' until the "
				  "export finishes, then 'done' with 'ok' ('1'/'0'), the "
				  "'artifactPath' (the built app/apk) on success or the 'error' on "
				  "failure, plus the exporter's 'outputTail'.",
				  { { "jobId", "string", "the id export_project returned",
				      true } } },
				{ "list_paintable_assets",
				  "List the open project's paintable palette for the grid-paint "
				  "level workflow and report the paint grid the paint verbs snap "
				  "to. Returns parallel lists 'paths' (project-relative asset "
				  "paths), 'names' (their stems) and 'kinds' ('prefab', 'texture' "
				  "or 'shape' - a prefab instantiates its subtree, a texture/shape "
				  "paints a bare tile), 'count', and the grid geometry "
				  "'origin_x'/'origin_y'/'cell_size' (the grid coincides with the "
				  "game's slots when the scene carries a level, else the editor's "
				  "translate snap step at the world origin).",
				  {} },
				{ "list_paint_prefabs",
				  "Back-compat alias of list_paintable_assets (which lists prefabs, "
				  "textures AND shapes, not prefabs only). Same result shape "
				  "including the 'kinds' list.",
				  {} },
				{ "paint_asset",
				  "Paint a tile into one grid cell as ONE undoable step - the MCP "
				  "equivalent of the editor's grid-paint tool (needs auth). The "
				  "'asset' is a project-relative or absolute path: a .oprefab "
				  "instantiates the prefab; a texture or .oshape paints a BARE tile "
				  "(a grid-cell sprite/shape object, no prefab file). An occupant of "
				  "the SAME cell is replaced (across kinds too); painting the "
				  "identical tile again is a no-op. Give the tile by 'cell' "
				  "{col,row} OR a world 'position' ('x y'/'x y z') snapped to the "
				  "nearest cell. Optional 'suppressed' drops prefab-local child ids "
				  "(prefab only). Returns the painted-root 'id', the 'kind', the "
				  "snapped 'col'/'row' and cell-center 'x'/'y', and 'painted' ('1', "
				  "or '0' on a no-op).",
				  { { "asset", "string",
				      "project-relative or absolute .oprefab/texture/.oshape path",
				      true },
				    { "cell", "object", "the target cell { col: int, row: int }",
				      false },
				    { "position", "string",
				      "world position 'x y' (or 'x y z') snapped to a cell", false },
				    { "suppressed", "array",
				      "prefab-local child ids to drop on the instance (prefab only)",
				      false } } },
				{ "paint_prefab",
				  "Back-compat alias of paint_asset. Accepts the source as 'prefab' "
				  "or 'asset'; despite the name it paints a texture/.oshape as a "
				  "bare tile too. Same result shape.",
				  { { "prefab", "string",
				      "project-relative or absolute .oprefab/texture/.oshape path",
				      true },
				    { "cell", "object", "the target cell { col: int, row: int }",
				      false },
				    { "position", "string",
				      "world position 'x y' (or 'x y z') snapped to a cell", false },
				    { "suppressed", "array",
				      "prefab-local child ids to drop on the instance", false } } },
				{ "erase_cell",
				  "Erase the tile in one grid cell as ONE undoable step, whatever "
				  "kind it is (prefab instance or bare tile) (needs auth). Give the "
				  "tile by 'cell' {col,row} OR a world 'position' snapped to the "
				  "nearest cell (same snap as paint_asset). Returns the snapped "
				  "'col'/'row', cell-center 'x'/'y' and 'erased' ('1' when a tile "
				  "was removed, '0' when the cell was already free).",
				  { { "cell", "object", "the target cell { col: int, row: int }",
				      false },
				    { "position", "string",
				      "world position 'x y' (or 'x y z') snapped to a cell",
				      false } } },
				{ "add_scene_to_levels",
				  "Append the current saved scene to the project's level sequence "
				  "(levels.olevels), minting the manifest 'levels' setting the first "
				  "time (needs auth). NOT undoable. Refused (honest error) when no "
				  "project is open, the scene is unsaved or outside the project "
				  "root, or the scene is already in the sequence.",
				  {} },
				{ "run_editor_script",
				  "Run a project editor TOOL (scripts/<name>.editor.lua) ONCE "
				  "through the editor-tool host - the same tool a human runs from "
				  "the Tools menu (needs auth). The tool's editor.* calls route "
				  "back through this verb handler and its whole run folds into ONE "
				  "undo step; a tool script error is reported (with its file:line) "
				  "and leaves NO partial edits. Author a tool with "
				  "write_project_file, then trigger it here. Returns 'name' and "
				  "'command_count' (undoable changes folded).",
				  { { "name", "string",
				      "the tool's stable name (its file base minus .editor.lua)",
				      true } } },
				{ "get_lua_api",
				  "The Lua scripting API signature index a ScriptComponent script "
				  "reaches: the global tables (world/screen/sound/music/tween/"
				  "guitween/haptics/cvar/save + the loc global) and the core value "
				  "types, one line per symbol. Read-only, needs no project or Play "
				  "session. Returns 'inventory' (the text index) and 'doc' (the path "
				  "to the full reference). Generated from the engine binding sources; "
				  "see Docs/lua-api.md for conventions (self/world/shared, the "
				  "lifecycle hooks) and the fuller type reference.",
				  {} },
				{ "get_project_setting",
				  "Read a project manifest Setting from the OPEN project - the "
				  "free-form key/value pairs in the .orkproj (e.g. "
				  "export.orientation, export.ios.bundleId). With 'key' returns that "
				  "one ('value' + 'has'); omit 'key' to return every setting as "
				  "'settings'. Read-only.",
				  { { "key", "string", "the Setting key (omit for all settings)",
				      false } } },
				{ "set_project_setting",
				  "Write a project manifest Setting on the OPEN project and persist "
				  "the .orkproj (needs auth). The authoritative path for export.* "
				  "config (export.orientation = portrait|landscape|auto, bundle ids, "
				  "versions): it updates the editor's IN-MEMORY project so a following "
				  "Build/export sees it - unlike a raw write_project_file of the "
				  ".orkproj, which the editor would not pick up. Refused when no "
				  "project is open.",
				  { { "key", "string", "the Setting key (e.g. export.orientation)",
				      true },
				    { "value", "string", "the new value", true } } },
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
		this->joinExportJobs();
		if (!this->mTokenFilePath.empty())
		{
			std::error_code ignored;
			std::filesystem::remove(this->mTokenFilePath, ignored);
			this->mTokenFilePath.clear();
		}
		this->mToken.clear();
		this->mAuthenticated = false;
		// the shutdown path aborts any open transaction with full context first;
		// drop the flag here defensively (a stopped server owns no transaction)
		this->mTransactionOpen = false;
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
	void EditorControlServer::joinExportJobs()
	{
		// wait for any in-flight exporter to exit before dropping the jobs
		// (same shutdown contract as the test workers)
		for (auto const& job : this->mExportJobs)
		{
			if (job->worker.joinable())
			{
				job->worker.join();
			}
		}
		this->mExportJobs.clear();
	}
	//---------------------------------------------------------
	EditorControlServer::TransactionFingerprint
	EditorControlServer::captureFingerprint(
		EditorControlContext const& context) const
	{
		TransactionFingerprint fp;
		if (context.state)
		{
			fp.projectRoot = context.state->project.getRootDirectory();
			fp.projectLoaded = context.state->project.isLoaded();
			fp.prefabActive = isPrefabEditActive(*context.state);
		}
		if (context.play)
		{
			fp.playActive = context.play->isActive();
		}
		return fp;
	}
	//---------------------------------------------------------
	void EditorControlServer::abortOpenTransaction(
		EditorControlContext const& context, String const& reason)
	{
		if (!this->mTransactionOpen)
		{
			return;
		}
		this->mTransactionOpen = false;
		std::size_t rolled = 0;
		if (context.core)
		{
			// unexecute every command executed since begin - no partial edits,
			// exactly the commit=false branch editor scripts take on a failed run
			rolled = context.core->endScriptTransaction(false,
				"MCP transaction (aborted)");
		}
		if (context.console)
		{
			context.console->addLine(ConsoleLevel::Warning,
				"[mcp] transaction auto-aborted (" + reason + ") - " +
				std::to_string(rolled) + " uncommitted edit(s) rolled back");
		}
	}
	//---------------------------------------------------------
	void EditorControlServer::checkTransactionLifecycle(
		EditorControlContext const& context)
	{
		if (!this->mTransactionOpen)
		{
			return;
		}
		const TransactionFingerprint now = this->captureFingerprint(context);
		const bool documentChanged =
			now.projectRoot != this->mTransactionFingerprint.projectRoot ||
			now.projectLoaded != this->mTransactionFingerprint.projectLoaded ||
			now.prefabActive != this->mTransactionFingerprint.prefabActive ||
			now.playActive != this->mTransactionFingerprint.playActive;
		// the undo history was reset/rewound beneath the transaction (a scene/
		// project/prefab switch clears it; a stray undo shrinks it) - the folded
		// range no longer describes the world, so bail out
		const bool historyRewound = context.core &&
			context.core->getUndoStackSize() < this->mTransactionUndoMark;
		if (documentChanged || historyRewound)
		{
			this->abortOpenTransaction(context,
				"the editor's document changed under the open transaction");
		}
	}
	//---------------------------------------------------------
	void EditorControlServer::update(EditorControlContext const& context)
	{
		if (!this->mServer.isListening())
		{
			return;
		}
		// catch a UI-triggered lifecycle change (the human used a menu while the
		// remote transaction was open) before this frame's verbs run
		this->checkTransactionLifecycle(context);
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

		// enforce the advertised inputSchema's REQUIRED parameters before
		// dispatch: a missing (or misnamed - unknown keys are dropped by the
		// codec) required argument must come back as an honest tool error,
		// never flow into a verb as an empty field (an empty scene path once
		// reached an internal assert and took the whole editor down)
		for (ToolSpec const& spec : toolSpecs())
		{
			if (name != spec.name)
			{
				continue;
			}
			JsonValue const& arguments = params.get("arguments");
			for (PropSpec const& prop : spec.properties)
			{
				if (!prop.required)
				{
					continue;
				}
				JsonValue const& value = arguments.get(prop.name);
				const bool missing = value.isNull() ||
					(value.isString() && value.asString().empty());
				if (missing)
				{
					return toolError(String(name) +
						": missing required parameter '" + prop.name + "'");
				}
			}
			break;
		}

		DebugMessage request(name);
		applyArguments(request, params.get("arguments"));

		// per-request auth: the verb handler's requireAuth reads mAuthenticated
		this->mAuthenticated = authenticated;
		this->runVerb(request, context);

		JsonValue structured = replyToJson(this->mReply);
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
			// inline the just-captured PNG as an MCP image content block so a
			// remote client sees the render directly (the path stays for pixel
			// diffing). Only the verbs that write the file synchronously before
			// this reply can carry it; opt out with argument inline:false, and a
			// too-large PNG is skipped with a note (both leave the path intact).
			// argToString maps a bool arg to "1"/"0", absent => "" (default on).
			const bool wantInline = request.get("inline") != "0";
			JsonValue imageBlock;
			bool haveImage = false;
			if (wantInline)
			{
				const std::string imagePath =
					inlineImagePathForVerb(name, this->mReply);
				if (!imagePath.empty())
				{
					std::ifstream file(imagePath,
						std::ios::binary | std::ios::ate);
					if (file)
					{
						const std::streamoff size = file.tellg();
						if (size > 0 && static_cast<size_t>(size) >
							kMaxInlineImageBytes)
						{
							structured.set("inline_skipped",
								JsonValue("too_large"));
						}
						else if (size > 0)
						{
							std::vector<unsigned char> bytes(
								static_cast<size_t>(size));
							file.seekg(0);
							file.read(reinterpret_cast<char*>(bytes.data()), size);
							imageBlock = JsonValue::object();
							imageBlock.set("type", JsonValue("image"));
							imageBlock.set("data", JsonValue(
								base64Encode(bytes.data(), bytes.size())));
							imageBlock.set("mimeType", JsonValue("image/png"));
							haveImage = true;
						}
					}
				}
			}
			text.set("text", JsonValue(structured.serialize()));
			content.push(text);
			if (haveImage)
			{
				content.push(imageBlock);
			}
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
	bool EditorControlServer::dispatchLocalVerb(DebugMessage const& request,
		EditorControlContext const& context, DebugMessage& outReply)
	{
		// a local caller (the editor-script host, a test) has the human's full
		// authority - pre-authenticate so mutation verbs run - then reuse the
		// exact verb handler and hand back its buffered reply.
		this->mAuthenticated = true;
		this->runVerb(request, context);
		outReply = this->mReply;
		return !this->mReplyIsError;
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
		const String& req = request.get(DebugProtocol::FIELD_REQ);
		EditorState& state = *context.state;
		EditorCore& core = *context.core;
		GameObjectManager& manager = *context.gameObjectManager;

		// auth gate: everything but the pure reads needs a prior valid token
		if (!isReadVerb(type) && !this->requireAuth(req))
		{
			return;
		}

		// prefab edit mode gate: while the isolation stage is open, the verbs
		// that address the scene/project document, Play, the paint grid or
		// prefab instancing are refused with an honest error naming the mode -
		// the same set the editor's prefabEditBlocks guard covers. Every pure
		// editing verb (hierarchy CRUD, get/set_component, undo/redo,
		// screenshot, run_editor_script) runs against the prefab stage
		// UNCHANGED, and open/save/close_prefab pass through.
		if (isPrefabEditActive(state) && isBlockedInPrefabEdit(type))
		{
			this->sendErr(req, type + " is unavailable while a prefab is open in "
				"the isolation stage - edit the prefab, then close_prefab (use "
				"save_prefab to write it, or open_scene/play once closed)");
			return;
		}

		// MCP transaction lifecycle: a transaction spans multiple requests, so a
		// verb that switches the edit document (scene/project/prefab) or starts
		// Play would strand it - its commands folded across a world it no longer
		// describes. Abort it FIRST, BEFORE the switch clobbers/stashes the world,
		// so the rolled-back edits are gone by the time the new document loads (a
		// UI-triggered switch gets the same safety from checkTransactionLifecycle).
		if (this->mTransactionOpen && abortsOpenTransaction(type))
		{
			this->abortOpenTransaction(context, "the '" + type + "' verb");
		}
		// an editor script is ITSELF one transaction (EditorCore::beginScript-
		// Transaction is not nestable), so refuse to run one inside an open MCP
		// transaction rather than trip the nesting assert
		if (type == "run_editor_script" && this->mTransactionOpen)
		{
			this->sendErr(req, "an MCP transaction is open - end_transaction before "
				"running an editor script (an editor script is itself one "
				"transaction)");
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
			// prefab edit stage: while a .oprefab is open in the isolation stage
			// the editing verbs address the prefab subtree, not the scene.
			// edit_context tells the two apart; when staged, scene_path/
			// scene_dirty above are re-reported from the STASHED scene (the one
			// the close will restore) so the agent's model of the document it
			// returns to stays truthful, and prefab_* describes the live stage.
			const bool inPrefabEdit = isPrefabEditActive(state);
			ok.set("edit_context", inPrefabEdit ? "prefab" : "scene");
			if (inPrefabEdit)
			{
				PrefabEditContext const& prefab = state.prefabEditStack.back();
				ok.set("prefab_path", prefab.prefabPath);
				ok.set("prefab_root", prefab.rootId);
				ok.set("prefab_dirty", core.isSceneDirty() ? "1" : "0");
				ok.set("scene_path", prefab.stashedScenePath);
				ok.set("scene_dirty", prefab.stashedSceneDirty ? "1" : "0");
			}
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
			// running-game trace (record_trace / stop_recording): the last
			// confirmed trace + a sequence a poller waits to advance
			ok.set("recording", play.recordingActive ? "1" : "0");
			ok.set("record_path", play.lastRecordPath);
			ok.set("record_ok", play.lastRecordOk ? "1" : "0");
			ok.set("record_seq", std::to_string(play.recordSeq));
			// running-game memory (streamed as MSG_STATS): the process resident
			// set size and the session peak, in bytes; "-1" until the player
			// streams one (or on a platform without a memory query) - an agent
			// reads the current value against the peak to spot growth
			ok.set("mem_rss", std::to_string(play.remoteMemRss));
			ok.set("mem_rss_peak", std::to_string(play.remoteMemRssPeak));
			// engine-level allocation counters + frame time (MSG_STATS): the
			// per-frame churn summary ("-1" until the player streams one).
			// alloc_tags/alloc_counts break the last frame down per subsystem;
			// profile_seq counts received MSG_PROFILE_DATA snapshots (see
			// get_profile for the full hierarchy).
			ok.set("alloc_per_frame", std::to_string(play.remoteAllocPerFrame));
			ok.set("alloc_peak", std::to_string(play.remoteAllocPeak));
			{
				std::ostringstream frameMs;
				frameMs << std::fixed << std::setprecision(3)
					<< play.remoteFrameMs;
				ok.set("frame_ms", frameMs.str());
			}
			// the running game's current named state (Lua game.setState via
			// core_game/GameState, streamed on MSG_STATS); "" until it sets one
			ok.set("game_state", play.remoteGameState);
			{
				StringVector allocTags;
				StringVector allocCounts;
				for (std::size_t i = 0; i < play.remoteAllocTags.size() &&
					i < play.remoteAllocCounts.size(); ++i)
				{
					allocTags.push_back(play.remoteAllocTags[i]);
					allocCounts.push_back(
						std::to_string(play.remoteAllocCounts[i]));
				}
				ok.setList("alloc_tags", allocTags);
				ok.setList("alloc_counts", allocCounts);
			}
			ok.set("profile_seq", std::to_string(play.profileSeq));
			// streamed music (MSG_STATS): the per-track snapshot an agent reads
			// to confirm background music is playing and at the right gain.
			// Parallel arrays keep the structuredContent flat: music_ids /
			// music_files, plus one "playing pos dur base group eff loop" info
			// string per id. Empty when nothing streams (or no live player).
			{
				StringVector musicIds;
				StringVector musicFiles;
				StringVector musicInfo;
				for (PlaySession::RemoteMusicTrack const& track :
					play.remoteMusic)
				{
					musicIds.push_back(track.id);
					musicFiles.push_back(track.file);
					std::ostringstream info;
					info << (track.playing ? 1 : 0) << ' '
						<< track.positionSec << ' '
						<< track.durationSec << ' '
						<< track.baseGain << ' '
						<< track.groupVolume << ' '
						<< track.effectiveGain << ' '
						<< (track.loop ? 1 : 0);
					musicInfo.push_back(info.str());
				}
				ok.setList("music_ids", musicIds);
				ok.setList("music_files", musicFiles);
				ok.setList("music_info", musicInfo);
			}
			// compile-on-Play build verdict (native-module projects): the
			// structured signal an agent reads instead of scraping [build] lines
			// out of console_tail - none/building/ok/failed, the module target,
			// and the compiler error tail on a failure (kept after Stop)
			ok.set("build_status", playSessionBuildName(play));
			ok.set("build_target", play.buildStatusTarget);
			ok.set("build_errors", lastLines(play.buildErrorLog, 40));
			// MCP transaction bracket: '1' while a begin_transaction is open (its
			// edits fold into one undo step at end_transaction). A human/agent
			// reads this to see the atomic-edit session is live.
			ok.set("transaction_open", this->mTransactionOpen ? "1" : "0");
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& component = request.get(DebugProtocol::FIELD_COMPONENT);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& component = request.get(DebugProtocol::FIELD_COMPONENT);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& newId = request.get("new_id");
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& parent = request.get("parent");	// "" = make a root
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& component = request.get(DebugProtocol::FIELD_COMPONENT);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& component = request.get(DebugProtocol::FIELD_COMPONENT);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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

		//--- MCP transactions (atomic multi-verb edits) ------
		// begin_transaction / end_transaction give a REMOTE client the same
		// one-undo atomicity an .editor.lua tool gets, over the wire: everything
		// executed between them folds into ONE undo step on commit, or unexecutes
		// wholesale on abort. Both reuse EditorCore::begin/endScriptTransaction
		// verbatim (see the .editor.lua host); the server just owns the flag
		// because the bracket spans HTTP requests, and auto-aborts it if a
		// document-lifecycle transition would strand it (abortsOpenTransaction /
		// checkTransactionLifecycle). KEEP IT SHORT-LIVED: the fold is origin-blind
		// (like an editor script's), so a manual editor edit the owner makes in
		// between IS adopted into the transaction too.
		if (type == "begin_transaction")
		{
			if (this->mTransactionOpen)
			{
				this->sendErr(req, "a transaction is already open - end_transaction "
					"(commit or roll back) before beginning another");
				return;
			}
			core.beginScriptTransaction();
			this->mTransactionOpen = true;
			this->mTransactionFingerprint = this->captureFingerprint(context);
			this->mTransactionUndoMark = core.getUndoStackSize();
			if (context.console)
			{
				context.console->addLine(ConsoleLevel::Info,
					"[mcp] transaction opened - edits fold into one undo step "
					"until end_transaction");
			}
			this->sendOk(req);
			return;
		}
		if (type == "end_transaction")
		{
			if (!this->mTransactionOpen)
			{
				this->sendErr(req, "no transaction is open - call begin_transaction "
					"first");
				return;
			}
			// 'commit' is REQUIRED (a headless caller states intent explicitly):
			// true folds every command executed since begin into ONE undo step,
			// false unexecutes them all (no partial edits) - editor-script semantics
			if (!request.has("commit"))
			{
				this->sendErr(req, "end_transaction needs 'commit' (true to fold the "
					"edits into one undo step, false to roll them all back)");
				return;
			}
			const String& commitArg = request.get("commit");
			const bool commit = commitArg == "1" || commitArg == "true";
			this->mTransactionOpen = false;
			const std::size_t count =
				core.endScriptTransaction(commit, "MCP transaction");
			DebugMessage ok(MSG_OK);
			ok.set("committed", commit ? "1" : "0");
			ok.set("command_count", std::to_string(count));
			this->sendOk(req, ok);
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
			// optional 'scene': open that scene into the edit world FIRST, then
			// play it. Jailed inside the open project (an absolute path or a
			// '..' escape is refused); with no project open the raw path is used
			// (same as open_scene). Honors the dirty-state policy - opening a
			// different scene discards the current unsaved edit world unless
			// force=1.
			const String& scene = request.get(DebugProtocol::FIELD_SCENE);
			if (!scene.empty())
			{
				if (clobberRefused()) return;
				String openPath = scene;
				if (state.project.isLoaded())
				{
					std::filesystem::path absolute;
					String relative;
					String jailError;
					if (!jailProjectPath(state.project.getRootDirectory(), scene,
						absolute, relative, jailError))
					{
						this->sendErr(req, "play scene: " + jailError);
						return;
					}
					openPath = absolute.string();
				}
				if (!openSceneFromPath(state, core, openPath))
				{
					this->sendErr(req, "play: could not open scene '" + scene +
						"'");
					return;
				}
			}
			// optional 'target': mirror the editor's Play target picker. ''/
			// 'desktop' runs the local player; a simulator UDID or an adb serial
			// (as list_play_targets reports them) runs on that device. Device
			// boots stay async (poll get_state), same as the toolbar.
			const String& target = request.get("target");
			String targetError;
			if (!applyPlayTarget(*context.play, target, targetError))
			{
				this->sendErr(req, targetError);
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
			ok.set("target", target.empty() ? String("desktop") : target);
			this->sendOk(req, ok);
			return;
		}
		if (type == "list_play_targets")
		{
			// what the Play target picker shows: the desktop player, iOS
			// simulators (APPLE), enumerated iOS hardware (only once signing
			// is configured, mirroring the toolbar - devicectl is the slowest
			// probe and its devices are undeployable without an identity) and
			// adb devices/emulators. Parallel lists keyed by index.
			//
			// The probes run CONCURRENTLY: each carries its own cold-service
			// timeout-and-retry budget, and the CLIENT times the whole verb
			// (the self-test reads with a 45s socket deadline) - sequential
			// worst cases add up past any such deadline, while the max of
			// them stays below it.
			DebugMessage ok(MSG_OK);
			StringVector kinds;
			StringVector ids;
			StringVector names;
			StringVector states;
			kinds.push_back("desktop");
			ids.push_back("desktop");
			names.push_back("Desktop");
			states.push_back("ready");
#ifdef __APPLE__
			std::future<std::vector<SimulatorDevice>> simulatorProbe =
				std::async(std::launch::async, listSimulators);
			std::future<std::vector<IosHardwareDevice>> hardwareProbe =
				std::async(std::launch::async, []
				{
					return isIosSigningConfigured()
						? listIosHardwareDevices()
						: std::vector<IosHardwareDevice>();
				});
#endif
			std::future<std::vector<AndroidDevice>> androidProbe =
				std::async(std::launch::async, listAdbDevices);
#ifdef __APPLE__
			for (SimulatorDevice const& device : simulatorProbe.get())
			{
				kinds.push_back("ios-simulator");
				ids.push_back(device.udid);
				names.push_back(device.name);
				states.push_back(device.booted ? "booted" : "shutdown");
			}
			for (IosHardwareDevice const& device : hardwareProbe.get())
			{
				// enumerated but not deployable over MCP (Play on iOS
				// hardware is the toolbar's export-and-deploy flow)
				kinds.push_back("ios-device");
				ids.push_back(device.udid);
				names.push_back(device.name);
				states.push_back("gated");
			}
#endif
			for (AndroidDevice const& device : androidProbe.get())
			{
				kinds.push_back("android");
				ids.push_back(device.serial);
				names.push_back(device.label);
				states.push_back("device");
			}
			ok.set("target_count", std::to_string(ids.size()));
			ok.setList("target_kinds", kinds);
			ok.setList("target_ids", ids);
			ok.setList("target_names", names);
			ok.setList("target_states", states);
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
		// get_safe_area: the running game's window size + safe-area insets
		// (streamed on MSG_STATS). Read-only; -1 fields = not reported yet.
		if (type == "get_safe_area")
		{
			PlaySession const& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("window_w", std::to_string(play.remoteWindowW));
			ok.set("window_h", std::to_string(play.remoteWindowH));
			ok.set("safe_left", std::to_string(play.remoteSafeLeft));
			ok.set("safe_top", std::to_string(play.remoteSafeTop));
			ok.set("safe_right", std::to_string(play.remoteSafeRight));
			ok.set("safe_bottom", std::to_string(play.remoteSafeBottom));
			this->sendOk(req, ok);
			return;
		}
		// get_ui_layout: the running game's gui widget rects (streamed on
		// MSG_UI_LAYOUT). Parallel ids/rects lists; each rect a flat
		// "left top width height visible enabled modal" string in pixels + flags
		// (enabled = interactive, modal = part of an active modal dialog).
		if (type == "get_ui_layout")
		{
			PlaySession const& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			StringVector ids;
			StringVector rects;
			for (PlaySession::RemoteWidgetRect const& widget :
				play.remoteUiLayout)
			{
				ids.push_back(widget.id);
				rects.push_back(std::to_string(widget.left) + " " +
					std::to_string(widget.top) + " " +
					std::to_string(widget.width) + " " +
					std::to_string(widget.height) + " " +
					(widget.visible ? "1" : "0") + " " +
					(widget.enabled ? "1" : "0") + " " +
					(widget.modal ? "1" : "0"));
			}
			DebugMessage ok(MSG_OK);
			ok.setList("ids", ids);
			ok.setList("rects", rects);
			// the screen router state (empty when the game uses no screen stack):
			// the current top screen + the space-joined bottom-to-top path
			ok.set("screen", play.remoteScreenCurrent);
			ok.set("screenStack", play.remoteScreenStack);
			this->sendOk(req, ok);
			return;
		}

		// get_profile: the RUNNING game's CPU frame profile - the last
		// streamed MSG_PROFILE_DATA snapshot, served like get_ui_layout.
		// Debug players stream it automatically; when nothing has arrived yet
		// (a Release player boots with the profiler off) the call ARMS the
		// runtime profiler over MSG_PROFILE, so a snapshot follows on the
		// stats cadence - poll until profile_seq advances.
		if (type == "get_profile")
		{
			PlaySession& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			if (play.profileSeq == 0)
			{
				DebugMessage arm(DebugProtocol::MSG_PROFILE);
				arm.set(DebugProtocol::FIELD_VALUE, "1");
				play.client.send(arm);
			}
			StringVector names;
			StringVector infos;
			for (PlaySession::RemoteProfileNode const& node :
				play.remoteProfile)
			{
				names.push_back(node.name);
				std::ostringstream info;
				info << node.depth << ' ' << node.calls << ' '
					<< std::fixed << std::setprecision(3)
					<< node.milliseconds << ' ' << node.maxMilliseconds;
				infos.push_back(info.str());
			}
			DebugMessage ok(MSG_OK);
			ok.setList("names", names);
			ok.setList("info", infos);
			{
				std::ostringstream frameMs;
				frameMs << std::fixed << std::setprecision(3)
					<< play.remoteProfileFrameMs;
				ok.set("frame_ms", frameMs.str());
			}
			ok.set("profile_seq", std::to_string(play.profileSeq));
			this->sendOk(req, ok);
			return;
		}
			// preview_ui: render a project .oui at a simulated device context into
			// an offscreen target and return a screenshot + widget rects. The same
			// GuiPreviewStage the GUI Preview tab uses; snapshot/restore keeps the
			// human's tab undisturbed. No running player needed.
			if (type == "preview_ui")
			{
				OrkigeEditor::GuiPreviewStage* stage = context.previewStage;
				if (!stage)
				{
					this->sendErr(req, "preview stage unavailable");
					return;
				}
				if (!state.project.isLoaded())
				{
					this->sendErr(req, "no project open - preview_ui needs an "
						"open project");
					return;
				}
				if (!Orkige::RenderTexture::canOwnLayers())
				{
					this->sendErr(req, "preview_ui is Ogre-Next only (offscreen "
						"2D composition is not supported on the classic backend)");
					return;
				}
				const String file = request.get("file");
				if (file.empty())
				{
					this->sendErr(req, "preview_ui needs a 'file' (project-"
						"relative .oui path)");
					return;
				}
				const std::string root = state.project.getRootDirectory();

				// language axis: load the project's loc/ directory (idempotent)
				// and resolve the requested preview language. Empty => the source
				// language. A project with no loc/ directory ignores 'language'
				// with a note (no error); an unknown language IS an error listing
				// the valid set.
				stage->loadLocalisation(state.project);
				const StringVector availableLanguages = stage->getLanguages();
				const String requestedLanguage = request.get("language");
				String appliedLanguage;
				String languageNote;
				if (!requestedLanguage.empty())
				{
					if (availableLanguages.empty())
					{
						languageNote = "the project has no localisation directory "
							"(manifest Settings 'localisation'); language ignored";
					}
					else if (std::find(availableLanguages.begin(),
						availableLanguages.end(), requestedLanguage) ==
						availableLanguages.end())
					{
						std::string list;
						for (String const& lang : availableLanguages)
						{
							list += (list.empty() ? "" : ", ") + lang;
						}
						this->sendErr(req, "preview_ui: unknown language '" +
							requestedLanguage + "' (available: " + list + ")");
						return;
					}
					else
					{
						appliedLanguage = requestedLanguage;
					}
				}

				auto trim = [](std::string s) -> std::string
				{
					size_t a = s.find_first_not_of(" \t");
					size_t b = s.find_last_not_of(" \t");
					return a == std::string::npos ? std::string()
						: s.substr(a, b - a + 1);
				};
				// parse a single "WxH[@scale][/l,t,r,b]" into a context
				auto parseContext = [&](std::string const& spec,
					OrkigeEditor::GuiPreviewContext& out) -> bool
				{
					std::string s = trim(spec);
					std::string insetPart;
					const size_t slash = s.find('/');
					if (slash != std::string::npos)
					{
						insetPart = s.substr(slash + 1);
						s = s.substr(0, slash);
					}
					std::string scalePart;
					const size_t at = s.find('@');
					if (at != std::string::npos)
					{
						scalePart = s.substr(at + 1);
						s = s.substr(0, at);
					}
					const size_t x = s.find('x');
					if (x == std::string::npos)
					{
						return false;
					}
					out.width = static_cast<unsigned int>(
						std::atoi(trim(s.substr(0, x)).c_str()));
					out.height = static_cast<unsigned int>(
						std::atoi(trim(s.substr(x + 1)).c_str()));
					if (!scalePart.empty())
					{
						out.contentScale = static_cast<float>(
							std::atof(trim(scalePart).c_str()));
					}
					if (!insetPart.empty())
					{
						unsigned int v[4] = { 0, 0, 0, 0 };
						std::stringstream ss(insetPart);
						std::string tok;
						int n = 0;
						while (n < 4 && std::getline(ss, tok, ','))
						{
							v[n++] = static_cast<unsigned int>(
								std::atoi(trim(tok).c_str()));
						}
						out.insets.mLeft = v[0];
						out.insets.mTop = v[1];
						out.insets.mRight = v[2];
						out.insets.mBottom = v[3];
					}
					return out.width > 0 && out.height > 0;
				};

				std::vector<OrkigeEditor::GuiPreviewContext> contexts;
				const String contextsArg = request.get("contexts");
				if (!contextsArg.empty())
				{
					std::stringstream ss(contextsArg);
					std::string entry;
					while (std::getline(ss, entry, ';'))
					{
						if (trim(entry).empty())
						{
							continue;
						}
						OrkigeEditor::GuiPreviewContext ctx;
						if (!parseContext(entry, ctx))
						{
							this->sendErr(req, "preview_ui: bad context '" +
								trim(entry) + "' (expected 'WxH[@scale][/l,t,r,b]')");
							return;
						}
						contexts.push_back(ctx);
					}
					if (contexts.empty())
					{
						this->sendErr(req, "preview_ui: 'contexts' had no entries");
						return;
					}
				}
				else
				{
					OrkigeEditor::GuiPreviewContext ctx;	// defaults 1179x2556@3
					if (!request.get("width").empty())
					{
						ctx.width = static_cast<unsigned int>(
							std::atoi(request.get("width").c_str()));
					}
					if (!request.get("height").empty())
					{
						ctx.height = static_cast<unsigned int>(
							std::atoi(request.get("height").c_str()));
					}
					if (!request.get("scale").empty())
					{
						ctx.contentScale = static_cast<float>(
							std::atof(request.get("scale").c_str()));
					}
					if (!request.get("insets").empty())
					{
						unsigned int v[4] = { 0, 0, 0, 0 };
						std::stringstream ss(request.get("insets"));
						unsigned int val;
						int n = 0;
						while (n < 4 && (ss >> val))
						{
							v[n++] = val;
						}
						ctx.insets.mLeft = v[0];
						ctx.insets.mTop = v[1];
						ctx.insets.mRight = v[2];
						ctx.insets.mBottom = v[3];
					}
					if (ctx.width == 0 || ctx.height == 0)
					{
						this->sendErr(req, "preview_ui: width/height must be > 0");
						return;
					}
					contexts.push_back(ctx);
				}

				// snapshot the human's tab so we can restore it afterwards
				const OrkigeEditor::GuiPreviewContext savedContext =
					stage->getContext();
				const std::string savedFile = stage->getLoadedFile();
				const std::string savedLanguage = stage->getPreviewLanguage();
				stage->setPreviewLanguage(appliedLanguage);	// show() applies it

				const bool sweep = contexts.size() > 1;
				const std::string basePath = request.get("path").empty()
					? (std::filesystem::temp_directory_path() /
						"orkige_preview_ui.png").string()
					: request.get("path");

				StringVector paths;
				StringVector labels;
				StringVector ids;
				StringVector rects;
				size_t primaryBatchCount = 0;
				std::string err;
				bool ok = true;
				for (size_t i = 0; i < contexts.size(); ++i)
				{
					stage->setContext(contexts[i]);
					if (!stage->show(root, file, err))
					{
						ok = false;
						break;
					}
					std::string outPath = basePath;
					if (sweep)
					{
						const std::filesystem::path p(basePath);
						outPath = (p.parent_path() / (p.stem().string() + "_" +
							std::to_string(i) + p.extension().string())).string();
					}
					if (!stage->renderAndCapture(outPath, err))
					{
						ok = false;
						break;
					}
					paths.push_back(outPath);
					labels.push_back(std::to_string(contexts[i].width) + "x" +
						std::to_string(contexts[i].height) + "@" +
						std::to_string(static_cast<int>(contexts[i].contentScale)));
					if (i == 0)
					{
						primaryBatchCount = stage->getLastBatchCount();
						for (OrkigeEditor::GuiPreviewWidgetRect const& r :
							stage->getWidgetRects())
						{
							ids.push_back(r.id);
							rects.push_back(
								std::to_string(static_cast<int>(r.left)) + " " +
								std::to_string(static_cast<int>(r.top)) + " " +
								std::to_string(static_cast<int>(r.width)) + " " +
								std::to_string(static_cast<int>(r.height)) + " " +
								(r.visible ? "1" : "0") + " " +
								(r.enabled ? "1" : "0") + " " +
								(r.modal ? "1" : "0"));
						}
					}
				}

				// restore the tab's previous view (undisturbed collaboration)
				std::string restoreErr;
				stage->setContext(savedContext);
				stage->setPreviewLanguage(savedLanguage);
				stage->show(root, savedFile, restoreErr);

				if (!ok)
				{
					this->sendErr(req, "preview_ui failed: " + err);
					return;
				}
				DebugMessage okMsg(MSG_OK);
				okMsg.set("file", file);
				okMsg.set("width", std::to_string(contexts[0].width));
				okMsg.set("height", std::to_string(contexts[0].height));
				okMsg.set("batch_count", std::to_string(primaryBatchCount));
				if (!sweep)
				{
					okMsg.set("path", paths[0]);
				}
				okMsg.setList("paths", paths);
				okMsg.setList("context_labels", labels);
				okMsg.setList("ids", ids);
				okMsg.setList("rects", rects);
				// the language axis: the applied language ("" = source), the
				// available set, and a note when a given language was ignored
				okMsg.set("language", appliedLanguage);
				okMsg.setList("languages", availableLanguages);
				if (!languageNote.empty())
				{
					okMsg.set("language_note", languageNote);
				}
				this->sendOk(req, okMsg);
				return;
			}

			// preview_animation: evaluate a project .oanim at a clip/time (+ an
			// optional same-rig blend) on the editor-owned animation stage, CPU-
			// rasterize the pose to a PNG and return the pose readback. The same
			// AnimationPreviewStage the Animation Preview panel uses; snapshot/
			// restore keeps the human's panel undisturbed. No running player,
			// both flavors (pure CPU raster - no offscreen target).
			if (type == "preview_animation")
			{
				OrkigeEditor::AnimationPreviewStage* stage =
					context.animPreviewStage;
				if (!stage)
				{
					this->sendErr(req, "animation preview stage unavailable");
					return;
				}
				if (!state.project.isLoaded())
				{
					this->sendErr(req, "no project open - preview_animation needs "
						"an open project");
					return;
				}
				const String asset = request.get("asset");
				if (asset.empty())
				{
					this->sendErr(req, "preview_animation needs an 'asset' "
						"(project-relative .oanim path)");
					return;
				}
				const std::string root = state.project.getRootDirectory();

				// the rig's clip names, comma-joined (for an unknown-clip error)
				auto clipList = [&]() -> std::string
				{
					std::string list;
					for (std::string const& name : stage->getInfo().clipNames)
					{
						list += (list.empty() ? "" : ", ") + name;
					}
					return list;
				};

				// snapshot the human's panel so we can restore it afterwards
				const std::string savedFile = stage->getLoadedFile();
				const int savedClip = stage->getClipIndex();
				const float savedTime = stage->getTimeSeconds();
				const int savedSize = stage->getSize();

				std::string err;
				if (!stage->load(root, asset, err))
				{
					this->sendErr(req, "preview_animation: " + err);
					// restore the panel's prior view before returning
					std::string restoreErr;
					stage->load(root, savedFile, restoreErr);
					return;
				}
				if (!request.get("size").empty())
				{
					stage->setSize(std::atoi(request.get("size").c_str()));
				}
				// clip: by name (default the first clip)
				const String clip = request.get("clip");
				if (!clip.empty() && !stage->setClipByName(clip))
				{
					this->sendErr(req, "preview_animation: unknown clip '" + clip +
						"' (available: " + clipList() + ")");
					std::string restoreErr;
					stage->load(root, savedFile, restoreErr);
					return;
				}
				if (!request.get("time").empty())
				{
					stage->setTimeSeconds(static_cast<float>(
						std::atof(request.get("time").c_str())));
				}
				// optional same-rig blend (a second clip mixed at the same time)
				const String blendClip = request.get("blendClip");
				if (!blendClip.empty())
				{
					float weight = 0.5f;
					if (!request.get("blendWeight").empty())
					{
						weight = static_cast<float>(
							std::atof(request.get("blendWeight").c_str()));
					}
					if (!stage->setBlend(blendClip, weight))
					{
						this->sendErr(req, "preview_animation: unknown blendClip '" +
							blendClip + "' (available: " + clipList() + ")");
						std::string restoreErr;
						stage->load(root, savedFile, restoreErr);
						return;
					}
				}

				const std::string outPath = request.get("path").empty()
					? (std::filesystem::temp_directory_path() /
						"orkige_preview_animation.png").string()
					: request.get("path");
				const bool ok = stage->renderToPng(outPath, err);
				const OrkigeEditor::AnimationPreviewInfo info = stage->getInfo();

				// restore the panel's previous view (undisturbed collaboration)
				std::string restoreErr;
				stage->load(root, savedFile, restoreErr);
				if (savedClip >= 0)
				{
					stage->setClipIndex(savedClip);
					stage->setTimeSeconds(savedTime);
				}
				stage->setSize(savedSize);

				if (!ok)
				{
					this->sendErr(req, "preview_animation failed: " + err);
					return;
				}
				DebugMessage okMsg(MSG_OK);
				okMsg.set("path", outPath);
				okMsg.set("asset", asset);
				okMsg.set("clip", info.clipName);
				okMsg.set("frame", std::to_string(info.frame));
				okMsg.set("time", std::to_string(info.timeSeconds));
				okMsg.set("duration", std::to_string(info.durationFrames));
				okMsg.set("fps", std::to_string(info.fps));
				okMsg.set("layer_count", std::to_string(info.layerCount));
				okMsg.set("shape_count", std::to_string(info.shapeCount));
				okMsg.set("vertex_count", std::to_string(info.vertexCount));
				okMsg.set("visible_pixel_count",
					std::to_string(info.visiblePixelCount));
				okMsg.set("coloured_pixel_count",
					std::to_string(info.colouredPixelCount));
				okMsg.set("at_end", info.atEnd ? "1" : "0");
				if (info.blending)
				{
					okMsg.set("blend_clip", info.blendClipName);
					okMsg.set("blend_weight", std::to_string(info.blendWeight));
				}
				okMsg.setList("clips", info.clipNames);
				this->sendOk(req, okMsg);
				return;
			}

		// get_lua_api: the generated Lua scripting API signature index (embedded
		// at build time from Docs/lua-api.md's generated block via
		// GeneratedLuaApi.h). Read-only, needs no project or player - it lets any
		// MCP client learn the scripting surface without shipping a doc alongside.
		if (type == "get_lua_api")
		{
			DebugMessage ok(MSG_OK);
			ok.set("inventory", kGeneratedLuaApiIndex);
			ok.set("doc", "Docs/lua-api.md");
			this->sendOk(req, ok);
			return;
		}
		// get_breadcrumbs: read the player's on-disk crash trail (pure file I/O -
		// the player may be dead, so this never touches the live link). The
		// player writes breadcrumbs.jsonl into its writable app dir
		// (getSupportDirectory on desktop), rotating the prior run's file to
		// breadcrumbs.prev.jsonl at boot. ORKIGE_BREADCRUMB_DIR overrides the
		// directory (matches the player's own override, used for test isolation).
		if (type == "get_breadcrumbs")
		{
			std::string dir;
			if (const char* dirEnv = std::getenv("ORKIGE_BREADCRUMB_DIR"))
			{
				dir = dirEnv;
			}
			else
			{
				dir = PlatformUtil::getSupportDirectory("Orkige Player");
			}
			if (!dir.empty() && dir.back() != '/')
			{
				dir += '/';
			}
			String live;
			String previous;
			Breadcrumbs::loadFile(dir + "breadcrumbs.jsonl", live);
			Breadcrumbs::loadFile(dir + "breadcrumbs.prev.jsonl", previous);
			DebugMessage ok(MSG_OK);
			ok.set("dir", dir);
			ok.set("live", live);
			ok.set("previous", previous);
			ok.set("live_bytes", std::to_string(live.size()));
			ok.set("previous_bytes", std::to_string(previous.size()));
			this->sendOk(req, ok);
			return;
		}
		// get_benchmark_results: read the per-scene performance artifact the
		// player wrote to its writable app dir (like get_breadcrumbs, pure file
		// I/O - the artifact outlives the player). Picks the newest
		// benchmark-*.jsonl by default, or the named 'file'; parses the JSONL
		// into the meta line, one string per scene record and the summary line
		// (the whole raw text too). ORKIGE_BENCHMARK_DIR overrides the directory
		// (matches the player's own override, used for test isolation).
		if (type == "get_benchmark_results")
		{
			std::string dir;
			if (const char* dirEnv = std::getenv("ORKIGE_BENCHMARK_DIR"))
			{
				dir = dirEnv;
			}
			else
			{
				dir = PlatformUtil::getSupportDirectory("Orkige Player");
			}
			if (!dir.empty() && dir.back() != '/')
			{
				dir += '/';
			}
			// choose the artifact: an explicit 'file' (basename), else the newest
			// benchmark-*.jsonl in the directory (by last-write time)
			String chosen = request.get("file");
			if (chosen.empty())
			{
				std::error_code listErr;
				std::filesystem::file_time_type newest{};
				for (std::filesystem::directory_iterator it(dir, listErr), end;
					it != end; it.increment(listErr))
				{
					if (listErr)
					{
						break;
					}
					const std::filesystem::path& path = it->path();
					const std::string name = path.filename().string();
					if (name.rfind("benchmark-", 0) != 0 ||
						path.extension() != ".jsonl")
					{
						continue;
					}
					std::error_code timeErr;
					const std::filesystem::file_time_type when =
						std::filesystem::last_write_time(path, timeErr);
					if (!timeErr && (chosen.empty() || when > newest))
					{
						newest = when;
						chosen = name;
					}
				}
			}
			String text;
			if (!chosen.empty())
			{
				Breadcrumbs::loadFile(dir + chosen, text);
			}
			// split the JSONL and classify each line (meta / scene / summary)
			String meta;
			String summary;
			StringVector scenes;
			bool aborted = false;
			std::istringstream lineStream(text);
			std::string jsonLine;
			while (std::getline(lineStream, jsonLine))
			{
				if (jsonLine.empty())
				{
					continue;
				}
				JsonValue value;
				if (!JsonValue::parse(jsonLine, value) || !value.isObject())
				{
					continue;
				}
				const String kind = value.get("type").asString();
				if (kind == "meta")
				{
					meta = jsonLine;
				}
				else if (kind == "scene")
				{
					scenes.push_back(jsonLine);
				}
				else if (kind == "summary")
				{
					summary = jsonLine;
					aborted = value.get("aborted").asBool();
				}
			}
			DebugMessage ok(MSG_OK);
			ok.set("dir", dir);
			ok.set("file", chosen);
			ok.set("text", text);
			ok.set("meta", meta);
			ok.set("summary", summary);
			ok.set("scene_count", std::to_string(scenes.size()));
			ok.set("aborted", aborted ? "true" : "false");
			ok.setList("scenes", scenes);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
			const String& component = request.get(DebugProtocol::FIELD_COMPONENT);
			const String& property = request.get(DebugProtocol::FIELD_PROPERTY);
			const String& value = request.get(DebugProtocol::FIELD_VALUE);
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
			const String& name = request.get(DebugProtocol::FIELD_CVAR_NAME);
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
			const String& id = request.get(DebugProtocol::FIELD_ID);
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
		if (type == "reload_ui")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			const String& file = request.get("file");
			if (file.empty())
			{
				this->sendErr(req, "reload_ui needs a 'file' (the .oui name)");
				return;
			}
			// fire-and-forget like reload_script: the player rebuilds the screen
			// (or, on a parse failure, keeps the old one and logs a [remote]
			// error visible in console_tail). Reuse the editor's own watcher path.
			reloadRemoteUi(*context.play, *context.console, file);
			this->sendOk(req);
			return;
		}
		if (type == "gui_press")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			const String& id = request.get(DebugProtocol::FIELD_ID);
			if (id.empty())
			{
				this->sendErr(req, "gui_press needs a widget 'id'");
				return;
			}
			DebugMessage press(DebugProtocol::MSG_GUI_PRESS);
			press.set(DebugProtocol::FIELD_ID, id);
			context.play->client.send(press);
			this->sendOk(req);
			return;
		}
		if (type == "dismiss_modal")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			// FIELD_ID may be "" = dismiss the topmost modal
			DebugMessage dismiss(DebugProtocol::MSG_GUI_DISMISS_MODAL);
			dismiss.set(DebugProtocol::FIELD_ID,
				request.get(DebugProtocol::FIELD_ID));
			context.play->client.send(dismiss);
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
			const String& path = request.get("path");
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
		if (type == "record_trace")
		{
			PlaySession& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			if (play.onSimulator || play.onAndroid)
			{
				this->sendErr(req, "record_trace is desktop-play only (the path "
					"lives on the player's filesystem)");
				return;
			}
			const String& path = request.get("path");
			if (path.empty())
			{
				this->sendErr(req, "record_trace needs a 'path'");
				return;
			}
			const float seconds = request.getFloat("seconds", 5.0f);
			const float every = request.getFloat("everyNth", 2.0f);
			const String& objects = request.get("objects");
			// async: the player samples over the next frames and answers with
			// record_saved; poll get_state (record_path/record_ok/record_seq)
			// for the confirmation. Report the accepted request + the sequence
			// BEFORE it so a poller knows a fresh save is a higher record_seq.
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("path", path);
			ok.set("prev_record_seq", std::to_string(play.recordSeq));
			requestRemoteRecord(play, path, seconds,
				every < 1.0f ? 1u : static_cast<unsigned int>(every), objects);
			this->sendOk(req, ok);
			return;
		}
		if (type == "stop_recording")
		{
			PlaySession& play = *context.play;
			if (!play.client.isConnected())
			{
				this->sendErr(req, "no live player - start Play first");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("prev_record_seq", std::to_string(play.recordSeq));
			stopRemoteRecord(play);
			this->sendOk(req, ok);
			return;
		}

		//--- screenshot (out of band; returns the written path) --
		if (type == "screenshot")
		{
			const String& path = request.get("path");
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
			// a freshly written script may introduce (or, on an overwrite/rename,
			// change) a SCRIPT COMPONENT KIND - rescan so add_component /
			// list_addable_components / the Add Component menu see it without
			// reopening the project
			if (relative.rfind("scripts/", 0) == 0)
			{
				ScriptComponentRegistry::getSingleton().scanProject(
					state.project.getScriptsDirectory(),
					state.project.getRootDirectory());
				// a freshly written *.editor.lua is a new/changed editor TOOL -
				// rescan so the Tools menu + run_editor_script see it at once
				if (state.editorScripts)
				{
					state.editorScripts->scanProject(
						state.project.getScriptsDirectory());
				}
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
			const String& glob = request.get("glob");
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
			const String& source = request.get("sourcePath");
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
			const String& targetDir = request.get("targetDir");
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
			const String& objectId = request.get("objectId");
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
			const String& parentId = request.get("parent");
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

		//--- prefab edit mode (the isolation stage over MCP) -
		// open_prefab / save_prefab / close_prefab wrap the EditorDocument free
		// functions 1:1. While a prefab is staged the pure editing verbs above
		// operate on the prefab subtree unchanged (the single-world payoff);
		// get_state reports the stage (edit_context/prefab_*). See open_scene's
		// refusal in the prefab-mode gate at the top of handleMessage.
		if (type == "open_prefab")
		{
			// give the .oprefab by 'path' (project-relative or absolute) or by
			// stable 'asset' id (resolved to a project path like import_asset)
			String pathArg = request.get("path");
			if (pathArg.empty())
			{
				const String assetId = request.get("asset");
				if (!assetId.empty() && state.project.isLoaded())
				{
					if (optr<AssetDatabase> const& database =
						state.project.getAssetDatabase())
					{
						pathArg = database->pathForId(assetId);
					}
				}
			}
			if (pathArg.empty())
			{
				this->sendErr(req, "open_prefab needs a 'path' (project-relative "
					"or absolute .oprefab) or an 'asset' stable id");
				return;
			}
			// a relative ref resolves against the project root (the paint/
			// instantiate verbs' convention)
			std::filesystem::path absolute(pathArg);
			if (!absolute.is_absolute() && state.project.isLoaded())
			{
				absolute =
					std::filesystem::path(state.project.resolvePath(pathArg));
			}
			if (absolute.extension() != ".oprefab")
			{
				this->sendErr(req, "open_prefab: '" + pathArg + "' is not a "
					".oprefab file");
				return;
			}
			if (!openPrefabForEdit(state, core, absolute.string()))
			{
				this->sendErr(req, "open_prefab refused - already editing a "
					"prefab, or '" + pathArg + "' is missing/corrupt/nested (see "
					"the editor log)");
				return;
			}
			PrefabEditContext const& opened = state.prefabEditStack.back();
			DebugMessage ok(MSG_OK);
			ok.set("root_id", opened.rootId);
			ok.set("prefab_path", opened.prefabPath);
			this->sendOk(req, ok);
			return;
		}
		if (type == "save_prefab")
		{
			if (!isPrefabEditActive(state))
			{
				this->sendErr(req, "save_prefab: no prefab is open (call "
					"open_prefab first)");
				return;
			}
			const String prefabPath = state.prefabEditStack.back().prefabPath;
			if (!savePrefabEdit(state, core))
			{
				this->sendErr(req, "save_prefab refused - the prefab root was "
					"deleted or objects exist outside it (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("prefab_path", prefabPath);
			this->sendOk(req, ok);
			return;
		}
		if (type == "close_prefab")
		{
			if (!isPrefabEditActive(state))
			{
				this->sendErr(req, "close_prefab: no prefab is open");
				return;
			}
			// policy is required (a headless caller never sees the UI confirm
			// modal): 'save' writes the prefab first (a refused save cancels the
			// close), 'discard' drops the unsaved stage edits
			const String& policyArg = request.get("policy");
			PrefabClosePolicy policy = PrefabClosePolicy::Discard;
			if (policyArg == "save")
			{
				policy = PrefabClosePolicy::Save;
			}
			else if (policyArg != "discard")
			{
				this->sendErr(req, "close_prefab needs an explicit 'policy': "
					"'save' (write the prefab first) or 'discard' (drop unsaved "
					"stage edits)");
				return;
			}
			const String scenePath =
				state.prefabEditStack.back().stashedScenePath;
			if (!closePrefabEdit(state, core, policy))
			{
				this->sendErr(req, "close_prefab failed - saving the prefab was "
					"refused, or the scene snapshot could not be restored (see "
					"the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("scene_path", scenePath);
			this->sendOk(req, ok);
			return;
		}

		//--- 2D grid painting (level authoring) --------------
		// list_paint_prefabs / list_paintable_assets: the project's paintable
		// palette (prefabs + textures + .oshape) + the grid the paint verbs snap
		// to. Parallel 'paths'/'names'/'kinds' lists (the list_project_files
		// shape); a loopback listing is harmless, so it is a read (no auth). The
		// two verb names share this handler - list_paintable_assets is the honest
		// name now that bare art is paintable, list_paint_prefabs the back-compat
		// alias.
		if (type == "list_paint_prefabs" || type == "list_paintable_assets")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - grid painting sources "
					"tiles from the project's assets/");
				return;
			}
			const unsigned int paintableMask =
				(1u << static_cast<unsigned int>(AssetKind::Prefab)) |
				(1u << static_cast<unsigned int>(AssetKind::Texture)) |
				(1u << static_cast<unsigned int>(AssetKind::VectorShape));
			std::vector<AssetBrowserItem> const items = searchAssets(
				state.project, state.project.getRootDirectory(), String(), true,
				paintableMask);
			StringVector paths;
			StringVector names;
			StringVector kinds;
			for (AssetBrowserItem const& item : items)
			{
				paths.push_back(item.relativePath);
				names.push_back(
					std::filesystem::path(item.relativePath).stem().string());
				kinds.push_back(item.kind == AssetKind::Prefab ? "prefab"
					: item.kind == AssetKind::VectorShape ? "shape" : "texture");
			}
			const EditorPaintGrid grid = core.resolvePaintGrid();
			DebugMessage ok(MSG_OK);
			ok.setList("paths", paths);
			ok.setList("names", names);
			ok.setList("kinds", kinds);
			ok.set("count", std::to_string(paths.size()));
			ok.setFloat("origin_x", grid.originX);
			ok.setFloat("origin_y", grid.originY);
			ok.setFloat("cell_size", grid.cellSize);
			this->sendOk(req, ok);
			return;
		}
		// paint_prefab / paint_asset: paint a tile into one grid cell (undoable,
		// same cell replaces its occupant of ANY kind) - the grid-paint tool's
		// core action, kept generic (no tag/stamp sugar; those are the editor
		// palette's tile layer). The source is a project-relative or absolute
		// path: a .oprefab instantiates the prefab; a texture or .oshape paints a
		// BARE tile (a grid-cell sprite/shape object, no prefab file). paint_asset
		// is the honest name, paint_prefab the back-compat alias; either arg name
		// ('prefab' or 'asset') is accepted.
		if (type == "paint_prefab" || type == "paint_asset")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - grid painting needs a "
					"project");
				return;
			}
			String assetArg = request.get("asset");
			if (assetArg.empty())
			{
				assetArg = request.get("prefab");
			}
			if (assetArg.empty())
			{
				this->sendErr(req, type + " needs an 'asset' (or 'prefab') path: "
					"a .oprefab, texture or .oshape");
				return;
			}
			// resolve to an absolute path (a relative ref resolves against the
			// project root), classify it, then derive its ref + id the same way
			// the asset-browser instantiate does
			std::filesystem::path absolute(assetArg);
			if (!absolute.is_absolute())
			{
				absolute = std::filesystem::path(
					state.project.resolvePath(assetArg));
			}
			std::error_code ec;
			if (!std::filesystem::is_regular_file(absolute, ec))
			{
				this->sendErr(req, type + ": '" + assetArg + "' is not a file");
				return;
			}
			const AssetKind kind = classifyAsset(absolute.string());
			if (kind != AssetKind::Prefab && kind != AssetKind::Texture &&
				kind != AssetKind::VectorShape)
			{
				this->sendErr(req, type + ": '" + assetArg + "' is not a "
					"paintable asset (prefab / texture / shape)");
				return;
			}
			const String assetRef =
				state.project.makeProjectRelative(absolute.string());
			optr<AssetDatabase> const& database =
				state.project.getAssetDatabase();
			const String assetId = (database && !assetRef.empty())
				? database->idForPath(assetRef) : String();
			const EditorPaintGrid grid = core.resolvePaintGrid();
			float centerX = 0.0f;
			float centerY = 0.0f;
			String cellError;
			if (!resolvePaintCell(request, grid, centerX, centerY, cellError))
			{
				this->sendErr(req, type + " " + cellError);
				return;
			}
			EditorPaintDesc desc;
			if (kind == AssetKind::Prefab)
			{
				desc.kind = PaintTileKind::Prefab;
				desc.prefabFilePath = absolute.string();
				desc.prefabRef = assetRef;
				desc.prefabAssetId = assetId;
				desc.suppressedChildren = request.getList("suppressed");
			}
			else
			{
				desc.kind = kind == AssetKind::VectorShape
					? PaintTileKind::ShapeTile
					: PaintTileKind::SpriteTile;
				desc.assetName = absolute.filename().string();
				desc.assetRef = assetRef;
				desc.assetId = assetId;
			}
			const bool painted =
				core.paintTileAtCell(desc, centerX, centerY, grid.cellSize, 0);
			const String rootId =
				core.findTileAtCell(centerX, centerY, grid.cellSize);
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, rootId);
			ok.set("painted", painted ? "1" : "0");
			ok.set("kind", kind == AssetKind::Prefab ? "prefab"
				: kind == AssetKind::VectorShape ? "shape" : "texture");
			ok.set("col", std::to_string(
				paintCellCoord(centerX, grid.originX, grid.cellSize)));
			ok.set("row", std::to_string(
				paintCellCoord(centerY, grid.originY, grid.cellSize)));
			ok.setFloat("x", centerX);
			ok.setFloat("y", centerY);
			this->sendOk(req, ok);
			return;
		}
		// erase_cell: remove the tile in one grid cell (undoable, any kind)
		if (type == "erase_cell")
		{
			const EditorPaintGrid grid = core.resolvePaintGrid();
			float centerX = 0.0f;
			float centerY = 0.0f;
			String cellError;
			if (!resolvePaintCell(request, grid, centerX, centerY, cellError))
			{
				this->sendErr(req, "erase_cell " + cellError);
				return;
			}
			const bool erased =
				core.eraseTileAtCell(centerX, centerY, grid.cellSize, 0);
			DebugMessage ok(MSG_OK);
			ok.set("erased", erased ? "1" : "0");
			ok.set("col", std::to_string(
				paintCellCoord(centerX, grid.originX, grid.cellSize)));
			ok.set("row", std::to_string(
				paintCellCoord(centerY, grid.originY, grid.cellSize)));
			ok.setFloat("x", centerX);
			ok.setFloat("y", centerY);
			this->sendOk(req, ok);
			return;
		}
		// add_scene_to_levels: append the current saved scene to the project's
		// level sequence (not undoable - it writes levels.olevels, like an import)
		if (type == "add_scene_to_levels")
		{
			if (!addCurrentSceneToLevels(state, core))
			{
				this->sendErr(req, "add_scene_to_levels refused - a saved scene "
					"inside the open project that is not already in the sequence "
					"is required (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("scene_path", state.currentScenePath);
			this->sendOk(req, ok);
			return;
		}

		// get_project_setting: read one manifest Setting ('key' -> value + has) or,
		// with no 'key', every setting as the parallel 'keys'/'values' lists.
		if (type == "get_project_setting")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - get_project_setting reads "
					"the open project's manifest Settings");
				return;
			}
			const String key = request.get("key");
			DebugMessage ok(MSG_OK);
			if (key.empty())
			{
				StringVector keys;
				StringVector values;
				for (auto const & [settingKey, settingValue] :
					state.project.getSettings())
				{
					keys.push_back(settingKey);
					values.push_back(settingValue);
				}
				ok.setList("keys", keys);
				ok.setList("values", values);
			}
			else
			{
				ok.set("key", key);
				ok.set("value", state.project.getSetting(key, ""));
				ok.set("has", state.project.hasSetting(key) ? "true" : "false");
			}
			this->sendOk(req, ok);
			return;
		}

		// set_project_setting: write one manifest Setting and persist the .orkproj
		// (auth-gated). Goes through the editor's IN-MEMORY project so a following
		// Build/export sees it - the authoritative path for export.* config.
		if (type == "set_project_setting")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - set_project_setting writes "
					"the open project's manifest Settings");
				return;
			}
			const String key = request.get("key");
			if (key.empty())
			{
				this->sendErr(req, "set_project_setting needs a non-empty 'key'");
				return;
			}
			const String value = request.get("value");
			state.project.setSetting(key, value);
			String saveError;
			if (!state.project.save(&saveError))
			{
				this->sendErr(req, "set_project_setting could not save the "
					"manifest: " + saveError);
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("key", key);
			ok.set("value", value);
			this->sendOk(req, ok);
			return;
		}

		//--- editor script tools -----------------------------
		// run_editor_script: run a project *.editor.lua tool ONCE through the
		// editor-tool host (the same tool a human runs from the Tools menu). The
		// tool's editor.* calls route back through THIS verb handler, and its
		// whole run folds into one undo step; a tool script error is reported as
		// isError with the file:line. Auth-gated (it mutates the scene).
		if (type == "run_editor_script")
		{
			if (!state.editorScripts)
			{
				this->sendErr(req, "editor scripting is not available");
				return;
			}
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open - editor tools live in the "
					"open project's scripts/ folder");
				return;
			}
			// accept the tool by its stable name under 'name' (or 'tool')
			String toolName = request.get("name");
			if (toolName.empty())
			{
				toolName = request.get("tool");
			}
			if (toolName.empty())
			{
				this->sendErr(req, "run_editor_script needs the tool 'name'");
				return;
			}
			if (!state.editorScripts->findByName(toolName))
			{
				this->sendErr(req, "no editor tool named '" + toolName +
					"' (see list_project_files scripts/*.editor.lua)");
				return;
			}
			const EditorScriptHost::RunResult result =
				state.editorScripts->runToolByName(toolName, context);
			if (!result.ran)
			{
				this->sendErr(req, "run_editor_script '" + toolName + "': " +
					result.error);
				return;
			}
			if (!result.ok)
			{
				this->sendErr(req, "editor tool '" + toolName +
					"' failed: " + result.error);
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("name", toolName);
			ok.set("command_count", std::to_string(result.commandCount));
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
			const String& jobId = request.get("jobId");
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

		//--- project export (drives Util/orkige_export.py) ---
		if (type == "export_project")
		{
			const String& platform = request.get("platform");
			if (platform != "macos" && platform != "ios-simulator" &&
				platform != "android")
			{
				this->sendErr(req, "export_project needs a 'platform' of macos, "
					"ios-simulator or android");
				return;
			}
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open");
				return;
			}
			const String tree = resolveExportTree(platform);
			// honest, structured preconditions BEFORE spawning: the export
			// pipeline ships the CLASSIC player/media set, so it needs a present,
			// classic-flavored engine tree. A missing or next-flavored tree is
			// refused up front (no exporter run, no new export machinery here).
			std::error_code treeIgnored;
			if (!std::filesystem::exists(
				std::filesystem::path(tree) / "CMakeCache.txt", treeIgnored))
			{
				this->sendErr(req, "no " + platform + " build tree at '" + tree +
					"' - build the matching classic preset first (project export "
					"is pinned to the classic flavor)");
				return;
			}
			if (readCmakeCacheVar(tree, "ORKIGE_RENDER_BACKEND") == "next")
			{
				this->sendErr(req, "project export is pinned to the classic "
					"flavor; the " + platform + " engine tree at '" + tree +
					"' is next-flavored - build a classic preset (e.g. "
					"macos-debug-classic) to export");
				return;
			}
			// build the exporter command on the main thread (Project is not
			// thread-safe), then hand it to the worker - same invocation as the
			// editor's Build menu (EditorExport.cpp)
			// preflight the python3 toolchain (cached per run) - the same
			// honest error the Build menu / SVG import surface when python3 is
			// missing or too old, so an MCP agent gets it too
			const PythonProbeResult& python = probePythonToolchain();
			if (!python.ok)
			{
				this->sendErr(req, python.error);
				return;
			}
			const String exporter =
				String(ORKIGE_EDITOR_ENGINE_ROOT) + "/Util/orkige_export.py";
			std::vector<std::string> command = { python.executable, exporter,
				"--project", state.project.getRootDirectory(),
				"--platform", platform, "--engine-build", tree };
			auto job = std::make_unique<EditorExportJob>();
			job->id = AssetDatabase::generateId();
			ExportRunResult params;
			params.platform = platform;
			params.engineBuild = tree;
			EditorExportJob* jobPtr = job.get();
			job->worker = std::thread(runExportJobWorker, jobPtr, params,
				command);
			this->mExportJobs.push_back(std::move(job));
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("jobId", jobPtr->id);
			ok.set("platform", platform);
			ok.set("engineBuild", tree);
			this->sendOk(req, ok);
			return;
		}
		// get_export_results: poll an export_project job for the artifact path
		if (type == "get_export_results")
		{
			const String& jobId = request.get("jobId");
			EditorExportJob* job = nullptr;
			for (auto const& candidate : this->mExportJobs)
			{
				if (candidate->id == jobId)
				{
					job = candidate.get();
					break;
				}
			}
			if (!job)
			{
				this->sendErr(req, "no such export job '" + jobId + "'");
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
			ExportRunResult const& result = job->result;
			ok.set("status", "done");
			ok.set("platform", result.platform);
			ok.set("engineBuild", result.engineBuild);
			ok.set("ok", result.ok ? "1" : "0");
			ok.set("artifactPath", result.artifactPath);
			if (!result.error.empty())
			{
				ok.set("error", result.error);
			}
			ok.set("outputTail", result.outputTail);
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
		//! timeout is a coarse anti-hang backstop only (never a per-request
		//! latency budget): every reply is produced on the editor's main thread
		//! when the server is pumped once per frame, so under a heavily loaded
		//! machine the gap between pumps can widen - the ceiling stays well above
		//! any plausible pump gap so a slow frame never false-fails a read, while
		//! still bounding a genuinely wedged socket below the ctest TIMEOUT.
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
				DWORD timeoutMs = 45000;
				::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
					reinterpret_cast<char*>(&timeoutMs), sizeof(timeoutMs));
#else
				struct timeval tv;
				tv.tv_sec = 45;
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

		// like callTool but hands back the WHOLE tool result (content[] included)
		// so a leg can inspect image content blocks alongside structuredContent
		auto callToolFull = [&](String const& tool, JsonValue const& args,
			bool withAuth, JsonValue& result) -> bool
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue(tool));
			params.set("arguments", args);
			JsonValue reply;
			if (!post("tools/call", params, withAuth, true, reply))
			{
				return false;
			}
			result = reply.get("result");
			return true;
		};

		// decode standard base64 (the image block's 'data'); returns the bytes so
		// a leg can assert an inlined PNG really carries the PNG signature
		auto base64Decode = [](std::string const& in) -> std::vector<unsigned char>
		{
			auto sextet = [](char c) -> int
			{
				if (c >= 'A' && c <= 'Z') return c - 'A';
				if (c >= 'a' && c <= 'z') return c - 'a' + 26;
				if (c >= '0' && c <= '9') return c - '0' + 52;
				if (c == '+') return 62;
				if (c == '/') return 63;
				return -1;
			};
			std::vector<unsigned char> out;
			int bits = 0, acc = 0;
			for (char c : in)
			{
				if (c == '=') break;
				const int v = sextet(c);
				if (v < 0) continue;
				acc = (acc << 6) | v;
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
				}
			}
			return out;
		};

		// the first image content block's decoded bytes, or empty when none
		auto firstInlineImage = [&](JsonValue const& result)
			-> std::vector<unsigned char>
		{
			JsonValue const& content = result.get("content");
			for (size_t i = 0; i < content.size(); ++i)
			{
				JsonValue const& block = content.at(i);
				if (block.get("type").asString() == "image")
				{
					return base64Decode(block.get("data").asString());
				}
			}
			return std::vector<unsigned char>();
		};

		// a get_state fetch (read, no auth) returning the structuredContent
		auto getState = [&](JsonValue& state) -> bool
		{
			bool isError = true;
			return callTool("get_state", JsonValue::object(), false, state,
				isError) && !isError;
		};

		// (8) tools/call get_lua_api (read, no auth): the generated Lua API
		// signature index must come back non-empty and carry a known symbol, so
		// an MCP-only agent can learn the scripting surface self-contained
		{
			JsonValue structured;
			bool isError = true;
			if (!callTool("get_lua_api", JsonValue::object(), false,
					structured, isError) || isError)
			{
				finish(false, "control self-test: get_lua_api call failed");
				return;
			}
			const String inventory = structured.get("inventory").asString();
			if (inventory.empty() ||
				inventory.find("music.play") == String::npos ||
				inventory.find("world.get") == String::npos)
			{
				finish(false, "control self-test: get_lua_api inventory empty or "
					"missing a known symbol (music.play/world.get)");
				return;
			}
			SDL_Log("orkige_editor: control self-test - get_lua_api OK "
				"(%zu byte inventory)", inventory.size());
		}

		// --- the RUNTIME DEBUG conversation (a separate ctest, needs the
		// built player): boot Play over MCP, then pause/step/inspect/mutate/
		// screenshot the RUNNING game and stop - all through the MCP tools,
		// exactly as an agent would drive them. Runs instead of the edit-world
		// conversation below when the runtime-debug env selected this mode.
		if (this->mRuntimeDebug)
		{
			JsonValue structured;
			bool isError = true;
			// generous ceiling for waits that hinge on the SPAWNED PLAYER's frame
			// cadence (live state stream, pause/step ack, property read-back,
			// screenshot/trace confirmation). When the full suite runs in
			// parallel, the player competes for the GPU with other windowed tests
			// and its frame rate can dip, so these polls need a wide margin over
			// the ~sub-second typical latency; the outer ctest TIMEOUT is the real
			// backstop. Boot (heaviest) and stop keep their own wider ceilings.
			const int kPlayerPollAttempts = 300;	// 300 * 100ms = 30s
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

			// (R1) play a SPECIFIC scene on the desktop target: save the fixture
			// world to a temp scene, clear the edit world, then play that scene
			// path - so a running Cube proves 'play { scene }' opened+played it
			// (not just whatever was already loaded).
			const std::string playSceneFile =
				(std::filesystem::temp_directory_path() /
					("orkige_ctrl_playscene_" + std::to_string(port) +
						".oscene")).string();
			{
				// required-parameter enforcement: a tools/call missing a
				// REQUIRED schema argument (or sending it under a wrong name -
				// the codec drops unknown keys) must come back as an honest
				// isError, and the editor must SURVIVE - an empty scene path
				// once flowed through open_scene into an internal assert and
				// aborted the whole editor
				JsonValue noArgs = JsonValue::object();
				if (!callTool("open_scene", noArgs, true, structured, isError) ||
					!isError)
				{
					finish(false, "control self-test: open_scene without its "
						"required 'scene' argument must be an isError reply");
					return;
				}

				JsonValue saveArgs = JsonValue::object();
				saveArgs.set("scene", JsonValue(String(playSceneFile)));
				if (!callTool("save_scene", saveArgs, true, structured, isError) ||
					isError)
				{
					finish(false, "control self-test: runtime - could not save the "
						"play-scene fixture");
					return;
				}
				JsonValue clearArgs = JsonValue::object();
				clearArgs.set("force", JsonValue("1"));
				if (!callTool("new_scene", clearArgs, true, structured, isError) ||
					isError)
				{
					finish(false, "control self-test: runtime - could not clear the "
						"scene before the scene-play");
					return;
				}
			}
			JsonValue playArgs = JsonValue::object();
			playArgs.set("scene", JsonValue(String(playSceneFile)));
			playArgs.set("target", JsonValue("desktop"));
			if (!callTool("play", playArgs, true, structured, isError) || isError)
			{
				finish(false, "control self-test: runtime - play { scene, target } "
					"was not accepted");
				return;
			}
			if (structured.get("target").asString() != "desktop")
			{
				finish(false, "control self-test: runtime - play did not echo the "
					"desktop target");
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

			// (R3) runtime_hierarchy (read) must list the running objects - and
			// specifically Cube1 from the scene we asked play to open, proving
			// the scene path took effect. remote_connected only means the debug
			// link is up; the player streams its hierarchy a VARIABLE number of
			// polls later, so hold + re-query until it lists objects (bounded, the
			// same 0.1s cadence as pollState) before asserting.
			JsonValue ids;
			bool hierarchyReady = false;
			int hierarchyWaits = 0;
			for (int attempt = 0; attempt < kPlayerPollAttempts; ++attempt)
			{
				if (!callTool("runtime_hierarchy", JsonValue::object(), false,
						structured, isError) || isError)
				{
					finish(false, "control self-test: runtime_hierarchy failed");
					return;
				}
				ids = structured.get("ids");
				if (ids.size() > 0)
				{
					hierarchyReady = true;
					break;
				}
				++hierarchyWaits;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (!hierarchyReady)
			{
				finish(false, "control self-test: runtime_hierarchy returned no "
					"objects");
				return;
			}
			if (hierarchyWaits > 0)
			{
				SDL_Log("orkige_editor: control self-test - runtime_hierarchy "
					"settled after %d extra poll(s)", hierarchyWaits);
			}
			bool playedSceneLoaded = false;
			for (size_t i = 0; i < ids.size(); ++i)
			{
				if (ids.at(i).asString() == "Cube1") playedSceneLoaded = true;
			}
			if (!playedSceneLoaded)
			{
				std::error_code sceneIgnored;
				std::filesystem::remove(playSceneFile, sceneIgnored);
				finish(false, "control self-test: play { scene } did not play the "
					"requested scene (Cube1 missing from the running game)");
				return;
			}
			std::error_code sceneIgnored;
			std::filesystem::remove(playSceneFile, sceneIgnored);
			SDL_Log("orkige_editor: control self-test - play { scene, target } "
				"opened+played the requested scene (Cube1 running)");
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
			for (int attempt = 0; attempt < kPlayerPollAttempts && !stateReady;
				++attempt)
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
				}, kPlayerPollAttempts, state))
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
			for (int attempt = 0; attempt < kPlayerPollAttempts && !moved;
				++attempt)
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
				}, kPlayerPollAttempts, state))
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

			// (R7b) record_trace (authed): record the RUNNING game to a
			// .jsonl flight recorder while nudging the object's Transform so its
			// position changes across samples, then verify the trace parses line
			// by line and the moving object's samples differ + carry dt
			{
				// resume so the trace covers the running (not paused) game
				if (!callTool("resume", JsonValue::object(), true, structured,
						isError) || isError)
				{
					finish(false, "control self-test: resume before record failed");
					return;
				}
				// the running game streams its memory footprint (MSG_STATS): by
				// now (play has been up through boot, inspect and screenshot)
				// get_state must carry a positive mem_rss and a peak at least
				// as large. Poll briefly in case the first stats line is in
				// flight (the player samples at ~4 Hz).
				{
					JsonValue memState;
					const bool haveMem = pollState(
						[](JsonValue const& s)
						{
							return std::atoll(s.get("mem_rss").asString()
								.c_str()) > 0;
						}, 40, memState);
					const long long memRss = haveMem ? std::atoll(
						memState.get("mem_rss").asString().c_str()) : -1;
					const long long memPeak = haveMem ? std::atoll(
						memState.get("mem_rss_peak").asString().c_str()) : -1;
					if (!haveMem || memRss <= 0 || memPeak < memRss)
					{
						finish(false, "control self-test: get_state did not report "
							"a positive running-game mem_rss with peak >= current");
						return;
					}
					SDL_Log("orkige_editor: control self-test - running-game "
						"mem_rss %lld bytes (peak %lld)", memRss, memPeak);
				}
				// the agent perf readback: the same MSG_STATS cadence carries
				// the engine-level allocation counters + the frame time, and
				// get_profile serves the hierarchical CPU snapshot (a Debug
				// player streams it unprompted; the verb arms a Release one).
				// Poll until alloc_per_frame reports and a profile snapshot
				// with the canonical "scripts" phase at depth 0 arrived.
				{
					JsonValue perfState;
					const bool havePerf = pollState(
						[](JsonValue const& s)
						{
							return std::atoll(s.get("alloc_per_frame")
									.asString().c_str()) >= 0 &&
								std::atof(s.get("frame_ms").asString()
									.c_str()) > 0.0;
						}, 40, perfState);
					if (!havePerf)
					{
						finish(false, "control self-test: get_state never "
							"reported alloc_per_frame + frame_ms");
						return;
					}
					// the per-tag breakdown rides alongside (parallel lists)
					if (perfState.get("alloc_tags").size() == 0 ||
						perfState.get("alloc_tags").size() !=
							perfState.get("alloc_counts").size())
					{
						finish(false, "control self-test: get_state alloc tag "
							"lists missing or unbalanced");
						return;
					}
					JsonValue profile;
					bool haveProfile = false;
					for (int attempt = 0; attempt < 40 && !haveProfile;
						++attempt)
					{
						if (!callTool("get_profile", JsonValue::object(),
								false, profile, isError) || isError)
						{
							finish(false,
								"control self-test: get_profile call failed");
							return;
						}
						JsonValue const& names = profile.get("names");
						JsonValue const& info = profile.get("info");
						for (size_t i = 0; i < names.size() &&
							i < info.size(); ++i)
						{
							// info entries are "depth calls ms maxMs" - the
							// canonical scripts phase sits at depth 0
							if (names.at(i).asString() == "scripts" &&
								info.at(i).asString().rfind("0 ", 0) == 0)
							{
								haveProfile = true;
								break;
							}
						}
						if (!haveProfile)
						{
							std::this_thread::sleep_for(
								std::chrono::milliseconds(100));
						}
					}
					if (!haveProfile)
					{
						finish(false, "control self-test: get_profile never "
							"served a snapshot with the 'scripts' phase");
						return;
					}
					SDL_Log("orkige_editor: control self-test - perf readback "
						"OK (alloc/frame %s, frame %s ms, profile_seq %s)",
						perfState.get("alloc_per_frame").asString().c_str(),
						perfState.get("frame_ms").asString().c_str(),
						profile.get("profile_seq").asString().c_str());
				}
				const std::string tracePath =
					std::filesystem::path(this->mScreenshotPath)
						.replace_extension(".jsonl").string();
				const int prevRecordSeq = std::atoi(
					state.get("record_seq").asString().c_str());
				JsonValue recordArgs = JsonValue::object();
				recordArgs.set("path", JsonValue(String(tracePath)));
				recordArgs.set("seconds", JsonValue(3.0));
				recordArgs.set("everyNth", JsonValue(1.0));
				if (!callTool("record_trace", recordArgs, true, structured,
						isError) || isError)
				{
					finish(false, "control self-test: record_trace was not accepted");
					return;
				}
				// drive movement WHILE recording so samples span positions
				for (int step = 1; step <= 4; ++step)
				{
					JsonValue moveArgs = JsonValue::object();
					moveArgs.set("id", JsonValue(firstId));
					moveArgs.set("component", JsonValue("TransformComponent"));
					moveArgs.set("property", JsonValue("position"));
					moveArgs.set("value",
						JsonValue(String(std::to_string(step) + " 0 0")));
					callTool("set_runtime_property", moveArgs, true, structured,
						isError);
					std::this_thread::sleep_for(std::chrono::milliseconds(120));
				}
				// end promptly (do not wait out the whole budget)
				if (!callTool("stop_recording", JsonValue::object(), true,
						structured, isError) || isError)
				{
					finish(false, "control self-test: stop_recording failed");
					return;
				}
				if (!pollState([prevRecordSeq](JsonValue const& s)
					{
						return std::atoi(s.get("record_seq").asString()
							.c_str()) > prevRecordSeq;
					}, kPlayerPollAttempts, state))
				{
					finish(false, "control self-test: running game never confirmed "
						"the trace");
					return;
				}
				if (state.get("record_ok").asString() != "1" ||
					state.get("record_path").asString() != tracePath)
				{
					finish(false, "control self-test: record_trace did not report a "
						"written trace");
					return;
				}
				// parse the trace: every line valid JSON; the moving object's
				// sampled pos.x must take at least two distinct values and every
				// sample must carry a positive dt
				std::ifstream traceFile(tracePath);
				if (!traceFile)
				{
					finish(false, "control self-test: record_trace wrote no file");
					return;
				}
				std::set<double> movingX;
				size_t sampleCount = 0;
				size_t lineCount = 0;
				bool everyLineParsed = true;
				bool everySampleHasDt = true;
				// the process memory footprint rides every sample line ("mem"
				// bytes) on a platform with a memory query (all the CI targets
				// have one) - assert it is present and positive
				bool everySampleHasMem = true;
				std::string rawLine;
				while (std::getline(traceFile, rawLine))
				{
					if (rawLine.empty())
					{
						continue;
					}
					++lineCount;
					JsonValue parsed;
					if (!JsonValue::parse(rawLine, parsed))
					{
						everyLineParsed = false;
						break;
					}
					JsonValue const& objects = parsed.get("objects");
					if (!objects.isArray())
					{
						continue;	// an event line
					}
					++sampleCount;
					if (parsed.get("dt").asNumber(-1.0) <= 0.0)
					{
						everySampleHasDt = false;
					}
					if (parsed.get("mem").asNumber(-1.0) <= 0.0)
					{
						everySampleHasMem = false;
					}
					for (size_t i = 0; i < objects.size(); ++i)
					{
						if (objects.at(i).get("id").asString() == firstId)
						{
							movingX.insert(
								objects.at(i).get("pos").at(0).asNumber(0));
						}
					}
				}
				if (lineCount < 2 || !everyLineParsed)
				{
					finish(false, "control self-test: record_trace produced an "
						"unparseable / trivial trace");
					return;
				}
				if (!everySampleHasDt)
				{
					finish(false, "control self-test: a trace sample was missing a "
						"positive dt");
					return;
				}
				if (!everySampleHasMem)
				{
					finish(false, "control self-test: a trace sample was missing a "
						"positive mem (process memory footprint)");
					return;
				}
				if (movingX.size() < 2)
				{
					finish(false, "control self-test: the moving object's position "
						"did not change across trace samples");
					return;
				}
				SDL_Log("orkige_editor: control self-test - record_trace wrote "
					"'%s' (%zu lines, %zu samples, %zu distinct positions)",
					tracePath.c_str(), lineCount, sampleCount, movingX.size());
			}

			// (R8) reload_script (authed): a smoke over the running player (the
			// fixtures carry no scripts, so this reloads zero but must succeed)
			if (!callTool("reload_script", JsonValue::object(), true, structured,
					isError) || isError)
			{
				finish(false, "control self-test: reload_script failed");
				return;
			}
			// (R8b) get_safe_area (read, no auth): the running player streams
			// its window size + safe-area insets on MSG_STATS. Poll until the
			// window size is reported (>0); desktop insets are 0, so the top
			// inset is a non-negative number, and window_w must be positive.
			if (!pollState([&](JsonValue const&) -> bool
				{
					JsonValue safeArea;
					bool safeError = true;
					return callTool("get_safe_area", JsonValue::object(), false,
						safeArea, safeError) && !safeError &&
						std::strtoll(safeArea.get("window_w").asString().c_str(),
							nullptr, 10) > 0;
				}, kPlayerPollAttempts, state))
			{
				finish(false, "control self-test: get_safe_area never reported a "
					"window size for the running player");
				return;
			}
			{
				JsonValue safeArea;
				if (!callTool("get_safe_area", JsonValue::object(), false,
						safeArea, isError) || isError)
				{
					finish(false, "control self-test: get_safe_area failed");
					return;
				}
				const long long topInset = std::strtoll(
					safeArea.get("safe_top").asString().c_str(), nullptr, 10);
				if (topInset < 0)
				{
					finish(false, "control self-test: get_safe_area reported a "
						"negative top inset");
					return;
				}
				SDL_Log("orkige_editor: control self-test - get_safe_area OK "
					"(window %s x %s, top inset %lld)",
					safeArea.get("window_w").asString().c_str(),
					safeArea.get("window_h").asString().c_str(), topInset);
			}
			// (R8c) get_ui_layout (read, no auth): the fixture scene carries no
			// gui HUD, so the widget list is empty - but the verb must
			// answer cleanly with the parallel ids/rects lists.
			{
				JsonValue layout;
				if (!callTool("get_ui_layout", JsonValue::object(), false,
						layout, isError) || isError)
				{
					finish(false, "control self-test: get_ui_layout failed");
					return;
				}
				if (layout.get("ids").size() != layout.get("rects").size())
				{
					finish(false, "control self-test: get_ui_layout ids/rects "
						"lists have mismatched lengths");
					return;
				}
				SDL_Log("orkige_editor: control self-test - get_ui_layout OK "
					"(%zu widgets)", layout.get("ids").size());
			}
			// (R8c2) gui_press / dismiss_modal (authed mutations against the live
			// player): the fixture has no gui, so a press on a bogus id forwards
			// cleanly (the player logs a [remote] error), an EMPTY id is rejected
			// up front, and dismiss_modal with nothing up is a harmless no-op.
			// Proves the two verbs are wired, forwarded and auth-gated. The deep
			// modal-blocks-input behaviour is covered by the demo_gui_modal
			// selfcheck (a real gui + synthetic input).
			{
				JsonValue emptyArgs = JsonValue::object();
				emptyArgs.set("id", JsonValue(String("")));
				JsonValue result;
				if (callTool("gui_press", emptyArgs, true, result, isError) &&
					!isError)
				{
					finish(false, "control self-test: gui_press accepted an "
						"empty widget id");
					return;
				}
				JsonValue bogusArgs = JsonValue::object();
				bogusArgs.set("id", JsonValue(String("no_such_widget")));
				if (!callTool("gui_press", bogusArgs, true, result, isError) ||
					isError)
				{
					finish(false, "control self-test: gui_press did not forward "
						"to the player");
					return;
				}
				if (!callTool("dismiss_modal", JsonValue::object(), true, result,
						isError) || isError)
				{
					finish(false, "control self-test: dismiss_modal failed");
					return;
				}
				SDL_Log("orkige_editor: control self-test - gui_press / "
					"dismiss_modal OK (empty id rejected, forward + no-op "
					"accepted)");
			}
			// (R8d) get_breadcrumbs (read, no auth): the booted player wrote a
			// "boot" breadcrumb to the shared ORKIGE_BREADCRUMB_DIR; the editor
			// reads the same file off disk over MCP. Proves the crash trail is
			// reachable from the editor's side (the primary readback path).
			{
				JsonValue crumbs;
				if (!callTool("get_breadcrumbs", JsonValue::object(), false,
						crumbs, isError) || isError)
				{
					finish(false, "control self-test: get_breadcrumbs failed");
					return;
				}
				if (crumbs.get("live").asString().find("\"boot\"") ==
					String::npos)
				{
					finish(false, "control self-test: get_breadcrumbs live trail "
						"has no boot marker from the running player");
					return;
				}
				SDL_Log("orkige_editor: control self-test - get_breadcrumbs OK "
					"(%s live bytes)",
					crumbs.get("live_bytes").asString().c_str());
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
				"set_cvar", "reload_script", "screenshot_game", "record_trace",
				"stop_recording" };
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

		// (7b) get_state exposes the streamed-music snapshot as a flat array an
		// agent polls to see what music is playing - headless (no live player)
		// it must be present and EMPTY (a real playing track is asserted by the
		// demo_music integration run and the runtime debug loop)
		{
			JsonValue state;
			if (!getState(state))
			{
				finish(false, "control self-test: get_state failed");
				return;
			}
			JsonValue const& musicIds = state.get("music_ids");
			if (!musicIds.isArray() || musicIds.size() != 0)
			{
				finish(false, "control self-test: get_state music_ids is not an "
					"empty array with no live player");
				return;
			}
			SDL_Log("orkige_editor: control self-test - get_state music "
				"snapshot present (empty, no live player)");
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
		// failing test prints to STDOUT before failing - some CTest builds
		// omit a test's stderr from the JUnit system-out, so stdout is the
		// only capture every writer preserves; the logTail must be non-empty.
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
				// a -P script fails AFTER printing to stdout (message(STATUS)),
				// so the captured output is non-empty on every CTest writer
				std::ofstream failing(probeSrc / "failing.cmake");
				failing <<
					"message(STATUS \"rtprobe_fail: deliberate probe failure\")\n"
					"message(FATAL_ERROR \"rtprobe_fail fails by design\")\n";
			}
			{
				std::ofstream lists(probeSrc / "CMakeLists.txt");
				lists <<
					"cmake_minimum_required(VERSION 3.20)\n"
					"project(rtprobe NONE)\n"
					"enable_testing()\n"
					"add_test(NAME rtprobe_pass COMMAND \"${CMAKE_COMMAND}\" "
					"-E true)\n"
					"add_test(NAME rtprobe_fail COMMAND \"${CMAKE_COMMAND}\" "
					"-P \"${CMAKE_CURRENT_SOURCE_DIR}/failing.cmake\")\n";
			}
			// the generator is pinned SINGLE-CONFIG: the platform default can
			// be a multi-config one, and ctest without a -C on such a tree
			// quietly marks every test "notrun" (total counts, nothing passes
			// or fails). Ninja is the project's required build tool, so it is
			// present wherever this editor was built.
			std::vector<std::string> configure = { std::string(ORKIGE_EDITOR_CMAKE),
				"-G", "Ninja",
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
				// name what actually came back - the variants (counts wrong,
				// name missing, tail empty) need different fixes
				String got = "finished=" + String(finished ? "1" : "0") +
					" total=" + result.get("total").asString() +
					" passed=" + result.get("passed").asString() +
					" failed=" + result.get("failed").asString() +
					" names=[";
				JsonValue const& gotNames = result.get("failed_names");
				JsonValue const& gotTails = result.get("failed_logtails");
				for (size_t i = 0; i < gotNames.size(); ++i)
				{
					got += (i ? "," : "") + gotNames.at(i).asString() +
						"(tail " + std::to_string(
							gotTails.at(i).asString().size()) + "b)";
				}
				got += "]";
				finish(false, "control self-test: run_tests (failure path) did "
					"not report the failing test with a log tail - " + got);
				return;
			}
			SDL_Log("orkige_editor: control self-test - run_tests failure path OK "
				"(1 pass, 1 fail with a captured log tail)");
		}

		// (RUN tools) list_play_targets: the desktop target is always present
		// (device targets depend on the host, so only 'desktop' is asserted -
		// this stays green on any machine and never boots a simulator/emulator)
		{
			JsonValue structured;
			bool isError = true;
			if (!callTool("list_play_targets", JsonValue::object(), false,
					structured, isError) || isError)
			{
				finish(false, "control self-test: list_play_targets failed");
				return;
			}
			JsonValue const& ids = structured.get("target_ids");
			bool hasDesktop = false;
			for (size_t i = 0; i < ids.size(); ++i)
			{
				if (ids.at(i).asString() == "desktop") hasDesktop = true;
			}
			if (!hasDesktop || !structured.get("target_kinds").isArray())
			{
				finish(false, "control self-test: list_play_targets did not report "
					"the desktop target");
				return;
			}
			SDL_Log("orkige_editor: control self-test - list_play_targets OK "
				"(%zu targets, desktop present)", ids.size());
		}

		// (RUN tools) export_project preconditions - asserted as REFUSALS so the
		// self-test stays fast and deterministic (a real export is a multi-minute
		// job the export_* ctests own): a mutation without the token is rejected,
		// and with the token but no project open it refuses honestly. No project
		// is open here yet (the authoring project is opened below).
		{
			JsonValue exportArgs = JsonValue::object();
			exportArgs.set("platform", JsonValue("macos"));
			JsonValue structured;
			bool isError = false;
			// unauthenticated mutation -> rejected by the auth gate
			if (!callTool("export_project", exportArgs, false, structured,
					isError) || !isError)
			{
				finish(false, "control self-test: unauthenticated export_project "
					"was NOT rejected");
				return;
			}
			// authed but no project open -> honest structured refusal
			isError = false;
			if (!callTool("export_project", exportArgs, true, structured,
					isError) || !isError)
			{
				finish(false, "control self-test: export_project with no project "
					"open was NOT refused");
				return;
			}
			// an unknown platform is refused too (still no exporter spawned)
			JsonValue badArgs = JsonValue::object();
			badArgs.set("platform", JsonValue("playstation"));
			isError = false;
			if (!callTool("export_project", badArgs, true, structured, isError) ||
				!isError)
			{
				finish(false, "control self-test: export_project accepted an "
					"unknown platform");
				return;
			}
			SDL_Log("orkige_editor: control self-test - export_project refuses "
				"unauthenticated / no-project / bad-platform requests");
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

		// (15b) set_project_setting -> get_project_setting round-trip on the OPEN
		// project (the manifest Settings the exporter reads). An unauthenticated
		// set must be rejected; an authed set persists and reads back.
		{
			JsonValue setArgs = JsonValue::object();
			setArgs.set("key", JsonValue("export.orientation"));
			setArgs.set("value", JsonValue("landscape"));
			JsonValue structured;
			bool isError = false;
			// unauthenticated set is rejected (it mutates + writes the manifest)
			if (!callTool("set_project_setting", setArgs, false, structured,
					isError) || !isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated "
					"set_project_setting was NOT rejected");
				return;
			}
			isError = true;
			if (!callTool("set_project_setting", setArgs, true, structured,
					isError) || isError ||
				structured.get("value").asString() != "landscape")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: set_project_setting did not "
					"persist the value");
				return;
			}
			JsonValue getArgs = JsonValue::object();
			getArgs.set("key", JsonValue("export.orientation"));
			if (!callTool("get_project_setting", getArgs, false, structured,
					isError) || isError ||
				structured.get("value").asString() != "landscape" ||
				structured.get("has").asString() != "true")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: get_project_setting did not "
					"read back the written setting");
				return;
			}
			SDL_Log("orkige_editor: control self-test - set/get_project_setting "
				"round-trip OK (export.orientation=landscape, auth enforced)");
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

		// (18b) SCRIPT COMPONENT KIND end to end: write a *.component.lua (which
		// makes it an addable component kind), attach it to a fresh object by its
		// kind NAME, set a DECLARED property and read it back - a script is a
		// first-class component over MCP with ZERO kind-specific server code
		// (add/get/set flow through the same generic path as any C++ component)
		{
			const String kindRel = "scripts/mcpkind.component.lua";
			const String kindSrc =
				"properties = { power = { type = \"number\", default = 3 } }\n"
				"function update(self, dt) end\n";
			JsonValue writeArgs = JsonValue::object();
			writeArgs.set("path", JsonValue(kindRel));
			writeArgs.set("content", JsonValue(kindSrc));
			JsonValue structured;
			bool isError = true;
			if (!callTool("write_project_file", writeArgs, true, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: writing the .component.lua kind "
					"failed");
				return;
			}
			// the write rescanned the registry - the kind is now addable
			JsonValue addable;
			bool addableErr = true;
			if (!callTool("list_addable_components", JsonValue::object(), false,
					addable, addableErr) || addableErr)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_addable_components failed");
				return;
			}
			JsonValue const& comps = addable.get("components");
			bool kindListed = false;
			for (size_t i = 0; i < comps.size(); ++i)
			{
				if (comps.at(i).asString() == "mcpkind")
				{
					kindListed = true;
				}
			}
			if (!kindListed)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the written .component.lua kind "
					"is not listed as addable");
				return;
			}
			// a fresh object to carry the script component
			JsonValue objArgs = JsonValue::object();
			objArgs.set("id", JsonValue("ScriptedByMcp"));
			if (!callTool("create_object", objArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_object for the script kind "
					"failed");
				return;
			}
			JsonValue addArgs = JsonValue::object();
			addArgs.set("id", JsonValue("ScriptedByMcp"));
			addArgs.set("component", JsonValue("mcpkind"));
			if (!callTool("add_component", addArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: add_component of the script kind "
					"failed");
				return;
			}
			// the DECLARED property is auto-exposed through the schema union, so
			// set/get resolve it by name with no kind-specific handler
			JsonValue props = JsonValue::object();
			props.set("power", JsonValue("5"));
			JsonValue setArgs = JsonValue::object();
			setArgs.set("id", JsonValue("ScriptedByMcp"));
			setArgs.set("component", JsonValue("mcpkind"));
			setArgs.set("properties", props);
			if (!callTool("set_component", setArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: set the script kind's declared "
					"property failed");
				return;
			}
			JsonValue readArgs = JsonValue::object();
			readArgs.set("id", JsonValue("ScriptedByMcp"));
			readArgs.set("component", JsonValue("mcpkind"));
			if (!callTool("get_component", readArgs, false, structured, isError) ||
				isError || structured.get("power").asString() != "5")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the script kind's declared "
					"property did not read back as '5'");
				return;
			}
			SDL_Log("orkige_editor: control self-test - script component kind "
				"(mcpkind.power -> 5) OK");
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

		// (19b) Lottie import + preview_animation: import a small animated .json
		// (cooks to .oanim, the SOURCE .json is KEPT beside it), then
		// preview_animation the rig at two times - the PNGs must DIFFER (the pose
		// moves) and the readback must carry the rig's clips/duration/layers.
		{
			// a minimal animated Lottie: one body rectangle rotating 0->90 over
			// the idle clip (rotation survives the raster's re-centering, so two
			// times give visibly different pixels). Two markers = two clips.
			const std::string lottie = R"json({"v":"5.7.0","fr":30,"ip":0,"op":60,"w":200,"h":200,"markers":[{"tm":0,"cm":"idle","dr":30},{"tm":30,"cm":"walk","dr":30}],"layers":[{"ty":4,"nm":"body","ind":1,"ip":0,"op":60,"ks":{"p":{"a":0,"k":[100,100]},"a":{"a":0,"k":[0,0]},"s":{"a":0,"k":[100,100]},"r":{"a":1,"k":[{"t":0,"s":[0]},{"t":30,"s":[90]},{"t":60,"s":[0]}]},"o":{"a":0,"k":100}},"shapes":[{"ty":"gr","it":[{"ty":"rc","p":{"a":0,"k":[0,0]},"s":{"a":0,"k":[30,90]},"r":{"a":0,"k":0}},{"ty":"fl","c":{"a":0,"k":[0.9,0.42,0.38,1]},"o":{"a":0,"k":100}}]}]}]})json";
			const fs::path outside = fs::temp_directory_path() /
				("orkige_mcp_anim_" + std::to_string(port) + ".json");
			{
				std::ofstream f(outside, std::ios::binary);
				f << lottie;
			}
			JsonValue importArgs = JsonValue::object();
			importArgs.set("sourcePath", JsonValue(outside.string()));
			JsonValue structured;
			bool isError = true;
			const bool imported = callTool("import_asset", importArgs, true,
				structured, isError) && !isError;
			const String animRel = structured.get("path").asString();
			fs::remove(outside, authIgnored);
			// the cook produced a .oanim (the doc animates), not a static .oshape
			if (!imported || animRel.size() < 6 ||
				animRel.substr(animRel.size() - 6) != ".oanim")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: importing a Lottie .json did not "
					"cook a .oanim");
				return;
			}
			// the SOURCE .json is KEPT beside the cooked asset (recook-on-reimport)
			const fs::path animAbs = authRoot / animRel;
			const fs::path keptJson =
				animAbs.parent_path() / (animAbs.stem().string() + ".json");
			if (!fs::exists(keptJson, authIgnored))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the Lottie source .json was not "
					"kept beside the cooked .oanim");
				return;
			}
			// preview at two times into two PNGs; read them back and compare
			auto previewToPng = [&](float timeSeconds, std::string const& pngPath,
				JsonValue& out) -> bool
			{
				JsonValue args = JsonValue::object();
				args.set("asset", JsonValue(animRel));
				args.set("clip", JsonValue(String("idle")));
				args.set("time", JsonValue(std::to_string(timeSeconds)));
				args.set("path", JsonValue(pngPath));
				bool err = true;
				return callTool("preview_animation", args, true, out, err) && !err;
			};
			auto readBytes = [](fs::path const& p) -> std::vector<char>
			{
				std::ifstream in(p, std::ios::binary);
				return std::vector<char>((std::istreambuf_iterator<char>(in)),
					std::istreambuf_iterator<char>());
			};
			const fs::path png0 = fs::temp_directory_path() /
				("orkige_mcp_anim0_" + std::to_string(port) + ".png");
			const fs::path png1 = fs::temp_directory_path() /
				("orkige_mcp_anim1_" + std::to_string(port) + ".png");
			JsonValue p0, p1;
			const bool okA = previewToPng(0.0f, png0.string(), p0);
			const bool okB = previewToPng(0.5f, png1.string(), p1);
			if (!okA || !okB)
			{
				fs::remove(png0, authIgnored);
				fs::remove(png1, authIgnored);
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: preview_animation failed");
				return;
			}
			// the readback carries the rig facts (clips, duration, layers)
			JsonValue const& clips = p0.get("clips");
			bool sawIdle = false, sawWalk = false;
			for (size_t i = 0; i < clips.size(); ++i)
			{
				if (clips.at(i).asString() == "idle") sawIdle = true;
				if (clips.at(i).asString() == "walk") sawWalk = true;
			}
			const String layerCount = p0.get("layer_count").asString();
			const int visiblePixels = std::atoi(
				p0.get("visible_pixel_count").asString().c_str());
			const int colouredPixels = std::atoi(
				p0.get("coloured_pixel_count").asString().c_str());
			if (!sawIdle || !sawWalk || layerCount.empty() || layerCount == "0" ||
				p0.get("clip").asString() != "idle" || visiblePixels < 100 ||
				colouredPixels < 100)
			{
				fs::remove(png0, authIgnored);
				fs::remove(png1, authIgnored);
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: preview_animation readback missing "
					"clips/layers or rendered a blank/white image");
				return;
			}
			// the two poses (0.0s vs 0.5s of a rotating rig) must render DIFFERENT
			// PNGs - the preview really evaluates the animation
			const std::vector<char> bytes0 = readBytes(png0);
			const std::vector<char> bytes1 = readBytes(png1);
			const bool differ = !bytes0.empty() && !bytes1.empty() &&
				bytes0 != bytes1;
			fs::remove(png0, authIgnored);
			fs::remove(png1, authIgnored);
			if (!differ)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: preview_animation produced "
					"identical PNGs at two different times");
				return;
			}
			// INLINE IMAGE: a preview_animation result carries the PNG as an image
			// content block (data decodes to the PNG signature); inline=false omits
			// it. The path stays in structuredContent either way (pixel diffing).
			{
				const fs::path pngInline = fs::temp_directory_path() /
					("orkige_mcp_anim_inline_" + std::to_string(port) + ".png");
				JsonValue inlineArgs = JsonValue::object();
				inlineArgs.set("asset", JsonValue(animRel));
				inlineArgs.set("clip", JsonValue(String("idle")));
				inlineArgs.set("path", JsonValue(pngInline.string()));
				JsonValue withImage;
				const bool okInline =
					callToolFull("preview_animation", inlineArgs, true, withImage);
				const std::vector<unsigned char> image =
					okInline ? firstInlineImage(withImage)
						: std::vector<unsigned char>();
				static const unsigned char kPngSig[8] =
					{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
				const bool imageOk = image.size() >= 8 &&
					std::equal(kPngSig, kPngSig + 8, image.begin());
				// structuredContent still carries the path
				const bool pathKept = okInline &&
					!withImage.get("structuredContent").get("path")
						.asString().empty();

				// inline=false: no image block, path still present
				JsonValue noInlineArgs = inlineArgs;
				noInlineArgs.set("inline", JsonValue(false));
				JsonValue withoutImage;
				const bool okNoInline = callToolFull("preview_animation",
					noInlineArgs, true, withoutImage);
				const bool noImage = okNoInline &&
					firstInlineImage(withoutImage).empty();

				fs::remove(pngInline, authIgnored);
				if (!imageOk || !pathKept || !noImage)
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: preview_animation inline image "
						"block missing/malformed, or inline=false did not omit it");
					return;
				}
			}
			// AUTH: preview_animation without a token must be rejected (it writes)
			JsonValue previewArgs = JsonValue::object();
			previewArgs.set("asset", JsonValue(animRel));
			JsonValue unauth;
			bool unauthError = false;
			if (!callTool("preview_animation", previewArgs, false, unauth,
				unauthError) || !unauthError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated preview_animation "
					"was NOT rejected");
				return;
			}
			SDL_Log("orkige_editor: control self-test - Lottie import + "
				"preview_animation OK (kept source, two poses differ, "
				"inline image block present)");
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

		// (RUN tools) export_project flavor pre-check, with the authoring project
		// OPEN so the request reaches the tree check. Exports are pinned to the
		// classic flavor: a NEXT-flavored editor tree must refuse honestly. Only
		// asserted when THIS editor build is next-flavored - on a classic tree
		// the request would pass the pre-check and start a real multi-minute
		// export, which is the export_* ctests' job, so we skip it there.
		if (readCmakeCacheVar(ORKIGE_EDITOR_ENGINE_BUILD_DIR,
			"ORKIGE_RENDER_BACKEND") == "next")
		{
			JsonValue exportArgs = JsonValue::object();
			exportArgs.set("platform", JsonValue("macos"));
			JsonValue structured;
			bool isError = false;
			if (!callTool("export_project", exportArgs, true, structured,
					isError) || !isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: export_project on a "
					"next-flavored tree was NOT refused");
				return;
			}
			SDL_Log("orkige_editor: control self-test - export_project refuses a "
				"next-flavored tree (classic-pinned)");
		}
		else
		{
			SDL_Log("orkige_editor: control self-test - classic tree: leaving the "
				"real macos export to the export_* ctests");
		}

		// (21) GRID PAINTING (level authoring): the paint palette lists the
		// project's prefab, painting a cell instantiates it, erasing removes it
		// and undo brings it back - the whole grid-paint loop over MCP. The
		// authoring project is still open and carries assets/McpProbe.oprefab
		// from (20), so it is a ready-made palette (no LevelComponent, so the
		// grid is the default snap-step grid at the world origin).
		{
			JsonValue structured;
			bool isError = true;
			// list_paint_prefabs (read): the prefab must be in the palette and
			// the grid must report a positive cell size
			if (!callTool("list_paint_prefabs", JsonValue::object(), false,
					structured, isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_paint_prefabs failed");
				return;
			}
			JsonValue const& prefabPaths = structured.get("paths");
			bool hasPrefab = false;
			for (size_t i = 0; i < prefabPaths.size(); ++i)
			{
				if (prefabPaths.at(i).asString() == "assets/McpProbe.oprefab")
				{
					hasPrefab = true;
				}
			}
			if (!hasPrefab ||
				std::atof(structured.get("cell_size").asString().c_str()) <= 0.0)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_paint_prefabs did not list "
					"the prefab / report a grid");
				return;
			}

			// a cell far from the fixture's origin objects, to paint cleanly
			JsonValue cell = JsonValue::object();
			cell.set("col", JsonValue(20));
			cell.set("row", JsonValue(-20));
			JsonValue paintArgs = JsonValue::object();
			paintArgs.set("prefab", JsonValue(String("assets/McpProbe.oprefab")));
			paintArgs.set("cell", cell);

			// AUTH: painting without a token must be rejected
			bool unauthError = false;
			if (!callTool("paint_prefab", paintArgs, false, structured,
					unauthError) || !unauthError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated paint_prefab was "
					"NOT rejected");
				return;
			}

			// paint_prefab (authed): a fresh instance root lands in the cell
			isError = true;
			if (!callTool("paint_prefab", paintArgs, true, structured, isError) ||
				isError || structured.get("painted").asString() != "1")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: paint_prefab did not paint a tile "
					"into the cell");
				return;
			}
			const String tileId = structured.get("id").asString();
			if (tileId.empty())
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: paint_prefab did not report the "
					"painted-root id");
				return;
			}

			// a hierarchy membership check reused across the paint/erase/undo
			// assertions (the same read verb the earlier steps use)
			auto tileInHierarchy = [&](String const& id) -> bool
			{
				JsonValue list;
				bool listError = true;
				if (!callTool("list_hierarchy", JsonValue::object(), false, list,
						listError) || listError)
				{
					return false;
				}
				JsonValue const& ids = list.get("ids");
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (ids.at(i).asString() == id)
					{
						return true;
					}
				}
				return false;
			};
			if (!tileInHierarchy(tileId))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the painted tile is missing from "
					"the hierarchy");
				return;
			}

			// erase_cell (authed) removes the tile from that same cell
			JsonValue eraseArgs = JsonValue::object();
			eraseArgs.set("cell", cell);
			isError = true;
			if (!callTool("erase_cell", eraseArgs, true, structured, isError) ||
				isError || structured.get("erased").asString() != "1")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: erase_cell did not erase the "
					"painted tile");
				return;
			}
			if (tileInHierarchy(tileId))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the tile survived erase_cell");
				return;
			}

			// undo (authed) restores the erased tile (DeleteSubtreeCommand brings
			// the subtree back under the same ids)
			isError = true;
			if (!callTool("undo", JsonValue::object(), true, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: undo after erase_cell failed");
				return;
			}
			if (!tileInHierarchy(tileId))
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: undo did not restore the erased "
					"tile");
				return;
			}

			// the SAME seam paints a BARE asset (a texture/.oshape) as a bare
			// tile - no prefab file. Write a probe texture (classification is by
			// extension; the sprite-quad load is harmless when the bytes are not a
			// real image, the tile object still lands), then paint_asset it and
			// erase it. list_paintable_assets must surface it as a 'texture'.
			{
				JsonValue writeArgs = JsonValue::object();
				writeArgs.set("path", JsonValue(String("assets/probe_tile.png")));
				writeArgs.set("content", JsonValue(String("probe-tile-bytes")));
				bool wErr = true;
				if (!callTool("write_project_file", writeArgs, true, structured,
						wErr) || wErr)
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: could not write the probe "
						"texture for the bare-tile paint leg");
					return;
				}
				bool listErr = true;
				bool hasTexture = false;
				if (callTool("list_paintable_assets", JsonValue::object(), false,
						structured, listErr) && !listErr)
				{
					JsonValue const& paths = structured.get("paths");
					JsonValue const& kinds = structured.get("kinds");
					for (size_t i = 0; i < paths.size(); ++i)
					{
						if (paths.at(i).asString() == "assets/probe_tile.png" &&
							i < kinds.size() &&
							kinds.at(i).asString() == "texture")
						{
							hasTexture = true;
						}
					}
				}
				if (!hasTexture)
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: list_paintable_assets did not "
						"list the probe texture as a paintable 'texture'");
					return;
				}
				JsonValue bareCell = JsonValue::object();
				bareCell.set("col", JsonValue(24));
				bareCell.set("row", JsonValue(-24));
				JsonValue bareArgs = JsonValue::object();
				bareArgs.set("asset", JsonValue(String("assets/probe_tile.png")));
				bareArgs.set("cell", bareCell);
				bool bareErr = true;
				if (!callTool("paint_asset", bareArgs, true, structured, bareErr) ||
					bareErr || structured.get("painted").asString() != "1" ||
					structured.get("kind").asString() != "texture")
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: paint_asset did not paint a "
						"bare texture tile");
					return;
				}
				const String bareId = structured.get("id").asString();
				if (bareId.empty() || !tileInHierarchy(bareId))
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: the bare texture tile is "
						"missing from the hierarchy");
					return;
				}
				JsonValue bareErase = JsonValue::object();
				bareErase.set("cell", bareCell);
				bool bareEraseErr = true;
				if (!callTool("erase_cell", bareErase, true, structured,
						bareEraseErr) || bareEraseErr ||
					structured.get("erased").asString() != "1" ||
					tileInHierarchy(bareId))
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: erase_cell did not erase the "
						"bare texture tile");
					return;
				}
				SDL_Log("orkige_editor: control self-test - bare-tile paint loop OK "
					"(paint_asset/erase texture tile '%s')", bareId.c_str());
			}
			SDL_Log("orkige_editor: control self-test - grid paint loop OK "
				"(paint/list/erase/undo tile '%s')", tileId.c_str());
		}

		// (8c) run_editor_script: an agent authors a *.editor.lua tool with
		// write_project_file, then runs it over MCP (the shared-surface story).
		{
			JsonValue writeArgs = JsonValue::object();
			writeArgs.set("path",
				JsonValue(String("scripts/mktool.editor.lua")));
			writeArgs.set("content", JsonValue(String(
				"-- tool: Make One\n"
				"editor.create_object{ id = \"McpToolCube\", mesh = \"cube\" }\n"
				"editor.log(\"mcp tool made McpToolCube\")\n")));
			JsonValue s;
			bool e = true;
			if (!callTool("write_project_file", writeArgs, true, s, e) || e)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: could not write an editor tool");
				return;
			}
			JsonValue runArgs = JsonValue::object();
			runArgs.set("name", JsonValue(String("mktool")));
			// a mutation without a bearer token must be refused
			JsonValue unauth;
			bool unauthErr = false;
			if (!callTool("run_editor_script", runArgs, false, unauth,
					unauthErr) || !unauthErr)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: run_editor_script without auth "
					"must be refused");
				return;
			}
			// the authed run succeeds and reports the folded command count
			JsonValue runStruct;
			bool runErr = true;
			if (!callTool("run_editor_script", runArgs, true, runStruct,
					runErr) || runErr ||
				runStruct.get("name").asString() != "mktool")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: run_editor_script did not run "
					"the tool");
				return;
			}
			// the tool's edit is live in the editor's world
			JsonValue hier;
			bool hierErr = true;
			bool foundToolCube = false;
			if (callTool("list_hierarchy", JsonValue::object(), false, hier,
					hierErr) && !hierErr)
			{
				JsonValue const& ids = hier.get("ids");
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (ids.at(i).asString() == "McpToolCube")
					{
						foundToolCube = true;
					}
				}
			}
			if (!foundToolCube)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: run_editor_script's object is "
					"missing from the hierarchy");
				return;
			}
			SDL_Log("orkige_editor: control self-test - run_editor_script OK "
				"(authored + ran a tool; auth enforced)");
		}

		// (22) PREFAB EDIT MODE (the isolation stage over MCP): open_prefab swaps
		// the scene out for the prefab subtree, the ordinary editing verbs work on
		// the stage UNCHANGED, a scene/project/play verb is refused with the
		// prefab-mode error, and close_prefab restores the scene. The authoring
		// project is still open and carries assets/McpProbe.oprefab (root stem
		// "McpProbe") from (20); McpToolCube is a scene object from (8c).
		{
			JsonValue structured;
			bool isError = true;

			// pre-state: the world is the SCENE (not a prefab stage)
			JsonValue before;
			if (!getState(before) ||
				before.get("edit_context").asString() != "scene")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: get_state did not report the "
					"scene edit context before open_prefab");
				return;
			}

			// open_prefab WITHOUT auth is refused (mutation) and opens nothing
			JsonValue openArgs = JsonValue::object();
			openArgs.set("path", JsonValue(String("assets/McpProbe.oprefab")));
			bool unauthError = false;
			if (!callTool("open_prefab", openArgs, false, structured,
					unauthError) || !unauthError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated open_prefab was "
					"NOT rejected");
				return;
			}
			JsonValue afterReject;
			if (!getState(afterReject) ||
				afterReject.get("edit_context").asString() != "scene")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: a rejected open_prefab still "
					"entered the prefab stage");
				return;
			}

			// open_prefab (authed): the stage root is the file stem
			isError = true;
			if (!callTool("open_prefab", openArgs, true, structured, isError) ||
				isError || structured.get("root_id").asString() != "McpProbe")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: open_prefab did not open the "
					"stage rooted at 'McpProbe'");
				return;
			}

			// get_state now reports the prefab context (root + path), and the
			// STASHED scene fields are still surfaced
			JsonValue staged;
			if (!getState(staged) ||
				staged.get("edit_context").asString() != "prefab" ||
				staged.get("prefab_root").asString() != "McpProbe" ||
				staged.get("prefab_path").asString().empty())
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: get_state did not report the "
					"prefab stage (edit_context/prefab_root/prefab_path)");
				return;
			}

			// the world is now the prefab subtree only: the stage root is there,
			// the scene's McpToolCube is swapped out
			JsonValue hier;
			bool hierErr = true;
			if (!callTool("list_hierarchy", JsonValue::object(), false, hier,
					hierErr) || hierErr)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: list_hierarchy in the prefab "
					"stage failed");
				return;
			}
			{
				JsonValue const& ids = hier.get("ids");
				bool hasRoot = false;
				bool hasSceneObject = false;
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (ids.at(i).asString() == "McpProbe") hasRoot = true;
					if (ids.at(i).asString() == "McpToolCube") hasSceneObject = true;
				}
				if (!hasRoot || hasSceneObject)
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: the prefab stage did not "
						"swap the scene out for the prefab subtree");
					return;
				}
			}

			// an ordinary editing chain runs against the stage UNCHANGED: create a
			// child under the root and edit a reflected property on it
			JsonValue childArgs = JsonValue::object();
			childArgs.set("id", JsonValue(String("PrefabStageChild")));
			childArgs.set("mesh", JsonValue(String("cube")));
			isError = true;
			if (!callTool("create_object", childArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_object in the prefab "
					"stage failed");
				return;
			}
			JsonValue reparentArgs = JsonValue::object();
			reparentArgs.set("id", JsonValue(String("PrefabStageChild")));
			reparentArgs.set("parent", JsonValue(String("McpProbe")));
			isError = true;
			if (!callTool("reparent_object", reparentArgs, true, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: reparent under the prefab root "
					"failed");
				return;
			}
			JsonValue childProps = JsonValue::object();
			childProps.set("position", JsonValue(String("5 6 7")));
			JsonValue setArgs = JsonValue::object();
			setArgs.set("id", JsonValue(String("PrefabStageChild")));
			setArgs.set("component", JsonValue(String("TransformComponent")));
			setArgs.set("properties", childProps);
			isError = true;
			if (!callTool("set_component", setArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: set_component on a prefab-stage "
					"child failed");
				return;
			}
			JsonValue readArgs = JsonValue::object();
			readArgs.set("id", JsonValue(String("PrefabStageChild")));
			readArgs.set("component", JsonValue(String("TransformComponent")));
			isError = true;
			if (!callTool("get_component", readArgs, false, structured, isError) ||
				isError || structured.get("position").asString() != "5 6 7")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the prefab-stage child edit did "
					"not read back");
				return;
			}

			// a scene/project verb is refused while staged, with the prefab-mode
			// error naming the mode (not a generic failure). Read the error text
			// off the tool result directly (an isError carries no
			// structuredContent).
			{
				JsonValue params = JsonValue::object();
				params.set("name", JsonValue(String("save_scene")));
				JsonValue emptyArgs = JsonValue::object();
				params.set("arguments", emptyArgs);
				JsonValue reply;
				if (!post("tools/call", params, true, true, reply))
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: blocked-verb call had a "
						"transport failure");
					return;
				}
				JsonValue const& result = reply.get("result");
				const String message =
					result.get("content").at(0).get("text").asString();
				if (!result.get("isError").asBool(false) ||
					message.find("prefab") == String::npos)
				{
					fs::remove_all(authRoot, authIgnored);
					finish(false, "control self-test: save_scene in prefab mode was "
						"not refused with the prefab-mode error");
					return;
				}
			}

			// save_prefab (authed) writes the stage back to its .oprefab
			isError = true;
			if (!callTool("save_prefab", JsonValue::object(), true, structured,
					isError) || isError ||
				structured.get("prefab_path").asString().empty())
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: save_prefab did not write the "
					"stage");
				return;
			}

			// close_prefab requires an explicit policy
			bool policyError = false;
			if (!callTool("close_prefab", JsonValue::object(), true, structured,
					policyError) || !policyError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: close_prefab without a policy was "
					"NOT refused");
				return;
			}

			// close_prefab {discard} (authed) restores the scene
			JsonValue closeArgs = JsonValue::object();
			closeArgs.set("policy", JsonValue(String("discard")));
			isError = true;
			if (!callTool("close_prefab", closeArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: close_prefab {discard} failed");
				return;
			}

			// back to the scene: edit_context restored and the scene object is
			// present again (the swapped-out world came back)
			JsonValue restored;
			if (!getState(restored) ||
				restored.get("edit_context").asString() != "scene")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: get_state did not report the "
					"scene context after close_prefab");
				return;
			}
			JsonValue restoredHier;
			hierErr = true;
			bool sceneRestored = false;
			if (callTool("list_hierarchy", JsonValue::object(), false,
					restoredHier, hierErr) && !hierErr)
			{
				JsonValue const& ids = restoredHier.get("ids");
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (ids.at(i).asString() == "McpToolCube") sceneRestored = true;
				}
			}
			if (!sceneRestored)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the scene was not restored after "
					"close_prefab (McpToolCube missing)");
				return;
			}
			SDL_Log("orkige_editor: control self-test - prefab edit mode OK "
				"(open/edit-in-stage/blocked-verb/save/close, scene restored)");
		}

		// (23) MCP TRANSACTIONS: begin_transaction .. end_transaction give a
		// remote client one-undo atomicity. The authRoot project is still open
		// (scene context, McpToolCube present, assets/McpProbe.oprefab on disk).
		{
			JsonValue structured;
			bool isError = true;
			// how many hierarchy ids equal objId (0 = absent, present otherwise)
			auto sceneHas = [&](String const& objId, bool& present) -> bool
			{
				JsonValue h;
				bool e = true;
				if (!callTool("list_hierarchy", JsonValue::object(), false, h, e) ||
					e)
				{
					return false;
				}
				present = false;
				JsonValue const& ids = h.get("ids");
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (ids.at(i).asString() == objId) present = true;
				}
				return true;
			};
			auto txState = [&](bool& open) -> bool
			{
				JsonValue s;
				if (!getState(s)) return false;
				open = s.get("transaction_open").asString() == "1";
				return true;
			};

			// end_transaction WITHOUT a begin is an honest error
			bool endNoBeginError = false;
			JsonValue endArgs = JsonValue::object();
			endArgs.set("commit", JsonValue(true));
			if (!callTool("end_transaction", endArgs, true, structured,
					endNoBeginError) || !endNoBeginError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: end_transaction without a begin "
					"was NOT refused");
				return;
			}

			// begin_transaction WITHOUT auth is refused (mutation) - no session opens
			bool beginUnauthError = false;
			if (!callTool("begin_transaction", JsonValue::object(), false,
					structured, beginUnauthError) || !beginUnauthError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: unauthenticated begin_transaction "
					"was NOT rejected");
				return;
			}

			// begin_transaction (authed) -> get_state reports transaction_open
			isError = true;
			bool open = false;
			if (!callTool("begin_transaction", JsonValue::object(), true, structured,
					isError) || isError || !txState(open) || !open)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: begin_transaction did not open a "
					"transaction (get_state transaction_open)");
				return;
			}

			// a second begin is refused (already open)
			bool doubleBeginError = false;
			if (!callTool("begin_transaction", JsonValue::object(), true, structured,
					doubleBeginError) || !doubleBeginError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: a double begin_transaction was NOT "
					"refused");
				return;
			}

			// several mutating verbs inside the transaction
			JsonValue createArgs = JsonValue::object();
			createArgs.set("id", JsonValue(String("TxCommitA")));
			isError = true;
			if (!callTool("create_object", createArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_object inside a transaction "
					"failed");
				return;
			}
			JsonValue setArgs = JsonValue::object();
			setArgs.set("id", JsonValue(String("TxCommitA")));
			setArgs.set("component", JsonValue(String("TransformComponent")));
			JsonValue setProps = JsonValue::object();
			setProps.set("position", JsonValue(String("5 6 7")));
			setArgs.set("properties", setProps);
			isError = true;
			if (!callTool("set_component", setArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: set_component inside a transaction "
					"failed");
				return;
			}

			// end_transaction {commit:true}: closes and folds into one undo step
			isError = true;
			JsonValue commitArgs = JsonValue::object();
			commitArgs.set("commit", JsonValue(true));
			if (!callTool("end_transaction", commitArgs, true, structured, isError) ||
				isError || structured.get("committed").asString() != "1")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: end_transaction {commit:true} "
					"failed");
				return;
			}
			open = true;
			bool presentA = false;
			if (!txState(open) || open || !sceneHas("TxCommitA", presentA) ||
				!presentA)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: after commit the object was missing "
					"or the transaction stayed open");
				return;
			}
			// ONE undo reverts EVERYTHING done in the transaction (create + set)
			bool undoError = true;
			if (!callTool("undo", JsonValue::object(), true, structured, undoError) ||
				undoError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: undo after a committed transaction "
					"failed");
				return;
			}
			presentA = true;
			bool cubeStill = false;
			if (!sceneHas("TxCommitA", presentA) || presentA ||
				!sceneHas("McpToolCube", cubeStill) || !cubeStill)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: one undo did not revert the whole "
					"committed transaction (or clobbered the scene)");
				return;
			}

			// begin -> mutate -> end {commit:false}: the hierarchy is unchanged
			isError = true;
			if (!callTool("begin_transaction", JsonValue::object(), true, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: begin_transaction (abort case) "
					"failed");
				return;
			}
			JsonValue createB = JsonValue::object();
			createB.set("id", JsonValue(String("TxAbortB")));
			isError = true;
			if (!callTool("create_object", createB, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_object (abort case) failed");
				return;
			}
			JsonValue abortArgs = JsonValue::object();
			abortArgs.set("commit", JsonValue(false));
			isError = true;
			if (!callTool("end_transaction", abortArgs, true, structured, isError) ||
				isError || structured.get("committed").asString() != "0")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: end_transaction {commit:false} "
					"failed");
				return;
			}
			bool presentB = true;
			if (!sceneHas("TxAbortB", presentB) || presentB)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: a rolled-back transaction still "
					"left its object in the scene");
				return;
			}

			// LIFECYCLE AUTO-ABORT: an open_prefab under an open transaction aborts
			// it (rolled back BEFORE the scene is stashed) - get_state reports it
			// closed and the prefab context, and the object never reaches the scene
			isError = true;
			if (!callTool("begin_transaction", JsonValue::object(), true, structured,
					isError) || isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: begin_transaction (auto-abort case) "
					"failed");
				return;
			}
			JsonValue createC = JsonValue::object();
			createC.set("id", JsonValue(String("TxAbortC")));
			isError = true;
			if (!callTool("create_object", createC, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: create_object (auto-abort case) "
					"failed");
				return;
			}
			JsonValue openPrefabArgs = JsonValue::object();
			openPrefabArgs.set("path",
				JsonValue(String("assets/McpProbe.oprefab")));
			isError = true;
			if (!callTool("open_prefab", openPrefabArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: open_prefab (auto-abort case) "
					"failed");
				return;
			}
			JsonValue staged;
			open = true;
			if (!getState(staged) || staged.get("edit_context").asString() !=
					"prefab" ||
				staged.get("transaction_open").asString() != "0")
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: open_prefab did not auto-abort the "
					"open transaction (transaction_open / edit_context)");
				return;
			}
			// restore the scene and confirm TxAbortC was rolled back (not stashed)
			JsonValue closeArgs = JsonValue::object();
			closeArgs.set("policy", JsonValue(String("discard")));
			isError = true;
			if (!callTool("close_prefab", closeArgs, true, structured, isError) ||
				isError)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: close_prefab (auto-abort case) "
					"failed");
				return;
			}
			bool presentC = true;
			if (!sceneHas("TxAbortC", presentC) || presentC)
			{
				fs::remove_all(authRoot, authIgnored);
				finish(false, "control self-test: the auto-aborted transaction's "
					"object survived into the restored scene");
				return;
			}
			SDL_Log("orkige_editor: control self-test - MCP transactions OK "
				"(commit=one undo, abort=rolled back, lifecycle auto-abort)");
		}

		fs::remove_all(authRoot, authIgnored);
		// (9) preview_ui - the collaborative UI loop end to end (edit-world only;
		// a project carrying gui_default is open): author a .oui, preview it,
		// MODIFY it, preview again, assert the rects followed the edit, and a bad
		// file returns an honest error. This IS the acceptance demo.
		if (!this->mRuntimeDebug)
		{
			// this leg needs an open project carrying a gui_default atlas; copy
			// jumper-lua to a temp dir (never touch the repo) and open it
			const std::string previewRoot =
				(std::filesystem::temp_directory_path() /
					("orkige_preview_project_" + std::to_string(port))).string();
			{
				std::error_code prepErr;
				std::filesystem::remove_all(previewRoot, prepErr);
				std::filesystem::copy(ORKIGE_EDITOR_JUMPER_LUA_PROJECT, previewRoot,
					std::filesystem::copy_options::recursive, prepErr);
				// seed a tiny loc/ directory (en source + de target for one key)
				// and point the copied manifest at it, so the language-axis leg
				// below can preview a @key caption in German
				{
					const std::filesystem::path locDir =
						std::filesystem::path(previewRoot) / "loc";
					std::filesystem::create_directories(locDir, prepErr);
					std::ofstream(locDir / "en.xlf")
						<< "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
						<< "<xliff version=\"1.2\" xmlns=\"urn:oasis:names:tc:"
						   "xliff:document:1.2\">\n"
						<< "  <file original=\"orkige-strings\" "
						   "source-language=\"en\" datatype=\"plaintext\">\n"
						<< "    <body>\n"
						<< "      <trans-unit id=\"preview.msg\" "
						   "resname=\"preview.msg\" xml:space=\"preserve\">\n"
						<< "        <source>Hello</source>\n"
						<< "      </trans-unit>\n"
						<< "    </body>\n  </file>\n</xliff>\n";
					std::ofstream(locDir / "de.xlf")
						<< "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
						<< "<xliff version=\"1.2\" xmlns=\"urn:oasis:names:tc:"
						   "xliff:document:1.2\">\n"
						<< "  <file original=\"orkige-strings\" "
						   "source-language=\"en\" target-language=\"de\" "
						   "datatype=\"plaintext\">\n"
						<< "    <body>\n"
						<< "      <trans-unit id=\"preview.msg\" "
						   "resname=\"preview.msg\" xml:space=\"preserve\">\n"
						<< "        <source>Hello</source>\n"
						<< "        <target state=\"translated\">"
						   "GutenTagDeutsch</target>\n"
						<< "      </trans-unit>\n"
						<< "    </body>\n  </file>\n</xliff>\n";
					const std::filesystem::path manifest =
						std::filesystem::path(previewRoot) / "project.orkproj";
					std::string text;
					{
						std::ifstream in(manifest);
						std::stringstream ss;
						ss << in.rdbuf();
						text = ss.str();
					}
					const std::string anchor = "</OrkigeProject>";
					const size_t at = text.find(anchor);
					if (at != std::string::npos)
					{
						// a <Settings> wrapper is what Project::loadFromFile reads
						// (the bare <Setting> keys jumper-lua carries are for the
						// exporter only) - point the C++ manifest at loc/
						text.insert(at, "    <Settings>\n"
							"        <Setting key=\"localisation\" value=\"loc\"/>\n"
							"    </Settings>\n");
						std::ofstream(manifest, std::ios::trunc) << text;
					}
				}
				JsonValue openArgs = JsonValue::object();
				openArgs.set("path", JsonValue(String(previewRoot)));
				openArgs.set("force", JsonValue("1"));
				JsonValue s;
				bool e = true;
				if (prepErr || !callTool("open_project", openArgs, true, s, e) || e)
				{
					finish(false, "control self-test: preview_ui - could not open "
						"the temp project copy");
					return;
				}
			}
			JsonValue structured;
			bool isError = true;
			const std::string previewOui = "screens/preview_test.oui";
			const std::string previewPng =
				(std::filesystem::temp_directory_path() /
					("orkige_preview_ui_test_" + std::to_string(port) +
						".png")).string();
			auto writeOui = [&](int boxTop) -> bool
			{
				JsonValue args = JsonValue::object();
				args.set("path", JsonValue(String(previewOui)));
				const std::string content =
					"[Layout]\natlas = gui_default\n\n"
					"[decorwidget box]\nz = 2\nsprite = none\n"
					"color = 0.2 0.6 1.0 1.0\nposition = 40 " +
					std::to_string(boxTop) + "\nsize = 300 200\n";
				args.set("content", JsonValue(String(content)));
				JsonValue s;
				bool e = true;
				return callTool("write_project_file", args, true, s, e) && !e;
			};
			auto boxTopOf = [&](JsonValue const& sc, int& outTop) -> bool
			{
				JsonValue const& ids = sc.get("ids");
				JsonValue const& rects = sc.get("rects");
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (ids.at(i).asString() == "box")
					{
						std::stringstream ss(rects.at(i).asString());
						int left = 0, top = 0;
						ss >> left >> top;
						outTop = top;
						return true;
					}
				}
				return false;
			};
			// author v1 (box near the top) and preview it
			if (!writeOui(40))
			{
				finish(false, "control self-test: preview_ui - write (v1) failed");
				return;
			}
			JsonValue pargs = JsonValue::object();
			pargs.set("file", JsonValue(String(previewOui)));
			pargs.set("path", JsonValue(String(previewPng)));
			// classic OGRE cannot composite 2D into an offscreen target, so
			// preview_ui reports an honest error there (the tab is disabled) -
			// assert that path instead of the render loop
			if (!Orkige::RenderTexture::canOwnLayers())
			{
				if (!callTool("preview_ui", pargs, true, structured, isError) ||
					!isError)
				{
					finish(false, "control self-test: preview_ui should return an "
						"honest error on the classic backend");
					return;
				}
				SDL_Log("orkige_editor: control self-test - preview_ui correctly "
					"unsupported on the classic backend");
			}
			else
			{
			pargs.set("width", JsonValue("1179"));
			pargs.set("height", JsonValue("2556"));
			pargs.set("scale", JsonValue("3"));
			if (!callTool("preview_ui", pargs, true, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: preview_ui (v1) returned an "
					"error");
				return;
			}
			// it actually rendered (a submitted batch) and wrote a non-empty png
			const int batchCount = std::atoi(
				structured.get("batch_count").asString().c_str());
			std::error_code ig;
			if (batchCount <= 0 ||
				!std::filesystem::exists(previewPng, ig) ||
				std::filesystem::file_size(previewPng, ig) == 0)
			{
				finish(false, "control self-test: preview_ui (v1) did not render "
					"(no batch / empty screenshot)");
				return;
			}
			int topV1 = -1;
			if (!boxTopOf(structured, topV1))
			{
				finish(false, "control self-test: preview_ui (v1) did not return "
					"the 'box' widget rect");
				return;
			}
			auto readPreviewPng = [](std::string const& path)
				-> std::vector<char>
			{
				std::ifstream input(path, std::ios::binary);
				return std::vector<char>(std::istreambuf_iterator<char>(input),
					std::istreambuf_iterator<char>());
			};
			const std::vector<char> previewPixelsV1 = readPreviewPng(previewPng);
			if (previewPixelsV1.empty())
			{
				finish(false, "control self-test: GUI Preview produced an empty "
					"first image");
				return;
			}
			// MODIFY the screen (move the box down) and preview again - the rect
			// must follow the edit (auto-reload of the shared stage)
			if (!writeOui(900))
			{
				finish(false, "control self-test: preview_ui - write (v2) failed");
				return;
			}
			if (!callTool("preview_ui", pargs, true, structured, isError) ||
				isError)
			{
				finish(false, "control self-test: preview_ui (v2) returned an "
					"error");
				return;
			}
			int topV2 = -1;
			if (!boxTopOf(structured, topV2) || topV2 == topV1)
			{
				finish(false, "control self-test: preview_ui - the widget rect did "
					"not follow the .oui edit");
				return;
			}
			const std::vector<char> previewPixelsV2 = readPreviewPng(previewPng);
			if (previewPixelsV2.empty() || previewPixelsV2 == previewPixelsV1)
			{
				finish(false, "control self-test: GUI Preview image did not change "
					"after moving the rendered widget");
				return;
			}
			// bad file => honest error (not a crash, not a silent success)
			JsonValue badArgs = JsonValue::object();
			badArgs.set("file", JsonValue("screens/does_not_exist.oui"));
			if (!callTool("preview_ui", badArgs, true, structured, isError) ||
				!isError)
			{
				finish(false, "control self-test: preview_ui did not error on a "
					"missing file");
				return;
			}
			SDL_Log("orkige_editor: control self-test - preview_ui OK (write -> "
				"preview top=%d -> edit -> preview top=%d; missing file errored)",
				topV1, topV2);

			// language axis: a screen whose caption is an @key routed through the
			// seeded loc/ (en source "Hello" / de target "GutenTag..."): preview it
			// in the SOURCE language, then in de, and assert the two renders DIFFER
			// (a different caption = different pixels) and that the result carries
			// the 'language'/'languages' fields. An unknown language is an error.
			{
				JsonValue writeArgs = JsonValue::object();
				writeArgs.set("path",
					JsonValue(String("screens/preview_loc.oui")));
				writeArgs.set("content", JsonValue(String(
					"[Layout]\natlas = gui_default\n\n"
					"[Label msg]\nz = 2\nfont = 9\ntext = @preview.msg\n"
					"position = 40 40\nsize = 400 40\n")));
				JsonValue ws;
				bool we = true;
				if (!callTool("write_project_file", writeArgs, true, ws, we) || we)
				{
					finish(false, "control self-test: preview_ui - loc screen write "
						"failed");
					return;
				}
				auto readBytes = [](std::string const& p) -> std::string
				{
					std::ifstream in(p, std::ios::binary);
					std::stringstream ss;
					ss << in.rdbuf();
					return ss.str();
				};
				const std::string srcPng =
					(std::filesystem::temp_directory_path() /
						("orkige_preview_loc_src_" + std::to_string(port) +
							".png")).string();
				const std::string dePng =
					(std::filesystem::temp_directory_path() /
						("orkige_preview_loc_de_" + std::to_string(port) +
							".png")).string();
				// (a) source language: omit 'language'
				JsonValue srcArgs = JsonValue::object();
				srcArgs.set("file", JsonValue(String("screens/preview_loc.oui")));
				srcArgs.set("path", JsonValue(String(srcPng)));
				if (!callTool("preview_ui", srcArgs, true, structured, isError) ||
					isError)
				{
					finish(false, "control self-test: preview_ui - source-language "
						"loc render failed");
					return;
				}
				// 'languages' must list the loaded set (source + de)
				bool sawDe = false;
				{
					JsonValue const& langs = structured.get("languages");
					for (size_t i = 0; i < langs.size(); ++i)
					{
						if (langs.at(i).asString() == "de")
						{
							sawDe = true;
						}
					}
				}
				if (!sawDe)
				{
					finish(false, "control self-test: preview_ui - 'languages' did "
						"not list the loaded 'de'");
					return;
				}
				// (b) German: the applied 'language' comes back as 'de'
				JsonValue deArgs = JsonValue::object();
				deArgs.set("file", JsonValue(String("screens/preview_loc.oui")));
				deArgs.set("path", JsonValue(String(dePng)));
				deArgs.set("language", JsonValue(String("de")));
				if (!callTool("preview_ui", deArgs, true, structured, isError) ||
					isError)
				{
					finish(false, "control self-test: preview_ui - de render "
						"failed");
					return;
				}
				if (structured.get("language").asString() != "de")
				{
					finish(false, "control self-test: preview_ui - result "
						"'language' was not 'de'");
					return;
				}
				// the German caption must actually change the rendered pixels
				const std::string srcBytes = readBytes(srcPng);
				const std::string deBytes = readBytes(dePng);
				if (srcBytes.empty() || deBytes.empty() || srcBytes == deBytes)
				{
					finish(false, "control self-test: preview_ui - the de render did "
						"not differ from the source render (localisation not "
						"applied)");
					return;
				}
				// (c) an unknown language is an honest error listing the set
				JsonValue badLang = JsonValue::object();
				badLang.set("file", JsonValue(String("screens/preview_loc.oui")));
				badLang.set("language", JsonValue(String("zz")));
				if (!callTool("preview_ui", badLang, true, structured, isError) ||
					!isError)
				{
					finish(false, "control self-test: preview_ui - an unknown "
						"language should error");
					return;
				}
				SDL_Log("orkige_editor: control self-test - preview_ui language axis "
					"OK (source vs de renders differ; unknown errored)");
			}
			}
		}

		finish(true, "");
	}
}
