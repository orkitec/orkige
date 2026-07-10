#!/usr/bin/env python3
"""Export (package) an Orkige .orkproj project as a distributable app - the
"Build Settings" step of the project ladder (python3 stdlib only, same rules
as the other Util/ generators).

    orkige_export.py --project <dir> --platform macos|ios-simulator|android
                     --engine-build <preset build dir> [--output <dir>]

The exporter never builds the ENGINE - it packages what a preset build tree
already produced (plus, for native-module projects on macOS, an incremental
build of the project's own module). Per platform:

  macos          a double-clickable <Name>.app:
                   Contents/MacOS/<Exe>       the player binary - the RELEASE
                                              one when a sibling
                                              macos-release-classic tree
                                              carries it, else the given
                                              tree's (with a warning when that
                                              is a Debug binary). A project
                                              with a native module ships the
                                              MODULE executable instead, built
                                              here (Release against the
                                              release engine tree when
                                              available, else Debug with a
                                              warning) into <project>/<native.
                                              buildDir>-export.
                   Contents/Frameworks/       the executable's non-system
                                              dylib closure (today: the vcpkg
                                              Vulkan loader), rpaths rewritten
                                              to @executable_path/../Frameworks
                                              (build-tree rpaths removed) and
                                              the binary ad-hoc re-signed - the
                                              bundle must not depend on this
                                              machine's build trees.
                   Contents/Resources/        Media/ (the engine shader media
                                              from the build's vcpkg - the
                                              classic RTSS library Main +
                                              RTShaderLib, or the Ogre-Next
                                              Hlms shader templates), project/
                                              (manifest, scenes/, assets/,
                                              scripts/) and the
                                              orkige_project.txt marker.
                 The marker is the no-args default-project mechanism: the
                 runtimes (PlayerBundle in engine_runtime/PlayerRuntime.h)
                 read it from SDL_GetBasePath() - Contents/Resources in a mac
                 bundle - so the app launches its project without arguments.
                 Info.plist: bundle id from the manifest setting
                 "export.macos.bundleId" (default com.orkitec.<name>).

  ios-simulator  reuses the OrkigePlayer.app the ios-simulator-debug preset
                 built and adds the project payload + marker at the (flat)
                 bundle root (= SDL_GetBasePath() on iOS). No re-signing
                 needed for the simulator. Native-module projects are refused
                 (mobile native modules are future work); physical-device
                 export (--platform ios) is gated on a signing identity, the
                 same as the editor's Play on iOS hardware.

  android        drives tools/player/android/package_apk.sh against the
                 android-debug build tree with the project payload + marker in
                 the APK assets/ (extracted by the player on first launch,
                 marker read from the extracted root), the package name from
                 the manifest setting "export.android.package" (default
                 com.orkitec.<name>) and the project name as the app label.
                 Native-module projects are refused (future work).

Output lands in <project>/builds/<platform>/ (or --output). The last line on
success is "orkige_export: OK <artifact>" - the editor's Build menu parses it
for the reveal-in-Finder nicety, tests for the artifact path.
"""

import argparse
import glob
import os
import plistlib
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

import cook_textures  # sibling Util helper (export-time texture cook)
import orkige_icons  # sibling Util helper (export-time app-icon generation)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# launch-screen background colour when a project sets no export.launch.background
# (the engine dark, matching the default icon's gradient bottom). Consumed at
# package time to generate platform launch resources - NOT copied into the
# payload, so it is deliberately NOT a CONFIG_SETTING_KEYS entry.
DEFAULT_LAUNCH_BACKGROUND = "#12161f"

# --platform value -> the texture-cook / import-settings platform token
# (desktop uses the default block; the mobile flavors resolve their overrides)
COOK_PLATFORM = {"macos": "", "ios-simulator": "ios", "ios": "ios",
                 "android": "android"}

# the marker file name PlayerBundle reads (engine_runtime/PlayerRuntime.cpp)
PROJECT_MARKER_FILE_NAME = "orkige_project.txt"
# the project payload's directory name inside every bundle (marker content)
PAYLOAD_DIR_NAME = "project"
# what of a project ships: the manifest + these subdirectories (native/ and
# builds/ stay home - compiled code ships as the packaged binary)
PAYLOAD_SUBDIRS = ("scenes", "assets", "scripts")

# PROJECT-CONFIG assets ride along too: files referenced by a manifest Settings
# key rather than living under scenes/assets/scripts (the config-asset
# convention - see engine_input/InputActionMap.h). Each entry is a Settings key
# whose value is a project-relative path to copy verbatim; later config
# packages (cvars persistence, physics.olayers) append their key here.
CONFIG_SETTING_KEYS = ("input.actions", "physics.layers", "levels",
                       "localisation")


def log(message):
    print("orkige_export: " + message, flush=True)


def warn(message):
    print("orkige_export: WARNING: " + message, flush=True)


def fail(message):
    print("orkige_export: ERROR: " + message, flush=True)
    sys.exit(1)


