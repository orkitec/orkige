#!/usr/bin/env python3
"""Currency gate for THIRD-PARTY-NOTICES.md.

The notices file discharges a distribution obligation: several licenses in the
statically linked closure require their text to travel with the BINARY. A
dependency added to vcpkg.json or to ports/ therefore has to be answered here
in the same change, one of two ways - a section in the notices file, or an
entry in BUILD_TIME_ONLY saying why it reaches no shipped artifact.

The check is deliberately mechanical and stdlib-only. It cannot tell whether a
license text is CORRECT; it can tell that a dependency was added and nobody
wrote anything down, which is the failure mode a notice file actually has.

    python3 Util/check_third_party_notices.py [--selftest]
"""

import argparse
import json
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTICES_FILE = "THIRD-PARTY-NOTICES.md"
MANIFEST_FILE = "vcpkg.json"
PORTS_DIR = "ports"

# port name -> the heading its section carries in the notices file. A port
# whose package name and display name differ needs the mapping written down;
# there is no guessing rule that survives ImGuiColorTextEdit and libjpeg-turbo
# in the same table.
SHIPPED = {
    "assimp": "Open Asset Import Library",
    "brotli": "Brotli",
    "bzip2": "bzip2",
    "earcut-hpp": "earcut.hpp",
    "fmt": "{fmt}",
    "freeimage": "FreeImage",
    "freetype": "FreeType",
    "glslang": "glslang",
    "imath": "Imath",
    "imgui": "Dear ImGui",
    "imgui-color-text-edit": "ImGuiColorTextEdit",
    "imguizmo": "ImGuizmo",
    "jasper": "JasPer",
    "joltphysics": "Jolt Physics",
    "jxrlib": "jxrlib",
    "ktx": "KTX-Software (libktx)",
    "kubazip": "kubazip",
    "lcms": "Little CMS",
    "libdeflate": "libdeflate",
    "libjpeg-turbo": "libjpeg-turbo",
    "liblzma": "liblzma (XZ Utils)",
    "libpng": "libpng",
    "libraw": "LibRaw",
    "libvterm": "libvterm",
    "libwebp": "libwebp",
    "lua": "Lua",
    "minizip": "minizip",
    "nanosvg": "nanosvg",
    "ogre": "OGRE",
    "ogre-next": "OGRE-Next",
    "openal-soft": "OpenAL Soft",
    "openexr": "OpenEXR",
    "openjpeg": "OpenJPEG",
    "openjph": "OpenJPH",
    "poly2tri": "poly2tri",
    "polyclipping": "Clipper",
    "pugixml": "pugixml",
    "rapidjson": "RapidJSON",
    "sdl3": "SDL3",
    "sol2": "sol2",
    "stb": "stb",
    "tiff": "LibTIFF",
    "tinyxml2": "TinyXML-2",
    "utfcpp": "UTF8-CPP",
    "vulkan-headers": "Vulkan-Headers",
    "vulkan-loader": "Vulkan-Loader",
    "zlib": "zlib",
    "zstd": "Zstandard",
}

# ports that reach no shipped artifact, with the reason. Attribution attaches
# to DISTRIBUTION, so these need no license section - but they need a written
# answer, or "it is not in the notices" stops meaning anything.
BUILD_TIME_ONLY = {
    "catch2": "the unit-test executables only; no shipped target links it",
    "sdl2": "installed as a render-backend sample dependency; the engine "
            "links SDL3 and never SDL2",
    "egl-registry": "a header registry consumed at build time",
    "opengl-registry": "a header registry consumed at build time",
}

# ports that DO ship but whose verbatim license text is not embedded yet, with
# the reason. Each is named in the notices file's own pending section, and each
# run of this check prints it, so the gap is loud rather than forgotten. It is
# not a failure: refusing every build over a text file nobody can fetch on this
# host would only teach people to disable the check.
PENDING_TEXT = {
    "curl": "Linux-only; its copyright file lives in a Linux vcpkg install "
            "tree, which the notices file was not assembled on",
}
#: the phrase the notices file's pending section has to carry, so a port cannot
#: be quietly moved into PENDING_TEXT without the document saying so
PENDING_MARKER = "the HTTP client closure"

# entries carried in the notices file that no port declares: media and sources
# committed to this tree. Listed so the file's own sections stay accounted for.
COMMITTED = ("Nunito", "Font Awesome Free", "IconFontCppHeaders", "DejaVu Sans")

# the vendored third-party sources doctrine_lint sanctions by name; the notices
# file records each one's origin
VENDORED_FILES = (
    "orkige_core/core_util/FastDelegate.h",
    "orkige_core/core_util/MacroRepeat.h",
    "orkige_core/core_util/ObjectFactory.h",
    "tools/editor/IconsFontAwesome6.h",
)


def manifest_ports(manifest):
    """every port name the manifest depends on, across every platform gate and
    every optional feature - a dependency that only a mobile preset installs
    still ships in that preset's binary"""
    names = set()

    def collect(entries):
        for entry in entries or ():
            names.add(entry if isinstance(entry, str) else entry.get("name"))

    collect(manifest.get("dependencies"))
    for feature in (manifest.get("features") or {}).values():
        collect(feature.get("dependencies"))
    names.discard(None)
    return names


def overlay_ports(root):
    ports = os.path.join(root, PORTS_DIR)
    if not os.path.isdir(ports):
        return set()
    return {name for name in os.listdir(ports)
            if os.path.isdir(os.path.join(ports, name))}


def sections(text):
    """the `### <name>` headings the notices file carries"""
    found = []
    for line in text.splitlines():
        if line.startswith("### "):
            found.append(line[4:].strip())
    return found


