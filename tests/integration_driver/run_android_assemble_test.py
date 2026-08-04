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
SDK programs, a real signed archive, verified by `apksigner verify` and read
back with the zip reader.

Two phases:

  the PLAYER phase packages the dev player's own APK off a synthesised build
  tree with the REAL SDL Java glue. It runs where the vcpkg root holds those
  sources and is announced as skipped where it does not.

  the LIBRARY phase packages a project that depends on an ANDROID LIBRARY
  ARCHIVE, generated here rather than committed as a binary. The engine half is
  synthesised down to the Java (a payload, the road a machine with no
  repository packages from); the ARCHIVE half is entirely real - a real zip
  holding real `javac` output, linked by the real `aapt2` and dexed by the real
  `d8`. What it asserts is the property that fails silently: a permission, a
  component, an asset, a native library and a generated resource id that the
  archive brought all reach the finished package.

SKIPPED (exit 77) when this machine cannot answer: no Android SDK build tools,
no platform jar, no JDK.

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
            return sdk, build_tools, java_home, jars[-1]
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
    """SDL3's Java glue, from the vcpkg root the exporter reads it from, or
    None where this machine's vcpkg holds neither the extracted sources nor the
    archive they come out of. Only the PLAYER phase needs it."""
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
    return None


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


#--- the library phase's fixtures ------------------------------------

#: the archive's own package - the one whose resource ids the app has to
#: generate, and whose classes have to reach the dex
LIBRARY_PACKAGE = "com.orkitec.testlibrary"
#: what the app itself is called, so the resolved ${applicationId} is checkable
APP_PACKAGE = "com.orkitec.aarfixture"

LIBRARY_MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="%(package)s">
    <uses-sdk android:minSdkVersion="21" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-feature android:name="android.hardware.touchscreen"
        android:required="false" />
    <application>
        <activity android:name="%(package)s.TestLibraryActivity"
            android:exported="false" />
        <meta-data android:name="%(package)s.HOST"
            android:value="${applicationId}" />
    </application>