def run(command, **kwargs):
    """run a subprocess with echoed command line; fail() on nonzero exit"""
    log("$ " + " ".join(command))
    result = subprocess.run(command, **kwargs)
    if result.returncode != 0:
        fail("command failed (exit %d): %s" % (result.returncode, command[0]))
    return result


# --- project manifest ------------------------------------------------------

class Project:
    """the slice of project.orkproj the exporter needs (read via tinyxml2's
    Python twin - the manifest is a small semantic XML document, see
    core_project/Project.h)"""

    def __init__(self, root_directory):
        self.root = os.path.abspath(root_directory)
        manifest_path = os.path.join(self.root, "project.orkproj")
        if os.path.isfile(self.root) and self.root.endswith(".orkproj"):
            manifest_path = self.root
            self.root = os.path.dirname(self.root)
        if not os.path.isfile(manifest_path):
            fail("no project.orkproj under '%s'" % self.root)
        try:
            manifest = ET.parse(manifest_path).getroot()
        except ET.ParseError as error:
            fail("unparseable manifest '%s': %s" % (manifest_path, error))
        if manifest.tag != "OrkigeProject":
            fail("'%s' is not an OrkigeProject manifest" % manifest_path)
        self.name = (manifest.findtext("Name") or "").strip()
        if not self.name:
            fail("manifest '%s' has no Name" % manifest_path)
        self.main_scene = (manifest.findtext("MainScene") or "").strip()
        self.settings = {}
        for setting in manifest.iter("Setting"):
            key = setting.get("key")
            if key:
                self.settings[key] = setting.get("value", "")

    @property
    def exe_name(self):
        """executable/artifact base name: the project name, alnum only"""
        return re.sub(r"[^A-Za-z0-9]", "", self.name) or "OrkigeGame"

    @property
    def id_slug(self):
        """reverse-DNS-safe lowercase slug for default bundle/package ids"""
        slug = re.sub(r"[^a-z0-9]", "", self.name.lower()) or "orkigegame"
        return ("p" + slug) if slug[0].isdigit() else slug

    def native_target(self):
        return self.settings.get("native.target", "").strip()


def stage_project_payload(project, dest_dir, platform="macos"):
    """copy the shippable project subset (manifest + scenes/assets/scripts)
    into dest_dir, run the export-time texture cook over the staged assets for
    the target platform, and return the number of files staged"""
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copy2(os.path.join(project.root, "project.orkproj"),
                 os.path.join(dest_dir, "project.orkproj"))
    staged = 1
    for subdir in PAYLOAD_SUBDIRS:
        source = os.path.join(project.root, subdir)
        if os.path.isdir(source):
            destination = os.path.join(dest_dir, subdir)
            shutil.copytree(source, destination, dirs_exist_ok=True)
            staged += sum(len(files) for _, _, files in os.walk(destination))
    # manifest-referenced project-config files (input.oactions and its future
    # siblings): copy each referenced file preserving its project-relative path
    for setting_key in CONFIG_SETTING_KEYS:
        relative = project.settings.get(setting_key, "").strip()
        if not relative:
            continue
        source = os.path.join(project.root, relative)
        if os.path.isfile(source):
            destination = os.path.join(dest_dir, relative)
            os.makedirs(os.path.dirname(destination) or dest_dir, exist_ok=True)
            shutil.copy2(source, destination)
            staged += 1
        else:
            warn("manifest setting '%s' references '%s' but no such file "
                 "exists - not bundled" % (setting_key, relative))
    # export-time texture cook: resize/premultiply the staged textures per
    # their sidecar import settings, resolved for the target platform. The
    # sidecars ship alongside (the runtime reads the LIVE sampler settings from
    # them); the cook only rewrites the pixels. Uncompressed only - GPU
    # compression is double-blocked (see cook_textures.py) and deferred.
    cooked = cook_textures.cook_payload(dest_dir, COOK_PLATFORM.get(platform,
        ""), log=lambda message: log(message))
    if cooked:
        log("cooked %d texture(s) for platform '%s'"
            % (cooked, COOK_PLATFORM.get(platform, "")))
    return staged


def launch_background(project):
    """the project's launch-screen background as a validated #RRGGBB string
    (falls back to the engine default on an absent or malformed value)."""
    value = project.settings.get("export.launch.background", "").strip()
    if re.fullmatch(r"#[0-9A-Fa-f]{6}", value):
        return value
    if value:
        warn("export.launch.background '%s' is not a #RRGGBB colour - using "
             "the default %s" % (value, DEFAULT_LAUNCH_BACKGROUND))
    return DEFAULT_LAUNCH_BACKGROUND


def write_marker(directory):
    with open(os.path.join(directory, PROJECT_MARKER_FILE_NAME), "w",
              newline="\n") as marker:
        marker.write(PAYLOAD_DIR_NAME + "\n")


def directory_size(path):
    if os.path.isfile(path):
        return os.path.getsize(path)
    total = 0
    for parent, _, files in os.walk(path):
        for name in files:
            file_path = os.path.join(parent, name)
            if not os.path.islink(file_path):
                total += os.path.getsize(file_path)
    return total


def human_size(byte_count):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if byte_count < 1024 or unit == "GiB":
            return ("%d %s" if unit == "B" else "%.1f %s") % (byte_count, unit)
        byte_count /= 1024.0


