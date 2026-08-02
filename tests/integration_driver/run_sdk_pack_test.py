#!/usr/bin/env python3
"""Prove the installed Orkige SDK pack is a complete, relocatable engine: a
project's compiled C++ game code configures, builds and RUNS against it with no
engine source tree and no engine build tree in reach.

That is the whole point of the pack (cmake/OrkigeSdk.cmake, Docs/sdk-pack.md).
A distributed editor carries no checkout, so a native-module project must get
everything - headers, archives, the dependency closure, the cmake surface -
from one directory it can unpack anywhere. The legs, in order:

  1. INSTALL      cmake --install the engine build tree into a scratch prefix,
                  then RELOCATE the result by renaming it. Everything after
                  reads only the renamed copy, so a path baked at install time
                  fails here rather than on a user's machine.
  2. SELF-CONTAINED  no text file in the pack's cmake surface or in its
                  dependency closure's cmake/pkg-config files may name the
                  engine source tree, the engine build tree or the machine's
                  vcpkg root.
  3. CONFIGURATION  the engine archives and the dependency closure must be the
                  SAME configuration, with the other half absent entirely.
                  A dependency's headers compile differently per configuration,
                  so mixing halves means the archives call into code the
                  shipped libraries do not contain. The pack must also carry no
                  host EXECUTABLES: a module build invokes none, and a
                  downloaded archive's executables meet Gatekeeper.
  3b. TARGET      a pack declares what it targets - platform, architectures,
                  triplet, module shape, OS floor, toolchain and standard
                  library - in the vocabulary projects are written against.
                  Packs are built per target, so the whole schema must be
                  present (empty where a target has no answer) and what a host
                  pack can answer must be right.
  3c. PRIVATE     no installed header may read a definition the engine keeps
                  PRIVATE. The compile contract is captured from the PUBLIC
                  set, so this is the floor under that capture: a private
                  define a header reads would change the header's meaning for
                  a consumer with nothing to say so.
  4. SURFACE      every engine header the source tree carries must be IN the
                  pack at the same layer-rooted path, and a translation unit
                  that includes all of them (minus the render backend the
                  pack's flavor does not build) must compile and link against
                  it. A header missing from the install set is a test failure
                  here instead of a consumer's build error later. Both lists
                  come from the SOURCE TREE, never from the pack - reading the
                  pack would make the check circular.
  5. ACCEPTANCE   configure + build + RUN projects/jumper-native against the
                  pack inside a CLEAN ROOM where the engine source tree and
                  the engine build tree are denied outright. Denial matters:
                  a build tree sitting at its usual absolute path would
                  silently satisfy a configure that should have failed. Where
                  the artifact landed is READ from the manifest the build
                  wrote, and the frame the module renders is examined: its own
                  self-check is a gui widget-state assertion that a blank
                  window would pass, so the pixels have to say a scene is
                  there.
  5b. CLOSURE     every library on the module's real link line must resolve
                  INSIDE the pack. The clean room denies our trees but cannot
                  deny the platform or a package manager's prefix, so a
                  host-installed copy could satisfy the link and leave the pack
                  looking self-sufficient here and broken on a user's machine.
                  Only the platform's own runtime is excepted, by name.
  6. COMPILE CONTRACT  every ABI-relevant definition AND compile option the
                  package RECORDS must appear on the module's actual compiler
                  command line. Leg 5 only catches a missing one when
                  something happens to reference the symbol it changes; this
                  catches the layout and inline-behaviour cases that would
                  otherwise link and then misbehave.
  7. ABI GUARD    edit an installed header in a throwaway copy of the pack and
                  assert the configure REFUSES with the ABI-mismatch message -
                  the pack form of the stale-library guard.

Exit 0 = the pack holds. 77 = the machine cannot run the check (no build tree,
no generator). Anything else = a diagnostic and a real failure.
"""

import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "Util"))
import orkige_png  # noqa: E402  (sibling Util helper - stdlib-only PNG codec)

