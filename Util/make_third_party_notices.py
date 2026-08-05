#!/usr/bin/env python3
"""Assemble THIRD-PARTY-NOTICES.md, the notices a shipped binary carries.

A maintainer tool, run deliberately - like a vcpkg pin bump, never as part of a
build. The COMMITTED file is what ships; this is how it is produced, so the
document can be rebuilt from evidence instead of hand-edited into drift.

Every license text comes from the authoritative source: the `copyright` file
vcpkg installs under `<vcpkg_installed>/<triplet>/share/<port>/`, and the
committed license file for media and in-tree pieces. Nothing is written from
memory.

    python3 Util/make_third_party_notices.py [--share <dir>] [--out <path>]

--share defaults to a configured build tree's vcpkg share directory. Run it on
a machine whose install tree holds the whole desktop closure; the components
`check_third_party_notices.py` records as awaiting their text (the Linux-only
HTTP closure) need a Linux install tree, and the document says so where they
would otherwise sit.

The currency gate for what this writes is Util/check_third_party_notices.py.
"""
import argparse
import glob
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OSX = ""    # resolved at run time (@see default_share_directory)

# name, port dir under share/ (or ("repo", relpath)), spdx, version, homepage,
# tiers, one-line role
COMPONENTS = [
    # --- in every shipped artifact ---
    ("SDL3", "sdl3", "Zlib AND MIT AND Apache-2.0", "3.4.12",
     "https://github.com/libsdl-org/SDL", "E D M W",
     "window, input, audio device and platform entry point"),
    ("Lua", "lua", "MIT", "5.5.0", "https://www.lua.org/",
     "E D M W", "the embedded scripting language"),
    ("sol2", "sol2", "MIT", "3.5.0", "https://github.com/ThePhD/sol2",
     "E D M W", "the C++ binding layer over Lua (header-only)"),
    ("Jolt Physics", "joltphysics", "MIT", "5.5.0",
     "https://github.com/jrouwe/JoltPhysics", "E D M W",
     "the rigid-body simulation"),
    ("TinyXML-2", "tinyxml2", "Zlib", "11.0.0",
     "https://github.com/leethomason/tinyxml2", "E D M W",
     "the XML archive behind scene, prefab and localisation files"),
    ("Open Asset Import Library", "assimp", "BSD-3-Clause", "6.0.4",
     "https://github.com/assimp/assimp", "E D M W",
     "mesh import (glTF/glb and friends)"),
    ("Clipper", "polyclipping", "BSL-1.0", "6.4.2",
     "http://www.angusj.com/delphi/clipper.php", "E D M W",
     "polygon clipping, used by the mesh importer"),
    ("poly2tri", "jhasse-poly2tri", "BSD-3-Clause", "2023-11-22",
     "https://github.com/jhasse/poly2tri", "E D M W",
     "constrained triangulation, used by the mesh importer"),
    ("pugixml", "pugixml", "MIT", "1.16", "https://pugixml.org/",
     "E D M W", "XML parsing inside the render backend"),
    ("UTF8-CPP", "utfcpp", "BSL-1.0", "4.1.1",
     "https://github.com/nemtrif/utfcpp", "E D M W",
     "UTF-8 handling inside the mesh importer (header-only)"),
    ("minizip", "minizip", "Zlib", "1.3.2",
     "https://github.com/madler/zlib", "E D M W",
     "the zip reader behind pak and APK mounting"),
    ("kubazip", "kubazip", "MIT", "0.3.14",
     "https://github.com/kuba--/zip", "E D M W",
     "zip support inside the render backend"),
    ("zlib", "zlib", "Zlib", "1.3.2", "https://zlib.net/", "E D M W",
     "deflate, under the archive and image readers"),
    ("nanosvg", "nanosvg", "Zlib", "2023-12-29",
     "https://github.com/memononen/nanosvg", "E D M W",
     "SVG parsing for UI sprites and shape cooking"),
    ("earcut.hpp", "earcut-hpp", "ISC", "2.2.4",
     "https://github.com/mapbox/earcut.hpp", "E D M W",
     "polygon triangulation for vector shapes (header-only)"),
    ("stb", "stb", "MIT OR CC-PDDC", "2024-07-29",
     "https://github.com/nothings/stb", "E D M W",
     "stb_truetype (glyphs), stb_vorbis (music), stb_image (PNG decode)"),
    ("miniaudio", "miniaudio", "Unlicense OR MIT-0", "0.11.25",
     "https://github.com/mackron/miniaudio", "E D M W",
     "the audio mixer and device backend"),
    ("{fmt}", "fmt", "MIT", "12.2.0", "https://fmt.dev/", "E D M",
     "string formatting inside the render backend"),

    # --- render backends ---
    ("OGRE", "ogre", "MIT", "14.x", "https://www.ogre3d.org/",
     "E D M W", "the classic-flavor render backend"),
    ("OGRE-Next", "ogre-next", "MIT", "4.x",
     "https://github.com/OGRECave/ogre-next", "E D M",
     "the default render backend"),
    ("RapidJSON", "rapidjson", "MIT", "2025-02-26",
     "https://rapidjson.org/", "E D M",
     "JSON parsing inside the default render backend"),
    ("FreeType", "freetype", "FTL OR GPL-2.0-or-later", "2.14.3",
     "https://freetype.org/", "E D M W",
     "font rasterisation inside the render backend's overlay component"),
    ("Brotli", "brotli", "MIT", "1.2.0",
     "https://github.com/google/brotli", "E D M W",
     "WOFF2 decompression inside FreeType"),
    ("bzip2", "bzip2", "bzip2-1.0.6", "1.0.8",
     "https://sourceware.org/bzip2/", "E D M W",
     "PCF decompression inside FreeType"),
    ("libpng", "libpng", "libpng-2.0", "1.6.58", "http://www.libpng.org/",
     "E D M W", "PNG-compressed bitmap glyphs inside FreeType"),
    ("glslang", "glslang",
     "Apache-2.0 AND BSD-3-Clause AND MIT AND GPL-3.0-or-later", "16.3.0",
     "https://github.com/KhronosGroup/glslang", "E D M",
     "GLSL to SPIR-V compilation for the Vulkan render system"),
    ("Vulkan-Loader", "vulkan-loader", "Apache-2.0", "1.4.350.1",
     "https://github.com/KhronosGroup/Vulkan-Loader", "E D",
     "the Vulkan loader (Linux and Windows builds)"),
    ("Vulkan-Headers", "vulkan-headers", "Apache-2.0 OR MIT", "1.4.350.1",
     "https://github.com/KhronosGroup/Vulkan-Headers", "E D",
     "the Vulkan API headers"),

    ("liblzma (XZ Utils)", "liblzma", "0BSD", "5.8.3",
     "https://tukaani.org/xz/", "E D",
     "LZMA decompression inside libsystemd, which the Linux D-Bus "
     "dependency links"),

    # --- editor only ---
    ("Dear ImGui", "imgui", "MIT", "1.92.8",
     "https://github.com/ocornut/imgui", "E", "the editor's user interface"),
    ("ImGuizmo", "imguizmo", "MIT", "1.10",
     "https://github.com/CedricGuillemet/ImGuizmo", "E",
     "the editor's transform gizmos"),
    ("ImGuiColorTextEdit", "imgui-color-text-edit", "MIT", "2026-05-03",
     "https://github.com/BalazsJako/ImGuiColorTextEdit", "E",
     "the editor's embedded code editor"),
    ("libvterm", "libvterm", "MIT", "0.3.3",
     "https://www.leonerd.org.uk/code/libvterm/", "E",
     "the terminal emulation behind the editor's embedded terminal"),
    ("KTX-Software (libktx)", "ktx", "Apache-2.0", "4.4.2",
     "https://github.com/KhronosGroup/KTX-Software", "E",
     "the export-time GPU texture encoder"),
    ("Zstandard", "zstd", "BSD-3-Clause OR GPL-2.0-only", "1.5.7",
     "https://facebook.github.io/zstd/", "E",
     "supercompression inside the texture encoder"),
]