# --- build-tree helpers ----------------------------------------------------

def read_cmake_cache(build_dir, variable):
    cache_path = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache_path):
        return ""
    with open(cache_path, "r", errors="replace") as cache:
        for line in cache:
            if line.startswith(variable + ":"):
                return line.split("=", 1)[1].strip()
    return ""


def vcpkg_triplet_dir(build_dir):
    """the build's vcpkg_installed/<triplet> (the dir with include/), same
    detection as cmake/OrkigeGameModule.cmake"""
    for candidate in sorted(glob.glob(
            os.path.join(build_dir, "vcpkg_installed", "*"))):
        if os.path.isdir(os.path.join(candidate, "include")):
            return candidate
    return ""


def render_backend(build_dir):
    """the tree's render flavor ("next" or "classic"); classic when the cache
    does not name one (the historical default)"""
    return read_cmake_cache(build_dir, "ORKIGE_RENDER_BACKEND") or "classic"


def ogre_media_dir(build_dir):
    """the classic flavor's RTSS shader-library media (Main + RTShaderLib)"""
    triplet = vcpkg_triplet_dir(build_dir)
    if not triplet:
        return ""
    media = os.path.join(triplet, "share", "ogre", "Media")
    return media if os.path.isdir(media) else ""


def ogre_next_media_dir(build_dir):
    """the Ogre-Next flavor's media root (contains Hlms/ - the shader templates
    the runtime registers via Engine::setHlmsMediaDir)"""
    triplet = vcpkg_triplet_dir(build_dir)
    if not triplet:
        return ""
    media = os.path.join(triplet, "share", "ogre-next", "Media")
    return media if os.path.isdir(media) else ""


def sibling_release_tree(engine_build):
    """the shippable Release tree next to the given tree, when it exists: each
    flavor's release tree is its own (trees are flavor-bound) - build/macos-
    release for the Ogre-Next flavor, build/macos-release-classic for the
    classic flavor. Debug players run ~19x slower, so export prefers the
    Release sibling of the SAME flavor."""
    name = ("macos-release" if render_backend(engine_build) == "next"
            else "macos-release-classic")
    return os.path.join(os.path.dirname(os.path.abspath(engine_build)), name)


def engine_tree_arch(build_dir):
    """the engine tree's target architecture, derived from its vcpkg triplet
    (arm64-osx -> arm64). The exporter PINS the native-module build to it via
    CMAKE_OSX_ARCHITECTURES: without the pin, clang targets whatever
    architecture the spawning process runs as - and this machine's Intel-
    Homebrew /usr/local (x86_64 python3 -> Rosetta) can silently turn the
    module build x86_64 while the engine libs are arm64."""
    triplet = os.path.basename(vcpkg_triplet_dir(build_dir))
    if triplet.startswith("arm64-"):
        return "arm64"
    if triplet.startswith(("x64-", "x86_64-")):
        return "x86_64"
    return ""


# --- macOS -----------------------------------------------------------------

def macos_collect_dylibs(executable, search_dirs):
    """the executable's non-system dylib dependencies (@rpath/... or absolute
    paths outside /usr/lib and /System), resolved against search_dirs.
    Returns [(dependency-as-written, resolved-file), ...]"""
    output = subprocess.run(["otool", "-L", executable], capture_output=True,
                            text=True, check=True).stdout
    dependencies = []
    for line in output.splitlines()[1:]:
        dep = line.strip().split(" (")[0]
        if not dep or dep.startswith(("/usr/lib/", "/System/")):
            continue
        resolved = ""
        if dep.startswith("@rpath/"):
            name = dep[len("@rpath/"):]
            for search_dir in search_dirs:
                candidate = os.path.join(search_dir, name)
                if os.path.isfile(candidate):
                    resolved = candidate
                    break
        elif os.path.isfile(dep):
            resolved = dep
        if not resolved:
            fail("cannot resolve dylib dependency '%s' of '%s' (searched %s)"
                 % (dep, executable, search_dirs))
        dependencies.append((dep, resolved))
    return dependencies


def macos_rpaths(executable):
    output = subprocess.run(["otool", "-l", executable], capture_output=True,
                            text=True, check=True).stdout
    rpaths = []
    lines = output.splitlines()
    for index, line in enumerate(lines):
        if "cmd LC_RPATH" in line:
            for path_line in lines[index:index + 4]:
                stripped = path_line.strip()
                if stripped.startswith("path "):
                    rpaths.append(stripped.split()[1])
                    break
    return rpaths


