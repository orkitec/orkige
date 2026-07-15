#!/usr/bin/env python3
"""Export (package) an Orkige .orkproj project as a distributable app - the
"Build Settings" step of the project ladder (python3 stdlib only, same rules
as the other Util/ generators).

    orkige_export.py --project <dir>
                     --platform macos|ios-simulator|ios|ios-ipa|android|android-aab
                     --engine-build <preset build dir> [--output <dir>]

The `ios-ipa` and `android-aab` platforms are the STORE-SUBMITTABLE layer (a
distribution-signed .ipa and a release-signed Android App Bundle); the others
package device/dev installs. The store paths gate + degrade honestly on this
machine's absent developer credentials (see Docs/store-release.md).

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
                                              Vulkan loader) plus each dylib's
                                              dlopen symlink aliases, rpaths
                                              rewritten to
                                              @executable_path/../Frameworks
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

  ios            packages the arm64-ios (physical-device) OrkigePlayer.app the
                 ios-device-debug / ios-device-release preset built, then
                 code-signs it inside-out with the resolved identity and embeds
                 the provisioning profile. Gated: absent an identity AND a
                 profile it refuses and produces nothing (an unsigned device
                 .app installs on no non-jailbroken device, so there is no
                 --unsigned escape hatch - it would only ever mislead; the
                 device .app in the build tree already exists for anyone who
                 wants to inspect the unsigned bundle). See Docs/ios-signing.md.

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
import tempfile
import xml.etree.ElementTree as ET
import zipfile

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


def stage_config_settings(project_root, settings, dest_dir):
    """copy the manifest-referenced project-config assets (the CONFIG_SETTING_KEYS
    values) into dest_dir, preserving each project-relative path, and return the
    file count staged. A setting may name a single FILE (input.oactions,
    physics.olayers, levels.olevels) or a DIRECTORY (localisation names loc/, a
    tree of one .xlf per language) - both bundle; a missing target warns and is
    skipped."""
    staged = 0
    for setting_key in CONFIG_SETTING_KEYS:
        relative = settings.get(setting_key, "").strip()
        if not relative:
            continue
        source = os.path.join(project_root, relative)
        destination = os.path.join(dest_dir, relative)
        if os.path.isdir(source):
            shutil.copytree(source, destination, dirs_exist_ok=True)
            staged += sum(len(files) for _, _, files in os.walk(destination))
        elif os.path.isfile(source):
            os.makedirs(os.path.dirname(destination) or dest_dir, exist_ok=True)
            shutil.copy2(source, destination)
            staged += 1
        else:
            warn("manifest setting '%s' references '%s' but no such file or "
                 "directory exists - not bundled" % (setting_key, relative))
    return staged


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
    # manifest-referenced project-config assets (input.oactions and its
    # siblings) ride along, preserving their project-relative paths
    staged += stage_config_settings(project.root, project.settings, dest_dir)
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


# the export.orientation Setting - portrait | landscape | auto. PORTRAIT is the
# default (and the value an absent or unrecognised setting degrades to): a mobile
# game is portrait unless it says otherwise, and it keeps the window's initial
# interface orientation deterministic - iOS picks the boot orientation from the
# allowed set by the window aspect, so an unconstrained app boots landscape (the
# desktop-ish window is wider than tall). auto opts back into every orientation.
# It maps to the iOS UISupportedInterfaceOrientations array and the Android
# activity's android:screenOrientation. The runtime reads the SAME setting to
# constrain the window orientation (SDL hint), so the OS-level lock and the
# render surface agree.
IOS_ORIENTATIONS = {
    "portrait": ["UIInterfaceOrientationPortrait"],
    "landscape": ["UIInterfaceOrientationLandscapeLeft",
                  "UIInterfaceOrientationLandscapeRight"],
    "auto": ["UIInterfaceOrientationPortrait",
             "UIInterfaceOrientationLandscapeLeft",
             "UIInterfaceOrientationLandscapeRight"],
}
ANDROID_SCREEN_ORIENTATION = {
    "portrait": "sensorPortrait",
    "landscape": "sensorLandscape",
    "auto": "unspecified",
}


DEFAULT_ORIENTATION = "portrait"


def orientation_setting(settings):
    """the normalised export.orientation - portrait | landscape | auto. portrait
    is the default; an unrecognised value degrades to it with a warning."""
    value = (settings.get("export.orientation", "") or "").strip().lower()
    if value in IOS_ORIENTATIONS:
        return value
    if value:
        warn("export.orientation '%s' is not portrait/landscape/auto - using "
             "%s" % (value, DEFAULT_ORIENTATION))
    return DEFAULT_ORIENTATION


def apply_ios_orientation(info, settings):
    """write UISupportedInterfaceOrientations onto an Info.plist dict per the
    project's export.orientation. Shared by the simulator + device plist rewrites
    (and exercised directly by the selftest)."""
    info["UISupportedInterfaceOrientations"] = \
        IOS_ORIENTATIONS[orientation_setting(settings)]
    return info


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


def engine_font_dir():
    """the engine-default font directory committed to the tree (Nunito, SIL
    OFL) - registered as a resource location at runtime so a project's .ogui
    can reference the font by name. Empty when the dir is absent."""
    fonts = os.path.join(REPO_ROOT, "orkige_engine", "media", "fonts")
    return fonts if os.path.isdir(fonts) else ""


def engine_water_dir():
    """the engine water media directory committed to the tree (the shared water
    plane mesh + tiling water normal map WaterComponent references) - registered
    as a resource location at runtime like the font dir. Empty when absent."""
    water = os.path.join(REPO_ROOT, "orkige_engine", "media", "water")
    return water if os.path.isdir(water) else ""


def ogre_media_dir(build_dir):
    """the classic flavor's RTSS shader-library media (Main + RTShaderLib)"""
    triplet = vcpkg_triplet_dir(build_dir)
    if not triplet:
        return ""
    media = os.path.join(triplet, "share", "ogre", "Media")
    return media if os.path.isdir(media) else ""


