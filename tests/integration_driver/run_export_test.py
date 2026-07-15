#!/usr/bin/env python3
"""ctest driver for project export: run Util/orkige_export.py
for a project/platform, assert the packaged artifact's structure, and - for
macOS - RUN the exported app from a neutral cwd (ORKIGE_DEMO_FRAMES caps the
run) so a clean exit proves the bundle is genuinely self-contained.

    run_export_test.py --repo <root> --project <dir>
                       --platform macos|ios-simulator|android
                       --engine-build <dir> --output <dir> [--run-frames N]

Exit codes: 0 pass, 77 skip (missing platform build/SDK - the ctest
SKIP_RETURN_CODE), anything else fail.
"""

import argparse
import os
import plistlib
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

SKIP = 77


def log(message):
    print("run_export_test: " + message, flush=True)


def fail(message):
    print("run_export_test: FAILED - " + message, flush=True)
    sys.exit(1)


def skip(message):
    print("run_export_test: SKIP - " + message, flush=True)
    sys.exit(SKIP)


def require(condition, message):
    if not condition:
        fail(message)
    log("ok: " + message)


def directory_size(path):
    if os.path.isfile(path):
        return os.path.getsize(path)
    return sum(os.path.getsize(os.path.join(parent, name))
               for parent, _, files in os.walk(path) for name in files
               if not os.path.islink(os.path.join(parent, name)))


def project_names(project_dir):
    """(display name, exe name) from the manifest, mirroring the exporter"""
    manifest = ET.parse(os.path.join(project_dir, "project.orkproj")).getroot()
    name = (manifest.findtext("Name") or "").strip()
    return name, re.sub(r"[^A-Za-z0-9]", "", name)


def read_cmake_cache(build_dir, variable):
    cache_path = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache_path):
        return ""
    with open(cache_path, "r", errors="replace") as cache:
        for line in cache:
            if line.startswith(variable + ":"):
                return line.split("=", 1)[1].strip()
    return ""


def check_macos(app_dir, exe_name, run_frames, flavor):
    contents = os.path.join(app_dir, "Contents")
    executable = os.path.join(contents, "MacOS", exe_name)
    require(os.path.isdir(app_dir), "app bundle exists: " + app_dir)
    require(os.path.isfile(os.path.join(contents, "Info.plist")),
            "Info.plist present")
    require(os.path.isfile(executable) and os.access(executable, os.X_OK),
            "executable Contents/MacOS/" + exe_name)
    resources = os.path.join(contents, "Resources")
    # app icon: a non-empty AppIcon.icns + the plist key that points at it
    icns = os.path.join(resources, "AppIcon.icns")
    require(os.path.isfile(icns) and os.path.getsize(icns) > 0,
            "app icon Resources/AppIcon.icns present")
    with open(os.path.join(contents, "Info.plist"), "rb") as plist_file:
        info = plistlib.load(plist_file)
    require(info.get("CFBundleIconFile") == "AppIcon",
            "Info.plist CFBundleIconFile names the icon")
    marker = os.path.join(resources, "orkige_project.txt")
    require(os.path.isfile(marker), "default-project marker present")
    with open(marker) as marker_file:
        require(marker_file.read().strip() == "project",
                "marker names the bundled project dir")
    require(os.path.isfile(os.path.join(resources, "project",
                                        "project.orkproj")),
            "project manifest bundled")
    require(os.path.isdir(os.path.join(resources, "project", "scenes")),
            "project scenes/ bundled")
    # the bundled engine media is flavor-specific: the classic flavor ships the
    # RTSS shader library (Main + RTShaderLib), the Ogre-Next flavor the Hlms
    # shader templates (Media/Hlms) the runtime registers via setHlmsMediaDir
    media_subdirs = ("Hlms",) if flavor == "next" else ("Main", "RTShaderLib")
    for media_subdir in media_subdirs:
        media = os.path.join(resources, "Media", media_subdir)
        require(os.path.isdir(media) and os.listdir(media),
                "engine media Media/%s bundled" % media_subdir)

    # self-containment of the binary itself: no dylib may resolve outside
    # the bundle or the OS (otool is available wherever these tests run)
    otool = subprocess.run(["otool", "-L", executable], capture_output=True,
                           text=True, check=True).stdout
    for line in otool.splitlines()[1:]:
        dep = line.strip().split(" (")[0]
        if not dep or dep.startswith(("/usr/lib/", "/System/")):
            continue
        if dep.startswith("@rpath/"):
            bundled = os.path.join(contents, "Frameworks",
                                   dep[len("@rpath/"):])
            require(os.path.isfile(bundled),
                    "rpath dependency bundled: " + dep)
        else:
            fail("executable references a machine path: " + dep)

    # a bundled Vulkan loader must carry its dlopen leaf-name aliases: the
    # render system's loader probe dlopens the unversioned names
    # ("libvulkan.dylib"/"libvulkan.1.dylib"), which the exporter recreates
    # as symlinks beside the versioned file (macos_dylib_aliases)
    frameworks = os.path.join(contents, "Frameworks")
    loader_bundled = os.path.isdir(frameworks) and any(
        re.match(r"libvulkan\.[0-9.]+\.dylib$", name)
        for name in os.listdir(frameworks))
    if loader_bundled:
        for alias in ("libvulkan.dylib", "libvulkan.1.dylib"):
            require(os.path.isfile(os.path.join(frameworks, alias)),
                    "Vulkan loader dlopen alias bundled: " + alias)

    # THE proof: the exported app runs standalone, from a NEUTRAL cwd (the
    # output dir - never the source tree, whose files could mask a missing
    # resource), and exits 0 after the frame cap
    environment = dict(os.environ)
    environment["ORKIGE_DEMO_FRAMES"] = str(run_frames)
    environment.pop("ORKIGE_DEMO_SCREENSHOT", None)
    log("running the exported app (%d frames, cwd = output dir)" % run_frames)
    result = subprocess.run([executable], cwd=os.path.dirname(app_dir),
                            env=environment)
    require(result.returncode == 0,
            "exported app ran standalone and exited 0")

    # Vulkan leg (classic flavor): with the loader + aliases bundled and the
    # platform's Vulkan driver installed (MoltenVK via brew, found through its
    # ICD manifest - the engine points the loader at it when the env is
    # unset), the exported app must also boot with the Vulkan render system
    # explicitly picked. Quietly skipped where the driver is absent.
    moltenvk_icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
    if flavor != "next" and loader_bundled and os.path.isfile(moltenvk_icd):
        environment["ORKIGE_RENDERSYSTEM"] = "Vulkan"
        log("running the exported app with ORKIGE_RENDERSYSTEM=Vulkan")
        result = subprocess.run([executable], cwd=os.path.dirname(app_dir),
                                env=environment)
        require(result.returncode == 0,
                "exported app ran on the Vulkan render system and exited 0")
    elif flavor != "next":
        log("Vulkan leg skipped (loader bundled: %s, MoltenVK ICD: %s)"
            % (loader_bundled, os.path.isfile(moltenvk_icd)))


