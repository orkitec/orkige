#!/usr/bin/env python3
"""Package a Release build tree as a downloadable Orkige EDITOR archive - the
desktop-binary half of the nightly pipeline (python3 stdlib only, same rules as
the other Util/ generators).

    orkige_nightly_package.py --platform macos|linux|windows
                              --build-dir build/<release preset>
                              --commit <sha> [--date YYYY-MM-DD]
                              [--output <dir>]

    orkige_nightly_package.py --verify <unpacked dir> --platform <p>
                              [--commit <sha>]

    orkige_nightly_package.py --selftest

This packages what a preset build tree ALREADY produced - it never builds. The
project exporter (orkige_export.py) is the sibling that packages a GAME; this
one packages the TOOL, and reuses the exporter's build-tree helpers (media
resolution, the macOS dylib closure) rather than restating them.

The staged tree has ONE shape on every platform:

    Orkige-<platform>-<short sha>/
        VERSION                 the build identity, one `key: value` per line
        KNOWN-LIMITATIONS.md    what this binary cannot do yet (generated from
                                the LIMITATIONS table below - a gap that closes
                                is a deleted entry, never rewritten prose)
        <the editor>            Orkige.app on macOS, orkige_editor[.exe] else
        <the player>            beside the editor, for Play
        Media/                  the engine shader/font/water/decal media

macOS puts the payload INSIDE the bundle (Contents/MacOS for the binaries,
Contents/Resources/Media for the media - where PlayerBundle already looks) and
repeats VERSION + KNOWN-LIMITATIONS.md at the archive root so a user reads them
before installing. Linux and Windows keep it flat beside the executable, which
is where SDL_GetBasePath resolves.

Archive format is the one each platform's users unpack without thinking: .zip on
macOS (through ditto, which preserves the bundle's symlinks and executable bits)
and Windows, .tar.gz on Linux.

The last line on success is "orkige_nightly_package: OK <artifact>", the same
machine-readable contract orkige_export.py ends on.
"""

import argparse
import datetime
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

import orkige_export  # sibling Util helper: build-tree + macOS bundle plumbing

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PLATFORMS = ("macos", "linux", "windows")

# the archive format per platform
ARCHIVE_SUFFIX = {"macos": ".zip", "linux": ".tar.gz", "windows": ".zip"}

# the macOS editor bundle the build tree produces (tools/editor/CMakeLists.txt
# sets OUTPUT_NAME "Orkige" + MACOSX_BUNDLE)
MACOS_APP_NAME = "Orkige.app"

# editor settings files the editor writes NEXT TO ITS EXECUTABLE. A build tree
# accumulates the developer's own layout, recent projects and window state -
# never ship those: a fresh download must start with the editor's own defaults.
SETTINGS_FILES = ("orkige_editor_view.ini", "orkige_editor_imgui.ini")


def log(message):
    print("orkige_nightly_package: " + message, flush=True)


def warn(message):
    print("orkige_nightly_package: WARNING: " + message, flush=True)


def fail(message):
    print("orkige_nightly_package: ERROR: " + message, flush=True)
    sys.exit(1)


# --- what a downloaded binary cannot do yet --------------------------------
#
# Every artifact carries this table rendered as KNOWN-LIMITATIONS.md. It is a
# LIST OF RECORDS, not prose: each entry stands alone, so closing a gap is
# deleting one entry (and its test stays green - the selftest asserts the
# rendered document lists exactly the entries that apply, whatever they are).
# Adding one is the same edit in reverse. Keep each `detail` to what the user
# observes and each `workaround` to what the user can do about it today.

class Limitation:
    """one honest gap in a downloaded build. `platforms` is the tuple this
    applies to; PLATFORMS means all of them."""

    def __init__(self, key, platforms, title, detail, workaround=""):
        self.key = key
        self.platforms = tuple(platforms)
        self.title = title
        self.detail = detail
        self.workaround = workaround

    def applies_to(self, platform):
        return platform in self.platforms


