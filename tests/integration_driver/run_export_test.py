#!/usr/bin/env python3
"""ctest driver for project export: run the exporter for a project/platform,
assert the packaged artifact's structure, and - for macOS - RUN the exported
app from a neutral cwd (ORKIGE_DEMO_FRAMES caps the run) so a clean exit proves
the bundle is genuinely self-contained.

    run_export_test.py --repo <root> --project <dir> --exporter <orkige_export>
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
import tempfile
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


def check_payload_cook(payload_dir, cooked_names):
    """assert the export-time texture cook conditioned the bundled payload:
    every texture the export REPORTED cooking ships as a container
    (.dds/.ktx/.oitd), and the payload carries no container the export did not
    report. The comparison is on STEMS: the cook renames as it compresses
    (ball.png -> ball.dds), so the log names the source and the payload carries
    the container.

    The expectation comes from the export's own log rather than a second
    reading of the sidecars: the auto-format table lives in the exporter
    (tools/exporter/ExportTextureCook.h) and restating it here would only prove
    the copy agrees with itself. Whether the table is RIGHT is
    ExportTextureCookTests' job; whether the export shipped what it said it
    cooked is this one's."""
    shipped = set()
    for parent, _dirs, files in os.walk(payload_dir):
        for name in files:
            if name.lower().endswith((".dds", ".ktx", ".oitd")):
                shipped.add(os.path.splitext(name)[0])
    require(shipped == cooked_names,
            "the payload ships exactly the textures the export reported "
            "cooking (shipped %s, reported %s)"
            % (sorted(shipped), sorted(cooked_names)))
    log("texture cook: %d compressed texture(s) shipped" % len(shipped))


def expected_samplers(project_dir, platform):
    """the non-default texture samplers the SOURCE project authors, resolved
    for a platform token - the answers the export must have baked. Read from
    the project's own sidecars, so the assertion compares the payload against
    the authoring intent rather than against the exporter restating itself."""
    slot = {"ios-simulator": "ios", "ios": "ios", "android": "android",
            "android-aab": "android", "web": "web"}.get(platform, "")
    samplers = {}
    for parent, _dirs, files in os.walk(project_dir):
        for name in files:
            if not name.endswith(".orkmeta"):
                continue
            try:
                root = ET.parse(os.path.join(parent, name)).getroot()
            except ET.ParseError:
                continue
            block = root.find("texture")
            if block is None:
                continue
            resolved = dict(block.attrib)
            override = block.find(slot) if slot else None
            if override is not None:
                resolved.update(override.attrib)
            filt = resolved.get("filter", "bilinear")
            wrap = resolved.get("wrap", "clamp")
            if filt == "bilinear" and wrap == "clamp":
                continue
            stem = os.path.splitext(os.path.splitext(name)[0])[0]
            samplers[stem] = (filt, wrap)
    return samplers


def check_payload_data(payload_dir, project_dir):
    """authored DATA files (data/) are content a running script reads by
    project-relative name, so every one the project authors must reach the
    payload at the SAME relative path. A payload missing them installs
    perfectly and then runs on nothing. A project with no data/ has nothing to
    prove here (logged, not asserted) - projects/jumper-lua is the one that
    carries the assertion."""
    source_data = os.path.join(project_dir, "data")
    if not os.path.isdir(source_data):
        log("data files: the project authors none - nothing to ship")
        return
    authored = sorted(
        os.path.relpath(os.path.join(parent, name), source_data)
        for parent, _dirs, files in os.walk(source_data) for name in files)
    require(authored, "the source project authors data files at all")
    for relative in authored:
        require(os.path.isfile(os.path.join(payload_dir, "data", relative)),
                "payload carries data/" + relative.replace(os.sep, "/"))
    log("data files: %d authored file(s) shipped" % len(authored))