def ogre_next_media_dir(build_dir):
    """the Ogre-Next flavor's media root (contains Hlms/ - the shader templates
    the runtime registers via Engine::setHlmsMediaDir - and Atmosphere/ - the
    sky material media the next backend auto-discovers as a sibling of Hlms)"""
    triplet = vcpkg_triplet_dir(build_dir)
    if not triplet:
        return ""
    media = os.path.join(triplet, "share", "ogre-next", "Media")
    return media if os.path.isdir(media) else ""


def ogre_next_media_subdirs(media_dir):
    """the Ogre-Next Media subdirs to bundle: Hlms (mandatory - materials
    don't work without it) plus Atmosphere (the sky material media the next
    backend's registerAtmosphereMedia looks up as a sibling of Hlms) when the
    installed vcpkg port ships it - an older port pin may not, and the
    runtime degrades that honestly (no sky, flat fog colour instead), so
    bundling stays optional here too rather than a hard failure."""
    subdirs = ["Hlms"]
    if os.path.isdir(os.path.join(media_dir, "Atmosphere")):
        subdirs.append("Atmosphere")
    return tuple(subdirs)


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


def macos_dylib_aliases(source_dir, dylib_name):
    """the symlink leaf names in source_dir that resolve to dylib_name -
    the dlopen aliases of a versioned dylib (e.g. libvulkan.dylib and
    libvulkan.1.dylib -> libvulkan.1.4.350.dylib). A leaf-name dlopen (the
    Vulkan loader probe in the render system) asks for the unversioned
    names, so a self-contained bundle must carry them beside the real file."""
    aliases = []
    target = os.path.realpath(os.path.join(source_dir, dylib_name))
    for entry in sorted(os.listdir(source_dir)):
        path = os.path.join(source_dir, entry)
        if entry != dylib_name and os.path.islink(path) \
                and os.path.realpath(path) == target:
            aliases.append(entry)
    return aliases


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
        # recreate the source dir's symlink aliases beside the copy: dyld
        # resolves the direct @rpath dependency by its versioned name, but a
        # leaf-name dlopen (the Vulkan loader probe) asks for the unversioned
        # aliases, which live as symlinks in the vcpkg lib dir
        for alias in macos_dylib_aliases(os.path.dirname(resolved),
                                         os.path.basename(resolved)):
            alias_path = os.path.join(frameworks_dir, alias)
            if os.path.lexists(alias_path):
                os.remove(alias_path)
            os.symlink(os.path.basename(resolved), alias_path)
            log("aliased dylib %s -> %s"
                % (alias, os.path.basename(resolved)))
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
    # RTShaderLib) or the Ogre-Next Hlms shader templates + Atmosphere sky
    # material media (Hlms, Atmosphere). The runtimes resolve <Resources>/Media
    # at boot (PlayerBundle::resolveMediaDirectory) and register it - RTSS
    # locations on classic, Engine::setHlmsMediaDir on next (which also drives
    # the next backend's registerAtmosphereMedia, looking for Atmosphere/ as a
    # sibling of Hlms/) - so the bundle carries no vcpkg or source-tree path.
    media_subdirs = (ogre_next_media_subdirs(media_dir) if flavor == "next"
                     else ("Main", "RTShaderLib"))
    for media_subdir in media_subdirs:
        shutil.copytree(os.path.join(media_dir, media_subdir),
                        os.path.join(resources, "Media", media_subdir))
    # the engine-default font (Nunito, SIL OFL) rides in the same bundled Media
    # dir so a project referencing it by name ships self-contained
    if engine_font_dir():
        shutil.copytree(engine_font_dir(),
                        os.path.join(resources, "Media", "fonts"),
                        dirs_exist_ok=True)
    # the engine water media (plane mesh + tiling normal) rides alongside so a
    # scene's WaterComponent ships self-contained
    if engine_water_dir():
        shutil.copytree(engine_water_dir(),
                        os.path.join(resources, "Media", "water"),
                        dirs_exist_ok=True)

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
    # the engine-default font (Nunito, SIL OFL) alongside the bundled engine
    # media so a project referencing it by name ships self-contained
    if engine_font_dir():
        shutil.copytree(engine_font_dir(),
                        os.path.join(app_dir, "Media", "fonts"),
                        dirs_exist_ok=True)
    # the engine water media (plane mesh + tiling normal) so a scene's
    # WaterComponent ships self-contained
    if engine_water_dir():
        shutil.copytree(engine_water_dir(),
                        os.path.join(app_dir, "Media", "water"),
                        dirs_exist_ok=True)
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
    apply_ios_orientation(info, project.settings)
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
# distribution (App Store) signing: a SEPARATE identity + profile from the
# development pair, also machine-local and never committed.
IOS_DISTRIBUTION_IDENTITY_ENV = "ORKIGE_IOS_DISTRIBUTION_IDENTITY"
IOS_DISTRIBUTION_PROFILE_ENV = "ORKIGE_IOS_DISTRIBUTION_PROFILE"