</manifest>
""" % {"package": LIBRARY_PACKAGE}

#: the library's own code, reading its own resource id. The `R` it reads is
#: NOT in the archive - a library archive ships its symbol list and the APP
#: generates the class, because only the app knows what the ids are.
LIBRARY_SOURCE = """package %(package)s;
public final class TestLibrary
{
    public static int nameId() { return R.string.orkige_test_library_name; }
}
""" % {"package": LIBRARY_PACKAGE}

#: a stand-in `R` to compile the library against. Its fields are deliberately
#: NOT final: a final one would be inlined into the caller and the class would
#: never be looked up, which is the case that passes without the app having
#: generated anything.
LIBRARY_R_SOURCE = """package %(package)s;
public final class R
{
    public static final class string
    {
        public static int orkige_test_library_name = 0;
    }
}
""" % {"package": LIBRARY_PACKAGE}

LIBRARY_VALUES = """<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="orkige_test_library_name">Orkige Test Library</string>
</resources>
"""

#: the engine's Java, stood in for. The library phase is about the ARCHIVE, and
#: the payload road is exactly the shape a machine with no repository packages
#: from - so the glue is synthesised down to two classes that compile against
#: android.jar alone.
PAYLOAD_JAVA = {
    os.path.join("org", "libsdl", "app", "SDLActivity.java"):
        "package org.libsdl.app;\n"
        "public class SDLActivity extends android.app.Activity {}\n",
    os.path.join("com", "orkitec", "orkigeplayer", "OrkigeActivity.java"):
        "package com.orkitec.orkigeplayer;\n"
        "public class OrkigeActivity extends org.libsdl.app.SDLActivity {}\n",
}


def write_file(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as handle:
        handle.write(text)


def make_library_archive(root, java_home, platform_jar):
    """a real Android library archive, GENERATED rather than committed: a zip
    with real javac output, a manifest fragment, a resource, an asset, a
    resource-symbol list and a native library for the ABI being packaged."""
    work = os.path.join(root, "library-source")
    classes = os.path.join(work, "classes")
    os.makedirs(classes)
    write_file(os.path.join(work, "TestLibrary.java"), LIBRARY_SOURCE)
    write_file(os.path.join(work, "R.java"), LIBRARY_R_SOURCE)
    compile_command = [os.path.join(java_home, "bin", "javac"),
                       "-source", "8", "-target", "8", "-nowarn",
                       "-bootclasspath", platform_jar, "-d", classes,
                       os.path.join(work, "TestLibrary.java"),
                       os.path.join(work, "R.java")]
    result = subprocess.run(compile_command, capture_output=True, text=True)
    require(result.returncode == 0,
            "the fixture library compiles: " + result.stderr.strip())

    archive = os.path.join(root, "orkige_test_library.aar")
    jar = os.path.join(work, "classes.jar")
    with zipfile.ZipFile(jar, "w", zipfile.ZIP_DEFLATED) as bundle:
        # only the library's OWN class: its `R` is the app's to generate, which
        # is exactly what makes the resource-id leg of this test mean something
        bundle.write(os.path.join(classes, *LIBRARY_PACKAGE.split("."),
                                  "TestLibrary.class"),
                     "/".join(LIBRARY_PACKAGE.split(".") +
                              ["TestLibrary.class"]))
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr("AndroidManifest.xml", LIBRARY_MANIFEST)
        bundle.write(jar, "classes.jar")
        bundle.writestr("res/values/orkige_test_library.xml", LIBRARY_VALUES)
        bundle.writestr("R.txt",
                        "int string orkige_test_library_name 0x7f010000\n")
        bundle.writestr("assets/orkige_test_library/config.json",
                        "{\"fixture\": true}\n")
        bundle.writestr("jni/arm64-v8a/liborkigetestlibrary.so",
                        "stand-in for the library's own native code\n")
        # ...and the parts an archive carries that this assembly has no
        # consumer for: they must be IGNORED, not refused
        bundle.writestr("proguard.txt", "-keep class ** { *; }\n")
        bundle.writestr("public.txt", "string orkige_test_library_name\n")
    return archive


def make_device_payload(root, repo):
    """the engine half, synthesised as an installed Android player - the road a
    machine with no repository packages from."""
    payload = os.path.join(root, "player-payload")
    os.makedirs(os.path.join(payload, "Media", "Hlms", "Common"))
    write_file(os.path.join(payload, "libmain.so"),
               "stand-in for the cross-built player")
    write_file(os.path.join(payload, "Media", "Hlms", "Common",
                            "PieceCommon.any"), "stand-in shader piece")
    write_file(os.path.join(payload, "orkige_payload.txt"),
               "abi: arm64-v8a\nflavor: next\n")
    # a payload carries the notices, the way every package does
    shutil.copy(os.path.join(repo, "THIRD-PARTY-NOTICES.md"),
                os.path.join(payload, "THIRD-PARTY-NOTICES.md"))
    android = os.path.join(payload, "android")
    source = os.path.join(repo, "tools", "player", "android")
    os.makedirs(android)
    shutil.copy(os.path.join(source, "AndroidManifest.xml"),
                os.path.join(android, "AndroidManifest.xml"))
    shutil.copytree(os.path.join(source, "res"), os.path.join(android, "res"))
    for relative, text in PAYLOAD_JAVA.items():
        write_file(os.path.join(android, "java", relative), text)
    return payload


def make_library_project(root, repo, archive):
    """a project that DEPENDS on the archive: the manifest Setting is the whole
    interface, so this is what a game author writes."""
    project = os.path.join(root, "aar-project")
    shutil.copytree(os.path.join(repo, "projects", "example"), project)
    os.makedirs(os.path.join(project, "libs"))
    shutil.copy(archive, os.path.join(project, "libs",
                                      "orkige_test_library.aar"))
    write_file(os.path.join(project, "project.orkproj"),
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<OrkigeProject version=\"1\">\n"
               "    <Name>Aar Fixture</Name>\n"
               "    <MainScene>scenes/main.oscene</MainScene>\n"
               "    <Setting key=\"export.android.package\" value=\"%s\"/>\n"
               "    <Setting key=\"export.android.libraries\""
               " value=\"libs/orkige_test_library.aar\"/>\n"
               "</OrkigeProject>\n" % APP_PACKAGE)
    return project


def run_library_phase(args, build_tools, java_home, platform_jar, environment):
    """package a project that depends on a library archive, and assert that
    every part the archive brought reached the finished package."""
    log("--- library phase ---")
    root = os.path.join(args.output, "library")
    os.makedirs(root)
    archive = make_library_archive(root, java_home, platform_jar)
    payload = make_device_payload(root, args.repo)
    project = make_library_project(root, args.repo, archive)
    output = os.path.join(root, "out")
    # the DISTRIBUTED shape: a staged engine payload plus the platform's own
    # fetched player, with no repository anywhere in the picture
    bundle = os.path.join(root, "engine-bundle")
    os.makedirs(bundle)

    command = [args.exporter, "--project", project, "--platform", "android",
               "--engine-bundle", bundle, "--device-payload", payload,
               "--output", output]
    log("$ " + " ".join(command))
    result = subprocess.run(command, cwd=args.repo, capture_output=True,
                            text=True, env=environment)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    require(result.returncode == 0, "the library-bearing export succeeded")

    apks = glob.glob(os.path.join(output, "*.apk"))
    require(len(apks) == 1, "it produced one APK")
    apk = apks[0]

    with zipfile.ZipFile(apk) as package:
        names = set(package.namelist())
        # the archive's native library, beside the engine's own. Without it the
        # app installs and then dies inside System.loadLibrary.
        require("lib/arm64-v8a/liborkigetestlibrary.so" in names,
                "the library's native code is packaged")
        require("lib/arm64-v8a/libmain.so" in names,
                "the engine's own native code is still packaged")
        # the archive's assets, by the name its own code reads them under
        require("assets/orkige_test_library/config.json" in names,
                "the library's assets are packaged")
        # ...and listed, so a compressed-mode package extracts them too
        listing = package.read("assets/orkige_assets.txt").decode().split("\n")
        require("orkige_test_library/config.json" in listing,
                "the library's assets are listed in the extraction manifest")
        dex = b"".join(package.read(name) for name in sorted(names)
                       if name.startswith("classes") and name.endswith(".dex"))
        require(b"Lcom/orkitec/testlibrary/TestLibrary;" in dex,
                "the library's compiled class is in the dex")
        # THE resource-id leg: the library's code reads R.string, and the class
        # holding that id is one only the app can generate - so its presence is
        # what says the ids were generated for the library's own package
        require(b"Lcom/orkitec/testlibrary/R$string;" in dex,
                "resource ids were generated for the library's package")
        # nothing the archive carries that this assembly cannot use came along
        for unwanted in ("proguard.txt", "public.txt", "R.txt", "classes.jar"):
            require(unwanted not in names,
                    "the archive's " + unwanted + " stayed out of the package")

    # ...and the platform's OWN tools read the merged manifest back
    aapt2 = os.path.join(build_tools, "aapt2")
    badging = subprocess.run([aapt2, "dump", "badging", apk],
                             capture_output=True, text=True)
    require(badging.returncode == 0, "aapt2 reads the package back")
    require("package: name='%s'" % APP_PACKAGE in badging.stdout,
            "the package keeps the project's own application id")
    # THE assertion this whole tier exists for: a permission the archive asked
    # for is in the manifest that shipped. Dropped, it is an app that installs,
    # launches and throws the first time the library does its job.
    require("uses-permission: name='android.permission.ACCESS_NETWORK_STATE'"
            in badging.stdout, "the library's permission reached the manifest")
    require("uses-permission: name='android.permission.INTERNET'"
            in badging.stdout, "the app's own permission survived the merge")
    require("uses-feature-not-required: name='android.hardware.touchscreen'"
            in badging.stdout,
            "the library's optional feature was carried, still optional")

    tree = subprocess.run([aapt2, "dump", "xmltree", "--file",
                           "AndroidManifest.xml", apk],
                          capture_output=True, text=True)
    require(tree.returncode == 0, "aapt2 reads the compiled manifest")
    require("com.orkitec.testlibrary.TestLibraryActivity" in tree.stdout,
            "the library's activity reached the manifest")
    require(LIBRARY_PACKAGE + ".HOST" in tree.stdout,
            "the library's meta-data reached the manifest")
    # the one placeholder this merge resolves: a host or an authority written
    # against ${applicationId} has to come out as the app's own id, because an
    # unresolved one names a component nothing can reach
    require("\"%s\"" % APP_PACKAGE in tree.stdout,
            "the library's ${applicationId} resolved to the app's package")
    require("${applicationId}" not in tree.stdout,
            "no manifest placeholder survived into the package")

    resources = subprocess.run([aapt2, "dump", "resources", apk],
                               capture_output=True, text=True)
    require(resources.returncode == 0, "aapt2 reads the resource table")
    require("orkige_test_library_name" in resources.stdout,
            "the library's resource is in the linked table")

    verify = subprocess.run(
        [os.path.join(java_home, "bin", "java"), "-jar",
         os.path.join(build_tools, "lib", "apksigner.jar"), "verify", apk],
        capture_output=True, text=True)
    require(verify.returncode == 0,
            "apksigner verifies the library-bearing package")

    run_refusal_legs(args, root, payload, bundle, environment)


def run_refusal_legs(args, root, payload, bundle, environment):
    """the two refusals that would otherwise ship a broken app: a library whose
    native code is for another ABI, and two libraries fighting over one file.
    Both stop the export before an SDK program runs, so they cost nothing."""
    def export_refuses(name, archives, expected):
        project = os.path.join(root, "refuse-" + name)
        shutil.copytree(os.path.join(args.repo, "projects", "example"), project)
        os.makedirs(os.path.join(project, "libs"))
        listed = []
        for index, (archive_name, entries) in enumerate(archives):
            path = os.path.join(project, "libs", archive_name)
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as bundle_zip:
                bundle_zip.writestr("AndroidManifest.xml",
                                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                    "<manifest package=\"com.orkitec.refuse%d\" "
                                    "/>" % index)
                for entry, text in entries.items():
                    bundle_zip.writestr(entry, text)
            listed.append("libs/" + archive_name)
        write_file(os.path.join(project, "project.orkproj"),
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<OrkigeProject version=\"1\">\n"
                   "    <Name>Refuse %s</Name>\n"
                   "    <MainScene>scenes/main.oscene</MainScene>\n"
                   "    <Setting key=\"export.android.libraries\""
                   " value=\"%s\"/>\n"
                   "</OrkigeProject>\n" % (name, ";".join(listed)))
        result = subprocess.run(
            [args.exporter, "--project", project, "--platform", "android",
             "--engine-bundle", bundle, "--device-payload", payload,
             "--output", os.path.join(root, "refuse-out-" + name)],
            cwd=args.repo, capture_output=True, text=True, env=environment)
        message = result.stdout + result.stderr
        require(result.returncode != 0, "the " + name + " export refuses")
        for needle in expected:
            require(needle in message,
                    "the refusal names " + needle + ": " + message.strip())

    # a library whose native code is for another ABI. Packaged without it, the
    # app installs and then dies the first time the library loads its own code
    # - so the export stops and says which archive and which ABIs.
    export_refuses("abi", [("foreign.aar", {
        "jni/x86/libforeign.so": "not the ABI being packaged\n",
        "jni/armeabi-v7a/libforeign.so": "not the ABI being packaged\n",
    })], ["foreign.aar", "x86", "armeabi-v7a", "arm64-v8a"])

    # two libraries bringing a different file under one name. The platform's
    # answer is an overlay order nobody declared, so picking one would be
    # inventing an answer.
    export_refuses("collision", [
        ("first.aar", {"assets/shared/config.json": "{\"from\": \"first\"}\n"}),
        ("second.aar", {"assets/shared/config.json": "{\"from\": \"second\"}\n"}),
    ], ["second.aar", "shared/config.json"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    sdk, build_tools, java_home, platform_jar = find_toolchain()
    log("build tools: " + build_tools)
    log("JDK: " + java_home)
    log("platform jar: " + platform_jar)

    shutil.rmtree(args.output, ignore_errors=True)
    os.makedirs(args.output)
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
    # the exporter is pointed at the EXACT toolchain this driver then verifies
    # the artifact with. Leaving it to find its own would let the two disagree,
    # and the scratch home above hides the default install location anyway.
    environment["ANDROID_HOME"] = sdk
    environment["JAVA_HOME"] = java_home

    glue = find_sdl_java_glue()
    if glue:
        log("SDL Java glue: " + glue)
        run_player_phase(args, build_tools, java_home, environment)
    else:
        # only the PLAYER phase reads them; the library phase synthesises its
        # engine half entirely, so it still runs here
        log("player phase SKIPPED: no SDL3 Java sources under the vcpkg root")
    run_library_phase(args, build_tools, java_home, platform_jar, environment)

    shutil.rmtree(args.output, ignore_errors=True)
    log("PASS")
    return 0


def run_player_phase(args, build_tools, java_home, environment):
    log("--- player phase ---")
    tree = make_engine_tree(args.output)
    apk = os.path.join(args.output, "OrkigePlayer.apk")
    home = environment["HOME"]

    command = [args.exporter, "android-player", "--engine-build", tree,
               "--output", apk]
    log("$ " + " ".join(command))
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


if __name__ == "__main__":
    sys.exit(main())
