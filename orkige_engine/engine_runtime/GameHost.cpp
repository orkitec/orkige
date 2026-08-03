/**************************************************************
	created:	2026/08/02 at 09:00
	filename: 	GameHost.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_runtime/GameHost.h"

#include "engine_runtime/PlayerRuntime.h"
#include "core_debug/Breadcrumbs.h"
#include "core_debug/Profile.h"
#include "core_event/GlobalEventManager.h"
#include "core_game/GameObjectManager.h"
#include "core_game/LevelManager.h"
#include "core_http/HttpClient.h"
#include "core_script/ScriptTaskManager.h"
#include "core_tween/TimerManager.h"
#include "core_tween/TweenManager.h"
#include "core_util/PathJail.h"
#include "core_util/PlatformUtil.h"
#include "engine_filesystem/MiniZip.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_graphic/DebugDraw.h"
#include "engine_graphic/ScreenFade.h"
#include "engine_graphic/ScreenShake.h"
#include "engine_input/InputActionMap.h"
#include "engine_input/InputManager.h"
#include "engine_physic/PhysicsWorld.h"
#include "engine_render/RenderSystem.h"
#include "engine_sound/SoundManager.h"

#include <SDL3/SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef __ANDROID__
#include <jni.h>	// the APK path is a JNI call on the SDL activity (stored mode)
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#if defined(_WIN32) && defined(_DEBUG)
// _CrtSetReportMode / _CrtSetReportFile: route Debug assert reporting to
// stderr so a failed assert names itself in a headless CI log
#include <crtdbg.h>
// _write(2): the async-signal-safe stderr write the SIGABRT trap uses
#include <io.h>
#endif

namespace Orkige
{

namespace
{

//! true on the platforms whose window is a fullscreen native surface
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
const bool MOBILE_PLATFORM = true;
#else
const bool MOBILE_PLATFORM = false;
#endif

//! append the platform separator when a directory string lacks one
void terminateDirectory(String & directory)
{
	if (!directory.empty() && directory.back() != '/')
	{
		directory += '/';
	}
}

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
//! the mount-versus-extract rule, shared by both packaged platforms and
//! unit-tested headlessly (@see Orkige::PlayerBundle::isMountedMediaPath)
using Orkige::PlayerBundle::isMountedMediaPath;
#endif // __ANDROID__ || __EMSCRIPTEN__

#ifdef __ANDROID__
//! @brief the APK file's own path, via a JNI call on the SDL activity
//! (Context.getPackageCodePath) - the file the player mounts in `stored` mode.
//! "" when JNI/the activity is unavailable (the mount then falls back to
//! extraction, the always-safe path).
String androidApkPath()
{
	JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!env || !activity)
	{
		return String();
	}
	String result;
	jclass cls = env->GetObjectClass(activity);
	if (cls)
	{
		jmethodID method = env->GetMethodID(cls, "getPackageCodePath",
			"()Ljava/lang/String;");
		if (method)
		{
			jstring jpath = static_cast<jstring>(
				env->CallObjectMethod(activity, method));
			if (jpath)
			{
				const char* chars = env->GetStringUTFChars(jpath, nullptr);
				if (chars)
				{
					result = chars;
					env->ReleaseStringUTFChars(jpath, chars);
				}
				env->DeleteLocalRef(jpath);
			}
		}
		env->DeleteLocalRef(cls);
	}
	env->DeleteLocalRef(activity);
	return result;
}

//! @brief extract the APK's bundled media into destRoot. APK assets are not
//! files - the render backend's filesystem archives, the scene loader
//! (tinyxml2/fopen) and the sound loader all want real paths, so everything is
//! materialized once under the app files dir. The packaging run writes
//! assets/orkige_assets.txt listing every bundled file (@see
//! tools/exporter/ExportAndroidAssemble.h); SDL_LoadFile with a relative path
//! reads from the APK assets. A file that already exists with the same size is
//! skipped (cheap re-launch).
//! @param mountMediaMode `stored` mode: skip the bulk binary media
//! (isMountedMediaPath) - the player mounts those in place - and extract only
//! the small fopen tree + shader/font media.
bool extractBundledAssets(String const & logTag, String const & destRoot,
	bool mountMediaMode)
{
	size_t manifestSize = 0;
	char* manifestData = static_cast<char*>(
		SDL_LoadFile("orkige_assets.txt", &manifestSize));
	if (!manifestData)
	{
		SDL_Log("%s: FAILED - no orkige_assets.txt in the APK "
			"assets: %s", logTag.c_str(), SDL_GetError());
		return false;
	}
	std::istringstream manifest(String(manifestData, manifestSize));
	SDL_free(manifestData);
	String relativePath;
	unsigned extracted = 0;
	unsigned kept = 0;
	while (std::getline(manifest, relativePath))
	{
		if (!relativePath.empty() && relativePath.back() == '\r')
		{
			relativePath.pop_back();
		}
		if (relativePath.empty())
		{
			continue;
		}
		if (mountMediaMode && isMountedMediaPath(relativePath))
		{
			continue;	// mounted in place from the APK, not extracted
		}
		size_t dataSize = 0;
		void* data = SDL_LoadFile(relativePath.c_str(), &dataSize);
		if (!data)
		{
			SDL_Log("%s: FAILED - manifest lists '%s' but the "
				"asset cannot be read: %s", logTag.c_str(),
				relativePath.c_str(), SDL_GetError());
			return false;
		}
		// zip-slip guard on the extract-to-disk boundary: refuse any entry
		// that (lexically or through a symlink) would land outside destRoot
		// before a single byte is written (Docs/filesystem.md - Security)
		std::filesystem::path destPath;
		if (!PathJail::resolveExtractPath(
			std::filesystem::path(destRoot), relativePath, destPath))
		{
			SDL_Log("%s: FAILED - refusing to extract '%s' "
				"(escapes the asset root)", logTag.c_str(),
				relativePath.c_str());
			return false;
		}
		std::error_code ignored;
		if (std::filesystem::exists(destPath, ignored) &&
			std::filesystem::file_size(destPath, ignored) == dataSize)
		{
			SDL_free(data);
			++kept;
			continue;
		}
		std::filesystem::create_directories(destPath.parent_path(), ignored);
		const bool saved =
			SDL_SaveFile(destPath.string().c_str(), data, dataSize);
		SDL_free(data);
		if (!saved)
		{
			SDL_Log("%s: FAILED - could not write '%s': %s", logTag.c_str(),
				destPath.string().c_str(), SDL_GetError());
			return false;
		}
		++extracted;
	}
	SDL_Log("%s: bundled assets ready under '%s' (%u extracted, "
		"%u up to date)", logTag.c_str(), destRoot.c_str(), extracted, kept);
	return true;
}
#endif // __ANDROID__

#ifdef __EMSCRIPTEN__
//! the browser export's payload archive, placed in the module filesystem by
//! the page's data loader (tools/player/web/pak_loader.js) before main() runs
const char* const WEB_PAK_FILE_NAME = "game.pak";

//! @brief unpack a browser export's game pak: write out the small tree that is
//! read through fopen (the orkige_project.txt marker, the project manifest,
//! scenes, scripts, config assets and the engine shader/font media) and report
//! back the media directories whose contents stay IN the archive to be mounted
//! in place (@see isMountedMediaPath - the Android `stored` split, verbatim).
//! @param pakPath the archive in the module filesystem
//! @param destRoot where the extracted tree lands (the module base directory)
//! @param outMountDirs receives one entry per media directory found, each an
//!        archive-internal path ending in '/' - what mountPak takes as its
//!        sub-tree mount point so files resolve by BARE resource name.
//! @return false only when the archive cannot be read or an entry cannot be
//!         written; a page with no pak never reaches here.
bool extractWebPak(String const & logTag, String const & pakPath,
	String const & destRoot, std::set<String> & outMountDirs)
{
	MiniZip pak;
	if (!pak.open(pakPath))
	{
		SDL_Log("%s: FAILED - '%s' is not a readable game pak",
			logTag.c_str(), pakPath.c_str());
		return false;
	}
	unsigned extracted = 0;
	std::vector<unsigned char> bytes;
	for (auto const & entry : pak.entries())
	{
		const String & name = entry.first;
		if (name.empty() || name.back() == '/')
		{
			continue;	// a directory record carries no bytes
		}
		if (isMountedMediaPath(name))
		{
			const std::size_t slash = name.find_last_of('/');
			if (slash != String::npos)
			{
				outMountDirs.insert(name.substr(0, slash + 1));
			}
			continue;	// mounted in place, never written out
		}
		// zip-slip guard on the extract-to-disk boundary: the archive is this
		// engine's own output, but the check costs nothing and the rule is the
		// same one every extraction here follows (Docs/filesystem.md)
		std::filesystem::path destPath;
		if (!PathJail::resolveExtractPath(
			std::filesystem::path(destRoot), name, destPath))
		{
			SDL_Log("%s: FAILED - refusing to extract '%s' "
				"(escapes the payload root)", logTag.c_str(), name.c_str());
			return false;
		}
		if (!pak.read(name, bytes))
		{
			SDL_Log("%s: FAILED - could not read '%s' from the "
				"game pak", logTag.c_str(), name.c_str());
			return false;
		}
		std::error_code ignored;
		std::filesystem::create_directories(destPath.parent_path(), ignored);
		// an empty entry is still a file the payload lists: hand SDL a valid
		// pointer with a zero length rather than a null one
		const unsigned char emptyEntry = 0;
		if (!SDL_SaveFile(destPath.string().c_str(),
			bytes.empty() ? &emptyEntry : bytes.data(), bytes.size()))
		{
			SDL_Log("%s: FAILED - could not write '%s': %s", logTag.c_str(),
				destPath.string().c_str(), SDL_GetError());
			return false;
		}
		++extracted;
	}
	SDL_Log("%s: game pak ready (%u files under '%s', %zu media "
		"dirs mounted in place)", logTag.c_str(), extracted, destRoot.c_str(),
		outMountDirs.size());
	return true;
}
#endif // __EMSCRIPTEN__

//--- abort traps (name a non-assert abort in the process log) -----------
// The CRT report routing in installAbortDiagnostics only catches
// _CRT_ASSERT/_CRT_ERROR; these traps widen that to the two abort classes
// that bypass it: an uncaught C++ exception reaching std::terminate (an
// engine exception escaping a path the render try/catch does not cover) and a
// raw abort()/SIGABRT (a driver / validation-layer abort). Each names the
// abort class on stderr - and, for terminate, the escaping exception's
// description - before letting the process die with an honest exit code. The
// durable scene trail rides the Breadcrumbs FILE (flushed per entry), which
// the always-on Breadcrumbs crash handler also stamps with the signal.
//
// The terminate handler is portable (an uncaught exception aborts on every
// platform and naming it helps every CI job; it only prints and then chains to
// the previous handler, so the exit code stays honest - trivially safe). The
// SIGABRT stderr trap is Windows-Debug-only: that is where a raw abort is
// otherwise silent, and elsewhere the Breadcrumbs SIGABRT file marker already
// names it while the sanitizer jobs own the signal handlers (ASan) and must
// keep them.

//! the log tag the handlers below name themselves with
String gAbortLogTag = "orkige";

//! print the tail of the in-memory breadcrumb ring to stderr. Called from the
//! terminate handler ONLY (a normal C++ call site, not a signal context), so
//! std::string / stdio are safe here. Cheap: reads the ring, no file I/O.
void dumpBreadcrumbTailToStderr()
{
	if (Breadcrumbs::getSingletonPtr() == nullptr)
	{
		return;
	}
	const String trail = Breadcrumbs::getSingleton().contents();
	if (trail.empty())
	{
		return;
	}
	std::fputs("orkige: breadcrumb trail (tail):\n", stderr);
	// the final entries are the ones that matter (the last scene reached)
	const size_t keep = 1200;
	const size_t from = trail.size() > keep ? trail.size() - keep : 0;
	std::fputs(trail.c_str() + from, stderr);
	if (trail.back() != '\n')
	{
		std::fputc('\n', stderr);
	}
}

//! the terminate handler installed at boot; chains to whatever was there
std::terminate_handler gPreviousTerminateHandler = nullptr;

//! std::terminate hook: the uncaught-exception abort path. Name the exception
//! (its what()) and the breadcrumb tail on stderr, then chain to the previous
//! handler (default = abort) so the process still dies with the honest code.
[[noreturn]] void hostTerminateHandler()
{
	std::fprintf(stderr, "%s: std::terminate called - the process is "
		"aborting\n", gAbortLogTag.c_str());
	if (std::exception_ptr active = std::current_exception())
	{
		try
		{
			std::rethrow_exception(active);
		}
		catch (std::exception const & e)
		{
			std::fprintf(stderr, "%s: unhandled exception: %s\n",
				gAbortLogTag.c_str(), e.what());
		}
		catch (...)
		{
			std::fprintf(stderr, "%s: unhandled non-standard exception\n",
				gAbortLogTag.c_str());
		}
	}
	dumpBreadcrumbTailToStderr();
	std::fflush(stderr);
	if (gPreviousTerminateHandler != nullptr &&
		gPreviousTerminateHandler != &hostTerminateHandler)
	{
		gPreviousTerminateHandler();
	}
	std::abort();
}

#if defined(_WIN32) && defined(_DEBUG)
//! the SIGABRT disposition installed just after Breadcrumbs armed its own (so
//! this chains INTO the Breadcrumbs file marker)
void (*gPreviousSigabrtHandler)(int) = nullptr;

//! the fixed message the signal handler writes, composed at install time so
//! the handler itself only does one write(2) (async-signal-safe)
char gAbortSignalMessage[192] = { 0 };
int gAbortSignalMessageLength = 0;

//! SIGABRT hook: the raw-abort path (a driver / validation-layer abort, or the
//! tail of std::terminate). Async-signal-safe - one fixed write(2) to stderr,
//! then chain to whatever was installed before us (the Breadcrumbs file
//! marker) so the durable trail still records the signal and the process still
//! dies with the right code.
extern "C" void hostAbortSignalHandler(int signo)
{
	(void)::_write(2, gAbortSignalMessage,
		static_cast<unsigned int>(gAbortSignalMessageLength));
	if (gPreviousSigabrtHandler != nullptr &&
		gPreviousSigabrtHandler != SIG_DFL &&
		gPreviousSigabrtHandler != SIG_ERR &&
		gPreviousSigabrtHandler != &hostAbortSignalHandler)
	{
		gPreviousSigabrtHandler(signo);
	}
	std::signal(signo, SIG_DFL);
	std::raise(signo);
}
#endif // _WIN32 && _DEBUG

//! the one frame loop of this process, reachable from the browser's
//! plain-function frame callback (which carries only the context pointer)
GameFrameLoop gActiveFrameLoop;

} // namespace

//======================================================================
// GamePlatform
//======================================================================

//---------------------------------------------------------
bool GamePlatform::isMobile()
{
	return MOBILE_PLATFORM;
}

//---------------------------------------------------------
bool GamePlatform::overridesEngineMedia() const
{
	// a packaged app's baked default names a build tree that does not exist
	// there, so its own media ALWAYS overrides; a desktop run overrides only
	// when an exported bundle carried its own Media/
	return MOBILE_PLATFORM ||
		this->mMediaDirectory != this->mConfig.desktopMediaDirectory;
}

//---------------------------------------------------------
bool GamePlatform::boot(GamePlatformConfig const & config)
{
	this->mConfig = config;
	this->mPakMounts.clear();
	this->mContentDirectories.clear();

	// the mobile Back button: TRAP it so it arrives as an ordinary key event
	// (SDL_SCANCODE_AC_BACK -> KC_WEBBACK, readable by scripts / an input
	// action) instead of letting the system finish the activity. The engine
	// default is DELIVER, don't exit - a game handles Back as "pause / go up
	// a menu"; a game that wants the exit behavior can undo it. Set before
	// the video subsystem comes up. A harmless no-op elsewhere.
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");

#ifdef __EMSCRIPTEN__
	// browser export: the page's data loader fetched the game pak and placed
	// it at the module filesystem root before main() ran. Unpack the small
	// fopen tree NOW - the bundled-project marker is read from it - and
	// remember the media directories that stay in the archive; they are
	// mounted in place once the render system exists. A page without a pak (a
	// dev module, a differently packaged one) simply has none of this.
	{
		const String webBase = PlayerBundle::baseDirectory();
		const String destRoot = webBase.empty() ? String("/") : webBase;
		const String candidate = destRoot + WEB_PAK_FILE_NAME;
		std::error_code ignored;
		if (std::filesystem::is_regular_file(candidate, ignored))
		{
			std::set<String> mountDirs;
			if (!extractWebPak(this->mConfig.logTag, candidate, destRoot,
				mountDirs))
			{
				return false;
			}
			// the bulk game media never left the pak - mount each media
			// DIRECTORY as its own flat sub-tree so files resolve by BARE
			// resource name (the Android `stored` mount, verbatim)
			for (String const & dir : mountDirs)
			{
				this->mPakMounts.emplace_back(candidate, dir);
			}
		}
	}
#endif

#ifdef __ANDROID__
	// the APK media must be extracted (and the bundled project resolved)
	// before the host boots, and extraction reads through SDL's asset IO -
	// initialise SDL video early; AppHost's own SDL_Init stacks on top
	// (per-subsystem refcount) and its teardown closes SDL for both.
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return false;
	}
	// the app files dir is the writable root - the historical PlatformUtil
	// Android path model (setFilesPath feeds getDocumentsDirectory,
	// getResourceDirectory & co)
	PlatformUtil::setFilesPath(
		String(SDL_GetAndroidInternalStoragePath()) + "/");
	// materialize the APK's bundled media (same set the iOS bundle carries)
	this->mContentRoot = PlatformUtil::getResourceDirectory() + "bundle/";
	// stored mode (export.android.assets=stored, the default): the packager
	// left the APK assets UNCOMPRESSED and dropped an orkige_mount.txt marker,
	// so the player MOUNTS the bulk game media in place (no extraction of the
	// big textures/audio/meshes) and extracts only the small fopen tree +
	// shader/font media. A resolvable APK path is required; if it or the marker
	// is absent the run falls back to full extraction (the always-safe path).
	bool mountAssets = false;
	String apkForMount;
	{
		size_t markerSize = 0;
		void* marker = SDL_LoadFile("orkige_mount.txt", &markerSize);
		const bool storedMode = (marker != nullptr);
		if (marker)
		{
			SDL_free(marker);
		}
		if (storedMode)
		{
			apkForMount = androidApkPath();
			mountAssets = !apkForMount.empty();
			if (!mountAssets)
			{
				SDL_Log("%s: stored APK but no resolvable APK path "
					"- falling back to full extraction",
					this->mConfig.logTag.c_str());
			}
		}
	}
	if (!extractBundledAssets(this->mConfig.logTag, this->mContentRoot,
		mountAssets))
	{
		return false;
	}
	if (mountAssets)
	{
		SDL_Log("%s: stored mode - mounting APK media in place from '%s'",
			this->mConfig.logTag.c_str(), apkForMount.c_str());
		// each media DIRECTORY becomes its own flat pak mount so files
		// resolve by BARE resource name, exactly like the loose-file
		// registration a desktop run does (a single sub-tree mount would only
		// resolve by full sub-path). MiniZip enumerates the APK's own
		// directory table.
		MiniZip apk;
		if (apk.open(apkForMount))
		{
			std::set<String> mediaDirs;
			for (auto const & entry : apk.entries())
			{
				const String & full = entry.first;
				if (full.rfind("assets/", 0) != 0)
				{
					continue;
				}
				if (!isMountedMediaPath(full.substr(7)))
				{
					continue;
				}
				const std::size_t slash = full.find_last_of('/');
				if (slash != String::npos)
				{
					mediaDirs.insert(full.substr(0, slash + 1));
				}
			}
			for (String const & dir : mediaDirs)
			{
				this->mPakMounts.emplace_back(apkForMount, dir);
			}
		}
		else
		{
			SDL_Log("%s: WARNING - could not open APK '%s' to mount media in "
				"place", this->mConfig.logTag.c_str(), apkForMount.c_str());
		}
	}
	// same layout as the iOS bundle, extracted from the APK assets above
	this->mMediaDirectory = this->mContentRoot + "Media";
	for (String const & subdir : this->mConfig.bundleContentSubdirectories)
	{
		this->mContentDirectories.push_back(this->mContentRoot + subdir);
	}
#elif defined(ORKIGE_IPHONE)
	// everything ships inside the app bundle (the CMake post-build step
	// copies it there), so the bundle root IS the content root
	this->mContentRoot.clear();
	const String bundleDir = PlatformUtil::getResourceDirectory();
	// note: "assets", not "media" - the Apple filesystems are case-insensitive
	// and a "media" dir would collide with "Media"
	this->mMediaDirectory = bundleDir + "Media";
	for (String const & subdir : this->mConfig.bundleContentSubdirectories)
	{
		this->mContentDirectories.push_back(bundleDir + subdir);
	}
#else
	// desktop (and the browser, whose payload unpacks into the module's own
	// base directory): build-tree defaults, except that an exported app
	// overrides the engine media with the Media/ it carries beside the project
	// marker so the bundle is self-contained - no dependency-closure or
	// source-tree path is touched at runtime
	this->mContentRoot.clear();
	this->mMediaDirectory =
		PlayerBundle::resolveMediaDirectory(this->mConfig.desktopMediaDirectory);
	this->mContentDirectories = this->mConfig.desktopContentDirectories;
#endif
	return true;
}

//---------------------------------------------------------
String GamePlatform::resolveScenePath(String const & scenePath) const
{
#ifdef ORKIGE_IPHONE
	// launched without arguments: the bundled example scene (a device play
	// session can still name a different bundled scene)
	if (scenePath.empty())
	{
		return PlatformUtil::getResourceDirectory() +
			this->mConfig.bundledSceneName;
	}
	return scenePath;
#elif defined(__ANDROID__)
	if (scenePath.empty())
	{
		return this->mContentRoot + this->mConfig.bundledSceneName;
	}
	if (scenePath[0] != '/')
	{
		// a device-side play session drops the temp scene into the app files
		// dir and names it relative to that root
		return PlatformUtil::getDocumentsDirectory() + scenePath;
	}
	return scenePath;
#else
	// desktop: an empty path stays empty, so the host reports its usage line
	return scenePath;
#endif
}

//---------------------------------------------------------
void GamePlatform::applyOrientationPolicy(String const & orientation)
{
	// A device otherwise picks the boot orientation from the allowed set by
	// the initial window aspect - and the mobile window is created
	// desktop-wide (w>h), so an unconstrained app boots LANDSCAPE. Pinning
	// the hint makes the render surface match the orientation the OS presents
	// and keeps the safe-area insets deterministic.
	this->mFollowDeviceRotation = false;
	if (orientation == "landscape")
	{
		SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	}
	else if (orientation == "auto")
	{
		this->mFollowDeviceRotation = true;
	}
	else
	{
		SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
	}
}

//---------------------------------------------------------
void GamePlatform::resolveDirectories(String const & logFileName,
	bool bundledRun)
{
	if (MOBILE_PLATFORM)
	{
		// sandboxed app: the working directory is not writable - everything
		// goes into the app container (iOS: Documents, fetchable through the
		// simulator's app-container path; Android: the files dir boot() set,
		// fetchable through the debug bridge)
		this->mStateDirectory = PlatformUtil::getDocumentsDirectory();
	}
	else
	{
		// an exported .app must never write into the cwd (a double-clicked
		// app runs with cwd "/") - its log goes to the app-support directory
		this->mStateDirectory =
			PlatformUtil::getSupportDirectory(this->mConfig.appName);
	}
	// TERMINATE FIRST, then compose: the platform call returns a trailing
	// separator on some hosts and not on others, so composing before this ran
	// produced a joined-up path ("...\Orkigeorkige.log") wherever it did not.
	terminateDirectory(this->mStateDirectory);
	// a dev desktop run keeps the historical cwd log; everything sandboxed or
	// bundled writes into the state directory
	this->mEngineLogPath = (MOBILE_PLATFORM || bundledRun)
		? this->mStateDirectory + logFileName
		: logFileName;
}

//---------------------------------------------------------
void GamePlatform::mountPackagedContent(RenderSystem & render,
	String const & resourceGroup) const
{
	if (this->mPakMounts.empty())
	{
		return;
	}
	for (auto const & mount : this->mPakMounts)
	{
		render.mountPak(mount.first, mount.second, resourceGroup);
	}
	SDL_Log("%s: mounted %zu packaged media dirs in place",
		this->mConfig.logTag.c_str(), this->mPakMounts.size());
}

//======================================================================
// process-level abort diagnostics
//======================================================================

//---------------------------------------------------------
void installAbortDiagnostics(String const & logTag)
{
	gAbortLogTag = logTag;
#if defined(_WIN32) && defined(_DEBUG)
	// Windows Debug: route CRT assertion reporting to stderr. A failed assert -
	// ours through _ASSERTE, the render backend's through the CRT assert(), or
	// the CRT's own runtime checks - reports by default through a modal window /
	// the debugger channel and then terminates the process. On a headless CI
	// runner that terminates with exit code 3 (abort) and NOTHING in the
	// captured log, so the failure cannot name itself. Reporting to stderr
	// instead prints the file:line:expression into the test's captured output
	// while the process still terminates (the assert still fails the test) -
	// the next occurrence is diagnosable from the log alone.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
	gPreviousTerminateHandler = std::set_terminate(&hostTerminateHandler);
}

//---------------------------------------------------------
void installAbortSignalTrap(String const & logTag)
{
#if defined(_WIN32) && defined(_DEBUG)
	const String message = logTag + ": SIGABRT (abort) - see the breadcrumb "
		"trail for the last scene reached\n";
	gAbortSignalMessageLength = static_cast<int>(
		message.size() < sizeof(gAbortSignalMessage)
			? message.size() : sizeof(gAbortSignalMessage) - 1);
	std::memcpy(gAbortSignalMessage, message.c_str(),
		static_cast<size_t>(gAbortSignalMessageLength));
	gPreviousSigabrtHandler = std::signal(SIGABRT, &hostAbortSignalHandler);
#else
	(void)logTag;
#endif
}

//---------------------------------------------------------
GameBuildIdentity describeBuild()
{
	GameBuildIdentity identity;
#ifdef ORKIGE_RENDER_NEXT
	identity.flavor = "next";
#else
	identity.flavor = "classic";
#endif
#if defined(ORKIGE_IPHONE)
	identity.platform = "ios";
#elif defined(__ANDROID__)
	identity.platform = "android";
#elif defined(__APPLE__)
	identity.platform = "macos";
#elif defined(_WIN32)
	identity.platform = "windows";
#else
	identity.platform = "linux";
#endif
	// the next flavor boots Metal on Apple / Vulkan elsewhere; classic
	// honours ORKIGE_RENDERSYSTEM (GL3Plus default)
#ifdef ORKIGE_RENDER_NEXT
#if defined(__APPLE__)
	identity.renderSystem = "Metal";
#else
	identity.renderSystem = "Vulkan";
#endif
#else
	if (const char* rs = std::getenv("ORKIGE_RENDERSYSTEM"))
	{
		identity.renderSystem = rs;
	}
	else
	{
		identity.renderSystem = "GL3Plus";
	}
#endif
#ifdef NDEBUG
	identity.build = "Release";
#else
	identity.build = "Debug";
#endif
	return identity;
}

//======================================================================
// the canonical gameplay tick
//======================================================================

//---------------------------------------------------------
void advanceGameWorld(GameTick const & tick, float deltaTime)
{
	// ============== GAME LOOP TICK ORDER (canonical) ==============
	// Ruled ONCE for every runtime feature:
	//   input (+ async answers) -> scripts/world -> tweens ->
	//   physics -> load pump.
	// A new runtime feature FILLS its labeled slot below instead of
	// appending elsewhere - a wrong position means silent
	// one-frame-lag bugs.
	//
	// TIME SCALE: the gameplay tick (scripts, tweens, physics) runs on
	// the SCALED delta (world.setTimeScale; 0 = hitstop, still renders);
	// input sampling, presentation overlays (fade/shake), audio, the
	// debug stream and rendering stay on the real delta.
	const float gameplayDelta = deltaTime * tick.timeScale;
	//
	// [1] INPUT - the raw events of this frame were polled and injected
	//     by the host at the top of its frame (InputManager::injectEvent).
	//     SLOT(input-actions): map raw input state to actions
	//     HERE, before the scripts that read them run. ONE edge
	//     snapshot per frame (pressed = down && !down-last-frame);
	//     scripts read the snapshot back, never recompute it.
	//     SLOT(input-devices): the RAW pointer/touch snapshot the
	//     `input` script table reads carries the SAME once-per-frame
	//     contract and is taken FIRST, so an action and a raw read
	//     inside one frame describe the same instant.
	{
		OPROFILE("input");
		if (InputManager::getSingletonPtr())
		{
			InputManager::getSingleton().updateFrameState();
		}
		if (tick.inputActions)
		{
			tick.inputActions->update(deltaTime);
		}
	}
	//
	// [1b] ASYNC ANSWERS - the network's frame boundary. An HTTP
	//     transfer progresses off the main thread; THIS is the one
	//     place its progress and completion callbacks run, so game
	//     code never sees an answer arrive mid-update. Placed with
	//     input, before the scripts that read it: an answer that
	//     landed between frames is applied before the code that
	//     looks at it runs, exactly like a key the OS delivered.
	//     Inside the fence, so a PAUSED runtime holds its answers
	//     (a callback must not mutate a frozen world) and they are
	//     delivered on resume - the injected-input discipline.
	if (tick.httpClient)
	{
		OPROFILE("http");
		tick.httpClient->update();
	}
	//
	// [2] SCRIPTS/WORLD - the component updates: ScriptComponent
	//     runs the game code, rigid bodies create lazily and sync
	//     their simulated pose into the transforms, sounds/sprites
	//     follow their transforms.
	if (tick.gameObjects)
	{
		OPROFILE("scripts");
		tick.gameObjects->update(gameplayDelta);
	}
	//
	// [2a] SCRIPT TASKS - the SINGLE point where a suspended task
	//     (script.async) is ever resumed. It rides the SCRIPT PHASE,
	//     right after the component updates, on the same scaled
	//     delta - NOT a new tick-order fence entry. Having exactly
	//     one resume site is the whole safety argument: a task can
	//     never continue inside a physics contact callback, an event
	//     dispatch or a render pass, because nothing else resumes
	//     one. Do not add a second site.
	if (tick.scriptTasks)
	{
		OPROFILE("scripts");
		tick.scriptTasks->update(gameplayDelta);
	}
	//
	// [2b] EVENT BUS - drain the ONE engine event bus
	//     (core_event/GlobalEventManager) in the SCRIPT PHASE: the
	//     script `events` table, the gui and the engine mirrors all
	//     queue onto it, and this tick fans each queued event out to
	//     its C++ and Lua listeners in subscription order. The
	//     manager's double-buffered queue makes an emit from inside a
	//     handler deliver NEXT frame (cascade-safe, no recursion). gui
	//     events queued during input dispatch (before [2]) and events
	//     emitted by the scripts just above are delivered HERE, this
	//     frame; events emitted LATER in the tick (the physics contact
	//     mirror in [4]) land next frame.
	if (GlobalEventManager::getSingletonPtr())
	{
		OPROFILE("events");
		GlobalEventManager::getSingleton().tick();
	}
	//
	// [3] TWEENS - after scripts (a tween started this frame takes
	//     its first step this frame), before physics (tweened poses
	//     are what the simulation sees). Dormant in the editor: only
	//     runtimes that tick this block create a TweenManager.
	if (tick.tweens || tick.timers)
	{
		OPROFILE("tweens");
		if (tick.tweens)
		{
			tick.tweens->update(gameplayDelta);
		}
		// timers ride the SAME phase (a timer is a degenerate tween);
		// scheduled callbacks fire here, after scripts, on the scaled
		// gameplay delta - NOT a new tick-order fence entry
		if (tick.timers)
		{
			tick.timers->update(gameplayDelta);
		}
	}
	//
	// [4] PHYSICS - the fixed-timestep simulation, then the
	//     sim->scene pose sync: dynamic bodies publish the pose
	//     THIS frame's step produced (component updates ran before
	//     physics, so without this pass rendering and the debug
	//     stream would lag the simulation by one tick).
	if (tick.physicsNeeded && tick.physics && tick.gameObjects)
	{
		OPROFILE("physics");
		tick.physics->update(gameplayDelta);
		RigidBodyComponent::syncDynamicBodyPoses(*tick.gameObjects);
		// contacts + sensors/triggers: the worker-thread
		// contact queue was drained inside update() above; dispatch
		// the coalesced frame contacts to game code on THIS main
		// thread (C++ ContactBegan/EndedEvent + the Lua
		// onContactBegin/onContactEnd hooks). A script mutating the
		// world here defers through the GameObjectManager delete
		// queue, so it stays safe mid-dispatch.
		RigidBodyComponent::dispatchContacts(*tick.gameObjects);
	}
	//
	// [5] SLOT(deferred-load pump): a script asked for a scene
	//     switch (world.loadScene / LevelManager:loadLevel set the
	//     pending request during [2]). Apply it HERE, at the frame
	//     boundary AFTER physics - never mid-update, where in-flight
	//     script/update pointers would dangle. The host's loadScene
	//     resolves the pending NAME against whatever it plays and tears
	//     the old world down through the GameObjectManager::clear
	//     teardown hook; the new scene's scripts init on the NEXT
	//     frame. Keep this slot LAST.
	if (tick.levels && tick.loadScene)
	{
		OPROFILE("load");
		int pendingLevelIndex = -1;
		String pendingScene;
		if (tick.levels->consumePendingLoad(pendingLevelIndex, pendingScene))
		{
			if (tick.loadScene(pendingScene) && pendingLevelIndex >= 0)
			{
				tick.levels->setCurrentIndex(pendingLevelIndex);
			}
		}
	}
	// ================ end GAME LOOP TICK ORDER ====================

	// audio listener follows the (script-driven) camera rig
	if (tick.sound)
	{
		OPROFILE("audio");
		tick.sound->update(deltaTime);
	}

	// the fade overlay is a PRESENTATION layer: ticked last, after
	// the deferred-load pump, so its alpha reflects the frame about to
	// render and a mid-fade scene switch is hidden under full opacity
	if (tick.screenFade)
	{
		OPROFILE("present");
		tick.screenFade->update(deltaTime);
	}
	// screen shake is a PRESENTATION effect too, ticked after the fade
	// and the deferred-load pump: it reads the camera's base pose AFTER
	// the scripts/physics of this frame set it, applies the wobble for
	// the frame about to render, and restores it. On the real delta so
	// it still animates during a hitstop (timeScale 0).
	if (tick.screenShake)
	{
		OPROFILE("present");
		tick.screenShake->update(deltaTime);
	}
	// immediate-mode debug lines are a PRESENTATION layer too: flushed after
	// the scripts of this frame queued their draw.* primitives, so the mesh
	// shows this frame's shapes, then their lifetimes age (frame-only shapes
	// drop). Real delta so a hitstop (timeScale 0) still ages TTLs correctly.
	if (tick.debugDraw)
	{
		OPROFILE("present");
		tick.debugDraw->update(deltaTime);
	}
}

//======================================================================
// the frame loop
//======================================================================

#ifdef __EMSCRIPTEN__
namespace
{
	//! the page's frame callback: one frame, and at the end of the run the
	//! same orderly shutdown every other platform runs on its own stack
	void webFrameCallback(void* context)
	{
		if (gActiveFrameLoop.frame(context))
		{
			return;
		}
		// the run ended: the orderly shutdown, then the ONE owning pointer is
		// released here, exactly once - the loop is cancelled and the runtime
		// exits with the game's code; nothing touches the context afterwards
		gActiveFrameLoop.finish(context);
		const int finalExitCode = gActiveFrameLoop.exitCode(context);
		gActiveFrameLoop.dispose(context);
		emscripten_cancel_main_loop();
		emscripten_force_exit(finalExitCode);
	}
}
#endif

//---------------------------------------------------------
bool gameFrameLoopOwnsContext()
{
#ifdef __EMSCRIPTEN__
	return true;
#else
	return false;
#endif
}

//---------------------------------------------------------
void runGameFrameLoop(GameFrameLoop const & loop)
{
	gActiveFrameLoop = loop;
#ifdef __EMSCRIPTEN__
	// pacing follows the automated-run window policy: a HUMAN run follows the
	// page's display refresh (fps 0), an automated (frame-capped/scripted) run
	// uses timer pacing so a headless session's virtual clock can fast-forward
	// the frames. The final `true` abandons the calling frame right here, so
	// the context ownership has already moved to the callback and nothing
	// after this call runs.
	const int framesPerSecond = loop.automatedRun ? 60 : 0;
	emscripten_set_main_loop_arg(&webFrameCallback, loop.context,
		framesPerSecond, true);
#else
	while (loop.frame(loop.context))
	{
	}
	loop.finish(loop.context);
#endif
}

} // namespace Orkige