def resolve_ios_signing(identity_arg, profile_arg, environ):
    """resolve (identity, profile) for a signed iOS DEVELOPMENT build: the CLI
    arg wins, else the environment. Returns a (identity, profile) pair of
    strings, each empty when unresolved (pure - no filesystem/subprocess, so it
    is unit-testable without a certificate)."""
    identity = (identity_arg or environ.get(IOS_SIGNING_IDENTITY_ENV, "")
                ).strip()
    profile = (profile_arg or environ.get(IOS_PROVISIONING_PROFILE_ENV, "")
               ).strip()
    return identity, profile


def resolve_ios_distribution_signing(identity_arg, profile_arg, environ):
    """resolve (identity, profile) for a DISTRIBUTION (App Store) build - the
    distribution certificate + App Store provisioning profile. Same arg-over-env
    precedence as the development pair; pure, so unit-testable without a cert."""
    identity = (identity_arg or environ.get(IOS_DISTRIBUTION_IDENTITY_ENV, "")
                ).strip()
    profile = (profile_arg or environ.get(IOS_DISTRIBUTION_PROFILE_ENV, "")
               ).strip()
    return identity, profile


def ios_entitlements(team_id, bundle_id, for_distribution=False):
    """the entitlements dict for a signed iOS build. Development builds set
    get-task-allow (the debugger attaches); a DISTRIBUTION build clears it (the
    App Store rejects get-task-allow=true). Pure - the same input always yields
    the same dict, so the composition is unit-testable without codesign."""
    app_identifier = ((team_id + "." + bundle_id) if team_id else bundle_id)
    return {
        "application-identifier": app_identifier,
        "com.apple.developer.team-identifier": team_id,
        # development attaches a debugger; distribution must NOT (App Store gate)
        "get-task-allow": not for_distribution,
    }


def ipa_arcname(app_dir, file_path):
    """the archive name of a bundle file inside an .ipa: Payload/<App>.app/...
    (an .ipa is a zip whose single top-level dir is Payload/). Pure, so the
    layout is unit-testable without a real signed app."""
    return os.path.join("Payload", os.path.basename(app_dir),
                        os.path.relpath(file_path, app_dir))


def package_ipa(app_dir, ipa_path):
    """zip a (signed) .app into an .ipa under Payload/ - the App Store upload
    container. Regular files only; the iOS device bundle is flat (no macOS-style
    version symlinks)."""
    if os.path.exists(ipa_path):
        os.remove(ipa_path)
    with zipfile.ZipFile(ipa_path, "w", zipfile.ZIP_DEFLATED) as ipa:
        for parent, _, files in os.walk(app_dir):
            for name in files:
                full = os.path.join(parent, name)
                if os.path.islink(full):
                    continue
                ipa.write(full, ipa_arcname(app_dir, full))
    return ipa_path


def require_ios_device_app(engine_build):
    """the arm64-ios (device, not simulator) OrkigePlayer.app the ios-device-*
    presets build; fail()s honestly when absent."""
    source_app = os.path.join(engine_build, "tools", "player",
                              "OrkigePlayer.app")
    if not os.path.isdir(source_app):
        fail("no device OrkigePlayer.app at '%s' - a signed device/store build "
             "needs an arm64-ios (device, not simulator) player build; "
             "configure + build the ios-device-debug (or -release) preset "
             "first (see Docs/ios-signing.md)" % source_app)
    return source_app


def build_signed_ios_bundle(project, source_app, output_dir, identity, profile,
                            for_distribution):
    """assemble the project into the device player bundle and codesign it
    inside-out with the resolved identity/profile. Shared by the development
    device export and the distribution .ipa export - the ONLY difference is the
    entitlements' get-task-allow (cleared for distribution). Returns the signed
    .app dir."""
    if not os.path.isfile(profile):
        fail("provisioning profile '%s' does not exist" % profile)
    bundle_id = project.settings.get(
        "export.ios.bundleId", "com.orkitec." + project.id_slug)
    team_id = project.settings.get("export.ios.teamId", "").strip()

    app_dir = os.path.join(output_dir, project.name + ".app")
    if os.path.exists(app_dir):
        shutil.rmtree(app_dir)
    shutil.copytree(source_app, app_dir, symlinks=True)
    # the engine-default font (Nunito, SIL OFL) alongside the bundled engine
    # media so a project referencing it by name ships self-contained
    if engine_font_dir():
        shutil.copytree(engine_font_dir(),
                        os.path.join(app_dir, "Media", "fonts"),
                        dirs_exist_ok=True)
    # the engine water media (plane mesh + tiling normal) so a scene's
    # WaterComponent ships self-contained
    if engine_water_dir():
        shutil.copytree(engine_water_dir(),
                        os.path.join(app_dir, "Media", "water"),
                        dirs_exist_ok=True)
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
    apply_ios_orientation(info, project.settings)
    with open(plist_path, "wb") as handle:
        plistlib.dump(info, handle)

    # embed the provisioning profile + write the entitlements the codesign call
    # binds into the signature
    shutil.copy2(profile, os.path.join(app_dir, "embedded.mobileprovision"))
    entitlements_path = os.path.join(output_dir, "entitlements.plist")
    with open(entitlements_path, "wb") as handle:
        plistlib.dump(ios_entitlements(team_id, bundle_id, for_distribution),
                      handle)

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
    log("signed with identity '%s' (team %s%s)" % (identity, team_id or "?",
        ", distribution" if for_distribution else ", development"))
    return app_dir