# A pack SHIPS every engine header, including the ones only one render flavor
# can compile - the install set is the whole surface. The probe, though,
# compiles as one flavor, so it leaves out the headers that flavor genuinely
# cannot compile: the other backend's private implementation of the
# engine_render facade, which includes ITS OGRE umbrella (a dependency this
# pack does not carry, and whose own guard says so with an #error).
#
# That set is DERIVED, never listed by hand, so it cannot rot as headers move.
# It seeds on the headers that SAY they refuse this flavor - a flavor-guarded
# `#error` right under an `#ifdef`/`#ifndef ORKIGE_RENDER_*`, which is how the
# engine states the rule for itself - plus the other backend's directory, and
# then closes over UNCONDITIONAL includes (a gated include compiles away, so it
# does not carry the refusal). Game code above the facade spells facade types,
# so nothing excluded here is a surface a consumer could miss.
FLAVOR_BACKEND_DIRS = {
    "classic": "engine_render_classic",
    "next": "engine_render_next",
}
FLAVOR_MACRO = {"classic": "ORKIGE_RENDER_CLASSIC", "next": "ORKIGE_RENDER_NEXT"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
CONDITIONAL_OPEN_RE = re.compile(r'^\s*#\s*if')
CONDITIONAL_CLOSE_RE = re.compile(r'^\s*#\s*endif')
IFNDEF_RE = re.compile(r'^\s*#\s*ifndef\s+(\S+)')
DEFINE_RE = re.compile(r'^\s*#\s*define\s+(\S+)')
FLAVOR_GUARD_RE = re.compile(r'^\s*#\s*(ifdef|ifndef)\s+(ORKIGE_RENDER_\w+)\s*$')
ERROR_RE = re.compile(r'^\s*#\s*error\b')

MISMATCH_SENTINEL = "Orkige engine ABI mismatch"
PACK_VALUE_MARKER = "ORKIGE_PACK_VALUE:"

# Definitions that MUST reach a consumer's command line whatever else the
# contract holds - the ones whose absence silently changes what the engine
# headers mean rather than failing to compile. They are named here so an empty
# or truncated contract cannot pass the compile-contract leg vacuously; the
# leg's real work is comparing against everything the package recorded.
CONTRACT_MUST_INCLUDE = ("ORKIGE_STATIC",)
CONTRACT_FLAVOR_MACRO = {"next": "ORKIGE_RENDER_NEXT",
                         "classic": "ORKIGE_RENDER_CLASSIC"}


def log(message):
    sys.stdout.write("sdk_pack: %s\n" % message)
    sys.stdout.flush()


def fail(message):
    sys.stderr.write("sdk_pack: FAILED - %s\n" % message)
    sys.exit(1)


def skip(message):
    log("skipped - %s" % message)
    sys.exit(77)


def load_module(name, path):
    """import a sibling driver by path - the clean-room sandbox profile is
    written in exactly ONE place (run_editor_bundle_test.py) and reused here"""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command, cwd=None, env=None, what="command", timeout=1800):
    result = subprocess.run(command, cwd=cwd, env=env, timeout=timeout,
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        fail("%s failed (%d): %s"
             % (what, result.returncode, " ".join(command[:4])))
    return result


def directory_size(path):
    total = 0
    for dirpath, _dirnames, files in os.walk(path):
        for name in files:
            full = os.path.join(dirpath, name)
            if not os.path.islink(full):
                total += os.path.getsize(full)
    return total


def megabytes(value):
    return "%.1f MB" % (value / (1024.0 * 1024.0))


def engine_headers(repo):
    """every engine header the SOURCE tree carries, layer-rooted exactly as an
    include line spells it - the install set the pack must reproduce"""
    headers = []
    for layer in ("orkige_core", "orkige_engine"):
        base = os.path.join(repo, layer)
        for dirpath, _dirnames, files in os.walk(base):
            if "media" in os.path.relpath(dirpath, base).split(os.sep):
                continue
            for name in sorted(files):
                if name.endswith(".h"):
                    relative = os.path.relpath(os.path.join(dirpath, name), base)
                    headers.append(relative.replace(os.sep, "/"))
    headers.sort()
    return headers


def unconditional_includes(path):
    """the engine-local headers this file includes OUTSIDE any preprocessor
    conditional - a gated include compiles away on the flavor that gates it.
    The file's own `#ifndef`/`#define` include guard is not a conditional in
    that sense (every header in this tree carries one), so it is discounted."""
    reached = []
    depth = 0
    guard = None
    with open(path, "r", errors="ignore") as handle:
        for line in handle:
            opening = IFNDEF_RE.match(line)
            if opening and guard is None and depth == 0:
                # the candidate include guard; confirmed by the #define below
                guard = opening.group(1)
                continue
            defined = DEFINE_RE.match(line)
            if defined and guard is not None and defined.group(1) == guard:
                guard = ""      # confirmed - stay at depth 0 inside the guard
                continue
            if guard:           # an #ifndef that was NOT the guard after all
                depth += 1
                guard = ""
            if CONDITIONAL_OPEN_RE.match(line):
                depth += 1
            elif CONDITIONAL_CLOSE_RE.match(line):
                depth = max(0, depth - 1)
            elif depth == 0:
                match = INCLUDE_RE.match(line)
                if match:
                    reached.append(match.group(1))
    return reached


def refuses_flavor(path, flavor):
    """does this header state, with a flavor-guarded #error, that the flavor
    being built may not compile it?"""
    lines = []
    with open(path, "r", errors="ignore") as handle:
        lines = handle.readlines()
    for index, line in enumerate(lines):
        guard = FLAVOR_GUARD_RE.match(line)
        if not guard:
            continue
        # the refusal sits directly under its guard, allowing for a comment
        for follower in lines[index + 1:index + 4]:
            if ERROR_RE.match(follower):
                defined = guard.group(2) == FLAVOR_MACRO[flavor]
                return defined if guard.group(1) == "ifdef" else not defined
            if follower.strip() and not follower.lstrip().startswith(("//", "/*", "*")):
                break
    return False


def flavor_bound_headers(repo, headers, flavor):
    """the headers only the OTHER flavor's engine build can compile: the ones
    that refuse this flavor outright, the other backend's directory, and
    everything that reaches one of those unconditionally"""
    other = "classic" if flavor == "next" else "next"
    backend = FLAVOR_BACKEND_DIRS[other] + "/"
    bound = set(header for header in headers if header.startswith(backend))

    edges = {}
    for header in headers:
        for layer in ("orkige_core", "orkige_engine"):
            path = os.path.join(repo, layer, header.replace("/", os.sep))
            if os.path.isfile(path):
                edges[header] = unconditional_includes(path)
                if refuses_flavor(path, flavor):
                    bound.add(header)
                break

    known = set(headers)
    changed = True
    while changed:
        changed = False
        for header, targets in edges.items():
            if header in bound:
                continue
            for target in targets:
                if target in known and target in bound:
                    bound.add(header)
                    changed = True
                    break
    return bound


def write_file(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as handle:
        handle.write(text)


def module_project(name, source):
    """the CMakeLists a project written against the pack carries: include the
    pack's own copy of the game-module helper and name its sources. The helper
    creates the target in the shape this pack's target needs, so the project
    file never spells a shape of its own."""
    return ("cmake_minimum_required(VERSION 3.28)\n"
            "project(%s LANGUAGES CXX)\n"
            'include("${ORKIGE_ROOT}/cmake/OrkigeGameModule.cmake")\n'
            "orkige_add_game_module(%s %s)\n" % (name, name, source))


def read_artifact_manifest(build, target):
    """what the build WROTE about the module it produced. The manifest is the
    contract for 'where did the artifact land' - a desktop guess of
    <build>/<target> is wrong on every non-desktop target - so the test reads
    it exactly as the editor and the exporter do."""
    manifest = os.path.join(build, "orkige_module_artifact.txt")
    if not os.path.isfile(manifest):
        fail("the module build wrote no orkige_module_artifact.txt - the "
             "helper is meant to report where the artifact landed rather than "
             "leaving its consumers to guess")
    values = {}
    with open(manifest) as handle:
        for line in handle:
            key, _, value = line.strip().partition("=")
            values[key] = value
    for required in ("target", "platform", "shape", "artifact"):
        if not values.get(required):
            fail("the module artifact manifest has no '%s': %r"
                 % (required, values))
    if values["target"] != target:
        fail("the artifact manifest names target '%s', expected '%s'"
             % (values["target"], target))
    log("artifact: the build reports a '%s' %s at %s"
        % (values["platform"], values["shape"], values["artifact"]))
    return values["artifact"]


def cmake_path(path):
    """CMake reads backslashes in a -D value as escapes; forward slashes are
    portable everywhere including Windows"""
    return path.replace("\\", "/")


def read_pack_values(args, pack, names):
    """what the pack SAYS about itself, read by INCLUDING its own
    OrkigeConfig.cmake - the same file a consumer's find_package runs, so the
    test reads the real thing rather than a re-implementation of it.

    A throwaway LANGUAGES NONE project rather than `cmake -P`: the config
    creates imported targets, and add_library is not available in script mode.
    NONE needs no compiler, so it stays cheap."""
    source = os.path.join(args.stage, "read-pack-values")
    lines = ["cmake_minimum_required(VERSION 3.28)",
             "project(orkige_pack_values LANGUAGES NONE)",
             'include("%s/cmake/OrkigeConfig.cmake")' % cmake_path(pack)]
    for name in names:
        lines.append('message("%s%s=${%s}")' % (PACK_VALUE_MARKER, name, name))
    write_file(os.path.join(source, "CMakeLists.txt"), "\n".join(lines) + "\n")
    result = subprocess.run(
        [args.cmake, "-S", source, "-B", os.path.join(source, "build")],
        timeout=300, capture_output=True, text=True)
    text = result.stdout + result.stderr
    values = {}
    for line in text.splitlines():
        if line.startswith(PACK_VALUE_MARKER):
            key, _, value = line[len(PACK_VALUE_MARKER):].partition("=")
            values[key] = value
    missing = [name for name in names if name not in values]
    if missing:
        sys.stderr.write(text)
        fail("the pack's OrkigeConfig.cmake did not report %s"
             % ", ".join(missing))
    return values


def configure_against_pack(args, source, build, pack, extra=None, env=None,
                           sandbox=None, cwd=None, allow_failure=False,
                           build_type=None):
    command = [args.cmake, "-S", source, "-B", build,
               "-DCMAKE_BUILD_TYPE=%s" % (build_type or args.pack_build_type),
               "-DORKIGE_ROOT=%s" % cmake_path(pack)]
    if args.generator:
        command[1:1] = ["-G", args.generator]
    if args.ignore_prefix_path:
        command.append("-DCMAKE_IGNORE_PREFIX_PATH=%s"
                       % cmake_path(args.ignore_prefix_path))
    command.extend(extra or [])
    if sandbox:
        command = ["sandbox-exec", "-f", sandbox] + command
    result = subprocess.run(command, env=env, cwd=cwd, timeout=1800,
                            capture_output=True, text=True)
    if allow_failure:
        return result
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        fail("configure against the pack failed (%d)" % result.returncode)
    return result


def build_target(args, build, env=None, sandbox=None, cwd=None):
    command = [args.cmake, "--build", build]
    if sandbox:
        command = ["sandbox-exec", "-f", sandbox] + command
    return run(command, env=env, cwd=cwd, what="build against the pack")


# --- leg 1: install + relocate ----------------------------------------------

def install_pack(args, stage):
    staged = os.path.join(stage, "installed")
    run([args.cmake, "--install", args.engine_build, "--prefix", staged,
         "--component", "sdk"], what="cmake --install of the SDK pack")

    # RELOCATE: the pack a consumer downloads is never at the path it was built
    # at. Renaming is the cheapest honest proof, and every later leg reads only
    # this path.
    pack = os.path.join(stage, "unpacked-elsewhere", "orkige-sdk")
    os.makedirs(os.path.dirname(pack), exist_ok=True)
    os.rename(staged, pack)

    for required in ("cmake/OrkigeConfig.cmake", "cmake/OrkigeConfigVersion.cmake",
                     "cmake/OrkigeGameModule.cmake", "cmake/OrkigeSdkPack.cmake",
                     "cmake/OrkigeAbiStamp.txt", "include", "lib", "vcpkg"):
        if not os.path.exists(os.path.join(pack, required)):
            fail("the installed pack has no '%s'" % required)

    total = directory_size(pack)
    log("pack installed and relocated to %s" % pack)
    log("pack size: %s" % megabytes(total))
    for part in sorted(os.listdir(pack)):
        full = os.path.join(pack, part)
        if os.path.isdir(full):
            log("  %-10s %s" % (part + "/", megabytes(directory_size(full))))
    with open(os.path.join(pack, "cmake", "OrkigeAbiStamp.txt")) as handle:
        log("pack ABI stamp: %s" % handle.read().strip())
    return pack


# --- leg 2: self-containment -------------------------------------------------

def assert_self_contained(args, pack):
    """no build-machine path may survive into the pack. The cmake surface and
    the closure's cmake/pkg-config files are what a consumer's configure reads,
    so those are the files that decide whether a pack works on another machine.
    """
    forbidden = [os.path.realpath(args.repo), os.path.realpath(args.engine_build)]
    if args.vcpkg_root:
        forbidden.append(os.path.realpath(args.vcpkg_root))
    roots = [os.path.join(pack, "cmake"),
             os.path.join(pack, "vcpkg", "share"),
             os.path.join(pack, "vcpkg", "lib", "pkgconfig")]
    offenders = []
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirnames, files in os.walk(root):
            for name in files:
                if not name.endswith((".cmake", ".pc", ".txt", ".in")):
                    continue
                full = os.path.join(dirpath, name)
                try:
                    with open(full, "r", errors="ignore") as handle:
                        text = handle.read()
                except OSError:
                    continue
                for path in forbidden:
                    if path in text:
                        offenders.append("%s names %s"
                                         % (os.path.relpath(full, pack), path))
    if offenders:
        fail("the pack is not relocatable - build-machine paths survived:\n  "
             + "\n  ".join(offenders[:20]))
    log("self-contained: no engine source, build tree or vcpkg root path in "
        "the pack's cmake or pkg-config surface")


def assert_configuration_coherent(args, pack):
    """the pack's engine archives and its dependency closure must be the SAME
    configuration.

    This is the property whose absence produces the nastiest class of failure:
    a dependency's headers compile differently per configuration (Jolt enables
    its asserts where NDEBUG is absent), so a Debug engine archive calls
    symbols only the debug build of that dependency defines. Shipping the other
    half is an undefined symbol at a consumer's link when it is lucky, and a
    binary that links and then misbehaves when it is not. Assert the shape
    directly rather than waiting for a link to notice."""
    closure = os.path.join(pack, "vcpkg")
    debug_half = os.path.isdir(os.path.join(closure, "debug", "lib"))
    release_half = os.path.isdir(os.path.join(closure, "lib"))
    if debug_half and release_half:
        fail("the pack ships BOTH halves of its dependency closure - a "
             "consumer could resolve either and mix configurations")
    if not debug_half and not release_half:
        fail("the pack ships NO dependency libraries under vcpkg/")
    closure_config = "Debug" if debug_half else "Release"
    expected = "Debug" if args.pack_build_type == "Debug" else "Release"
    if closure_config != expected:
        fail("the pack's engine archives are %s but its dependency closure is "
             "%s - the halves must match (see cmake/OrkigeSdk.cmake)"
             % (args.pack_build_type, closure_config))

    # and the per-config imported-target files of the half that is NOT here
    # must be gone, or a consumer's configure trips an import check on
    # archives the pack does not carry
    stale = "release" if debug_half else "debug"
    leftovers = []
    share = os.path.join(closure, "share")
    for dirpath, _dirnames, files in os.walk(share):
        for name in files:
            if name.endswith("-%s.cmake" % stale):
                leftovers.append(os.path.relpath(os.path.join(dirpath, name),
                                                 pack))
    if leftovers:
        fail("%d %s-configuration target file(s) survived into a %s pack:\n  "
             % (len(leftovers), stale, closure_config)
             + "\n  ".join(sorted(leftovers)[:20]))
    log("configuration: engine archives and dependency closure are both %s, "
        "and the %s half is absent entirely" % (closure_config, stale))


# --- leg 3: the public header surface ----------------------------------------

def run_surface_probe(args, stage, pack):
    headers = engine_headers(args.repo)
    if len(headers) < 50:
        fail("only %d engine headers found under '%s' - wrong repo root?"
             % (len(headers), args.repo))

    # COMPLETENESS: every source header, at the same layer-rooted path
    missing = [header for header in headers
               if not os.path.isfile(os.path.join(pack, "include",
                                                  header.replace("/", os.sep)))]
    if missing:
        fail("%d engine header(s) are missing from the pack's include set - "
             "the install rules in cmake/OrkigeSdk.cmake do not cover them:\n  "
             % len(missing) + "\n  ".join(sorted(missing)[:20]))
    log("surface: all %d engine headers are installed, layer-rooted" % len(headers))

    # COMPILABILITY: one TU over everything this flavor can compile
    bound = flavor_bound_headers(args.repo, headers, args.flavor)
    probed = [header for header in headers if header not in bound]
    source = os.path.join(stage, "surface")
    body = "".join("#include <%s>\n" % header for header in probed)
    body += ("// the probe LINKS as well as compiles, so the archives the pack\n"
             "// ships are proven usable, not merely present\n"
             "int main() { return 0; }\n")
    write_file(os.path.join(source, "surface.cpp"), body)
    write_file(os.path.join(source, "CMakeLists.txt"),
               module_project("orkige_surface_probe", "surface.cpp"))
    build = os.path.join(stage, "surface-build")
    configure_against_pack(args, source, build, pack)
    build_target(args, build)
    log("surface: %d of them compile and link against the installed pack in "
        "ONE translation unit (%d left out as the '%s' backend's own, which "
        "only that flavor's engine build compiles)"
        % (len(probed), len(bound),
           "classic" if args.flavor == "next" else "next"))


# --- leg 4: the clean-room acceptance run ------------------------------------

def stage_project(args, stage):
    """the project as a DISTRIBUTED project looks: its own tree, carrying its
    own sources. The in-tree module reads two headers straight out of the
    engine's jumper sample (one source of truth for the shared gameplay math);
    a project built against a pack has no checkout to read them from, so the
    copy travels with the project - which is what its CMakeLists searches
    first."""
    project = os.path.join(stage, "project")
    # WITHOUT the source project's own build output. Those directories are the
    # in-tree module's build trees - multi-gigabyte, useless here (this stage
    # configures its own against the pack), and they carry cache entries
    # pointing at the engine tree this leg exists to prove is unnecessary.
    # Copying them once filled the disk of a machine with a warm build.
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("build*", "builds",
                                                  ".orkige"))
    native = os.path.join(project, "native")
    for name in sorted(os.listdir(args.shared_headers)):
        if name.endswith(".h"):
            shutil.copy2(os.path.join(args.shared_headers, name), native)
    return project


def clean_room(args, stage, pack, project):
    """deny the engine source tree and the engine build tree outright, and
    re-allow the scratch the pack and the project live in"""
    if sys.platform != "darwin":
        log("clean room: no path sandbox on this platform - the pack and the "
            "project are staged outside the repository and the "
            "self-containment leg audits the pack, but the engine tree is not "
            "made unreachable here")
        return None
    bundle = load_module("orkige_editor_bundle_driver",
                         os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      "run_editor_bundle_test.py"))
    profile = os.path.join(stage, "cleanroom.sb")
    denied = [args.repo, args.engine_build]
    if args.vcpkg_root and os.path.isdir(args.vcpkg_root):
        denied.append(args.vcpkg_root)
    bundle.write_sandbox_profile(profile, denied, [stage, pack, project])
    log("clean room: the engine source tree, the engine build tree and the "
        "machine's vcpkg root are denied for the acceptance leg")
    return profile