LIMITATIONS = (
    Limitation(
        key="engine-media-path",
        platforms=PLATFORMS,
        title="The editor does not render in a copied build",
        detail="The editor resolves the engine's shader, font, water and decal "
               "media from the absolute path of the build tree it was compiled "
               "in, so on any other machine it finds none of it: the window "
               "opens and the editor reports its version, but the shader "
               "material system has no templates and nothing draws.",
        workaround="This artifact already ships that media under Media/ - the "
                   "missing piece is the editor asking for it next to its own "
                   "executable first. Until it does, use a build tree "
                   "(cmake --preset macos-release) for real work."),
    Limitation(
        key="project-export",
        platforms=PLATFORMS,
        title="Exporting a game needs the engine repository",
        detail="Build > Export packages a project by copying binaries and media "
               "out of an engine build tree and running Util/orkige_export.py, "
               "so it needs both that tree and python3 3.10+ on PATH.",
        workaround="Export from a clone of the engine repository."),
    Limitation(
        key="play-player-path",
        platforms=PLATFORMS,
        title="Play looks for the runtime at its build-time path",
        detail="The Play button spawns the standalone player by the absolute "
               "path recorded when the editor was compiled.",
        workaround="This artifact ships the matching player beside the editor, "
                   "ready for the lookup to prefer it."),
    Limitation(
        key="asset-cooks-need-python",
        platforms=PLATFORMS,
        title="SVG and Lottie import needs python3 and the repository",
        detail="Importing an .svg or a Lottie .json cooks it through "
               "Util/cook_shapes.py / Util/cook_vector_anim.py, which the "
               "editor runs from the engine source tree it was built against.",
        workaround="Author .oshape / .oanim text assets directly, or import "
                   "from a clone of the engine repository."),
    Limitation(
        key="native-modules",
        platforms=PLATFORMS,
        title="Compiled C++ game code needs a toolchain",
        detail="A project with a native module builds its own C++ against the "
               "engine, so it needs CMake 3.28+, Ninja, a C++20 compiler and an "
               "engine build tree. Games written in Lua need none of that.",
        workaround="Write game behaviour in Lua, or install a toolchain and "
                   "build from the engine repository."),
    Limitation(
        key="settings-beside-executable",
        platforms=PLATFORMS,
        title="Settings are written next to the executable",
        detail="Window layout, view settings and recent projects are stored "
               "beside the editor binary, so the editor must live somewhere the "
               "user can write.",
        workaround="Unpack into your home directory rather than a "
                   "system-managed location."),
    Limitation(
        key="unsigned-macos",
        platforms=("macos",),
        title="The app is not signed or notarized",
        detail="macOS refuses a downloaded app from an unidentified developer "
               "and reports it as damaged or unopenable.",
        workaround="Remove the download quarantine flag once, then open it:\n"
                   "    xattr -dr com.apple.quarantine Orkige.app\n"
                   "Or right-click the app, choose Open, and confirm."),
    Limitation(
        key="unsigned-windows",
        platforms=("windows",),
        title="The executable is not signed",
        detail="SmartScreen warns that it does not recognise the publisher.",
        workaround="Choose \"More info\" and then \"Run anyway\". If the "
                   "download was blocked, right-click the .zip, open "
                   "Properties, tick Unblock, and unpack it again."),
    Limitation(
        key="system-libraries-linux",
        platforms=("linux",),
        title="System libraries must already be present",
        detail="The engine is linked statically, but the binary still loads the "
               "distribution's X11 or Wayland, OpenGL/Vulkan, ALSA or PulseAudio "
               "and D-Bus libraries.",
        workaround="On Debian and Ubuntu: apt install libx11-6 libxrandr2 "
                   "libxcursor1 libxi6 libxkbcommon0 libgl1 libasound2 "
                   "libdbus-1-3. A single-file bundle that carries them is "
                   "future work."),
    Limitation(
        key="msvc-runtime",
        platforms=("windows",),
        title="The Microsoft C++ runtime must be resolvable",
        detail="The build links the shared Visual C++ runtime, so "
               "VCRUNTIME140.dll and MSVCP140.dll have to be found at launch.",
        workaround="This artifact ships them beside the executable when the "
                   "build machine had the redistributable files; the VERSION "
                   "file's msvc-runtime line records whether it did. Otherwise "
                   "install the Microsoft Visual C++ Redistributable for x64."),
)


def limitations_for(platform):
    return tuple(entry for entry in LIMITATIONS if entry.applies_to(platform))


def limitations_markdown(platform, identity_lines=()):
    """KNOWN-LIMITATIONS.md for one platform: a heading, one section per
    applicable entry, nothing else. The wording lives in the LIMITATIONS
    table, so this renderer never needs editing when a gap closes."""
    lines = ["# Known limitations",
             "",
             "What this build of the Orkige editor cannot do yet. Everything "
             "not listed here works.",
             ""]
    for line in identity_lines:
        lines.append("- " + line)
    if identity_lines:
        lines.append("")
    for entry in limitations_for(platform):
        lines.append("## " + entry.title)
        lines.append("")
        lines.append(entry.detail)
        lines.append("")
        if entry.workaround:
            lines.append("What to do: " + entry.workaround)
            lines.append("")
    return "\n".join(lines).rstrip() + "\n"


# --- identity --------------------------------------------------------------

def short_commit(commit):
    """the 9-character abbreviation the repository's own log uses"""
    return (commit or "").strip()[:9]


def project_version(root=REPO_ROOT):
    """the engine version from the root CMakeLists project() call - the ONE
    place it is declared (the editor's ORKIGE_EDITOR_VERSION is the same
    value, handed down by CMake)"""
    path = os.path.join(root, "CMakeLists.txt")
    try:
        with open(path, "r", errors="replace") as lists:
            text = lists.read()
    except OSError:
        return ""
    match = re.search(r"project\(Orkige\s+VERSION\s+([0-9][0-9.]*)", text)
    return match.group(1) if match else ""