def check_payload_dev_only(payload_dir, project_dir):
    """DEVELOPMENT artefacts never ship. Two of them, out for two different
    reasons, and both reasons are load-bearing:

      tests/       a project's Lua test suite is run against a project on a dev
                   machine (orkige_player --run-tests), never by a shipped
                   game. It is out BY CONSTRUCTION - `tests` is not a
                   payloadSubdirs() entry - so this asserts the construction,
                   which is what makes adding it to that list fail loudly.
      *.editor.lua an editor TOOL rides inside scripts/, which IS a payload
                   subdirectory, so it needs an explicit strip
                   (stripEditorScripts) and this asserts the strip happened."""
    strays = sorted(
        os.path.relpath(os.path.join(parent, name), payload_dir)
        for parent, _dirs, files in os.walk(payload_dir) for name in files
        if name.endswith(".editor.lua"))
    require(not strays,
            "the payload carries no editor tool scripts (found %s)" % strays)
    require(not os.path.exists(os.path.join(payload_dir, "tests")),
            "the payload carries no tests/ directory")
    authored_tests = os.path.isdir(os.path.join(project_dir, "tests"))
    authored_tools = any(
        name.endswith(".editor.lua")
        for _parent, _dirs, files in os.walk(project_dir) for name in files)
    log("dev-only artefacts: none shipped (source authors tests/=%s, "
        "editor tools=%s)" % (authored_tests, authored_tools))


def check_payload_samplers(payload_dir, project_dir, platform):
    """a packaged payload carries NO .orkmeta - sidecars are editor
    bookkeeping - and the one answer a runtime reads out of them (how a texture
    is sampled) rides in the manifest's baked <TextureSamplers> block instead,
    resolved for the platform being packaged. A sidecar that reaches a payload
    is exactly the packaging bug this asserts away."""
    authored_sidecars = sum(
        1 for _parent, _dirs, files in os.walk(project_dir)
        for name in files if name.endswith(".orkmeta"))
    require(authored_sidecars > 0,
            "the source project authors sidecars at all (%d) - otherwise the "
            "absence assertion below proves nothing" % authored_sidecars)
    strays = []
    for parent, _dirs, files in os.walk(payload_dir):
        for name in files:
            if name.endswith(".orkmeta"):
                strays.append(os.path.relpath(os.path.join(parent, name),
                                              payload_dir))
    require(not strays, "the payload carries no .orkmeta sidecars (found %s)"
            % sorted(strays)[:5])

    manifest = ET.parse(os.path.join(payload_dir, "project.orkproj")).getroot()
    block = manifest.find("TextureSamplers")
    baked = {}
    if block is not None:
        for entry in block.findall("Sampler"):
            baked[entry.get("texture")] = (entry.get("filter", "bilinear"),
                                           entry.get("wrap", "clamp"))
    expected = expected_samplers(project_dir, platform)
    require(baked == expected,
            "the manifest bakes exactly the project's authored samplers for "
            "'%s' (baked %s, authored %s)" % (platform, baked, expected))
    log("texture samplers: %d authored sampler(s) baked, 0 sidecars shipped"
        % len(baked))


# Environment the MACHINE provides, as opposed to the developer tree: the
# display/driver plumbing a windowed run genuinely needs (a headless CI display,
# a software Vulkan ICD) and the audio-driver choice the suite pins.
PASSTHROUGH_ENV = ("DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                   "VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                   "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER", "ORKIGE_AUDIO_BACKEND")


def write_sandbox_profile(path, denied, allowed):
    """a macOS sandbox profile denying every path in `denied` - the clean room:
    the repository, the vcpkg tree and Homebrew's tool directories simply are
    not there - and then re-allowing `allowed` (the export output directory,
    which lives inside the build tree). SBPL evaluates rules in order and the
    LAST match decides, so the allow must come after the deny.

    The final rule makes the allowed area REACHABLE: a subpath deny covers the
    ancestors of the allowed directory too, and a tool that stats its way down
    would fail before reaching it. The allowance is per-DIRECTORY, never a
    subpath, so every other path under a denied root stays invisible to stat as
    well as to read - which matters, because a resource locator's developer-tree
    fallbacks are decided by an existence probe."""
    with open(path, "w") as profile:
        profile.write("(version 1)\n(allow default)\n"
                      "(deny file-read* file-write*\n")
        for entry in denied:
            profile.write('    (subpath "%s")\n' % os.path.realpath(entry))
        profile.write(")\n(allow file-read* file-write*\n")
        for entry in allowed:
            profile.write('    (subpath "%s")\n' % os.path.realpath(entry))
        profile.write(")\n(allow file-read-metadata\n")
        traversable = set()
        for entry in allowed:
            directory = os.path.realpath(entry)
            while True:
                traversable.add(directory)
                parent = os.path.dirname(directory)
                if parent == directory:
                    break
                directory = parent
        for directory in sorted(traversable):
            profile.write('    (literal "%s")\n' % directory)
        profile.write(")\n")