def assert_rendered(shot):
    """the screenshot must show a rendered scene, not a blank page.

    The module's own self-check is a gui WIDGET-STATE assertion at frame 5: it
    proves the HUD booted from the project's atlas, and a run that renders
    nothing at all passes it. So the frame the module dumps is examined the way
    the browser boot test examines its own - a sampled grid of pixels must carry
    several distinct colours. A black window, a cleared-but-empty frame or a
    single flat fill all collapse to one or two, which is exactly the failure a
    pack with the wrong media directory produced."""
    if not os.path.isfile(shot) or os.path.getsize(shot) == 0:
        fail("the module ran but wrote no screenshot - it reached the frame "
             "the dump hook sits on without producing a frame")
    image = orkige_png.decode_png(shot)
    colours = set()
    stride = 4 * 13     # a sampled grid; counting every pixel is wasteful
    for offset in range(0, len(image.pixels) - 4, stride):
        colours.add(bytes(image.pixels[offset:offset + 3]))
        if len(colours) > 16:
            break
    if len(colours) <= 4:
        fail("the module's frame is near-uniform (%d sampled colours in "
             "%dx%d) - it ran without rendering the scene"
             % (len(colours), image.width, image.height))
    log("acceptance: the frame it rendered carries >%d distinct colours over "
        "%dx%d - a real scene, not a blank window"
        % (len(colours) - 1, image.width, image.height))