def check(root, notes=None):
    problems = []
    notices_path = os.path.join(root, NOTICES_FILE)
    if not os.path.isfile(notices_path):
        return ["%s is missing - every published binary needs it" %
                NOTICES_FILE]
    with open(notices_path, encoding="utf-8") as handle:
        text = handle.read()
    headings = sections(text)

    with open(os.path.join(root, MANIFEST_FILE), encoding="utf-8") as handle:
        manifest = json.load(handle)
    declared = manifest_ports(manifest) | overlay_ports(root)

    for port in sorted(declared):
        if port in BUILD_TIME_ONLY:
            continue
        if port in PENDING_TEXT:
            if PENDING_MARKER not in text:
                problems.append(
                    "'%s' is recorded as awaiting its license text, but %s "
                    "carries no pending section saying so" % (port,
                                                              NOTICES_FILE))
            elif notes is not None:
                notes.append("'%s' ships without its verbatim license text "
                             "(%s)" % (port, PENDING_TEXT[port]))
            continue
        heading = SHIPPED.get(port)
        if heading is None:
            problems.append(
                "'%s' is a declared dependency with no answer: give it a "
                "section in %s and a SHIPPED entry here, or a BUILD_TIME_ONLY "
                "entry saying why it reaches no shipped artifact"
                % (port, NOTICES_FILE))
        elif heading not in headings:
            problems.append("'%s' maps to the section '%s', which %s does not "
                            "carry" % (port, heading, NOTICES_FILE))

    for heading in COMMITTED:
        if heading not in headings:
            problems.append("%s carries no section for the committed "
                            "third-party entry '%s'" % (NOTICES_FILE, heading))

    for relative in VENDORED_FILES:
        if not os.path.isfile(os.path.join(root, relative)):
            continue    # the file is gone; nothing to attribute
        if relative not in text:
            problems.append("%s does not record the vendored source '%s'"
                            % (NOTICES_FILE, relative))

    # a heading with no fenced block after it is an entry with no license text,
    # which is the one thing this file exists to carry
    for heading in headings:
        if heading not in SHIPPED.values() and heading not in COMMITTED:
            continue
        after = text.split("### " + heading + "\n", 1)[-1]
        head = after.split("\n### ", 1)[0]
        if "```text" not in head:
            problems.append("the section '%s' carries no license text block"
                            % heading)
    return problems


SELFTEST_NOTICES = """# Third-party notices

### Lua

`MIT`

```text
Copyright the Lua authors
```

### Nunito

```text
SIL Open Font License
```

### Font Awesome Free

```text
CC BY 4.0
```

### IconFontCppHeaders

```text
zlib
```

### DejaVu Sans

```text
Bitstream Vera
```
"""


def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as temp:
        os.makedirs(os.path.join(temp, PORTS_DIR, "ogre"))
        with open(os.path.join(temp, MANIFEST_FILE), "w") as handle:
            json.dump({"dependencies": ["lua", "catch2"]}, handle)
        with open(os.path.join(temp, NOTICES_FILE), "w") as handle:
            handle.write(SELFTEST_NOTICES)
        # the overlay port `ogre` has a SHIPPED mapping but no section
        problems = check(temp)
        assert any("OGRE" in problem for problem in problems), problems
        # ...and catch2 is answered by BUILD_TIME_ONLY, so it never complains
        assert not any("catch2" in problem for problem in problems), problems

        # an undeclared new dependency is the failure this gate exists for
        with open(os.path.join(temp, MANIFEST_FILE), "w") as handle:
            json.dump({"dependencies": ["lua", {"name": "brand-new-lib"}]},
                      handle)
        problems = check(temp)
        assert any("brand-new-lib" in problem for problem in problems), problems

        # a feature's dependency counts too: a preset that installs it ships it
        with open(os.path.join(temp, MANIFEST_FILE), "w") as handle:
            json.dump({"dependencies": ["lua"],
                       "features": {"x": {"dependencies": ["feature-only"]}}},
                      handle)
        assert any("feature-only" in problem for problem in check(temp))

        # a clean tree: only the overlay port is left to answer for
        os.rmdir(os.path.join(temp, PORTS_DIR, "ogre"))
        with open(os.path.join(temp, MANIFEST_FILE), "w") as handle:
            json.dump({"dependencies": ["lua"]}, handle)
        assert check(temp) == [], check(temp)

        # a port awaiting its license text is a NOTE, not a failure - but only
        # while the document itself says the gap is there
        pending = sorted(PENDING_TEXT)[0]
        with open(os.path.join(temp, MANIFEST_FILE), "w") as handle:
            json.dump({"dependencies": ["lua", pending]}, handle)
        problems = check(temp)
        assert any(pending in problem for problem in problems), problems
        with open(os.path.join(temp, NOTICES_FILE), "w") as handle:
            handle.write(SELFTEST_NOTICES + "\n### " + PENDING_MARKER + "\n")
        notes = []
        assert check(temp, notes) == [], check(temp)
        assert any(pending in note for note in notes), notes

        # a section with no license text block is an entry that carries nothing
        with open(os.path.join(temp, NOTICES_FILE), "w") as handle:
            handle.write(SELFTEST_NOTICES.replace(
                "```text\nCopyright the Lua authors\n```\n", ""))
        assert any("no license text block" in problem
                   for problem in check(temp)), check(temp)
    print("check_third_party_notices: selftest OK")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true",
                        help="run the embedded fixture checks and exit")
    parser.add_argument("--root", default=REPO_ROOT)
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    notes = []
    problems = check(args.root, notes)
    for note in notes:
        print("check_third_party_notices: NOTE " + note)
    for problem in problems:
        print("check_third_party_notices: " + problem, file=sys.stderr)
    if problems:
        print("check_third_party_notices: %d problem(s)" % len(problems),
              file=sys.stderr)
        return 1
    print("check_third_party_notices: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
