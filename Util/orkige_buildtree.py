#!/usr/bin/env python3
"""Engine build-tree facts for the repository's Python tooling.

Two dev/CI scripts need to know things about a configured Orkige build tree -
which render flavor it was configured with, where its vcpkg triplet lives,
which engine media directories exist in the source tree - and both drive the
engine's own host binaries as subprocesses:

  * Util/orkige_nightly_package.py - packages the editor for distribution
  * Util/orkige_device.py          - the phone deploy-and-run front door

Neither packages a GAME. That is the exporter's job, and the exporter is C++
(tools/exporter): a library the editor links and the `orkige_export` binary
these scripts call. This module is only what the two share - the build-tree
lookups, the small process/size helpers and the manifest slice - so neither has
to reach into the other.

Pure stdlib, no side effects on import.
"""

import glob
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def log(message):
    print("orkige: " + message, flush=True)


def warn(message):
    print("orkige: WARNING: " + message, flush=True)


def fail(message):
    print("orkige: ERROR: " + message, flush=True)
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
    """the slice of project.orkproj a deploy needs (the manifest is a small
    semantic XML document, see core_project/Project.h). The exporter reads the
    same facts in C++ (tools/exporter/ExportProject.h); this is the reader for
    the scripts that only need to NAME a project before handing it over."""

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


# --- signing environment ---------------------------------------------------
# The signing IDENTITY and the provisioning PROFILE are developer-machine
# specific and must never be committed - they come from CLI args or the
# environment. Only the Team ID (export.ios.teamId) is a project-level,
# safe-to-commit value. See Docs/ios-signing.md. The names match
# tools/exporter/ExportSettings.h, which is what actually reads them.

IOS_SIGNING_IDENTITY_ENV = "ORKIGE_IOS_SIGNING_IDENTITY"
IOS_PROVISIONING_PROFILE_ENV = "ORKIGE_IOS_PROVISIONING_PROFILE"


# --- sizes -----------------------------------------------------------------

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


# the desktop preset trees a HOST tool can come from, best first. A mobile or
# web preset never builds one (it cross-compiles a player), so a phone session
# takes its exporter from whichever desktop tree this machine has.
HOST_BUILD_TREES = ("macos-release", "macos-debug", "linux-release",
                    "linux-debug", "windows-release", "windows-debug",
                    "macos-release-classic", "macos-debug-classic",
                    "linux-release-classic", "linux-debug-classic")


def host_tool(build_dir, name):
    """a host tool the engine build produced (`orkige_export`, `texcook`) in
    @p build_dir, or "" when that tree has none. ORKIGE_<NAME> overrides, so a
    caller can point at a binary from another tree."""
    override = os.environ.get("ORKIGE_" + name.upper())
    if override and os.path.isfile(override):
        return override
    suffix = ".exe" if os.name == "nt" else ""
    for relative in (os.path.join("tools", name, name + suffix),
                     os.path.join("tools", "exporter", name + suffix)):
        candidate = os.path.join(build_dir, relative)
        if os.path.isfile(candidate):
            return candidate
    return ""


def find_host_tool(name):
    """a host tool from any desktop build tree in this repository (or the
    ORKIGE_<NAME> override, or PATH). "" when this machine has built none - the
    caller then says which preset to build rather than spawning nothing."""
    override = os.environ.get("ORKIGE_" + name.upper())
    if override and os.path.isfile(override):
        return override
    for preset in HOST_BUILD_TREES:
        found = host_tool(os.path.join(REPO_ROOT, "build", preset), name)
        if found:
            return found
    return shutil.which(name) or ""


# --- engine media directories ----------------------------------------------

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


def engine_decal_dir():
    """the engine decal media directory committed to the tree (the default mark
    + blob-shadow textures DecalComponent references) - registered as a resource
    location at runtime like the font/water dirs. Empty when absent."""
    decals = os.path.join(REPO_ROOT, "orkige_engine", "media", "decals")
    return decals if os.path.isdir(decals) else ""


def engine_bloom_dir(flavor):
    """the engine bloom compositor media (bright/blur/combine material + shaders
    engine:setBloom needs) - PER FLAVOR: bloom/next (the quad-chain shaders) vs
    bloom/classic (the viewport-compositor shaders). Registered at runtime under
    Media/bloom/<flavor> like the font/water/decal dirs. Empty when absent."""
    bloom = os.path.join(REPO_ROOT, "orkige_engine", "media", "bloom", flavor)
    return bloom if os.path.isdir(bloom) else ""


def engine_grade_dir(flavor):
    """the engine output-grade compositor media (the grade material + shaders
    engine:setGrade needs) - PER FLAVOR: grade/next vs grade/classic. Registered
    at runtime under Media/grade/<flavor> like the bloom dir. Empty when absent."""
    grade = os.path.join(REPO_ROOT, "orkige_engine", "media", "grade", flavor)
    return grade if os.path.isdir(grade) else ""


def engine_rtss_dir():
    """the engine-owned classic shader library (the metal-rough lighting GLSL)
    - merged INTO the bundled Media/RTShaderLib so the runtime's one registered
    shader location serves it (a dev run registers the source dir instead)"""
    rtss = os.path.join(REPO_ROOT, "orkige_engine", "media", "rtss")
    return rtss if os.path.isdir(rtss) else ""


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


# --- macOS -----------------------------------------------------------------

def macos_make_self_contained(exporter, executable, frameworks_dir,
                              search_dirs):
    """copy the non-system dylib closure into Contents/Frameworks, point the
    executable at it (@executable_path/../Frameworks), REMOVE the build-tree
    rpaths (so a missing dylib fails here on this machine, not on the user's)
    and ad-hoc re-sign the modified binary.

    The operation itself lives in the exporter (tools/exporter/
    ExportSelfContain.h) and is reachable as its `self-contain` subcommand -
    the ONE implementation, shared with what an export does to a packaged
    game, so a distributed editor and a distributed game are made
    self-contained by the same code."""
    command = [exporter, "self-contain", "--frameworks", frameworks_dir]
    for directory in search_dirs:
        command += ["--search", directory]
    command += ["--banned", "vcpkg_installed", "--banned", REPO_ROOT]
    command.append(executable)
    run(command)
