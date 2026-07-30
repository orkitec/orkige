#!/usr/bin/env python3
"""Make a macOS binary inside an app bundle self-contained.

A binary linked against vcpkg dylibs carries `@rpath` dependencies plus
build-tree rpaths, so a COPY of the app on another machine dies in dyld before
`main`. This module copies the non-system dylib closure into the bundle's
`Contents/Frameworks`, points the binary there (`@executable_path/../Frameworks`),
REMOVES every build-tree rpath - so a missing dylib fails on the build machine
rather than on a user's - and ad-hoc re-signs, which `install_name_tool` makes
necessary on arm64.

The ONE implementation of that operation: `Util/orkige_export.py` uses it for an
exported game, and the editor build uses it (through the CLI below) for the
editor app it stages. Stdlib only, per the toolchain policy.
"""

import argparse
import os
import shutil
import subprocess
import sys


def _plain_log(message):
    print("macos_self_contain: " + message, flush=True)


def collect_dylibs(executable, search_dirs, on_unresolved=None):
    """the binary's non-system dylib dependencies (`@rpath/...` or absolute
    paths outside /usr/lib and /System), resolved against search_dirs.
    Returns [(dependency-as-written, resolved-file), ...]. A dependency that
    resolves nowhere is reported to `on_unresolved(dep)` - an exporter fails on
    it - and skipped when there is no handler."""
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
            if on_unresolved is not None:
                on_unresolved(dep)
            continue
        dependencies.append((dep, resolved))
    return dependencies


def dylib_aliases(source_dir, dylib_name):
    """the symlink leaf names in source_dir that resolve to dylib_name - the
    dlopen aliases of a versioned dylib (e.g. libvulkan.dylib and
    libvulkan.1.dylib -> libvulkan.1.4.350.dylib). A leaf-name dlopen (the
    Vulkan loader probe in the render system) asks for the unversioned names, so
    a self-contained bundle must carry them beside the real file."""
    aliases = []
    target = os.path.realpath(os.path.join(source_dir, dylib_name))
    for entry in sorted(os.listdir(source_dir)):
        path = os.path.join(source_dir, entry)
        if entry != dylib_name and os.path.islink(path) \
                and os.path.realpath(path) == target:
            aliases.append(entry)
    return aliases


def rpaths(executable):
    """the LC_RPATH entries baked into the binary"""
    output = subprocess.run(["otool", "-l", executable], capture_output=True,
                            text=True, check=True).stdout
    found = []
    lines = output.splitlines()
    for index, line in enumerate(lines):
        if "cmd LC_RPATH" in line:
            for path_line in lines[index:index + 4]:
                stripped = path_line.strip()
                if stripped.startswith("path "):
                    found.append(stripped.split()[1])
                    break
    return found


def make_self_contained(executable, frameworks_dir, search_dirs,
                        banned_rpath_markers=("vcpkg_installed",),
                        log=_plain_log, run=None, on_unresolved=None):
    """copy the closure into frameworks_dir, retarget the binary at it, delete
    every build-tree rpath and ad-hoc re-sign. `banned_rpath_markers` are the
    substrings that mark an rpath as belonging to the build machine."""
    def _run(command):
        if run is not None:
            return run(command)
        log("$ " + " ".join(command))
        result = subprocess.run(command)
        if result.returncode != 0:
            raise RuntimeError("command failed (exit %d): %s"
                               % (result.returncode, command[0]))
        return result

    dependencies = collect_dylibs(executable, search_dirs, on_unresolved)
    if dependencies:
        os.makedirs(frameworks_dir, exist_ok=True)
    changed = False
    for dep, resolved in dependencies:
        shutil.copy2(resolved, os.path.join(frameworks_dir,
                                            os.path.basename(resolved)))
        log("bundled dylib %s" % os.path.basename(resolved))
        for alias in dylib_aliases(os.path.dirname(resolved),
                                   os.path.basename(resolved)):
            alias_path = os.path.join(frameworks_dir, alias)
            if os.path.lexists(alias_path):
                os.remove(alias_path)
            os.symlink(os.path.basename(resolved), alias_path)
            log("aliased dylib %s -> %s" % (alias, os.path.basename(resolved)))
        if not dep.startswith("@rpath/"):
            # absolute dev path -> load via the bundle rpath instead
            _run(["install_name_tool", "-change", dep,
                  "@rpath/" + os.path.basename(resolved), executable])
        changed = True
    bundle_rpath = "@executable_path/../Frameworks"
    existing = rpaths(executable)
    for rpath in existing:
        # every build-machine path is banned from the shipped binary
        if any(marker in rpath for marker in banned_rpath_markers):
            _run(["install_name_tool", "-delete_rpath", rpath, executable])
            changed = True
    if dependencies and bundle_rpath not in existing:
        _run(["install_name_tool", "-add_rpath", bundle_rpath, executable])
        changed = True
    if changed:
        # install_name_tool invalidates the (linker) ad-hoc signature and arm64
        # macOS refuses to run unsigned binaries - re-sign ad-hoc
        _run(["codesign", "--force", "-s", "-", executable])
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frameworks", required=True,
                        help="the bundle's Contents/Frameworks directory")
    parser.add_argument("--search", action="append", default=[],
                        help="a directory to resolve @rpath dependencies in "
                             "(repeatable)")
    parser.add_argument("--banned", action="append", default=["vcpkg_installed"],
                        help="a substring marking an rpath as build-machine "
                             "local (repeatable)")
    parser.add_argument("binaries", nargs="+",
                        help="the binaries inside the bundle to retarget")
    args = parser.parse_args()
    if sys.platform != "darwin":
        return 0
    for binary in args.binaries:
        if not os.path.isfile(binary):
            _plain_log("skipping absent binary %s" % binary)
            continue
        make_self_contained(binary, args.frameworks, args.search,
                            tuple(args.banned))
    return 0


if __name__ == "__main__":
    sys.exit(main())