def make_clean_room(repo_root, output_dir):
    """the sandbox profile an exported app is RUN under, or "" where the
    platform has no sandbox tool. An exported game is self-contained by
    contract; running it with the repository reachable would let a build
    machine's files stand in for a resource the bundle forgot to carry."""
    if sys.platform != "darwin" or not os.path.exists("/usr/bin/sandbox-exec"):
        log("clean room: unavailable on this platform - running unsandboxed")
        return ""
    denied = [repo_root]
    # Homebrew's TOOL directories go too - an exported game spawns no tool -
    # but the rest of Homebrew stays readable, because MoltenVK installs there
    # and is the platform's Vulkan DRIVER: system-tier, like a GPU driver
    # anywhere else, and not something an app carries.
    for extra in ("/opt/homebrew/bin", "/opt/homebrew/sbin",
                  "/usr/local/bin", "/usr/local/sbin"):
        if os.path.isdir(extra):
            denied.append(extra)
    profile = os.path.join(output_dir, "cleanroom.sb")
    write_sandbox_profile(profile, denied, [output_dir])
    log("clean room: the repository and the machine's tool directories are "
        "denied (only the exported app is reachable)")
    return profile


def clean_room_env(stage_dir, extra=None):
    """no repository, no developer PATH (and so no python3), a scratch HOME and
    a scratch temp"""
    env = {
        "PATH": "/usr/bin:/bin",
        "HOME": os.path.join(stage_dir, "home"),
        "TMPDIR": os.path.join(stage_dir, "tmp"),
    }
    os.makedirs(env["HOME"], exist_ok=True)
    os.makedirs(env["TMPDIR"], exist_ok=True)
    for name in PASSTHROUGH_ENV:
        if name in os.environ:
            env[name] = os.environ[name]
    env.update(extra or {})
    return env


NOTICES_FILE = "THIRD-PARTY-NOTICES.md"


def check_third_party_notices(resource_root, label):
    """the licenses of the statically linked closure require their text to
    travel with the BINARY, so every package carries the notices at the
    resource root the runtime resolves - beside the default-project marker. An
    empty or stub file would satisfy a presence check and discharge nothing, so
    the content is checked too."""
    path = os.path.join(resource_root, NOTICES_FILE)
    require(os.path.isfile(path), "%s at %s" % (NOTICES_FILE, label))
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    require(len(text) > 10000,
            "%s carries the license texts (%d bytes)" % (NOTICES_FILE,
                                                         len(text)))
    for marker in ("Third-party notices", "OpenAL Soft"):
        require(marker in text, "%s names '%s'" % (NOTICES_FILE, marker))