# components whose text is committed in this repository rather than installed
REPO_TEXTS = [
    ("Nunito", ("repo", "orkige_engine/media/fonts/OFL.txt"), "OFL-1.1",
     "3.603", "https://github.com/googlefonts/nunito", "E D M W",
     "the engine-default font, bundled as a media file"),
    ("Font Awesome Free", ("repo", "tools/editor/media/LICENSE-fontawesome.txt"),
     "CC-BY-4.0 AND OFL-1.1 AND MIT", "6.x",
     "https://fontawesome.com/", "E",
     "the editor's interface icon font"),
    ("IconFontCppHeaders", ("repo",
     "tools/editor/media/LICENSE-iconfontcppheaders.txt"), "Zlib", "-",
     "https://github.com/juliettef/IconFontCppHeaders", "E",
     "the icon-font glyph table (tools/editor/IconsFontAwesome6.h)"),
    ("DejaVu Sans", ("repo", "tools/editor/media/LICENSE-dejavu.txt"),
     "Bitstream-Vera AND Public-Domain", "2.37",
     "https://dejavu-fonts.github.io/", "E",
     "the editor's mono symbol fallback (block and braille glyphs)"),
]

# Linux-only closure. This machine holds no Linux install tree, so their
# verbatim texts are not embedded yet - the entry says so rather than
# reproducing a text nobody checked.
PENDING = [
    ("libcurl", "curl AND ISC AND BSD-3-Clause", "8.21.0",
     "https://curl.se/", "E D",
     "the HTTP client, on Linux only - every other platform drives its own"),
    ("OpenSSL (the TLS library libcurl resolves against)", "Apache-2.0",
     "-", "https://www.openssl.org/", "E D",
     "certificate verification for libcurl, on Linux only"),
]

