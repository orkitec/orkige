#!/usr/bin/env python3
"""The Android package assembly, run for REAL against the machine's own SDK -
without an Android build tree.

`export_android` proves the whole road end to end, but it needs an `android-*`
preset tree (an NDK, a vcpkg cross closure, ~an hour cold), so on most machines
it skips. What it would prove and this proves too is the part that has nothing
to do with the ARM library inside the package: that the exporter's in-process
assembly drives `javac`, `d8`, `aapt2`, `zipalign`, `keytool` and `apksigner`
into an APK the platform's own tools then accept.

So the engine source is SYNTHESISED - a CMake cache, a stand-in `libmain.so`
and a stand-in shader tree - and everything else is the real thing: the real
Java glue, the real SDK programs, a real signed archive, verified by
`apksigner verify` and read back with the zip reader.

SKIPPED (exit 77) when this machine cannot answer: no Android SDK build tools,
no platform jar, no JDK, or no SDL Java glue under the vcpkg root.

    python3 run_android_assemble_test.py --repo <dir> --exporter <orkige_export>
                                         --output <work dir>
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import zipfile


def log(message):
    print("run_android_assemble_test: " + message, flush=True)


def skip(reason):
    log("SKIP: " + reason)
    sys.exit(77)


def require(condition, what):
    if not condition:
        log("FAIL: " + what)
        sys.exit(1)
    log("ok: " + what)


def newest_numeric(directory):
    """the newest version-shaped subdirectory name, compared NUMERICALLY
    ("9.0.0" is older than "35.0.0", which a plain sort gets backwards)."""
    best, best_parts = "", ()
    for name in os.listdir(directory) if os.path.isdir(directory) else []:
        parts = name.split(".")
        if not all(part.isdigit() for part in parts):
            continue
        key = tuple(int(part) for part in parts)
        if key > best_parts:
            best, best_parts = name, key
    return best


def find_toolchain():
    """the SDK build tools, the newest platform jar and a JDK - resolved the
    way the exporter resolves them, so a skip here means the exporter would
    have refused by name."""
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get(
        "ANDROID_SDK_ROOT") or os.path.expanduser("~/Library/Android/sdk")
    if not os.path.isdir(sdk):
        sdk = os.path.expanduser("~/Android/Sdk")
    build_tools_root = os.path.join(sdk, "build-tools")
    version = newest_numeric(build_tools_root)
    if not version:
        skip("no Android SDK build tools under " + build_tools_root)
    build_tools = os.path.join(build_tools_root, version)
    platforms = os.path.join(sdk, "platforms")
    jars = sorted(glob.glob(os.path.join(platforms, "android-*", "android.jar")))
    if not jars:
        skip("no Android platform jar under " + platforms)
    for program in ("aapt2", "zipalign"):
        if not os.path.isfile(os.path.join(build_tools, program)):
            skip("no %s in %s" % (program, build_tools))
    for java_home in jdk_candidates():
        # a real JDK, the way the exporter judges one: all three programs AND
        # the `release` descriptor at its root. macOS ships /usr/bin/javac as a
        # STUB that only forwards to an installed JDK, so a home derived from
        # it lands on /usr - which would report "found a JDK" and then fail
        # inside javac.
        if not all(os.path.isfile(os.path.join(java_home, "bin", program))
                   for program in ("javac", "java", "keytool")):
            continue
        if os.path.isfile(os.path.join(java_home, "release")) \
                or os.path.isfile(os.path.join(java_home, "lib", "modules")):
            return build_tools, java_home
    skip("no JDK (set JAVA_HOME, or install one)")


def jdk_candidates():
    """every JDK home worth probing, in the exporter's own order."""
    candidates = []
    if os.environ.get("JAVA_HOME"):
        candidates.append(os.environ["JAVA_HOME"])
    if shutil.which("javac"):
        candidates.append(os.path.dirname(os.path.dirname(
            os.path.realpath(shutil.which("javac")))))
    candidates.append("/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/"
                      "Contents/Home")
    jvms = "/Library/Java/JavaVirtualMachines"
    for name in sorted(os.listdir(jvms) if os.path.isdir(jvms) else []):
        candidates.append(os.path.join(jvms, name, "Contents", "Home"))
    return candidates


def find_sdl_java_glue():
    """SDL3's Java glue, from the vcpkg root the exporter reads it from."""
    vcpkg = os.environ.get("VCPKG_ROOT") or os.path.expanduser(
        "~/Development/vcpkg")
    relative = os.path.join("android-project", "app", "src", "main", "java",
                            "org", "libsdl", "app")
    for source in sorted(glob.glob(os.path.join(vcpkg, "buildtrees", "sdl3",
                                                "src", "*.clean"))):
        candidate = os.path.join(source, relative)
        if os.path.isdir(candidate):
            return candidate
    if glob.glob(os.path.join(vcpkg, "downloads",
                              "libsdl-org-SDL-release-3.*.tar.gz")):
        # the exporter unpacks the archive itself; that road is exercised too
        return "archive"
    skip("no SDL3 Java sources under " + vcpkg)