def check_macos(app_dir, exe_name, run_frames, flavor, sandbox_profile):
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
    check_third_party_notices(resources, "Contents/Resources")

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
    # resource) and inside a CLEAN ROOM (the repository and the machine's tool
    # directories denied outright, a scrubbed PATH with no interpreter), and
    # exits 0 after the frame cap. Neutral cwd alone only stops a RELATIVE
    # path from resolving; the sandbox stops an absolute one too.
    output_dir = os.path.dirname(app_dir)
    environment = clean_room_env(output_dir,
                                 {"ORKIGE_DEMO_FRAMES": str(run_frames)})

    def run_exported(extra_env=None):
        env = dict(environment)
        env.update(extra_env or {})
        command = [executable]
        if sandbox_profile:
            command = ["/usr/bin/sandbox-exec", "-f", sandbox_profile] + command
        return subprocess.run(command, cwd=output_dir, env=env)

    log("running the exported app (%d frames, cwd = output dir%s)"
        % (run_frames, ", clean room" if sandbox_profile else ""))
    require(run_exported().returncode == 0,
            "exported app ran standalone in a clean room and exited 0")

    # Vulkan leg (classic flavor): with the loader + aliases bundled and the
    # platform's Vulkan driver installed (MoltenVK via brew, found through its
    # ICD manifest - the engine points the loader at it when the env is
    # unset), the exported app must also boot with the Vulkan render system
    # explicitly picked. Quietly skipped where the driver is absent.
    moltenvk_icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
    if flavor != "next" and loader_bundled and os.path.isfile(moltenvk_icd):
        log("running the exported app with ORKIGE_RENDERSYSTEM=Vulkan")
        require(run_exported({"ORKIGE_RENDERSYSTEM": "Vulkan"}).returncode == 0,
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
    # an iOS bundle is FLAT, so its root IS the resource root
    check_third_party_notices(app_dir, "the bundle root")
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
    # App Transport Security: cleartext reaches the local network only, so the
    # HTTP client's per-request opt-in works against a service on the
    # developer's machine while arbitrary cleartext stays blocked (see
    # app_transport_security() in the exporter)
    ats = info.get("NSAppTransportSecurity", {})
    require(ats.get("NSAllowsLocalNetworking") is True,
            "Info.plist allows cleartext to the local network")
    require("NSAllowsArbitraryLoads" not in ats,
            "Info.plist does not open arbitrary cleartext loads")
    # the privacy manifest: every iOS bundle carries a parseable
    # PrivacyInfo.xcprivacy declaring no tracking and the audited
    # required-reason API categories (see privacy_manifest() in the exporter)
    privacy_path = os.path.join(app_dir, "PrivacyInfo.xcprivacy")
    require(os.path.isfile(privacy_path),
            "privacy manifest PrivacyInfo.xcprivacy at the bundle root")
    with open(privacy_path, "rb") as privacy_file:
        privacy = plistlib.load(privacy_file)  # raises if not a plist
    require(privacy.get("NSPrivacyTracking") is False,
            "privacy manifest declares no tracking")
    accessed = privacy.get("NSPrivacyAccessedAPITypes", [])
    categories = {entry.get("NSPrivacyAccessedAPIType") for entry in accessed}
    for category in ("NSPrivacyAccessedAPICategoryFileTimestamp",
                     "NSPrivacyAccessedAPICategorySystemBootTime"):
        require(category in categories,
                "privacy manifest declares " + category)
    require(all(entry.get("NSPrivacyAccessedAPITypeReasons")
                for entry in accessed),
            "every accessed-API entry carries a reason code")


def check_android(apk_path, aapt2):
    require(os.path.isfile(apk_path), "APK exists: " + apk_path)
    with zipfile.ZipFile(apk_path) as apk:
        names = set(apk.namelist())
        for required in ("classes.dex", "lib/arm64-v8a/libmain.so",
                         "AndroidManifest.xml", "assets/orkige_project.txt",
                         "assets/project/project.orkproj",
                         "assets/orkige_assets.txt",
                         # the license texts the bundled libraries require to
                         # travel with the binary, at the assets root the
                         # runtime resolves
                         "assets/" + NOTICES_FILE):
            require(required in names, "APK carries " + required)
        require(len(apk.read("assets/" + NOTICES_FILE)) > 10000,
                "the APK's %s carries the license texts" % NOTICES_FILE)
        extraction_list = apk.read("assets/orkige_assets.txt").decode()
        for listed in ("orkige_project.txt", "project/project.orkproj"):
            require(listed in extraction_list.splitlines(),
                    "extraction manifest lists " + listed)
        # export.android.assets defaults to `stored`: the APK's asset entries
        # are UNCOMPRESSED so the player MOUNTS the APK and reads/streams them in
        # place, and a marker tells it to mount rather than extract. (A
        # `compressed` project would carry no marker and deflate its assets - the
        # older extract-on-first-launch path; the mode parse is unit-covered by
        # orkige_export --selftest.)
        require("assets/orkige_mount.txt" in names,
                "APK carries the stored-mode mount marker")
        for info in apk.infolist():
            if info.filename.startswith("assets/") \
                    and not info.filename.endswith("/"):
                require(info.compress_type == zipfile.ZIP_STORED,
                        "asset '%s' is STORED (mount in place)"
                        % info.filename)
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
                         "assets/orkige_assets.txt",
                         "assets/" + NOTICES_FILE,
                         # stored mode: the mount marker is staged; bundletool
                         # keeps the assets uncompressed in the generated APKs
                         # via the BundleConfig uncompressedGlob the export
                        # writes
                         "assets/orkige_mount.txt"):
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
    parser.add_argument("--exporter", required=True,
                        help="the orkige_export executable under test")
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
    exporter = [args.exporter,
                "--project", args.project, "--platform", args.platform,
                "--engine-build", args.engine_build, "--output", args.output]
    if args.platform == "android-aab":
        # the release-bundle test asserts the STRUCTURE of the bundle module -
        # the bundletool-free, keystore-free slice. The signed .aab needs
        # machine-local secrets this machine (and CI) does not have; that path
        # is gated + documented, exercised by the exporter's selftest.
        exporter.append("--aab-unsigned-module")
    log("$ " + " ".join(exporter))
    result = subprocess.run(exporter, capture_output=True, text=True)
    print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", flush=True)
    if result.returncode != 0:
        fail("exporter exited nonzero")
    # what the export SAID it cooked - the expectation the payload is held to
    cooked_names = set(os.path.splitext(match.group(1))[0]
                       for match in re.finditer(
                           r"^orkige_export: cooked (\S+) \(",
                           result.stdout or "", re.MULTILINE))

    name, exe_name = project_names(args.project)
    flavor = read_cmake_cache(args.engine_build,
                              "ORKIGE_RENDER_BACKEND") or "classic"
    if args.platform == "macos":
        artifact = os.path.join(args.output, name + ".app")
        payload = os.path.join(artifact, "Contents", "Resources", "project")
        check_payload_cook(payload, cooked_names)
        check_payload_samplers(payload, args.project, args.platform)
        check_payload_data(payload, args.project)
        check_payload_dev_only(payload, args.project)
        check_macos(artifact, exe_name, args.run_frames, flavor,
                    make_clean_room(args.repo, args.output))
    elif args.platform == "ios-simulator":
        artifact = os.path.join(args.output, name + ".app")
        check_ios(artifact, flavor)
        check_payload_cook(os.path.join(artifact, "project"), cooked_names)
        check_payload_samplers(os.path.join(artifact, "project"), args.project,
                               args.platform)
        check_payload_data(os.path.join(artifact, "project"), args.project)
        check_payload_dev_only(os.path.join(artifact, "project"),
                               args.project)
    elif args.platform == "android-aab":
        artifact = os.path.join(args.output, exe_name + ".aab.module.zip")
        check_android_aab_module(artifact)
    else:
        artifact = os.path.join(args.output, exe_name + ".apk")
        check_android(artifact, aapt2)
    if args.platform in ("android", "android-aab"):
        # the payload rides inside the archive: extract it and run the same
        # cooked-payload assertions the directory bundles get
        with zipfile.ZipFile(artifact) as archive, \
                tempfile.TemporaryDirectory() as temp_dir:
            members = [entry for entry in archive.namelist()
                       if entry.startswith("assets/project/")]
            archive.extractall(temp_dir, members)
            payload = os.path.join(temp_dir, "assets", "project")
            check_payload_cook(payload, cooked_names)
            check_payload_samplers(payload, args.project, args.platform)
            check_payload_data(payload, args.project)
            check_payload_dev_only(payload, args.project)

    log("artifact %s (%.1f MiB)" % (artifact,
        directory_size(artifact) / (1024.0 * 1024.0)))
    log("PASS")


if __name__ == "__main__":
    main()