def macos_make_self_contained(executable, frameworks_dir, search_dirs):
    """copy the non-system dylib closure into Contents/Frameworks, point the
    executable at it (@executable_path/../Frameworks), REMOVE the build-tree
    rpaths (so a missing dylib fails here on this machine, not on the user's)
    and ad-hoc re-sign the modified binary"""
    dependencies = macos_collect_dylibs(executable, search_dirs)
    if dependencies:
        os.makedirs(frameworks_dir, exist_ok=True)
    changed = False
    for dep, resolved in dependencies:
        shutil.copy2(resolved, os.path.join(frameworks_dir,
                                            os.path.basename(resolved)))
        log("bundled dylib %s" % os.path.basename(resolved))
        if not dep.startswith("@rpath/"):
            # absolute dev path -> load via the bundle rpath instead
            run(["install_name_tool", "-change", dep,
                 "@rpath/" + os.path.basename(resolved), executable])
        changed = True
    for rpath in macos_rpaths(executable):
        # every build-machine path is banned from the shipped binary
        if "vcpkg_installed" in rpath or rpath.startswith(REPO_ROOT):
            run(["install_name_tool", "-delete_rpath", rpath, executable])
            changed = True
    if dependencies:
        run(["install_name_tool", "-add_rpath",
             "@executable_path/../Frameworks", executable])
        changed = True
    if changed:
        # install_name_tool invalidates the (linker) ad-hoc signature and
        # arm64 macOS refuses to run unsigned binaries - re-sign ad-hoc
        run(["codesign", "--force", "-s", "-", executable])


def macos_build_native_module(project, target, engine_build, cmake, ninja):
    """build the project's native module for export (see
    cmake/OrkigeGameModule.cmake): Release against the sibling
    macos-release-classic engine tree when its libraries exist, else the
    given tree's build type with a warning. A SEPARATE build tree (<native.buildDir>-export) keeps
    the editor's compile-on-Play cache untouched."""
    engine_tree = engine_build
    release_tree = sibling_release_tree(engine_build)
    if read_cmake_cache(engine_build, "CMAKE_BUILD_TYPE") != "Release":
        if os.path.isfile(os.path.join(release_tree, "orkige_engine",
                                       "liborkige_engine.a")):
            engine_tree = release_tree
            log("native module: building Release against '%s'" % engine_tree)
        else:
            warn("no release engine tree at '%s' - exporting a %s build of "
                 "the native module" % (release_tree,
                 read_cmake_cache(engine_build, "CMAKE_BUILD_TYPE") or "?"))
    build_type = read_cmake_cache(engine_tree, "CMAKE_BUILD_TYPE") or "Debug"
    source_dir = os.path.join(
        project.root, project.settings.get("native.cmakeDir", "native"))
    if not os.path.isfile(os.path.join(source_dir, "CMakeLists.txt")):
        fail("native module source '%s' has no CMakeLists.txt" % source_dir)
    build_dir = os.path.join(project.root, project.settings.get(
        "native.buildDir", "native/build") + "-export")
    arch = engine_tree_arch(engine_tree)
    if not arch:
        fail("cannot derive the target architecture from '%s' (no vcpkg "
             "triplet dir)" % engine_tree)
    if os.path.isfile(os.path.join(build_dir, "CMakeCache.txt")):
        cached_engine = os.path.abspath(read_cmake_cache(
            build_dir, "ORKIGE_ENGINE_BUILD_DIR")) if read_cmake_cache(
            build_dir, "ORKIGE_ENGINE_BUILD_DIR") else ""
        if read_cmake_cache(build_dir, "CMAKE_OSX_ARCHITECTURES") != arch:
            # a cache without the arch pin (or with the wrong one) produces
            # objects that cannot link against the engine libs - heal it
            warn("export build tree '%s' is not pinned to %s - reconfiguring"
                 % (build_dir, arch))
            shutil.rmtree(build_dir)
        elif cached_engine != os.path.abspath(engine_tree):
            # the module was built against a DIFFERENT engine tree before (e.g.
            # the other render flavor): its objects link the wrong backend
            # closure and the bundled media would not match - heal it
            warn("export build tree '%s' targeted a different engine tree "
                 "('%s' != '%s') - reconfiguring"
                 % (build_dir, cached_engine, os.path.abspath(engine_tree)))
            shutil.rmtree(build_dir)
    if not os.path.isfile(os.path.join(build_dir, "CMakeCache.txt")):
        configure = [cmake, "-G", "Ninja", "-S", source_dir, "-B", build_dir,
                     "-DCMAKE_BUILD_TYPE=" + build_type,
                     "-DORKIGE_ROOT=" + REPO_ROOT,
                     "-DORKIGE_ENGINE_BUILD_DIR=" + engine_tree,
                     # hermeticity, same as the presets
                     "-DCMAKE_IGNORE_PREFIX_PATH=/usr/local",
                     "-DCMAKE_OSX_ARCHITECTURES=" + arch,
                     "-DCMAKE_OSX_SYSROOT="
                     + (read_cmake_cache(engine_tree, "CMAKE_OSX_SYSROOT")
                        or "macosx")]
        scripting = read_cmake_cache(engine_tree, "ORKIGE_SCRIPTING")
        if scripting:
            configure.append("-DORKIGE_SCRIPTING=" + scripting)
        if ninja:
            configure.append("-DCMAKE_MAKE_PROGRAM=" + ninja)
        run(configure)
    run([cmake, "--build", build_dir])
    executable = os.path.join(build_dir, target)
    if not os.path.isfile(executable):
        fail("native module build produced no '%s'" % executable)
    return executable, engine_tree