def make_engine_tree(root):
    """a stand-in android-debug tree: everything the assembly READS about an
    engine source, and nothing it cannot synthesise. The library is a stand-in
    because what rides in `lib/` does not change how the package is built."""
    tree = os.path.join(root, "android-debug")
    player = os.path.join(tree, "tools", "player")
    hlms = os.path.join(tree, "vcpkg_installed", "arm64-android", "share",
                        "ogre-next", "Media", "Hlms", "Common")
    os.makedirs(player)
    os.makedirs(hlms)
    os.makedirs(os.path.join(tree, "vcpkg_installed", "arm64-android",
                             "include"))
    with open(os.path.join(player, "libmain.so"), "w") as handle:
        handle.write("stand-in for the cross-built player")
    with open(os.path.join(hlms, "PieceCommon.any"), "w") as handle:
        handle.write("stand-in shader piece")
    with open(os.path.join(tree, "CMakeCache.txt"), "w") as handle:
        handle.write("ORKIGE_RENDER_BACKEND:STRING=next\n")
        handle.write("ANDROID_ABI:STRING=arm64-v8a\n")
        handle.write("VCPKG_TARGET_TRIPLET:STRING=arm64-android\n")
    return tree


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    build_tools, java_home = find_toolchain()
    log("build tools: " + build_tools)
    log("JDK: " + java_home)
    log("SDL Java glue: " + find_sdl_java_glue())

    shutil.rmtree(args.output, ignore_errors=True)
    os.makedirs(args.output)
    tree = make_engine_tree(args.output)
    apk = os.path.join(args.output, "OrkigePlayer.apk")

    command = [args.exporter, "android-player", "--engine-build", tree,
               "--output", apk]
    log("$ " + " ".join(command))
    # a SCRATCH home, so the debug keystore is created rather than inherited.
    # A developer machine that has ever run an Android tool already holds
    # ~/.android/debug.keystore, so running under the real home exercises the
    # "a keystore is already there" path and NEVER the creation one - which is
    # how a missing `mkdir` for that directory reached a fresh CI runner and
    # failed there while passing on every machine that had signed anything.
    home = os.path.join(args.output, "home")
    os.makedirs(home)
    environment = dict(os.environ)
    environment["HOME"] = home
    environment["USERPROFILE"] = home
    result = subprocess.run(command, cwd=args.repo, capture_output=True,
                            text=True, env=environment)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    require(result.returncode == 0, "the assembly succeeded")
    require("orkige_export: OK " in result.stdout, "it reported its artifact")
    # and it MADE the keystore under that empty home, directory included -
    # keytool writes the file but never creates the folder holding it
    require(os.path.isfile(os.path.join(home, ".android", "debug.keystore")),
            "a machine with no Android history gets a debug keystore")

    # NOTHING may have gone through a shell: the assembly names its programs.
    # A run that quietly grew one back would still produce an APK here, so the
    # structural gate is the unit suite's - this asserts the OTHER half, that
    # the argv road actually produces a package the platform accepts.
    require(os.path.isfile(apk), "the APK exists")

    with zipfile.ZipFile(apk) as package:
        names = set(package.namelist())
        for required in ("AndroidManifest.xml", "resources.arsc",
                         "res/xml/orkige_network_security.xml", "classes.dex",
                         "lib/arm64-v8a/libmain.so",
                         "assets/orkige_assets.txt",
                         "assets/orkige_mount.txt"):
            require(required in names, "the APK carries " + required)
        # the platform maps resources.arsc in place and refuses a compressed
        # one from API 30 on; zipalign put it on a 4-byte boundary
        arsc = package.getinfo("resources.arsc")
        require(arsc.compress_type == zipfile.ZIP_STORED,
                "resources.arsc is STORED")
        require(arsc.header_offset % 4 == 0, "resources.arsc is 4-aligned")
        # `stored` is the default mode: every asset goes in uncompressed so the
        # runtime mounts the package and reads it in place
        deflated = [entry.filename for entry in package.infolist()
                    if entry.filename.startswith("assets/")
                    and entry.compress_type != zipfile.ZIP_STORED]
        require(not deflated, "every asset entry is STORED (mount in place)")
        listing = package.read("assets/orkige_assets.txt").decode().split("\n")
        require("orkige_mount.txt" in listing,
                "the extraction manifest lists the bundled files")
        # the dex is real: javac and d8 both ran over the real Java glue
        require(package.read("classes.dex")[:4] == b"dex\n",
                "classes.dex is a real dex image")

    # ...and the platform's OWN tools accept it
    java = os.path.join(java_home, "bin", "java")
    verify = subprocess.run(
        [java, "-jar", os.path.join(build_tools, "lib", "apksigner.jar"),
         "verify", apk], capture_output=True, text=True)
    require(verify.returncode == 0,
            "apksigner verifies the signature: " + verify.stdout.strip())
    badging = subprocess.run([os.path.join(build_tools, "aapt2"), "dump",
                              "badging", apk], capture_output=True, text=True)
    require(badging.returncode == 0, "aapt2 reads the package back")
    require("com.orkitec.orkigeplayer" in badging.stdout,
            "the manifest names the player package")
    require("targetSdkVersion:'35'" in badging.stdout,
            "the manifest keeps its target SDK")

    shutil.rmtree(args.output, ignore_errors=True)
    log("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