def export_ios(project, engine_build, output_dir, identity, profile):
    """package + development-codesign a device .app for a direct device install.
    Requires an arm64-ios (device, not simulator) player build; the owner-side
    setup is in Docs/ios-signing.md."""
    if project.native_target():
        fail("project '%s' has a native module - mobile native builds are "
             "future work" % project.name)
    source_app = require_ios_device_app(engine_build)
    app_dir = build_signed_ios_bundle(project, source_app, output_dir,
                                      identity, profile, for_distribution=False)
    log("install: xcrun devicectl device install app --device <udid> '%s'"
        % app_dir)
    return app_dir


def export_ios_ipa(project, engine_build, output_dir, identity, profile):
    """package + DISTRIBUTION-codesign a device .app and wrap it into an .ipa -
    the App Store Connect upload container. Requires a distribution certificate
    + an App Store provisioning profile (this machine has neither, so the caller
    gates on their presence); the owner-side setup + upload step are in
    Docs/store-release.md."""
    if project.native_target():
        fail("project '%s' has a native module - mobile native builds are "
             "future work" % project.name)
    source_app = require_ios_device_app(engine_build)
    app_dir = build_signed_ios_bundle(project, source_app, output_dir,
                                      identity, profile, for_distribution=True)
    ipa_path = os.path.join(output_dir, project.exe_name + ".ipa")
    package_ipa(app_dir, ipa_path)
    log("artifact %s" % ipa_path)
    log("upload: xcrun altool --upload-package '%s' --type ios "
        "--apple-id <app-id> --bundle-id <bundle-id> "
        "--apiKey <key> --apiIssuer <issuer> (see Docs/store-release.md)"
        % ipa_path)
    return ipa_path


# --- Android ---------------------------------------------------------------
# Release-bundle signing config, all machine-local and never committed: the
# keystore + its passwords come from CLI args or the environment (like the iOS
# identity/profile). bundletool is a separate download (NOT part of the SDK
# build-tools), also resolved from arg/env/PATH. Google Play's current
# target-SDK floor for new uploads.
ANDROID_KEYSTORE_ENV = "ORKIGE_ANDROID_KEYSTORE"
ANDROID_KEY_ALIAS_ENV = "ORKIGE_ANDROID_KEY_ALIAS"
ANDROID_KEYSTORE_PASS_ENV = "ORKIGE_ANDROID_KEYSTORE_PASS"
ANDROID_KEY_PASS_ENV = "ORKIGE_ANDROID_KEY_PASS"
BUNDLETOOL_ENV = "ORKIGE_BUNDLETOOL"
PLAY_TARGET_SDK_FLOOR = 35


def android_version(settings):
    """(versionCode:int, versionName:str) for a release bundle from the manifest
    settings export.android.versionCode / .versionName. versionCode must be a
    positive integer - Google Play requires a STRICTLY INCREASING integer across
    uploads (see Docs/store-release.md). Pure (no I/O), so it is unit-testable;
    raises ValueError on a malformed versionCode - the caller turns that into a
    fail()."""
    code_text = (settings.get("export.android.versionCode", "") or "").strip()
    code_text = code_text or "1"
    if not re.fullmatch(r"[0-9]+", code_text) or int(code_text) < 1:
        raise ValueError(
            "export.android.versionCode '%s' is not a positive integer "
            "(Google Play requires a strictly increasing integer version code "
            "- see Docs/store-release.md)" % code_text)
    name = (settings.get("export.android.versionName", "") or "").strip() or "1.0"
    return int(code_text), name


def resolve_android_keystore(keystore_arg, alias_arg, environ):
    """resolve the release keystore + alias for a signed .aab: the CLI arg wins,
    else the environment. Passwords are NOT returned - they stay in the
    environment (ORKIGE_ANDROID_KEYSTORE_PASS / _KEY_PASS) and are read straight
    by jarsigner via -storepass:env, so no secret ever reaches a command line.
    Returns (keystore, alias, has_store_pass) - keystore/alias empty when
    unresolved. Pure (only reads the passed environ), so unit-testable without a
    keystore."""
    keystore = (keystore_arg or environ.get(ANDROID_KEYSTORE_ENV, "")).strip()
    alias = (alias_arg or environ.get(ANDROID_KEY_ALIAS_ENV, "")).strip()
    has_store_pass = bool((environ.get(ANDROID_KEYSTORE_PASS_ENV, "") or "").strip())
    return keystore, alias, has_store_pass


def resolve_bundletool(bundletool_arg, environ, which=shutil.which):
    """resolve the bundletool jar: the CLI arg wins, else ORKIGE_BUNDLETOOL, else
    a `bundletool` launcher on PATH. Returns the path (or launcher name) or an
    empty string when unresolved. `which` is injectable so the arg/env
    precedence is unit-testable without bundletool installed."""
    explicit = (bundletool_arg or environ.get(BUNDLETOOL_ENV, "")).strip()
    if explicit:
        return explicit
    return which("bundletool") or ""