def abi_stamp(build_dir):
    """the engine ABI stamp the build tree recorded (cmake/OrkigeAbiStamp.cmake)
    - what a native game module must match. Empty when the tree has none."""
    path = os.path.join(build_dir, "OrkigeAbiStamp.txt")
    if not os.path.isfile(path):
        return ""
    with open(path, "r", errors="replace") as stamp:
        return stamp.read().strip()


def artifact_stem(platform, commit):
    return "Orkige-%s-%s" % (platform, short_commit(commit) or "unstamped")


def artifact_name(platform, commit):
    return artifact_stem(platform, commit) + ARCHIVE_SUFFIX[platform]


def version_text(platform, commit, date, build_dir, extra=()):
    """the VERSION file: one `key: value` per line, so a human reads it and a
    script greps it. The commit and date are the SAME values the editor binary
    was stamped with, so `orkige_editor --version` and this file agree."""
    fields = [("product", "orkige editor"),
              ("version", project_version()),
              ("commit", commit.strip()),
              ("built", date),
              ("platform", platform),
              ("flavor", orkige_export.render_backend(build_dir)),
              ("build-type",
               orkige_export.read_cmake_cache(build_dir, "CMAKE_BUILD_TYPE")),
              ("engine-abi-stamp", abi_stamp(build_dir))]
    fields.extend(extra)
    return "".join("%s: %s\n" % (key, value) for key, value in fields)


def today():
    return datetime.date.today().isoformat()


# --- staging ---------------------------------------------------------------

def editor_binary(build_dir, platform):
    """the built editor: the .app bundle DIRECTORY on macOS, the executable
    file elsewhere"""
    editor_dir = os.path.join(build_dir, "tools", "editor")
    if platform == "macos":
        return os.path.join(editor_dir, MACOS_APP_NAME)
    name = "orkige_editor.exe" if platform == "windows" else "orkige_editor"
    return os.path.join(editor_dir, name)


def player_binary(build_dir, platform):
    name = "orkige_player.exe" if platform == "windows" else "orkige_player"
    return os.path.join(build_dir, "tools", "player", name)


def strip_developer_settings(directory):
    """remove any editor settings file that rode along from the build tree"""
    for name in SETTINGS_FILES:
        path = os.path.join(directory, name)
        if os.path.isfile(path):
            os.remove(path)
            log("dropped the build tree's %s" % name)


def stage_engine_media(build_dir, media_root):
    """the engine media a runtime registers at boot, laid out under Media/
    exactly as the project exporter lays it out (so both packagings resolve the
    same names): the flavor's shader library plus the font, water, decal, bloom
    and grade dirs. Returns the list of staged subdirectory names."""
    os.makedirs(media_root, exist_ok=True)
    staged = []

    def copy_into(source, name):
        if not source:
            return
        destination = os.path.join(media_root, name)
        if os.path.isdir(destination):
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
        staged.append(name)

    flavor = orkige_export.render_backend(build_dir)
    if flavor == "next":
        shader_media = orkige_export.ogre_next_media_dir(build_dir)
        if not shader_media:
            fail("no Ogre-Next media in '%s' - is it a configured build tree?"
                 % build_dir)
        for subdir in orkige_export.ogre_next_media_subdirs(shader_media):
            copy_into(os.path.join(shader_media, subdir), subdir)
    else:
        shader_media = orkige_export.ogre_media_dir(build_dir)
        if not shader_media:
            fail("no OGRE media in '%s' - is it a configured build tree?"
                 % build_dir)
        for subdir in ("Main", "RTShaderLib"):
            copy_into(os.path.join(shader_media, subdir), subdir)
        # the engine-owned classic shader library merges INTO RTShaderLib, the
        # one location the runtime registers (same rule as the game exporter)
        rtss = orkige_export.engine_rtss_dir()
        if rtss and "RTShaderLib" in staged:
            shutil.copytree(rtss, os.path.join(media_root, "RTShaderLib"),
                            dirs_exist_ok=True)
    copy_into(orkige_export.engine_font_dir(), "fonts")
    copy_into(orkige_export.engine_water_dir(), "water")
    copy_into(orkige_export.engine_decal_dir(), "decals")
    copy_into(orkige_export.engine_bloom_dir(flavor),
              os.path.join("bloom", flavor))
    copy_into(orkige_export.engine_grade_dir(flavor),
              os.path.join("grade", flavor))
    return staged