def export_macos(project, engine_build, output_dir, cmake, ninja):
    native_target = project.native_target()
    if native_target and render_backend(engine_build) == "next":
        # native modules link the classic OGRE closure and are refused against a
        # next engine tree by cmake/OrkigeGameModule.cmake - refuse here too,
        # honestly, instead of failing deep in the module's cmake configure.
        # (The Lua/scene parts of such a project export fine on next without a
        # module; the compiled module is desktop-classic-only for now.)
        fail("project '%s' has a native module ('%s') - native modules link "
             "the classic OGRE closure and are classic-flavor-only; pass a "
             "classic engine tree (macos-debug-classic or macos-release-"
             "classic)" % (project.name, native_target))
    if native_target:
        executable, source_tree = macos_build_native_module(
            project, native_target, engine_build, cmake, ninja)
    else:
        # prefer the RELEASE player: Debug runs ~19x slower (see CLAUDE.md)
        executable = os.path.join(engine_build, "tools", "player",
                                  "orkige_player")
        source_tree = engine_build
        if read_cmake_cache(engine_build, "CMAKE_BUILD_TYPE") != "Release":
            release_player = os.path.join(sibling_release_tree(engine_build),
                                          "tools", "player", "orkige_player")
            if os.path.isfile(release_player):
                executable = release_player
                source_tree = sibling_release_tree(engine_build)
                log("using the release player '%s'" % executable)
            else:
                warn("no release player at '%s' - exporting the DEBUG player "
                     "(build the macos-release-classic preset for shippable "
                     "speed)" % release_player)
        if not os.path.isfile(executable):
            fail("no player binary at '%s' - build the preset first"
                 % executable)

    flavor = render_backend(source_tree)
    if flavor == "next":
        media_dir = ogre_next_media_dir(source_tree)
        if not media_dir:
            fail("no vcpkg Ogre-Next media under '%s'" % source_tree)
    else:
        media_dir = ogre_media_dir(source_tree)
        if not media_dir:
            fail("no vcpkg OGRE media under '%s'" % source_tree)

    app_dir = os.path.join(output_dir, project.name + ".app")
    if os.path.exists(app_dir):
        shutil.rmtree(app_dir)
    contents = os.path.join(app_dir, "Contents")
    macos_dir = os.path.join(contents, "MacOS")
    resources = os.path.join(contents, "Resources")
    os.makedirs(macos_dir)
    os.makedirs(resources)

    bundled_exe = os.path.join(macos_dir, project.exe_name)
    shutil.copy2(executable, bundled_exe)
    os.chmod(bundled_exe, 0o755)

    # the dylib closure: rpath deps resolve against the source tree's vcpkg
    triplet = vcpkg_triplet_dir(source_tree)
    search_dirs = [os.path.join(triplet, "debug", "lib"),
                   os.path.join(triplet, "lib")] if triplet else []
    macos_make_self_contained(bundled_exe,
                              os.path.join(contents, "Frameworks"),
                              macos_rpaths(bundled_exe) + search_dirs)

    # engine media, per flavor: the classic RTSS shader library (Main +
    # RTShaderLib) or the Ogre-Next Hlms shader templates (Hlms). The runtimes
    # resolve <Resources>/Media at boot (PlayerBundle::resolveMediaDirectory)
    # and register it - RTSS locations on classic, Engine::setHlmsMediaDir on
    # next - so the bundle carries no vcpkg or source-tree path.
    media_subdirs = ("Hlms",) if flavor == "next" else ("Main", "RTShaderLib")
    for media_subdir in media_subdirs:
        shutil.copytree(os.path.join(media_dir, media_subdir),
                        os.path.join(resources, "Media", media_subdir))

    staged = stage_project_payload(
        project, os.path.join(resources, PAYLOAD_DIR_NAME), "macos")
    write_marker(resources)
    log("project payload: %d files" % staged)

    # app icon: Contents/Resources/AppIcon.icns from export.icon (or the engine
    # default). macOS has no launch-image concept, so this is icon-only.
    source = orkige_icons.resolve_icon_source(project, log=log)
    iconset = os.path.join(output_dir, project.exe_name + ".iconset")
    orkige_icons.make_macos_iconset(
        orkige_icons.load_square_source(source), iconset)
    run(["iconutil", "-c", "icns", iconset,
         "-o", os.path.join(resources, "AppIcon.icns")])
    shutil.rmtree(iconset, ignore_errors=True)

    bundle_id = project.settings.get(
        "export.macos.bundleId", "com.orkitec." + project.id_slug)
    with open(os.path.join(contents, "Info.plist"), "wb") as plist:
        plistlib.dump({
            "CFBundleDevelopmentRegion": "en",
            "CFBundleExecutable": project.exe_name,
            "CFBundleIdentifier": bundle_id,
            "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleName": project.name,
            "CFBundleDisplayName": project.name,
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": "1.0",
            "CFBundleVersion": "1",
            "LSMinimumSystemVersion": "11.0",
            "NSHighResolutionCapable": True,
            # CFBundleIconFile is what macOS reads; CFBundleIconName is the
            # modern spelling (harmless, future-proofs an asset-catalog move)
            "CFBundleIconFile": "AppIcon",
            "CFBundleIconName": "AppIcon",
        }, plist)
    log("bundle id %s" % bundle_id)
    return app_dir