def acceptance(args, stage, pack, project, sandbox):
    native = os.path.join(project, "native")
    build = os.path.join(native, "build-sdk")
    # every sandboxed command runs FROM the scratch: the test's own working
    # directory is inside the denied build tree, and a process cannot resolve a
    # cwd it may not read
    configure_against_pack(args, native, build, pack, sandbox=sandbox, cwd=stage)
    build_target(args, build, sandbox=sandbox, cwd=stage)

    # WHERE the module landed is the build's answer, never a guess: the helper
    # writes it down (cmake/OrkigeGameModule.cmake), and the tools that launch
    # a module read the same file (core_project/NativeModule.h). Reading it
    # here is also what proves it is written and correct.
    executable = read_artifact_manifest(build, args.module_target)
    if not os.path.isfile(executable):
        fail("the module built against the pack produced no '%s'" % executable)
    log("acceptance: %s configured and built against the pack with the engine "
        "tree unreachable" % args.module_target)

    scene = os.path.join(project, "scenes", args.scene)
    command = [executable, os.path.join("scenes", args.scene),
               "--project", project]
    if sandbox:
        command = ["sandbox-exec", "-f", sandbox] + command
    if not os.path.isfile(scene):
        fail("no scene '%s' in the staged project" % scene)
    # the module dumps a frame at frame 55, so the run must reach past it; the
    # shot lands in the scratch, which the clean room allows writing
    shot = os.path.join(stage, "acceptance_frame.png")
    env = dict(os.environ)
    env.update({"ORKIGE_JUMPER_NATIVE_SELFCHECK": "1",
                "ORKIGE_DEMO_FRAMES": "60",
                "ORKIGE_DEMO_SCREENSHOT": shot})
    started = time.time()
    result = subprocess.run(command, env=env, cwd=stage, timeout=args.run_timeout,
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-4000:])
        sys.stderr.write(result.stderr[-4000:])
        fail("the module built against the pack exited %d" % result.returncode)
    output = result.stdout + result.stderr
    if "hud selfcheck passed" not in output:
        sys.stderr.write(output[-4000:])
        fail("the module ran but its own self-check did not report success")
    log("acceptance: it RAN (%.1fs) and passed its self-check - a game built "
        "only from the pack" % (time.time() - started))
    assert_rendered(shot)
    return build