def msvc_runtime_dlls(environ=None):
    """the Visual C++ redistributable DLLs to ship beside a Windows build. The
    x64-windows-static-md triplet links the SHARED CRT, so a machine without the
    redistributable installed cannot launch the editor - and app-local copies of
    these files are the supported deployment. Resolved from VCToolsRedistDir
    (the MSVC environment sets it); empty when it is not set, which the caller
    records honestly rather than pretending."""
    environ = os.environ if environ is None else environ
    redist = environ.get("VCToolsRedistDir", "").strip()
    if not redist:
        return []
    crt_parent = os.path.join(redist, "x64")
    if not os.path.isdir(crt_parent):
        return []
    found = []
    for entry in sorted(os.listdir(crt_parent)):
        if not entry.endswith(".CRT"):
            continue
        crt_dir = os.path.join(crt_parent, entry)
        for name in sorted(os.listdir(crt_dir)):
            if name.lower().endswith(".dll"):
                found.append(os.path.join(crt_dir, name))
    return found


def stage_macos(build_dir, stage_root, editor, player):
    app = os.path.join(stage_root, MACOS_APP_NAME)
    shutil.copytree(editor, app, symlinks=True)
    macos_dir = os.path.join(app, "Contents", "MacOS")
    resources = os.path.join(app, "Contents", "Resources")
    strip_developer_settings(resources)
    shutil.copy2(player, os.path.join(macos_dir, os.path.basename(player)))
    media_root = os.path.join(resources, "Media")
    staged = stage_engine_media(build_dir, media_root)
    # cut every build-tree dylib rpath and carry the non-system closure inside
    # the bundle: a shipped binary must not reach into the machine that built it
    search_dirs = []
    triplet = orkige_export.vcpkg_triplet_dir(build_dir)
    if triplet:
        search_dirs.append(os.path.join(triplet, "lib"))
    frameworks = os.path.join(app, "Contents", "Frameworks")
    for binary in (os.path.join(macos_dir, "Orkige"),
                   os.path.join(macos_dir, os.path.basename(player))):
        orkige_export.macos_make_self_contained(binary, frameworks, search_dirs)
    return staged, os.path.join(macos_dir, "Orkige")


def seal_macos_bundle(app):
    """ad-hoc re-sign the finished bundle, inside-out. The linker's own ad-hoc
    signature covers the executable only; adding the player, the media and the
    text files leaves the bundle with no sealed resource directory, and a
    DOWNLOADED app is held to stricter rules than a locally built one. Signing
    with the ad-hoc identity ("-") needs no certificate, so this runs on any
    machine - it makes the bundle internally consistent, NOT trusted (an
    unsigned-by-a-developer app still trips Gatekeeper, see KNOWN-LIMITATIONS)."""
    macos_dir = os.path.join(app, "Contents", "MacOS")
    frameworks = os.path.join(app, "Contents", "Frameworks")
    # nested code first: a bundle seal records the signatures beneath it
    nested = []
    if os.path.isdir(frameworks):
        nested += [os.path.join(frameworks, name)
                   for name in sorted(os.listdir(frameworks))
                   if os.path.isfile(os.path.join(frameworks, name))
                   and not os.path.islink(os.path.join(frameworks, name))]
    nested += [os.path.join(macos_dir, name)
               for name in sorted(os.listdir(macos_dir)) if name != "Orkige"]
    for binary in nested:
        orkige_export.run(["codesign", "--force", "--sign", "-", binary])
    orkige_export.run(["codesign", "--force", "--sign", "-", app])
    orkige_export.run(["codesign", "--verify", app])


def stage_flat(build_dir, platform, stage_root, editor, player):
    """Linux and Windows: the binaries and Media/ beside each other, which is
    where SDL_GetBasePath (the executable's own directory) resolves."""
    shutil.copy2(editor, os.path.join(stage_root, os.path.basename(editor)))
    shutil.copy2(player, os.path.join(stage_root, os.path.basename(player)))
    # the editor's own fonts: it looks for these next to its executable before
    # falling back to the build tree, so a copied build keeps its icons and its
    # terminal/script mono glyphs (the macOS bundle already carries them)
    editor_media = os.path.join(REPO_ROOT, "tools", "editor", "media")
    for name in ("fa-solid-900.ttf", "DejaVuSans.ttf",
                 "LICENSE-fontawesome.txt", "LICENSE-iconfontcppheaders.txt",
                 "LICENSE-dejavu.txt"):
        source = os.path.join(editor_media, name)
        if os.path.isfile(source):
            shutil.copy2(source, os.path.join(stage_root, name))
    # whatever shared libraries the build placed beside the executable (on
    # Windows the vcpkg DLLs the loader needs, e.g. the Vulkan loader)
    build_output = os.path.dirname(editor)
    library_suffix = ".dll" if platform == "windows" else ".so"
    for name in sorted(os.listdir(build_output)):
        source = os.path.join(build_output, name)
        if name.lower().endswith(library_suffix) and os.path.isfile(source):
            shutil.copy2(source, os.path.join(stage_root, name))
            log("bundled library %s" % name)
    staged = stage_engine_media(build_dir, os.path.join(stage_root, "Media"))
    return staged, os.path.join(stage_root, os.path.basename(editor))