# --- iOS simulator ---------------------------------------------------------

def export_ios_simulator(project, engine_build, output_dir):
    if project.native_target():
        fail("project '%s' has a native module ('%s') - native modules are "
             "desktop-only for now, mobile native builds are future work "
             "(the Lua/scene parts of a project export fine without one)"
             % (project.name, project.native_target()))
    source_app = os.path.join(engine_build, "tools", "player",
                              "OrkigePlayer.app")
    if not os.path.isdir(source_app):
        fail("no OrkigePlayer.app at '%s' - build the ios-simulator-debug "
             "preset first" % source_app)
    app_dir = os.path.join(output_dir, project.name + ".app")
    if os.path.exists(app_dir):
        shutil.rmtree(app_dir)
    # the simulator player bundle already carries the engine media; add the
    # project payload + marker at the flat bundle root (= SDL_GetBasePath()
    # on iOS). No re-signing needed on the simulator.
    shutil.copytree(source_app, app_dir, symlinks=True)
    staged = stage_project_payload(project,
                                   os.path.join(app_dir, PAYLOAD_DIR_NAME),
                                   "ios-simulator")
    write_marker(app_dir)
    log("project payload: %d files" % staged)

    # per-project icons + Info.plist identity. The prebuilt player bundle ships
    # the generic player identity; rewrite it to the project's and add the loose
    # CFBundleIconFiles PNGs the simulator honours at the bundle root (no asset
    # catalog / actool - those need non-stdlib tools). UILaunchScreen (added to
    # the player template) opts the app into native full resolution.
    icon_source = orkige_icons.load_square_source(
        orkige_icons.resolve_icon_source(project, log=log))
    icon_files = orkige_icons.make_ios_icons(icon_source, app_dir)
    plist_path = os.path.join(app_dir, "Info.plist")
    with open(plist_path, "rb") as handle:
        info = plistlib.load(handle)
    info["CFBundleIdentifier"] = project.settings.get(
        "export.ios.bundleId", "com.orkitec." + project.id_slug)
    info["CFBundleName"] = project.name
    info["CFBundleDisplayName"] = project.name
    info["CFBundleIcons"] = {"CFBundlePrimaryIcon": {
        "CFBundleIconFiles": [name[:-4] for name in icon_files]}}
    info.setdefault("UILaunchScreen", {})  # native full-resolution launch
    with open(plist_path, "wb") as handle:
        plistlib.dump(info, handle)
    log("bundle id %s" % info["CFBundleIdentifier"])
    log("install: xcrun simctl install <udid> '%s'" % app_dir)
    return app_dir


# --- iOS device (signed) ---------------------------------------------------
# The signing IDENTITY and the provisioning PROFILE are developer-machine
# specific and must never be committed - they come from CLI args or the
# environment. Only the Team ID (export.ios.teamId) is a project-level, safe-to-
# commit value. See Docs/ios-signing.md for the one-time Apple-side setup.

IOS_SIGNING_IDENTITY_ENV = "ORKIGE_IOS_SIGNING_IDENTITY"
IOS_PROVISIONING_PROFILE_ENV = "ORKIGE_IOS_PROVISIONING_PROFILE"


def resolve_ios_signing(identity_arg, profile_arg, environ):
    """resolve (identity, profile) for a signed iOS build: the CLI arg wins,
    else the environment. Returns a (identity, profile) pair of strings, each
    empty when unresolved (pure - no filesystem/subprocess, so it is unit-
    testable without a certificate)."""
    identity = (identity_arg or environ.get(IOS_SIGNING_IDENTITY_ENV, "")
                ).strip()
    profile = (profile_arg or environ.get(IOS_PROVISIONING_PROFILE_ENV, "")
               ).strip()
    return identity, profile


def ios_entitlements(team_id, bundle_id):
    """the development entitlements dict for a signed device build. Pure - the
    same input always yields the same dict, so the composition is unit-testable
    without codesign."""
    app_identifier = ((team_id + "." + bundle_id) if team_id else bundle_id)
    return {
        "application-identifier": app_identifier,
        "com.apple.developer.team-identifier": team_id,
        "get-task-allow": True,  # development builds allow the debugger to attach
    }