def assert_compile_contract(args, pack, module_build):
    """every ABI-relevant definition the engine archives were compiled with
    must reach the consumer's own translation units.

    A module compiled without one of them disagrees with the archive about
    what the shared headers mean. When that difference is a referenced symbol
    it shows up as an undefined symbol - the LUCKY case, and the only one
    building a single module happens to catch. When it is struct layout or an
    inline body, the module links and then misbehaves. So the contract is
    checked directly: what the package RECORDS against what the compiler was
    actually told, rather than trusting one module's link to notice."""
    recorded = read_pack_values(args, pack,
                                ["ORKIGE_ENGINE_COMPILE_DEFINITIONS",
                                 "ORKIGE_ENGINE_COMPILE_OPTIONS"])
    contract = [entry for entry
                in recorded["ORKIGE_ENGINE_COMPILE_DEFINITIONS"].split(";")
                if entry]
    options = [entry for entry
               in recorded["ORKIGE_ENGINE_COMPILE_OPTIONS"].split(";")
               if entry]
    if not contract:
        fail("the pack records an EMPTY compile contract")

    database = os.path.join(module_build, "compile_commands.json")
    if not os.path.isfile(database):
        fail("no compile_commands.json in the module build '%s' - the helper "
             "is meant to export one" % module_build)
    with open(database) as handle:
        entries = json.load(handle)
    if not entries:
        fail("the module's compile_commands.json is empty")
    command = entries[0].get("command") or " ".join(entries[0].get("arguments", []))
    defined = set(re.findall(r'-D\s*([A-Za-z_][A-Za-z0-9_]*)', command))
    # the command line as written carries shell/JSON escaping around any value;
    # compare against a form with those removed rather than reproducing the
    # quoting rules of every generator
    unquoted = command.replace("\\", "").replace('"', "").replace("'", "")

    missing = []
    for entry in contract:
        # a generator expression is resolved per consumer target, not here -
        # its literal text could never appear on a command line
        if "$<" in entry:
            continue
        name, sep, value = entry.partition("=")
        if name not in defined:
            missing.append("%s (absent)" % entry)
        elif sep and ("-D%s=%s" % (name, value)) not in unquoted:
            missing.append("%s (defined with a different value)" % entry)
    if missing:
        sys.stderr.write(command + "\n")
        fail("the package records %d compile-contract definition(s) the "
             "module was NOT compiled with - a consumer's objects would "
             "disagree with the engine archives:\n  %s"
             % (len(missing), "\n  ".join(missing)))

    # a contract that somehow recorded nothing meaningful must not pass by
    # being trivially satisfied
    required = list(CONTRACT_MUST_INCLUDE) + [CONTRACT_FLAVOR_MACRO[args.flavor]]
    absent = [name for name in required if name not in defined]
    if absent:
        fail("the module's command line lacks %s - the recorded contract is "
             "not describing this engine" % ", ".join(absent))
    log("compile contract: all %d recorded definitions reached the module's "
        "command line (%s)" % (len(contract), ", ".join(sorted(contract))))

    # THE OPTION HALF. Definitions are not the whole contract: an exception
    # model, sanitizer instrumentation or an object-section capacity flag
    # reaches a consumer's translation units the same way and matters as much.
    # A host whose engine build declares no global options records an empty set
    # and this passes trivially - which is the correct answer there, and the
    # same channel carries the ones that are not empty elsewhere.
    missing_options = []
    for option in options:
        if "$<" in option:
            # a generator expression resolves per consumer target; compare the
            # flag it wraps rather than its literal text
            inner = re.findall(r'>:([^>]+)>', option)
            if not inner or all(flag in unquoted for flag in inner):
                continue
            missing_options.append(option)
        elif option not in unquoted:
            missing_options.append(option)
    if missing_options:
        sys.stderr.write(command + "\n")
        fail("the package records %d compile OPTION(s) the module was not "
             "compiled with - a consumer's objects would be built by a "
             "different toolchain contract than the archives:\n  %s"
             % (len(missing_options), "\n  ".join(missing_options)))
    log("compile contract: %d recorded compile option(s) reached the command "
        "line%s" % (len(options),
                    " (%s)" % ", ".join(options) if options else
                    " - this engine build declares none"))

    # THE OS FLOOR is contract too, and it is the one a module could silently
    # get wrong while everything else matched: compiled on a current machine
    # without inheriting it, the shipped game refuses to launch on systems the
    # engine archives it links were built to run on. Read it off the command
    # line rather than off a variable, because only the flag proves it took.
    if sys.platform == "darwin":
        floor = read_pack_values(
            args, pack,
            ["ORKIGE_PACKAGE_OSX_DEPLOYMENT_TARGET"]
        )["ORKIGE_PACKAGE_OSX_DEPLOYMENT_TARGET"]
        if not floor:
            fail("the package records no macOS deployment target - the pack's "
                 "binaries have whatever floor its SDK defaulted to")
        expected = "-mmacosx-version-min=%s" % floor
        if expected not in unquoted:
            sys.stderr.write(command + "\n")
            fail("the module was not compiled for the engine's OS floor (%s "
                 "absent) - it would claim a floor its objects cannot honour"
                 % expected)
        log("OS floor: the module compiles for macOS %s, the floor the engine "
            "archives were built for" % floor)