def check_ios(app_dir, flavor):
    require(os.path.isdir(app_dir), "app bundle exists: " + app_dir)
    require(os.path.isfile(os.path.join(app_dir, "OrkigePlayer")),
            "player binary present")
    plist_path = os.path.join(app_dir, "Info.plist")
    require(os.path.isfile(plist_path), "Info.plist present")
    marker = os.path.join(app_dir, "orkige_project.txt")
    require(os.path.isfile(marker), "default-project marker present")
    require(os.path.isfile(os.path.join(app_dir, "project",
                                        "project.orkproj")),
            "project manifest bundled")
    # flavor-specific engine media (see check_macos): classic RTSS (Main) vs
    # Ogre-Next Hlms shader templates
    media_subdir = "Hlms" if flavor == "next" else "Main"
    require(os.path.isdir(os.path.join(app_dir, "Media", media_subdir)),
            "engine media Media/%s bundled" % media_subdir)
    # icon + launch screen + rewritten identity: the export must replace the
    # generic player identity and add the loose icons + a launch screen
    with open(plist_path, "rb") as plist_file:
        info = plistlib.load(plist_file)
    require("UILaunchScreen" in info, "Info.plist has a UILaunchScreen key")
    icon_files = (info.get("CFBundleIcons", {})
                  .get("CFBundlePrimaryIcon", {}).get("CFBundleIconFiles"))
    require(bool(icon_files), "Info.plist lists CFBundleIconFiles")
    require(any(name.startswith("AppIcon") and name.endswith(".png")
                for name in os.listdir(app_dir)),
            "loose AppIcon*.png at the bundle root")
    require(info.get("CFBundleIdentifier") != "com.orkitec.orkige-player",
            "CFBundleIdentifier rewritten off the generic player identity")


def check_android(apk_path, aapt2):
    require(os.path.isfile(apk_path), "APK exists: " + apk_path)
    with zipfile.ZipFile(apk_path) as apk:
        names = set(apk.namelist())
        for required in ("classes.dex", "lib/arm64-v8a/libmain.so",
                         "AndroidManifest.xml", "assets/orkige_project.txt",
                         "assets/project/project.orkproj",
                         "assets/orkige_assets.txt"):
            require(required in names, "APK carries " + required)
        extraction_list = apk.read("assets/orkige_assets.txt").decode()
        for listed in ("orkige_project.txt", "project/project.orkproj"):
            require(listed in extraction_list.splitlines(),
                    "extraction manifest lists " + listed)
    # launcher icon + label via aapt2 (build-tools are guaranteed by the skip
    # guard): badging reports the compiled application-icon densities + label
    badging = subprocess.run([aapt2, "dump", "badging", apk_path],
                             capture_output=True, text=True, check=True).stdout
    require("application-icon" in badging,
            "aapt2 badging reports a launcher icon (mipmaps linked)")
    require("mipmap" not in badging or "ic_launcher" in badging,
            "launcher icon resolves to ic_launcher")