def export_ios(project, engine_build, output_dir, identity, profile):
    """package + codesign a device .app. Requires an arm64-ios (device) player
    build - only the simulator player exists today, so this fails honestly until
    a device player preset lands (see Docs/ios-signing.md). The signing plumbing
    itself (entitlements, embedded profile, codesign order) is in place."""
    if project.native_target():
        fail("project '%s' has a native module - mobile native builds are "
             "future work" % project.name)
    if not os.path.isfile(profile):
        fail("provisioning profile '%s' does not exist" % profile)
    source_app = os.path.join(engine_build, "tools", "player",
                              "OrkigePlayer.app")
    if not os.path.isdir(source_app):
        fail("no device OrkigePlayer.app at '%s' - a signed device install "
             "needs an arm64-ios (device, not simulator) player build, which "
             "is not wired up yet (see Docs/ios-signing.md)" % source_app)
    bundle_id = project.settings.get(
        "export.ios.bundleId", "com.orkitec." + project.id_slug)
    team_id = project.settings.get("export.ios.teamId", "").strip()

    app_dir = os.path.join(output_dir, project.name + ".app")
    if os.path.exists(app_dir):
        shutil.rmtree(app_dir)
    shutil.copytree(source_app, app_dir, symlinks=True)
    staged = stage_project_payload(project,
                                   os.path.join(app_dir, PAYLOAD_DIR_NAME),
                                   "ios")
    write_marker(app_dir)
    log("project payload: %d files" % staged)

    icon_source = orkige_icons.load_square_source(
        orkige_icons.resolve_icon_source(project, log=log))
    icon_files = orkige_icons.make_ios_icons(icon_source, app_dir)
    plist_path = os.path.join(app_dir, "Info.plist")
    with open(plist_path, "rb") as handle:
        info = plistlib.load(handle)
    info["CFBundleIdentifier"] = bundle_id
    info["CFBundleName"] = project.name
    info["CFBundleDisplayName"] = project.name
    info["CFBundleIcons"] = {"CFBundlePrimaryIcon": {
        "CFBundleIconFiles": [name[:-4] for name in icon_files]}}
    info.setdefault("UILaunchScreen", {})
    with open(plist_path, "wb") as handle:
        plistlib.dump(info, handle)

    # embed the provisioning profile + write the entitlements the codesign call
    # binds into the signature
    shutil.copy2(profile, os.path.join(app_dir, "embedded.mobileprovision"))
    entitlements_path = os.path.join(output_dir, "entitlements.plist")
    with open(entitlements_path, "wb") as handle:
        plistlib.dump(ios_entitlements(team_id, bundle_id), handle)

    # sign inside-out: nested dylibs/frameworks first, then the bundle (the same
    # order the macOS self-contain step respects)
    frameworks = os.path.join(app_dir, "Frameworks")
    if os.path.isdir(frameworks):
        for name in sorted(os.listdir(frameworks)):
            run(["codesign", "--force", "--sign", identity,
                 os.path.join(frameworks, name)])
    run(["codesign", "--force", "--sign", identity,
         "--entitlements", entitlements_path, "--generate-entitlement-der",
         app_dir])
    os.remove(entitlements_path)
    log("signed with identity '%s' (team %s)" % (identity, team_id or "?"))
    log("install: xcrun devicectl device install app --device <udid> '%s'"
        % app_dir)
    return app_dir


# --- Android ---------------------------------------------------------------

def export_android(project, engine_build, output_dir):
    if project.native_target():
        fail("project '%s' has a native module ('%s') - native modules are "
             "desktop-only for now, mobile native builds are future work "
             "(the Lua/scene parts of a project export fine without one)"
             % (project.name, project.native_target()))
    native_lib = os.path.join(engine_build, "tools", "player", "libmain.so")
    if not os.path.isfile(native_lib):
        fail("no libmain.so at '%s' - build the android-debug preset first"
             % native_lib)
    payload_dir = os.path.join(output_dir, "payload-staging")
    if os.path.exists(payload_dir):
        shutil.rmtree(payload_dir)
    staged = stage_project_payload(project, payload_dir, "android")
    log("project payload: %d files" % staged)
    package = project.settings.get(
        "export.android.package", "com.orkitec." + project.id_slug)
    if not re.fullmatch(r"[a-zA-Z_][\w]*(\.[a-zA-Z_][\w]*)+", package):
        fail("'%s' is not a valid Android package name "
             "(export.android.package)" % package)

    # app icon (launcher mipmaps) + launch-screen colour: stage a res/ tree the
    # packager compiles with aapt2. The launcher label is the project name.
    res_dir = os.path.join(output_dir, "res-staging")
    if os.path.exists(res_dir):
        shutil.rmtree(res_dir)
    icon_source = orkige_icons.load_square_source(
        orkige_icons.resolve_icon_source(project, log=log))
    orkige_icons.make_android_mipmaps(icon_source, res_dir)
    launch_color = launch_background(project)

    apk_path = os.path.join(output_dir, project.exe_name + ".apk")
    run(["bash",
         os.path.join(REPO_ROOT, "tools", "player", "android",
                      "package_apk.sh"),
         "--project-payload", payload_dir,
         "--package", package,
         "--label", project.name,
         "--res-dir", res_dir,
         "--launch-color", launch_color,
         "--output", apk_path,
         engine_build])
    shutil.rmtree(payload_dir)
    shutil.rmtree(res_dir, ignore_errors=True)
    if not os.path.isfile(apk_path):
        fail("package_apk.sh produced no '%s'" % apk_path)
    log("install: adb install -r '%s'" % apk_path)
    return apk_path