# WHOSE LIBRARIES a module is allowed to link besides the pack's own.
#
# The clean room denies the engine source tree, the engine build tree and the
# machine's vcpkg root - but it cannot deny the PLATFORM, and it does not deny
# a package manager's prefix. So a link could quietly resolve against a
# host-installed copy of a library the pack is supposed to be carrying, and
# every leg would still pass: the pack would look self-sufficient here and fail
# on a machine that has no such copy. That is the same shape as a link
# resolving to the wrong thing rather than to nothing.
#
# The allowance is therefore explicit, and it is the platform's OWN runtime -
# the parts of the operating system every binary on it links and which no pack
# would ever ship. It is deliberately NOT "any absolute path outside the pack",
# and deliberately NOT the package-manager prefixes (/opt/homebrew, /usr/local,
# /home/linuxbrew) that are exactly what this check is looking for.
#
#   - Apple: the SDK and the OS itself. Frameworks are the platform's API
#     surface, and /usr/lib on macOS is the dyld shared cache - there is no
#     static copy of libSystem to bundle, and MoltenVK is the platform's Vulkan
#     DRIVER by the same rule GPU drivers are (see the hermeticity note in
#     CLAUDE.md), so the Vulkan LOADER comes from the pack while the driver does
#     not appear on a link line at all.
#   - Linux: the C runtime and its siblings, which come from the distribution's
#     glibc and are the floor a Linux pack states rather than carries.
#   - Windows: the SDK import libraries under the Windows Kits root.
# (spelled lower-case; the comparison lower-cases the token, which is what
# makes the Windows entries work at all)
SYSTEM_LIBRARY_ROOTS = (
    "/usr/lib/", "/system/library/", "/library/developer/commandlinetools/",
    "/applications/xcode", "/lib/", "/lib64/", "/usr/lib64/",
    "c:/program files (x86)/windows kits/", "c:/program files/windows kits/",
)
# bare -l names the linker resolves from its own default search path. Only the
# platform runtime belongs here: anything a port provides must arrive as a path
# INTO the pack, because a bare name would find whatever the host has.
SYSTEM_LIBRARY_NAMES = frozenset((
    "c", "m", "dl", "rt", "pthread", "System", "objc", "c++", "stdc++",
    "gcc", "gcc_s", "util", "resolv", "atomic", "log", "android",
))
# the platform's WINDOWING and device stacks, which on Linux arrive as bare
# names the way macOS frameworks arrive as -framework. A pack must not bundle
# these - they are the distribution's, versioned with the user's display server
# and drivers, and are exactly the "Linux floor" a pack RECORDS rather than
# carries. Matched by prefix because the families are open-ended (Xrandr, Xi,
# Xcursor, xcb-randr, xkbcommon-x11, wayland-egl...), and safe to match that
# way: no Orkige or vcpkg library is spelled like one, so this cannot hide the
# leak the audit exists to catch.
SYSTEM_LIBRARY_PREFIXES = (
    "X",            # X11, X11-xcb, Xaw, Xt, Xrandr, Xi, Xcursor, Xext, Xss...
    "xcb",          # xcb, xcb-randr, xcb-keysyms...
    "xkbcommon",
    "wayland-",
    "decor-",
    "EGL", "GL", "GLX", "OpenGL",
    "asound", "pulse", "udev", "dbus-1", "ibus-1.0",
)
LIBRARY_SUFFIXES = (".a", ".dylib", ".so", ".tbd", ".lib")


def assert_closure_is_the_packs(args, pack, module_build):
    """every library the module links must come out of the pack.

    Reads the REAL link line the generator wrote, not a re-derivation of it."""
    if args.generator != "Ninja":
        log("closure: link-line audit needs the Ninja generator - skipped for "
            "'%s'" % args.generator)
        return
    ninja_file = os.path.join(module_build, "build.ninja")
    if not os.path.isfile(ninja_file):
        fail("no build.ninja in the module build '%s'" % module_build)
    link_lines = []
    with open(ninja_file, errors="ignore") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped.startswith(("LINK_LIBRARIES =", "LINK_PATH =",
                                    "LINK_FLAGS =")):
                link_lines.append(stripped.partition("=")[2])
    if not link_lines:
        fail("the module's build.ninja carries no link line to audit")

    pack_real = os.path.realpath(pack)
    offenders = []
    audited = 0
    for line in link_lines:
        tokens = line.replace("\\ ", "\x00").split()
        skip_next = False
        for raw in tokens:
            token = raw.replace("\x00", " ").strip('"')
            if skip_next:
                skip_next = False
                continue
            if token == "-framework":
                # the platform's API surface; the next token is its name
                skip_next = True
                continue
            if token.startswith("-l"):
                name = token[2:]
                if (name and name not in SYSTEM_LIBRARY_NAMES
                        and not name.startswith(SYSTEM_LIBRARY_PREFIXES)):
                    offenders.append("-l%s (a bare name the linker resolves "
                                     "from the host's search path)" % name)
                continue
            if token.startswith("-L"):
                token = token[2:]
            elif token.startswith("-"):
                continue
            elif not token.lower().endswith(LIBRARY_SUFFIXES):
                continue
            if not os.path.isabs(token):
                continue
            audited += 1
            lowered = token.lower().replace("\\", "/")
            if os.path.realpath(token).startswith(pack_real + os.sep):
                continue
            if any(lowered.startswith(root) for root in SYSTEM_LIBRARY_ROOTS):
                continue
            offenders.append(token)
    if offenders:
        fail("%d linked librar(y/ies) do not come from the pack - the module "
             "resolved them against this machine, so the pack only looks "
             "self-sufficient here:\n  %s"
             % (len(offenders), "\n  ".join(sorted(set(offenders))[:20])))
    log("closure: all %d absolute library paths on the module's link line "
        "resolve inside the pack (platform runtime excepted)" % audited)