def check_android_aab_module(module_path):
    """assert the STRUCTURE of the proto bundle module (the bundletool input) -
    the signing-free slice the release-bundle path can always build. A real
    bundletool build-bundle + jarsigner is gated on machine-local secrets."""
    require(os.path.isfile(module_path), "bundle module exists: " + module_path)
    with zipfile.ZipFile(module_path) as module:
        names = set(module.namelist())
        for required in ("manifest/AndroidManifest.xml", "dex/classes.dex",
                         "resources.pb", "lib/arm64-v8a/libmain.so",
                         "assets/orkige_project.txt",
                         "assets/project/project.orkproj",
                         "assets/orkige_assets.txt"):
            require(required in names, "module carries " + required)
        # the manifest must be PROTOBUF-encoded (bundletool rejects binary AXML
        # or plain-text XML). A proto manifest is not valid XML text; assert it
        # does not start with the XML declaration a text/binary manifest would.
        manifest = module.read("manifest/AndroidManifest.xml")
        require(not manifest.lstrip().startswith(b"<?xml"),
                "manifest is protobuf-encoded (not plain-text XML)")
        # the renamed release package + version live as plaintext inside the
        # protobuf; the debuggable attribute name is present (release flips it
        # to false)
        require(b"com.orkitec.jumperlua" in manifest,
                "manifest carries the release package name")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--platform", required=True,
                        choices=["macos", "ios-simulator", "android",
                                 "android-aab"])
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--run-frames", type=int, default=90)
    args = parser.parse_args()

    player_dir = os.path.join(args.engine_build, "tools", "player")
    if args.platform == "ios-simulator" and not os.path.isdir(
            os.path.join(player_dir, "OrkigePlayer.app")):
        skip("no built iOS player app under '%s' - build the matching "
             "ios-simulator preset to enable this test" % player_dir)
    aapt2 = ""
    if args.platform in ("android", "android-aab"):
        if not os.path.isfile(os.path.join(player_dir, "libmain.so")):
            skip("no built Android player under '%s' - build the "
                 "android-debug preset to enable this test" % player_dir)
        sdk = os.environ.get("ANDROID_HOME",
                             os.path.expanduser("~/Library/Android/sdk"))
        aapt2 = os.path.join(sdk, "build-tools", "35.0.0", "aapt2")
        if not os.path.isfile(aapt2):
            skip("no Android build-tools 35.0.0 under '%s'" % sdk)

    # a fresh output dir per run - stale artifacts must not mask a failure
    if os.path.exists(args.output):
        shutil.rmtree(args.output)
    exporter = [sys.executable,
                os.path.join(args.repo, "Util", "orkige_export.py"),
                "--project", args.project, "--platform", args.platform,
                "--engine-build", args.engine_build, "--output", args.output]
    if args.platform == "android-aab":
        # the release-bundle test asserts the STRUCTURE of the bundle module -
        # the bundletool-free, keystore-free slice. The signed .aab needs
        # machine-local secrets this machine (and CI) does not have; that path
        # is gated + documented, exercised by the exporter's selftest.
        exporter.append("--aab-unsigned-module")
    log("$ " + " ".join(exporter))
    if subprocess.run(exporter).returncode != 0:
        fail("exporter exited nonzero")

    name, exe_name = project_names(args.project)
    flavor = read_cmake_cache(args.engine_build,
                              "ORKIGE_RENDER_BACKEND") or "classic"
    if args.platform == "macos":
        artifact = os.path.join(args.output, name + ".app")
        check_macos(artifact, exe_name, args.run_frames, flavor)
    elif args.platform == "ios-simulator":
        artifact = os.path.join(args.output, name + ".app")
        check_ios(artifact, flavor)
    elif args.platform == "android-aab":
        artifact = os.path.join(args.output, exe_name + ".aab.module.zip")
        check_android_aab_module(artifact)
    else:
        artifact = os.path.join(args.output, exe_name + ".apk")
        check_android(artifact, aapt2)

    log("artifact %s (%.1f MiB)" % (artifact,
        directory_size(artifact) / (1024.0 * 1024.0)))
    log("PASS")


if __name__ == "__main__":
    main()