# --- entry point -----------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="package an Orkige project as a distributable app")
    parser.add_argument("--project", required=True,
                        help="project directory (or its .orkproj)")
    parser.add_argument("--platform", required=True,
                        choices=["macos", "ios-simulator", "ios", "android"])
    parser.add_argument("--engine-build", required=True,
                        help="the preset build tree to package from (either "
                             "render flavor; the bundled engine media follows "
                             "the tree's ORKIGE_RENDER_BACKEND) - macos: "
                             "build/macos-debug[-classic] or -release[-classic], "
                             "ios-simulator: build/ios-simulator-debug[-next], "
                             "android: build/android-debug[-next]")
    parser.add_argument("--output",
                        help="output directory (default: "
                             "<project>/builds/<platform>)")
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake",
                        help="cmake executable for native-module builds")
    parser.add_argument("--ninja", default=shutil.which("ninja") or "",
                        help="ninja executable for native-module builds")
    # iOS device signing (--platform ios): machine-local, never committed - the
    # arg wins, else the environment (see resolve_ios_signing)
    parser.add_argument("--signing-identity", default="",
                        help="iOS codesigning identity name/SHA (or env "
                             + IOS_SIGNING_IDENTITY_ENV + ")")
    parser.add_argument("--provisioning-profile", default="",
                        help="path to the .mobileprovision (or env "
                             + IOS_PROVISIONING_PROFILE_ENV + ")")
    args = parser.parse_args()

    project = Project(args.project)
    engine_build = os.path.abspath(args.engine_build)
    if not os.path.isdir(engine_build):
        fail("engine build tree '%s' does not exist" % engine_build)
    output_dir = os.path.abspath(
        args.output or os.path.join(project.root, "builds", args.platform))
    os.makedirs(output_dir, exist_ok=True)
    log("project '%s' -> %s (%s)" % (project.name, output_dir, args.platform))

    if args.platform == "macos":
        artifact = export_macos(project, engine_build, output_dir,
                                args.cmake, args.ninja)
    elif args.platform == "ios-simulator":
        artifact = export_ios_simulator(project, engine_build, output_dir)
    elif args.platform == "ios":
        identity, profile = resolve_ios_signing(
            args.signing_identity, args.provisioning_profile, os.environ)
        if not identity or not profile:
            fail("physical-device iOS export needs a codesigning identity AND "
                 "a provisioning profile (unsigned apps cannot install on "
                 "hardware - the same gate as the editor's Play on an iPhone). "
                 "Set --signing-identity/" + IOS_SIGNING_IDENTITY_ENV + " and "
                 "--provisioning-profile/" + IOS_PROVISIONING_PROFILE_ENV
                 + ", or use --platform ios-simulator. See Docs/ios-signing.md")
        artifact = export_ios(project, engine_build, output_dir,
                              identity, profile)
    else:
        artifact = export_android(project, engine_build, output_dir)

    log("artifact size %s" % human_size(directory_size(artifact)))
    print("orkige_export: OK %s" % artifact, flush=True)


def selftest():
    """cert-free validation of the signing-config logic: the entitlements
    composition and the identity/profile resolution precedence (arg over env).
    The codesign call itself needs a real identity and is left to a machine with
    one; this covers the pure parts that gate it."""
    entitlements = ios_entitlements("ABCDE12345", "com.example.game")
    assert entitlements["application-identifier"] == \
        "ABCDE12345.com.example.game", "app-identifier composition"
    assert entitlements["com.apple.developer.team-identifier"] == "ABCDE12345"
    assert entitlements["get-task-allow"] is True
    # no team id -> the bundle id stands alone (still a valid entitlements dict)
    assert ios_entitlements("", "com.example.game")["application-identifier"] \
        == "com.example.game", "app-identifier without a team id"

    # resolution precedence: arg beats env, env fills a blank arg, both blank
    # stays blank (the gate then refuses)
    ident, profile = resolve_ios_signing("cli-id", "cli.mobileprovision", {})
    assert (ident, profile) == ("cli-id", "cli.mobileprovision"), "args win"
    ident, profile = resolve_ios_signing("", "", {
        IOS_SIGNING_IDENTITY_ENV: "env-id",
        IOS_PROVISIONING_PROFILE_ENV: "env.mobileprovision"})
    assert (ident, profile) == ("env-id", "env.mobileprovision"), "env fallback"
    ident, profile = resolve_ios_signing("cli-id", "", {
        IOS_PROVISIONING_PROFILE_ENV: "env.mobileprovision"})
    assert (ident, profile) == ("cli-id", "env.mobileprovision"), "arg+env mix"
    assert resolve_ios_signing("", "", {}) == ("", ""), "unresolved stays blank"

    # launch-background validation: a good hex passes, a bad/absent one defaults
    class _Stub:
        def __init__(self, value):
            self.settings = {"export.launch.background": value} if value else {}
    assert launch_background(_Stub("#a1b2c3")) == "#a1b2c3", "valid hex kept"
    assert launch_background(_Stub("blue")) == DEFAULT_LAUNCH_BACKGROUND, \
        "malformed hex -> default"
    assert launch_background(_Stub("")) == DEFAULT_LAUNCH_BACKGROUND, \
        "absent -> default"
    print("orkige_export: selftest OK")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest()
    else:
        main()