def strip_comment(line):
    """the code part of a header line. A doc comment naming a private define is
    documentation, not a use - the engine's own comments explain where a
    compile-time default comes from, and that must not read as a violation.
    Line comments are cut, and a continuation line of a block comment (the
    house style starts those with '*') is dropped whole."""
    stripped = line.lstrip()
    if stripped.startswith(("//", "*", "/*")):
        return ""
    return line.split("//", 1)[0]


def assert_private_definitions_stay_private(args, pack):
    """the floor under 'captured, never restated'.

    The compile contract is read off the engine's PUBLIC definitions, which is
    only complete while the engine declares an ABI-relevant define PUBLIC. A
    define added PRIVATE that some header then reads would change what that
    header means for a consumer while escaping the contract in silence - no
    link error, no diagnostic, just objects that disagree.

    So the package records the private set too, and the check is mechanical:
    none of those names may appear in any header the pack installs. The day one
    does, this says so and the fix is to declare it PUBLIC."""
    recorded = read_pack_values(args, pack,
                                ["ORKIGE_PRIVATE_COMPILE_DEFINITIONS"])
    private = [name for name
               in recorded["ORKIGE_PRIVATE_COMPILE_DEFINITIONS"].split(";")
               if name]
    if not private:
        log("private definitions: the engine declares none")
        return
    pattern = re.compile(r'\b(%s)\b' % "|".join(re.escape(n) for n in private))
    offenders = []
    include_root = os.path.join(pack, "include")
    for dirpath, _dirnames, files in os.walk(include_root):
        for name in files:
            if not name.endswith((".h", ".inc")):
                continue
            full = os.path.join(dirpath, name)
            with open(full, "r", errors="ignore") as handle:
                for number, line in enumerate(handle, 1):
                    found = pattern.search(strip_comment(line))
                    if found:
                        offenders.append("%s:%d reads %s"
                                         % (os.path.relpath(full, include_root),
                                            number, found.group(1)))
    if offenders:
        fail("%d installed header line(s) read a definition the engine keeps "
             "PRIVATE, so a consumer compiles them with a different meaning "
             "than the archives did - declare the define PUBLIC so the "
             "captured contract carries it:\n  %s"
             % (len(offenders), "\n  ".join(offenders[:20])))
    log("private definitions: none of the %d implementation-only defines (%s) "
        "is read by an installed header" % (len(private), ", ".join(private)))


def assert_target_contract(args, pack):
    """a pack says what it targets, in the vocabulary a project's own files are
    written against. The fields are a public contract - packs are built per
    target and adding one later is fine, changing what one MEANS is not - so
    the whole schema is asserted present, and the ones this pack can answer are
    asserted correct."""
    schema = ["ORKIGE_SDK_TARGET_PLATFORM", "ORKIGE_SDK_TARGET_ARCHS",
              "ORKIGE_SDK_TARGET_TRIPLET", "ORKIGE_SDK_MODULE_SHAPE",
              "ORKIGE_SDK_MODULE_OUTPUT_NAME",
              "ORKIGE_SDK_OS_DEPLOYMENT_TARGET", "ORKIGE_SDK_TOOLCHAIN_KIND",
              "ORKIGE_SDK_TOOLCHAIN_VERSION", "ORKIGE_SDK_TOOLCHAIN_FILE",
              "ORKIGE_SDK_TOOLCHAIN_OPTIONS", "ORKIGE_SDK_CXX_COMPILER_ID",
              "ORKIGE_SDK_CXX_COMPILER_VERSION", "ORKIGE_SDK_CXX_STDLIB",
              "ORKIGE_SDK_BUILD_HOST"]
    source = os.path.join(args.stage, "read-pack-target")
    lines = ["cmake_minimum_required(VERSION 3.28)",
             "project(orkige_pack_target LANGUAGES NONE)",
             'include("%s/cmake/OrkigeSdkPack.cmake")' % cmake_path(pack)]
    for name in schema:
        lines.append('message("%s%s=${%s}")' % (PACK_VALUE_MARKER, name, name))
    write_file(os.path.join(source, "CMakeLists.txt"), "\n".join(lines) + "\n")
    result = subprocess.run(
        [args.cmake, "-S", source, "-B", os.path.join(source, "build")],
        timeout=300, capture_output=True, text=True)
    values = {}
    for line in (result.stdout + result.stderr).splitlines():
        if line.startswith(PACK_VALUE_MARKER):
            key, _, value = line[len(PACK_VALUE_MARKER):].partition("=")
            values[key] = value
    absent = [name for name in schema if name not in values]
    if absent:
        sys.stderr.write(result.stdout + result.stderr)
        fail("the pack's own description does not declare %s - every field of "
             "the target contract must be present, empty where this target "
             "has no answer" % ", ".join(absent))

    expected_platform = {"darwin": "macos", "linux": "linux",
                         "win32": "windows"}.get(sys.platform)
    if expected_platform and values["ORKIGE_SDK_TARGET_PLATFORM"] != expected_platform:
        fail("a host pack built here targets '%s', not '%s'"
             % (expected_platform, values["ORKIGE_SDK_TARGET_PLATFORM"]))
    if values["ORKIGE_SDK_MODULE_SHAPE"] != "executable":
        fail("a desktop game module is an executable, not '%s'"
             % values["ORKIGE_SDK_MODULE_SHAPE"])
    for required in ("ORKIGE_SDK_TARGET_TRIPLET", "ORKIGE_SDK_TOOLCHAIN_KIND",
                     "ORKIGE_SDK_CXX_COMPILER_ID", "ORKIGE_SDK_CXX_STDLIB"):
        if not values[required]:
            fail("%s is empty, but a host pack knows the answer" % required)
    if sys.platform == "darwin" and not values["ORKIGE_SDK_OS_DEPLOYMENT_TARGET"]:
        fail("the pack records no OS deployment target, but an Apple build "
             "pins one - a pack whose floor is whatever SDK happened to build "
             "it cannot tell a consumer what its binaries will run on")
    log("target contract: %s/%s, triplet %s, %s toolchain, %s %s on %s, OS "
        "floor '%s'"
        % (values["ORKIGE_SDK_TARGET_PLATFORM"],
           values["ORKIGE_SDK_TARGET_ARCHS"],
           values["ORKIGE_SDK_TARGET_TRIPLET"],
           values["ORKIGE_SDK_TOOLCHAIN_KIND"],
           values["ORKIGE_SDK_CXX_COMPILER_ID"],
           values["ORKIGE_SDK_CXX_COMPILER_VERSION"],
           values["ORKIGE_SDK_CXX_STDLIB"],
           values["ORKIGE_SDK_OS_DEPLOYMENT_TARGET"]))