def stage_android_res(project, output_dir):
    """stage the launcher-icon res/ tree + resolve the launch-screen colour,
    shared by the APK and the App Bundle paths. Returns (res_dir, launch_color)."""
    res_dir = os.path.join(output_dir, "res-staging")
    if os.path.exists(res_dir):
        shutil.rmtree(res_dir)
    icon_source = orkige_icons.load_square_source(
        orkige_icons.resolve_icon_source(project, log=log))
    orkige_icons.make_android_mipmaps(icon_source, res_dir)
    return res_dir, launch_background(project)


def android_package_name(project):
    """the validated Android package name from export.android.package (default
    com.orkitec.<slug>); fail()s on a malformed value."""
    package = project.settings.get(
        "export.android.package", "com.orkitec." + project.id_slug)
    if not re.fullmatch(r"[a-zA-Z_][\w]*(\.[a-zA-Z_][\w]*)+", package):
        fail("'%s' is not a valid Android package name "
             "(export.android.package)" % package)
    return package


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
    package = android_package_name(project)

    # app icon (launcher mipmaps) + launch-screen colour: stage a res/ tree the
    # packager compiles with aapt2. The launcher label is the project name.
    res_dir, launch_color = stage_android_res(project, output_dir)

    apk_path = os.path.join(output_dir, project.exe_name + ".apk")
    command = ["bash",
               os.path.join(REPO_ROOT, "tools", "player", "android",
                            "package_apk.sh"),
               "--project-payload", payload_dir,
               "--package", package,
               "--label", project.name,
               "--res-dir", res_dir,
               "--launch-color", launch_color,
               "--output", apk_path]
    # only a non-auto lock injects android:screenOrientation; auto keeps the
    # template's default (unspecified) so the manifest stays byte-identical
    orientation = orientation_setting(project.settings)
    if orientation != "auto":
        command += ["--orientation", ANDROID_SCREEN_ORIENTATION[orientation]]
    command.append(engine_build)
    run(command)
    shutil.rmtree(payload_dir)
    shutil.rmtree(res_dir, ignore_errors=True)
    if not os.path.isfile(apk_path):
        fail("package_apk.sh produced no '%s'" % apk_path)
    log("install: adb install -r '%s'" % apk_path)
    return apk_path


def export_android_bundle(project, engine_build, output_dir, args, environ):
    """package a RELEASE Android App Bundle (.aab) - the Google-Play-submittable
    artifact. Drives tools/player/android/build_aab.sh against the engine tree
    (a build/android-release tree is preferred for an optimized libmain.so; a
    Debug tree packages too, with a warning). Two modes:

      - module-only (args.aab_unsigned_module): produce ONLY the proto bundle
        module (no bundletool, no keystore) - the structural / CI slice.
      - signed (default): require a resolvable bundletool jar AND a release
        keystore, then build-bundle + jarsigner. Absent either, it REFUSES and
        produces nothing (the honest gate, like the iOS device path).
    """
    if project.native_target():
        fail("project '%s' has a native module ('%s') - native modules are "
             "desktop-only for now, mobile native builds are future work"
             % (project.name, project.native_target()))
    native_lib = os.path.join(engine_build, "tools", "player", "libmain.so")
    if not os.path.isfile(native_lib):
        fail("no libmain.so at '%s' - build the android-release (or "
             "android-debug) preset first" % native_lib)
    if read_cmake_cache(engine_build, "CMAKE_BUILD_TYPE") != "Release":
        warn("engine tree '%s' is a %s build - the release bundle will carry a "
             "non-optimized libmain.so; build the android-release preset for a "
             "shippable bundle" % (engine_build,
             read_cmake_cache(engine_build, "CMAKE_BUILD_TYPE") or "?"))

    package = android_package_name(project)
    try:
        version_code, version_name = android_version(project.settings)
    except ValueError as error:
        fail(str(error))
    log("release bundle: versionCode %d, versionName %s"
        % (version_code, version_name))

    module_only = bool(getattr(args, "aab_unsigned_module", False))
    keystore, alias, has_store_pass = resolve_android_keystore(
        args.android_keystore, args.android_key_alias, environ)
    bundletool = resolve_bundletool(args.bundletool, environ)
    if not module_only:
        # the honest gate: a signed, submittable .aab needs both, so refuse and
        # produce nothing rather than a half-artifact (mirrors the iOS gate)
        missing = []
        if not bundletool:
            missing.append("a bundletool jar (--bundletool / " + BUNDLETOOL_ENV
                           + " / a `bundletool` on PATH)")
        if not keystore:
            missing.append("a release keystore (--android-keystore / "
                           + ANDROID_KEYSTORE_ENV + ")")
        if keystore and not alias:
            missing.append("a key alias (--android-key-alias / "
                           + ANDROID_KEY_ALIAS_ENV + ")")
        if keystore and not has_store_pass:
            missing.append("the keystore password (" + ANDROID_KEYSTORE_PASS_ENV
                           + ")")
        if missing:
            fail("a signed Android App Bundle needs " + "; ".join(missing)
                 + ". See Docs/store-release.md for the one-time setup, or pass "
                 "--aab-unsigned-module to build just the unsigned bundle "
                 "module for inspection/CI.")

    payload_dir = os.path.join(output_dir, "payload-staging")
    if os.path.exists(payload_dir):
        shutil.rmtree(payload_dir)
    staged = stage_project_payload(project, payload_dir, "android")
    log("project payload: %d files" % staged)
    res_dir, launch_color = stage_android_res(project, output_dir)

    if module_only:
        artifact = os.path.join(output_dir, project.exe_name + ".aab.module.zip")
    else:
        artifact = os.path.join(output_dir, project.exe_name + ".aab")
    command = ["bash",
               os.path.join(REPO_ROOT, "tools", "player", "android",
                            "build_aab.sh"),
               "--project-payload", payload_dir,
               "--package", package,
               "--label", project.name,
               "--res-dir", res_dir,
               "--launch-color", launch_color,
               "--version-code", str(version_code),
               "--version-name", version_name,
               "--output", artifact]
    # only a non-auto lock injects android:screenOrientation (see export_android)
    orientation = orientation_setting(project.settings)
    if orientation != "auto":
        command += ["--orientation", ANDROID_SCREEN_ORIENTATION[orientation]]
    command.append(engine_build)
    if module_only:
        command.append("--module-only")
    else:
        command += ["--keystore", keystore, "--key-alias", alias,
                    "--bundletool", bundletool]
    run(command)
    shutil.rmtree(payload_dir, ignore_errors=True)
    shutil.rmtree(res_dir, ignore_errors=True)
    if not os.path.isfile(artifact):
        fail("build_aab.sh produced no '%s'" % artifact)
    if module_only:
        log("unsigned bundle module (NOT submittable) - see Docs/store-release.md")
    else:
        log("upload: submit '%s' to Google Play (see Docs/store-release.md)"
            % artifact)
    return artifact