def make_zip(stage_root, archive_path):
    """a .zip of the staged directory (kept as the archive's single top-level
    entry). ditto where it exists, because a macOS bundle carries symlinks and
    executable bits a plain zipfile write would flatten."""
    if shutil.which("ditto"):
        orkige_export.run(["ditto", "-c", "-k", "--sequesterRsrc",
                           "--keepParent", stage_root, archive_path])
        return
    parent = os.path.dirname(os.path.abspath(stage_root))
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for directory, _, files in os.walk(stage_root):
            for name in files:
                path = os.path.join(directory, name)
                archive.write(path, os.path.relpath(path, parent))


def make_tar_gz(stage_root, archive_path):
    with tarfile.open(archive_path, "w:gz") as archive:
        archive.add(stage_root, arcname=os.path.basename(stage_root))


def package(platform, build_dir, commit, date, output_dir):
    build_dir = os.path.abspath(build_dir)
    if not os.path.isdir(build_dir):
        fail("no build tree at '%s'" % build_dir)
    editor = editor_binary(build_dir, platform)
    player = player_binary(build_dir, platform)
    for path, what in ((editor, "editor"), (player, "player")):
        if not os.path.exists(path):
            fail("no %s at '%s' - build the release preset first" % (what, path))
    build_type = orkige_export.read_cmake_cache(build_dir, "CMAKE_BUILD_TYPE")
    if build_type != "Release":
        warn("packaging a %s build - distributable builds are Release"
             % (build_type or "no-build-type"))

    os.makedirs(output_dir, exist_ok=True)
    stem = artifact_stem(platform, commit)
    stage_parent = os.path.join(output_dir, "stage")
    if os.path.isdir(stage_parent):
        shutil.rmtree(stage_parent)
    stage_root = os.path.join(stage_parent, stem)
    os.makedirs(stage_root)

    extra_fields = []
    if platform == "macos":
        staged_media, staged_editor = stage_macos(build_dir, stage_root,
                                                  editor, player)
    else:
        staged_media, staged_editor = stage_flat(build_dir, platform,
                                                 stage_root, editor, player)
        strip_developer_settings(stage_root)
    if platform == "windows":
        runtime = msvc_runtime_dlls()
        for dll in runtime:
            shutil.copy2(dll, os.path.join(stage_root, os.path.basename(dll)))
            log("bundled MSVC runtime %s" % os.path.basename(dll))
        if runtime:
            extra_fields.append(("msvc-runtime", "bundled (%s)" % ", ".join(
                sorted(os.path.basename(dll) for dll in runtime))))
        else:
            warn("no Visual C++ redistributable DLLs found (VCToolsRedistDir "
                 "unset) - the artifact needs one installed on the user's "
                 "machine")
            extra_fields.append(("msvc-runtime", "not bundled - install the "
                                                "Visual C++ Redistributable"))
    log("staged engine media: %s" % (", ".join(staged_media) or "none"))

    version = version_text(platform, commit, date, build_dir, extra_fields)
    limitations = limitations_markdown(
        platform, ["%s %s" % (key, value) for key, value in
                   (line.split(": ", 1) for line in version.splitlines()
                    if line.startswith(("version:", "commit:", "built:")))])
    targets = [stage_root]
    if platform == "macos":
        targets.append(os.path.join(stage_root, MACOS_APP_NAME, "Contents",
                                    "Resources"))
    for target in targets:
        with open(os.path.join(target, "VERSION"), "w") as handle:
            handle.write(version)
        with open(os.path.join(target, "KNOWN-LIMITATIONS.md"), "w") as handle:
            handle.write(limitations)
    if platform == "macos":
        # LAST: every byte the seal covers must already be in place
        seal_macos_bundle(os.path.join(stage_root, MACOS_APP_NAME))

    archive_path = os.path.join(output_dir, artifact_name(platform, commit))
    if os.path.exists(archive_path):
        os.remove(archive_path)
    if ARCHIVE_SUFFIX[platform] == ".zip":
        make_zip(stage_root, archive_path)
    else:
        make_tar_gz(stage_root, archive_path)
    log("staged %s (%s), archive %s"
        % (stem, orkige_export.human_size(
            orkige_export.directory_size(stage_root)),
           orkige_export.human_size(os.path.getsize(archive_path))))
    log("editor: %s" % os.path.relpath(staged_editor, stage_root))
    log("OK " + archive_path)
    return archive_path


# --- verification (the pipeline's smoke test) ------------------------------