def assert_no_host_executables(pack):
    """a pack carries no host tools.

    A game-module build configures and compiles; it invokes none of the
    encoders, validators or uninstall scripts a port installs beside its
    library. Shipping them is size and license surface for nothing, and on
    macOS a downloaded archive's executables carry the quarantine attribute -
    a Gatekeeper encounter on a user's machine that this machine never sees."""
    offenders = []
    for sub in ("tools", "bin", os.path.join("debug", "tools"),
                os.path.join("debug", "bin")):
        full = os.path.join(pack, "vcpkg", sub)
        if os.path.isdir(full):
            offenders.append(sub)
    if offenders:
        fail("the pack ships host-executable directories (%s) a module build "
             "never invokes" % ", ".join(offenders))
    log("no host executables: the pack's closure carries libraries, headers "
        "and cmake, and nothing to run")


# --- leg 5: the ABI guard in pack form ---------------------------------------

def abi_guard(args, stage, pack, project):
    """a pack whose headers no longer match its archives must REFUSE. Done on a
    throwaway copy of the cmake surface plus a header edit, so the pack every
    other leg used stays pristine."""
    tampered = os.path.join(stage, "tampered-pack")
    shutil.copytree(pack, tampered, symlinks=True,
                    copy_function=os.link, ignore_dangling_symlinks=True)
    victim = os.path.join(tampered, "include", "core_util", "String.h")
    if not os.path.isfile(victim):
        fail("expected core_util/String.h in the pack for the guard leg")
    # hard links share content with the pristine pack - replace, never append
    os.remove(victim)
    with open(victim, "w") as handle:
        handle.write("// a header edited after the pack was installed\n")

    build = os.path.join(stage, "guard-build")
    result = configure_against_pack(args, os.path.join(project, "native"),
                                    build, tampered, allow_failure=True)
    combined = result.stdout + result.stderr
    if result.returncode == 0:
        fail("a pack with an edited installed header CONFIGURED - the ABI "
             "guard did not fire")
    if MISMATCH_SENTINEL not in combined:
        sys.stderr.write(combined[-4000:])
        fail("the tampered pack failed, but not with the ABI-mismatch "
             "diagnostic ('%s')" % MISMATCH_SENTINEL)
    log("ABI guard: an edited installed header is a hard configure error, "
        "with the mismatch named")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, help="the engine source root")
    parser.add_argument("--engine-build", required=True,
                        help="the built engine tree the pack installs from")
    parser.add_argument("--project", required=True,
                        help="the native-module project to build against the pack")
    parser.add_argument("--shared-headers", required=True,
                        help="the sample dir whose headers the module includes")
    parser.add_argument("--module-target", default="jumper_native")
    parser.add_argument("--scene", default="main.oscene")
    parser.add_argument("--stage", required=True, help="scratch directory")
    parser.add_argument("--flavor", default="next", choices=("next", "classic"))
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--ignore-prefix-path", default="")
    parser.add_argument("--vcpkg-root", default=os.environ.get("VCPKG_ROOT", ""))
    parser.add_argument("--run-timeout", type=float, default=180.0)
    args = parser.parse_args()

    if not shutil.which(args.cmake) and not os.path.isfile(args.cmake):
        skip("no cmake on this machine")
    if args.generator == "Ninja" and not shutil.which("ninja"):
        skip("no ninja generator available")
    if not os.path.isfile(os.path.join(args.engine_build, "OrkigeConfig.cmake")):
        skip("'%s' carries no Orkige package - build the engine tree first"
             % args.engine_build)

    if os.path.isdir(args.stage):
        shutil.rmtree(args.stage)
    os.makedirs(args.stage)

    pack = install_pack(args, args.stage)
    args.pack_build_type = read_pack_values(
        args, pack, ["ORKIGE_PACKAGE_BUILD_TYPE"])["ORKIGE_PACKAGE_BUILD_TYPE"]
    log("pack configuration: %s (a distribution pack is installed from a "
        "Release engine tree)" % args.pack_build_type)
    assert_self_contained(args, pack)
    assert_configuration_coherent(args, pack)
    assert_no_host_executables(pack)
    assert_target_contract(args, pack)
    assert_private_definitions_stay_private(args, pack)
    run_surface_probe(args, args.stage, pack)
    project = stage_project(args, args.stage)
    sandbox = clean_room(args, args.stage, pack, project)
    module_build = acceptance(args, args.stage, pack, project, sandbox)
    assert_closure_is_the_packs(args, pack, module_build)
    assert_compile_contract(args, pack, module_build)
    abi_guard(args, args.stage, pack, project)
    log("the SDK pack holds: relocatable, self-contained, configuration-"
        "coherent, complete, contract-complete, target-declared and guarded")

    # A pack is a large artifact - a Debug one several gigabytes, because its
    # dependency closure carries debug archives. Nothing needs it once the legs
    # have passed, so it is not left sitting in the build tree; a FAILING run
    # keeps everything for inspection, which is when it is worth the disk.
    shutil.rmtree(args.stage, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