TIER_NAMES = {
    "E": "the Orkige editor",
    "D": "the desktop player and every desktop game exported from it",
    "M": "the iOS and Android players and the apps exported from them",
    "W": "the browser player and web exports",
}


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read().rstrip("\n")


def text_of(source):
    if isinstance(source, tuple):
        return read(os.path.join(REPO, source[1]))
    return read(os.path.join(OSX, source, "copyright"))


def indent_block(text):
    """the license text as a fenced block - verbatim, never reflowed"""
    return "```text\n" + text.replace("\r\n", "\n") + "\n```"


def anchor(name):
    keep = []
    for ch in name.lower():
        if ch.isalnum():
            keep.append(ch)
        elif ch in " -_":
            keep.append("-")
    return "".join(keep).strip("-")


def main():
    all_components = COMPONENTS + REPO_TEXTS
    out = []
    w = out.append

    w("# Third-party notices")
    w("")
    w("Orkige links a large third-party closure statically and bundles a small")
    w("amount of third-party media. Most of those licenses require their")
    w("copyright notice and license text to travel with a **binary**")
    w("distribution, not merely with the source. This file is that text.")
    w("")
    w("Orkige's own code is licensed separately - see `LICENSE`. Nothing here")
    w("changes that; these are the notices of other people's work that a")
    w("shipped Orkige binary contains.")
    w("")
    w("## Where this file travels")
    w("")
    w("A notice that only exists in a repository discharges nothing, so this")
    w("file is packaged into every artifact, at the resource root each runtime")
    w("already resolves - the same directory that holds the default-project")
    w("marker:")
    w("")
    w("| Artifact | Where the notices sit |")
    w("|---|---|")
    w("| Orkige editor (macOS) | `Orkige.app/Contents/Resources/THIRD-PARTY-NOTICES.md`, and at the download archive's root |")
    w("| Orkige editor (Linux, Windows) | `share/orkige/THIRD-PARTY-NOTICES.md` beside the executable, and at the download archive's root |")
    w("| Exported macOS game | `<Game>.app/Contents/Resources/THIRD-PARTY-NOTICES.md` |")
    w("| Exported iOS app | `<Game>.app/THIRD-PARTY-NOTICES.md` |")
    w("| Exported Android APK / App Bundle | `assets/THIRD-PARTY-NOTICES.md` |")
    w("| Exported web build | `THIRD-PARTY-NOTICES.md` beside `index.html` |")
    w("| Engine SDK pack | `THIRD-PARTY-NOTICES.md` at the pack root |")
    w("")
    w("A game shipped to a store carries this file inside its application")
    w("bundle. Whether the game ALSO surfaces the notices in its own interface")
    w("is the game's decision; the file being present is what the licenses")
    w("require of the package.")
    w("")
    w("## Obligations that go beyond attribution")
    w("")
    w("Most entries below are permissive: keep the copyright line and the")
    w("license text with the binary and there is nothing further to do. These")
    w("are the ones that are not, stated plainly rather than buried in the")
    w("table. **Read them before publishing a closed-source binary.**")
    w("")
    w("No component in the shipped closure is copyleft. Every entry that")
    w("offers a copyleft option offers a permissive one beside it, and the")
    w("permissive one is what is taken; the sections here record the")
    w("attribution each of those still asks for.")
    w("")
    w("### FreeType - FTL OR GPL-2.0-or-later")
    w("")
    w("Under the FreeType License the following credit must appear in the")
    w("documentation of any product using FreeType, which this file is:")
    w("")
    w("> Portions of this software are copyright (c) The FreeType Project")
    w("> (https://www.freetype.org). All rights reserved.")
    w("")
    w("### glslang - carries a GPL-3.0-or-later component")
    w("")
    w("glslang's package license is recorded as")
    w("`Apache-2.0 AND BSD-3-Clause AND MIT AND GPL-3.0-or-later`. The")
    w("GPL-covered part is the parser generator's output, which upstream")
    w("distributes with the generator's linking exception; the full text as")
    w("upstream ships it is reproduced below and has not been independently")
    w("audited here.")
    w("")
    w("### Dual-licensed entries where the permissive side is elected")
    w("")
    w("`zstd` (BSD-3-Clause OR GPL-2.0-only), `stb` (MIT OR CC-PDDC),")
    w("`vulkan-headers` (Apache-2.0 OR MIT) and `freetype` (FTL OR")
    w("GPL-2.0-or-later) are distributed under the permissive alternative.")
    w("")
    w("## What ships where")
    w("")
    w("The table below reads: **E** " + TIER_NAMES["E"] + "; **D** "
      + TIER_NAMES["D"] + "; **M** " + TIER_NAMES["M"] + "; **W** "
      + TIER_NAMES["W"] + ".")
    w("")
    w("The marks come from the link closures of the built artifacts, plus the")
    w("platform gates in `vcpkg.json`. Where a component ships on one render")
    w("flavor of a platform and not the other, the table marks the platform:")
    w("over-inclusion in a notice costs nothing, and a missing entry is the")
    w("failure this file exists to prevent.")
    w("")
    w("| Component | Version | License | Ships in | Role |")
    w("|---|---|---|---|---|")
    for name, _source, spdx, version, home, tiers, role in all_components:
        w("| [%s](#%s) | %s | `%s` | %s | %s |"
          % (name, anchor(name), version, spdx, tiers, role))
    for name, spdx, version, home, tiers, role in PENDING:
        w("| %s | %s | `%s` | %s | %s |" % (name, version, spdx, tiers, role))
    w("")
    w("Upstream homepages are recorded per component in the sections below.")
    w("")
    w("### Linux-only: the HTTP client closure")
    w("")
    w("The Linux desktop builds are the one platform whose own HTTP stack the")
    w("engine cannot reach, so they link libcurl and the TLS library it")
    w("resolves against. The verbatim license texts of those two are NOT")
    w("embedded below - they come from a Linux install tree, and this document")
    w("was assembled on macOS. **A published Linux binary needs them added**;")
    w("the sources are `share/curl/copyright` and the TLS port's own copyright")
    w("file in the Linux `vcpkg_installed` tree, and the licenses are the ones")
    w("named in the table above.")
    w("")
    w("## What does not ship, and why it carries no notice here")
    w("")
    w("Attribution obligations attach to DISTRIBUTION. These are used while")
    w("building or testing Orkige and are in no shipped artifact, so they are")
    w("named here rather than given a license section:")
    w("")
    w("| Component | License | Why it is not in a binary |")
    w("|---|---|---|")
    w("| Catch2 | BSL-1.0 | the unit-test executables only; no shipped target links it |")
    w("| SDL2 | Zlib | installed as a render-backend sample dependency; the engine links SDL3 and never SDL2 |")
    w("| vcpkg helper ports (`vcpkg-cmake`, `vcpkg-cmake-config`, ...) | MIT | build scripts; they emit no code |")
    w("| OpenGL / EGL registry headers | Apache-2.0 / MIT | header registries consumed at build time |")
    w("| The Android SDK build tools and JDK | vendor terms | invoked as programs on the packaging machine; nothing from them is redistributed except the developer's own compiled classes |")
    w("| CMake, Ninja, Emscripten, the Android NDK | their own terms | toolchains. Orkige ships an engine, never a toolchain. |")
    w("")
    w("## Components vendored in this repository")
    w("")
    w("Four third-party sources live in the tree rather than coming from a")
    w("package, and keep their upstream notice in the file itself:")
    w("")
    w("| File | Component | License |")
    w("|---|---|---|")
    w("| `orkige_core/core_util/FastDelegate.h` | FastDelegate 1.5 (Don Clugston) | CodeProject Open License |")
    w("| `orkige_core/core_util/MacroRepeat.h` | MacroRepeat (Robert Geiman, 2004) | as stated in the file |")
    w("| `orkige_core/core_util/ObjectFactory.h` | ObjectFactory (Robert Geiman, 2004) | as stated in the file |")
    w("| `tools/editor/IconsFontAwesome6.h` | IconFontCppHeaders glyph table | Zlib (reproduced below) |")
    w("")
    w("## License texts")
    w("")
    w("Each section reproduces the component's own copyright and license file")
    w("verbatim.")
    w("")

    for name, source, spdx, version, home, tiers, role in all_components:
        w("### %s" % name)
        w("")
        w("`%s` - %s - version %s" % (spdx, home, version))
        w("")
        w(indent_block(text_of(source)))
        w("")

    return "\n".join(out) + "\n"