def verify_layout(root, platform):
    """the structural half of the smoke test: every file a downloaded build is
    supposed to contain, checked on the UNPACKED tree. Returns the editor
    executable path; a list of complaints means the packaging is broken."""
    problems = []
    if platform == "macos":
        app = os.path.join(root, MACOS_APP_NAME)
        editor = os.path.join(app, "Contents", "MacOS", "Orkige")
        resources = os.path.join(app, "Contents", "Resources")
        expected_files = [editor,
                          os.path.join(root, "VERSION"),
                          os.path.join(root, "KNOWN-LIMITATIONS.md"),
                          os.path.join(resources, "VERSION")]
        media_root = os.path.join(resources, "Media")
        player_dir = os.path.join(app, "Contents", "MacOS")
    else:
        name = "orkige_editor.exe" if platform == "windows" else "orkige_editor"
        editor = os.path.join(root, name)
        expected_files = [editor,
                          os.path.join(root, "VERSION"),
                          os.path.join(root, "KNOWN-LIMITATIONS.md")]
        media_root = os.path.join(root, "Media")
        player_dir = root
    for path in expected_files:
        if not os.path.isfile(path):
            problems.append("missing " + os.path.relpath(path, root))
    if platform != "windows" and os.path.isfile(editor) \
            and not os.access(editor, os.X_OK):
        # an archive format that drops the executable bit turns a download into
        # a permission error nobody can diagnose
        problems.append("the editor is not executable")
    player = "orkige_player.exe" if platform == "windows" else "orkige_player"
    if not os.path.isfile(os.path.join(player_dir, player)):
        problems.append("missing the player (%s)" % player)
    if not os.path.isdir(media_root):
        problems.append("missing Media/")
    else:
        # the shader library is the media without which nothing renders, so its
        # absence is a packaging failure and not a soft note
        shader = ("Hlms" if os.path.isdir(os.path.join(media_root, "Hlms"))
                  else "RTShaderLib")
        if not os.path.isdir(os.path.join(media_root, shader)):
            problems.append("missing the shader media (Media/Hlms or "
                            "Media/RTShaderLib)")
        for name in ("fonts", "water", "decals"):
            if not os.path.isdir(os.path.join(media_root, name)):
                problems.append("missing Media/" + name)
    for name in SETTINGS_FILES:
        for directory in {root, player_dir}:
            if os.path.isfile(os.path.join(directory, name)):
                problems.append("ships a build tree's %s" % name)
    return editor, problems


def verify(root, platform, commit="", runner=subprocess.run):
    """unpack-and-run verification: the layout above, plus the binary answering
    `--version` with the commit it was stamped with. A nightly that publishes a
    binary which cannot start is worse than no nightly."""
    root = os.path.abspath(root)
    # tolerate being pointed at the directory the archive unpacked INTO
    if not os.path.isfile(os.path.join(root, "VERSION")):
        entries = [os.path.join(root, name) for name in sorted(os.listdir(root))]
        nested = [path for path in entries if os.path.isdir(path)
                  and os.path.isfile(os.path.join(path, "VERSION"))]
        if len(nested) == 1:
            root = nested[0]
            log("verifying the unpacked tree '%s'" % os.path.basename(root))
    editor, problems = verify_layout(root, platform)
    for problem in problems:
        print("orkige_nightly_package: LAYOUT: " + problem, flush=True)
    if problems:
        fail("the unpacked artifact is incomplete (%d problems)" % len(problems))
    result = runner([editor, "--version"], capture_output=True, text=True)
    reported = (result.stdout or "").strip()
    if result.returncode != 0:
        fail("'%s --version' exited %d: %s"
             % (editor, result.returncode,
                (result.stderr or "").strip() or "no output"))
    log("the binary reports: " + (reported or "<nothing>"))
    if not reported.startswith("orkige_editor "):
        fail("the binary did not report a version line")
    if commit and short_commit(commit) not in reported:
        fail("the binary reports '%s' - expected the stamp %s"
             % (reported, short_commit(commit)))
    log("OK " + root)
    return reported


# --- entry point -----------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="package a Release build tree as a downloadable Orkige "
                    "editor archive")
    parser.add_argument("--platform", choices=PLATFORMS)
    parser.add_argument("--build-dir",
                        help="the Release preset build tree to package")
    parser.add_argument("--commit", default="",
                        help="the source commit this build came from")
    parser.add_argument("--date", default="",
                        help="build date (ISO, default today)")
    parser.add_argument("--output", default="",
                        help="output directory (default <build-dir>/nightly)")
    parser.add_argument("--verify", default="",
                        help="verify an UNPACKED artifact directory instead of "
                             "packaging one")
    parser.add_argument("--selftest", action="store_true",
                        help="run the packaging self-checks and exit")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return
    if not args.platform:
        parser.error("--platform is required")
    if args.verify:
        verify(args.verify, args.platform, args.commit)
        return
    if not args.build_dir:
        parser.error("--build-dir is required")
    output = args.output or os.path.join(args.build_dir, "nightly")
    package(args.platform, args.build_dir, args.commit,
            args.date or today(), output)


# --- self-checks -----------------------------------------------------------