# --- entry point -----------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="package an Orkige project as a distributable app")
    parser.add_argument("--project", required=True,
                        help="project directory (or its .orkproj)")
    parser.add_argument("--platform", required=True,
                        choices=["macos", "ios-simulator", "ios", "ios-ipa",
                                 "android", "android-aab"])
    parser.add_argument("--engine-build", required=True,
                        help="the preset build tree to package from (either "
                             "render flavor; the bundled engine media follows "
                             "the tree's ORKIGE_RENDER_BACKEND) - macos: "
                             "build/macos-debug[-classic] or -release[-classic], "
                             "ios-simulator: build/ios-simulator-debug[-next], "
                             "ios: build/ios-device-debug[-release], "
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
    # iOS distribution signing (--platform ios-ipa): the App Store cert + profile
    parser.add_argument("--distribution-identity", default="",
                        help="iOS distribution codesigning identity (or env "
                             + IOS_DISTRIBUTION_IDENTITY_ENV + ")")
    parser.add_argument("--distribution-profile", default="",
                        help="path to the App Store .mobileprovision (or env "
                             + IOS_DISTRIBUTION_PROFILE_ENV + ")")
    # Android release bundle signing (--platform android-aab): machine-local
    parser.add_argument("--android-keystore", default="",
                        help="release keystore path (or env "
                             + ANDROID_KEYSTORE_ENV + "); passwords stay in "
                             + ANDROID_KEYSTORE_PASS_ENV + "/"
                             + ANDROID_KEY_PASS_ENV)
    parser.add_argument("--android-key-alias", default="",
                        help="release key alias (or env " + ANDROID_KEY_ALIAS_ENV
                             + ")")
    parser.add_argument("--bundletool", default="",
                        help="bundletool jar path (or env " + BUNDLETOOL_ENV
                             + ", or a `bundletool` on PATH)")
    parser.add_argument("--aab-unsigned-module", action="store_true",
                        help="android-aab: build only the unsigned proto bundle "
                             "module (no bundletool/keystore) - for "
                             "inspection/CI, NOT submittable")
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
    elif args.platform == "ios-ipa":
        identity, profile = resolve_ios_distribution_signing(
            args.distribution_identity, args.distribution_profile, os.environ)
        if not identity or not profile:
            fail("App Store .ipa export needs a DISTRIBUTION codesigning "
                 "identity AND an App Store provisioning profile. Set "
                 "--distribution-identity/" + IOS_DISTRIBUTION_IDENTITY_ENV
                 + " and --distribution-profile/" + IOS_DISTRIBUTION_PROFILE_ENV
                 + ". See Docs/store-release.md")
        artifact = export_ios_ipa(project, engine_build, output_dir,
                                  identity, profile)
    elif args.platform == "android-aab":
        artifact = export_android_bundle(project, engine_build, output_dir,
                                         args, os.environ)
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
    # distribution entitlements clear get-task-allow (the App Store rejects it)
    dist = ios_entitlements("ABCDE12345", "com.example.game",
                            for_distribution=True)
    assert dist["get-task-allow"] is False, "distribution clears get-task-allow"
    assert dist["application-identifier"] == "ABCDE12345.com.example.game"

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
    # malformed (whitespace-only) values are stripped to blank, so the device
    # export gate treats them as absent and refuses rather than shelling out a
    # broken codesign identity/profile
    assert resolve_ios_signing("   ", "\t", {}) == ("", ""), \
        "whitespace-only args -> blank"
    assert resolve_ios_signing("", "", {
        IOS_SIGNING_IDENTITY_ENV: "  ",
        IOS_PROVISIONING_PROFILE_ENV: "  "}) == ("", ""), \
        "whitespace-only env -> blank"

    # distribution signing resolves off its OWN env pair (independent of the
    # development one), same arg-over-env precedence
    ident, profile = resolve_ios_distribution_signing("", "", {
        IOS_DISTRIBUTION_IDENTITY_ENV: "dist-id",
        IOS_DISTRIBUTION_PROFILE_ENV: "AppStore.mobileprovision"})
    assert (ident, profile) == ("dist-id", "AppStore.mobileprovision"), \
        "distribution env"
    assert resolve_ios_distribution_signing("cli", "", {}) == ("cli", ""), \
        "distribution arg wins"
    assert resolve_ios_distribution_signing("", "", {
        IOS_SIGNING_IDENTITY_ENV: "dev-id"}) == ("", ""), \
        "distribution ignores the development env"

    # .ipa layout: every bundle file lands under Payload/<App>.app/...
    assert ipa_arcname("/out/MyGame.app", "/out/MyGame.app/Info.plist") == \
        os.path.join("Payload", "MyGame.app", "Info.plist"), "ipa top-level"
    assert ipa_arcname("/out/MyGame.app",
                       "/out/MyGame.app/Frameworks/libx.dylib") == \
        os.path.join("Payload", "MyGame.app", "Frameworks", "libx.dylib"), \
        "ipa nested"

    # Android release version: a positive integer versionCode passes; a
    # missing one defaults to 1; a non-integer or non-positive one raises
    assert android_version({"export.android.versionCode": "7",
                            "export.android.versionName": "1.2.3"}) == \
        (7, "1.2.3"), "explicit version"
    assert android_version({}) == (1, "1.0"), "version defaults"
    # a whitespace-only versionCode is treated as absent -> default 1
    assert android_version({"export.android.versionCode": "  "}) == (1, "1.0"), \
        "blank versionCode defaults"
    for bad in ("0", "-1", "1.0", "v3", "abc"):
        try:
            android_version({"export.android.versionCode": bad})
            assert False, "versionCode '%s' should have raised" % bad
        except ValueError:
            pass

    # Android keystore resolution: arg over env, passwords stay in the env
    ks, alias, has_pass = resolve_android_keystore("cli.jks", "cliAlias", {
        ANDROID_KEYSTORE_ENV: "env.jks", ANDROID_KEY_ALIAS_ENV: "envAlias",
        ANDROID_KEYSTORE_PASS_ENV: "secret"})
    assert (ks, alias, has_pass) == ("cli.jks", "cliAlias", True), \
        "keystore args win, store pass detected"
    ks, alias, has_pass = resolve_android_keystore("", "", {
        ANDROID_KEYSTORE_ENV: "env.jks", ANDROID_KEY_ALIAS_ENV: "envAlias"})
    assert (ks, alias, has_pass) == ("env.jks", "envAlias", False), \
        "keystore env fallback, no store pass"
    assert resolve_android_keystore("", "", {}) == ("", "", False), \
        "keystore unresolved stays blank"

    # bundletool resolution: arg > env > a `which` hit; the `which` is injected
    assert resolve_bundletool("cli.jar", {}, which=lambda _: None) == "cli.jar", \
        "bundletool arg wins"
    assert resolve_bundletool("", {BUNDLETOOL_ENV: "env.jar"},
                              which=lambda _: None) == "env.jar", \
        "bundletool env fallback"
    assert resolve_bundletool("", {}, which=lambda _: "/opt/bin/bundletool") == \
        "/opt/bin/bundletool", "bundletool from PATH"
    assert resolve_bundletool("", {}, which=lambda _: None) == "", \
        "bundletool unresolved stays blank"

    # launch-background validation: a good hex passes, a bad/absent one defaults
    class _Stub:
        def __init__(self, value):
            self.settings = {"export.launch.background": value} if value else {}
    assert launch_background(_Stub("#a1b2c3")) == "#a1b2c3", "valid hex kept"
    assert launch_background(_Stub("blue")) == DEFAULT_LAUNCH_BACKGROUND, \
        "malformed hex -> default"
    assert launch_background(_Stub("")) == DEFAULT_LAUNCH_BACKGROUND, \
        "absent -> default"

    # export.orientation -> the iOS UISupportedInterfaceOrientations array and
    # the Android android:screenOrientation. portrait is the default and the
    # degrade target; the platform maps are the exact values the plist/manifest
    # rewrites write.
    assert orientation_setting({}) == "portrait", \
        "absent orientation -> portrait (mobile default)"
    assert orientation_setting({"export.orientation": "auto"}) == "auto", \
        "explicit auto opts into all orientations"
    assert orientation_setting({"export.orientation": "Portrait"}) == "portrait", \
        "case-insensitive"
    assert orientation_setting({"export.orientation": " landscape "}) == "landscape", \
        "trimmed"
    assert orientation_setting({"export.orientation": "sideways"}) == "portrait", \
        "unknown -> portrait default"
    assert IOS_ORIENTATIONS["portrait"] == ["UIInterfaceOrientationPortrait"], \
        "portrait plist"
    assert IOS_ORIENTATIONS["landscape"] == [
        "UIInterfaceOrientationLandscapeLeft",
        "UIInterfaceOrientationLandscapeRight"], "landscape plist"
    assert len(IOS_ORIENTATIONS["auto"]) == 3, "auto plist keeps all three"
    assert ANDROID_SCREEN_ORIENTATION == {
        "portrait": "sensorPortrait", "landscape": "sensorLandscape",
        "auto": "unspecified"}, "android screenOrientation map"
    # the actual iOS plist rewrite (the exact helper both export paths call),
    # through a plistlib round-trip so the array survives serialization
    for value, expected in (("portrait", IOS_ORIENTATIONS["portrait"]),
                            ("landscape", IOS_ORIENTATIONS["landscape"]),
                            ("auto", IOS_ORIENTATIONS["auto"])):
        info = apply_ios_orientation({"UILaunchScreen": {}},
                                     {"export.orientation": value})
        reloaded = plistlib.loads(plistlib.dumps(info))
        assert reloaded["UISupportedInterfaceOrientations"] == expected, \
            "ios plist orientation '%s'" % value
        assert reloaded["UILaunchScreen"] == {}, "orientation keeps other keys"
    # the Android manifest injection the packagers apply (the same portable sed),
    # asserted well-formed with screenOrientation ahead of configChanges
    manifest_template = os.path.join(
        REPO_ROOT, "tools", "player", "android", "AndroidManifest.xml")
    injected = subprocess.run(
        ["sed", "-e", "s|android:configChanges=|android:screenOrientation="
         "\"sensorPortrait\" android:configChanges=|", manifest_template],
        capture_output=True, text=True, check=True).stdout
    assert 'android:screenOrientation="sensorPortrait"' in injected, \
        "android manifest injection"
    import xml.dom.minidom
    xml.dom.minidom.parseString(injected)  # raises if malformed

    # config-setting staging: a FILE setting copies one file preserving its
    # relative path; a DIRECTORY setting (localisation names loc/) copies the
    # whole tree; a dangling setting is skipped, not fatal
    with tempfile.TemporaryDirectory() as work:
        project_root = os.path.join(work, "proj")
        loc_dir = os.path.join(project_root, "loc")
        os.makedirs(loc_dir)
        for name in ("en.xlf", "de.xlf", "en-XA.xlf"):
            with open(os.path.join(loc_dir, name), "w") as handle:
                handle.write("<xliff/>")
        with open(os.path.join(project_root, "input.oactions"), "w") as handle:
            handle.write("actions")
        settings = {
            "localisation": "loc",           # a directory setting
            "input.actions": "input.oactions",  # a file setting
            "levels": "does_not_exist.olevels"  # a dangling setting
        }
        dest = os.path.join(work, "staged")
        os.makedirs(dest)
        count = stage_config_settings(project_root, settings, dest)
        assert count == 4, "3 .xlf files + 1 .oactions staged (got %d)" % count
        for name in ("en.xlf", "de.xlf", "en-XA.xlf"):
            assert os.path.isfile(os.path.join(dest, "loc", name)), \
                "localisation directory bundled (%s)" % name
        assert os.path.isfile(os.path.join(dest, "input.oactions")), \
            "file setting bundled"
        assert not os.path.exists(os.path.join(dest, "does_not_exist.olevels")), \
            "dangling setting skipped, not staged"

    # next-flavor Media subdirs to bundle: Hlms is always included; Atmosphere
    # (the sky material media) rides along only when the installed vcpkg port
    # actually ships it - an older port pin degrades honestly, not fatally
    with tempfile.TemporaryDirectory() as work:
        media_with_sky = os.path.join(work, "with_sky")
        os.makedirs(os.path.join(media_with_sky, "Hlms"))
        os.makedirs(os.path.join(media_with_sky, "Atmosphere"))
        assert ogre_next_media_subdirs(media_with_sky) == ("Hlms", "Atmosphere"), \
            "Atmosphere bundled when present"

        media_without_sky = os.path.join(work, "without_sky")
        os.makedirs(os.path.join(media_without_sky, "Hlms"))
        assert ogre_next_media_subdirs(media_without_sky) == ("Hlms",), \
            "Atmosphere skipped when absent (older vcpkg port pin)"

    # dlopen symlink aliases of a versioned dylib: symlinks resolving to the
    # real file are found, the file itself and unrelated entries are not
    with tempfile.TemporaryDirectory() as work:
        real = os.path.join(work, "libfoo.1.2.3.dylib")
        with open(real, "w") as handle:
            handle.write("x")
        os.symlink("libfoo.1.2.3.dylib", os.path.join(work, "libfoo.dylib"))
        os.symlink("libfoo.1.2.3.dylib", os.path.join(work, "libfoo.1.dylib"))
        with open(os.path.join(work, "libbar.dylib"), "w") as handle:
            handle.write("x")
        os.symlink("libbar.dylib", os.path.join(work, "libbar.1.dylib"))
        assert macos_dylib_aliases(work, "libfoo.1.2.3.dylib") == \
            ["libfoo.1.dylib", "libfoo.dylib"], "loader aliases discovered"
        assert macos_dylib_aliases(work, "libbar.dylib") == \
            ["libbar.1.dylib"], "unrelated aliases stay separate"

    print("orkige_export: selftest OK")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest()
    else:
        main()