def default_share_directory():
    """a configured build tree's vcpkg share directory - the widest closure a
    dev machine has. Preferred in preset order rather than by mtime, so two
    runs on the same machine read the same tree."""
    for preset in ("macos-debug", "macos-release", "linux-debug-next",
                   "linux-release-next", "windows-debug"):
        for candidate in sorted(glob.glob(os.path.join(
                REPO, "build", preset, "vcpkg_installed", "*", "share"))):
            if os.path.isdir(os.path.join(candidate, "zlib")):
                return candidate
    return ""


def run():
    global OSX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--share", default="",
                        help="a vcpkg_installed/<triplet>/share directory")
    parser.add_argument("--out", default=os.path.join(REPO,
                                                      "THIRD-PARTY-NOTICES.md"))
    args = parser.parse_args()
    OSX = args.share or default_share_directory()
    if not OSX or not os.path.isdir(OSX):
        print("make_third_party_notices: no vcpkg share directory (pass "
              "--share <vcpkg_installed/<triplet>/share>); a configured build "
              "tree is where the license texts come from", file=sys.stderr)
        return 1
    missing = [name for _n, source, *_rest in COMPONENTS
               for name in [source]
               if not os.path.isfile(os.path.join(OSX, name, "copyright"))]
    if missing:
        print("make_third_party_notices: '%s' carries no copyright file for: "
              "%s - use an install tree with the whole desktop closure"
              % (OSX, ", ".join(sorted(missing))), file=sys.stderr)
        return 1
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(main())
    print("make_third_party_notices: wrote %s from %s" % (args.out, OSX))
    return 0


if __name__ == "__main__":
    sys.exit(run())