def selftest():
    """Exercises the parts that do not need a built editor: the identity
    strings, the limitations table and its rendering, the media staging over a
    synthetic build tree, the archive round-trip, and every verdict the
    verifier can return (including a fake binary reporting the wrong stamp)."""

    # --- identity -------------------------------------------------------
    assert short_commit("a16c0227a1234567") == "a16c0227a"
    assert short_commit("") == ""
    assert artifact_stem("macos", "a16c0227a1234") == "Orkige-macos-a16c0227a"
    assert artifact_name("linux", "abc") == "Orkige-linux-abc.tar.gz"
    assert artifact_name("windows", "abc") == "Orkige-windows-abc.zip"
    assert artifact_name("macos", "abc") == "Orkige-macos-abc.zip"
    assert artifact_stem("linux", "") == "Orkige-linux-unstamped"
    # the version comes from the ONE declaration in the root CMakeLists
    assert re.match(r"^\d+\.\d+", project_version()), project_version()

    # --- the limitations table ------------------------------------------
    keys = [entry.key for entry in LIMITATIONS]
    assert len(keys) == len(set(keys)), "duplicate limitation key"
    for entry in LIMITATIONS:
        assert entry.title and entry.detail and entry.workaround, entry.key
        assert entry.platforms, entry.key
        for platform in entry.platforms:
            assert platform in PLATFORMS, entry.key
        # the docs are a reference, not a changelog: no dev-process narration
        for text in (entry.title, entry.detail, entry.workaround):
            assert "TODO" not in text and "phase" not in text.lower(), entry.key
    # every platform gets the shared gaps plus its own, and nobody else's
    for platform in PLATFORMS:
        rendered = limitations_markdown(platform)
        applicable = limitations_for(platform)
        assert applicable, platform
        for entry in LIMITATIONS:
            present = entry.title in rendered
            assert present == entry.applies_to(platform), \
                "%s / %s" % (platform, entry.key)
        assert rendered.startswith("# Known limitations")
        assert rendered.endswith("\n") and "\n\n\n" not in rendered
    assert "com.apple.quarantine" in limitations_markdown("macos")
    assert "SmartScreen" in limitations_markdown("windows")
    assert "apt install" in limitations_markdown("linux")
    # the identity lines a real artifact carries at the top
    with_identity = limitations_markdown("linux", ["commit abc1234"])
    assert "- commit abc1234" in with_identity

    # --- the MSVC runtime resolution -----------------------------------
    with tempfile.TemporaryDirectory() as temp:
        assert msvc_runtime_dlls({}) == []
        assert msvc_runtime_dlls({"VCToolsRedistDir": temp}) == []
        crt = os.path.join(temp, "x64", "Microsoft.VC143.CRT")
        os.makedirs(crt)
        for name in ("vcruntime140.dll", "msvcp140.dll", "notes.txt"):
            open(os.path.join(crt, name), "w").close()
        found = [os.path.basename(path) for path in
                 msvc_runtime_dlls({"VCToolsRedistDir": temp})]
        assert found == ["msvcp140.dll", "vcruntime140.dll"], found

    # --- a synthetic build tree, staged and archived --------------------
    with tempfile.TemporaryDirectory() as temp:
        build = os.path.join(temp, "build", "linux-release-next")
        editor_dir = os.path.join(build, "tools", "editor")
        player_dir = os.path.join(build, "tools", "player")
        os.makedirs(editor_dir)
        os.makedirs(player_dir)
        with open(os.path.join(build, "CMakeCache.txt"), "w") as cache:
            cache.write("CMAKE_BUILD_TYPE:STRING=Release\n"
                        "ORKIGE_RENDER_BACKEND:STRING=next\n")
        with open(os.path.join(build, "OrkigeAbiStamp.txt"), "w") as stamp:
            stamp.write("content.deadbeef1234\n")
        # a stand-in editor that answers --version like the real one
        editor_path = os.path.join(editor_dir, "orkige_editor")
        with open(editor_path, "w") as script:
            script.write("#!/bin/sh\n"
                         "echo 'orkige_editor 2.0.0 (c0ffee123, 2026-07-30) "
                         "[next, Release]'\n")
        os.chmod(editor_path, 0o755)
        open(os.path.join(player_dir, "orkige_player"), "w").close()
        # a build tree's leftover settings file must NOT ride along
        open(os.path.join(editor_dir, "orkige_editor_view.ini"), "w").close()
        # the vcpkg media layout the staging reads
        media = os.path.join(build, "vcpkg_installed", "x64-linux")
        os.makedirs(os.path.join(media, "include"))
        hlms = os.path.join(media, "share", "ogre-next", "Media", "Hlms", "Pbs")
        os.makedirs(hlms)
        open(os.path.join(hlms, "PixelShader_ps.glsl"), "w").close()

        output = os.path.join(temp, "out")
        archive = package("linux", build, "c0ffee1234567", "2026-07-30", output)
        assert archive.endswith("Orkige-linux-c0ffee123.tar.gz"), archive
        stage = os.path.join(output, "stage", "Orkige-linux-c0ffee123")
        version = open(os.path.join(stage, "VERSION")).read()
        assert "commit: c0ffee1234567\n" in version, version
        assert "built: 2026-07-30\n" in version
        assert "flavor: next\n" in version
        assert "engine-abi-stamp: content.deadbeef1234\n" in version
        assert "platform: linux\n" in version
        # the real engine media (committed to the tree) rode along
        for name in ("Hlms", "fonts", "water", "decals"):
            assert os.path.isdir(os.path.join(stage, "Media", name)), name
        assert not os.path.exists(
            os.path.join(stage, "orkige_editor_view.ini"))
        # the editor's own fonts sit beside the executable, where it looks
        assert os.path.isfile(os.path.join(stage, "fa-solid-900.ttf"))

        # the archive really carries the tree under its one top-level dir
        with tarfile.open(archive) as tar:
            names = tar.getnames()
        assert "Orkige-linux-c0ffee123/VERSION" in names, names[:5]
        assert any(name.startswith("Orkige-linux-c0ffee123/Media/Hlms")
                   for name in names)

        # --- the verifier, on a real unpack ----------------------------
        unpacked = os.path.join(temp, "unpacked")
        os.makedirs(unpacked)
        with tarfile.open(archive) as tar:
            try:
                tar.extractall(unpacked, filter="data")
            except TypeError:  # the filter argument predates python 3.12
                tar.extractall(unpacked)
        os.chmod(os.path.join(unpacked, "Orkige-linux-c0ffee123",
                              "orkige_editor"), 0o755)
        # pointed at the parent, it finds the single unpacked tree
        reported = verify(unpacked, "linux", "c0ffee1234567")
        assert reported.startswith("orkige_editor 2.0.0 (c0ffee123"), reported

        # every layout verdict, on a tree with pieces removed
        tree = os.path.join(unpacked, "Orkige-linux-c0ffee123")
        _, problems = verify_layout(tree, "linux")
        assert problems == [], problems
        os.remove(os.path.join(tree, "orkige_player"))
        shutil.rmtree(os.path.join(tree, "Media", "fonts"))
        open(os.path.join(tree, "orkige_editor_imgui.ini"), "w").close()
        _, problems = verify_layout(tree, "linux")
        assert any("player" in problem for problem in problems), problems
        assert any("Media/fonts" in problem for problem in problems), problems
        assert any("imgui.ini" in problem for problem in problems), problems
        shutil.rmtree(os.path.join(tree, "Media"))
        _, problems = verify_layout(tree, "linux")
        assert "missing Media/" in problems, problems

    # --- a wrong stamp is a failure, not a shrug ------------------------
    class FakeResult:
        def __init__(self, stdout, returncode=0, stderr=""):
            self.stdout = stdout
            self.returncode = returncode
            self.stderr = stderr

    with tempfile.TemporaryDirectory() as temp:
        tree = os.path.join(temp, "Orkige-linux-abc")
        os.makedirs(os.path.join(tree, "Media", "Hlms"))
        for name in ("fonts", "water", "decals"):
            os.makedirs(os.path.join(tree, "Media", name))
        for name in ("orkige_editor", "orkige_player", "VERSION",
                     "KNOWN-LIMITATIONS.md"):
            open(os.path.join(tree, name), "w").close()
        # a non-executable editor is a packaging failure of its own
        _, problems = verify_layout(tree, "linux")
        assert problems == ["the editor is not executable"], problems
        os.chmod(os.path.join(tree, "orkige_editor"), 0o755)
        for stdout, expected in (
                ("orkige_editor 2.0.0 (abcdef123, 2026-07-30) [next, Release]",
                 "stamp"),                       # right shape, wrong commit
                ("Segmentation fault", "version line")):   # not a version line
            try:
                verify(tree, "linux", "abc1234",
                       runner=lambda *_a, **_k: FakeResult(stdout))
                raise AssertionError("expected a refusal for %r" % stdout)
            except SystemExit:
                pass
        # a nonzero exit is a refusal too, even with plausible output
        try:
            verify(tree, "linux", "", runner=lambda *_a, **_k: FakeResult(
                "orkige_editor 2.0.0 (abc1234, 2026-07-30) [next, Release]",
                returncode=134, stderr="abort"))
            raise AssertionError("expected a refusal for a nonzero exit")
        except SystemExit:
            pass
        # the matching stamp passes
        assert verify(tree, "linux", "abc1234", runner=lambda *_a, **_k:
                      FakeResult("orkige_editor 2.0.0 (abc1234, 2026-07-30) "
                                 "[next, Release]"))

    print("orkige_nightly_package: selftest OK")


if __name__ == "__main__":
    main()
