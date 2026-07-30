#!/usr/bin/env python3
"""Package a Release build tree as a downloadable Orkige EDITOR archive - the
desktop-binary half of the nightly pipeline (python3 stdlib only, same rules as
the other Util/ generators).

    orkige_nightly_package.py --platform macos|linux|windows
                              --build-dir build/<release preset>
                              --commit <sha> [--date YYYY-MM-DD]
                              [--version <ordered version>] [--since <sha>]
                              [--output <dir>]

    orkige_nightly_package.py --verify <unpacked dir> --platform <p>
                              [--commit <sha>] [--version <ordered version>]

    orkige_nightly_package.py --verify-dmg <disk image>

    orkige_nightly_package.py --identity --commit <sha> [--date YYYY-MM-DD]

    orkige_nightly_package.py --changelog --commit <sha> [--since <sha>]
                              [--changelog-out <file>]

    orkige_nightly_package.py --verify-checksums <assets dir>

    orkige_nightly_package.py --selftest [--selftest-dmg]

This packages what a preset build tree ALREADY produced - it never builds. The
project exporter (orkige_export.py) is the sibling that packages a GAME; this
one packages the TOOL, and reuses the exporter's build-tree helpers (media
resolution, the macOS dylib closure) rather than restating them.

The staged tree has ONE shape on every platform:

    Orkige-<platform>-<version>/
        VERSION                 the build identity, one `key: value` per line
        CHANGELOG.md            what landed since the previous nightly
        KNOWN-LIMITATIONS.md    what this binary cannot do yet (generated from
                                the LIMITATIONS table below - a gap that closes
                                is a deleted entry, never rewritten prose)
        <the editor>            Orkige.app on macOS, orkige_editor[.exe] else
        <the player>            beside the editor, for Play
        Media/                  the engine shader/font/water/decal media

IDENTITY: one ordered version, composed HERE (nightly_version) and consumed by
every surface - the archive filename, the VERSION file, the release notes an
updater reads, and the binary itself, which composes the same grammar from the
same two stamped values (orkige_core/core_util/VersionOrder.h; the smoke test
matches the two, so they cannot drift). A commit sha cannot answer "is that
download newer than what I run"; "2.0.0-nightly.20260730+dea551f9e" can.

macOS puts the payload INSIDE the bundle (Contents/MacOS for the binaries,
Contents/Resources/Media for the media - where PlayerBundle already looks) and
repeats VERSION + KNOWN-LIMITATIONS.md at the archive root so a user reads them
before installing. Linux and Windows keep it flat beside the executable, which
is where SDL_GetBasePath resolves.

Each platform ships the container its users expect. The PORTABLE archive is
.zip on macOS (through ditto, which preserves the bundle's symlinks and
executable bits) and Windows, .tar.gz on Linux. The two desktop platforms with
an installable convention get a second asset from the SAME staging: a
drag-to-Applications .dmg on macOS and a per-user installer on Windows (see the
block above make_dmg for why each exists).

macOS artifacts are signed. With a Developer ID certificate reachable (an
identity from --signing-identity or ORKIGE_MACOS_SIGNING_IDENTITY) the bundle is
sealed inside-out with the hardened runtime and a secure timestamp, the app and
the disk image are each notarized and stapled, and the VERSION file records
`signing: developer-id-notarized`. Without one - a fork, a pull request, a hand
run on a machine with no certificate - the same seal runs ad-hoc, the log says
so, and the artifact's KNOWN-LIMITATIONS.md carries the record describing what
that costs its user. Nothing in between ships: a certificate that cannot sign or
a submission Apple does not accept fails the build.

The last line on success is "orkige_nightly_package: OK <artifact>", the same
machine-readable contract orkige_export.py ends on.
"""

import argparse
import datetime
import hashlib
import json
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

# the pipeline this packager serves. The release notes it composes carry the
# machine-readable markers a client reads, so the self-checks drive that step's
# own script (workflow_step_script) instead of restating what it says.
NIGHTLY_WORKFLOW = os.path.join(REPO_ROOT, ".github", "workflows",
                                "nightly.yml")

PLATFORMS = ("macos", "linux", "windows")

# the archive format per platform
ARCHIVE_SUFFIX = {"macos": ".zip", "linux": ".tar.gz", "windows": ".zip"}

# the macOS editor bundle the build tree produces (tools/editor/CMakeLists.txt
# sets OUTPUT_NAME "Orkige" + MACOSX_BUNDLE)
MACOS_APP_NAME = "Orkige.app"

# editor settings files. The editor writes these into the per-user application
# data directory, but an older tree may still hold a copy beside the binary -
# never ship one: a fresh download must start with the editor's own defaults,
# not with a build machine's window layout and recent projects.
SETTINGS_FILES = ("orkige_editor_view.ini", "orkige_editor_imgui.ini")

# where a non-Apple build keeps the resources the editor resolves relative to
# its own executable (the macOS bundle uses Contents/Resources). This is the
# editor's own layout contract - the archive must match it or a downloaded
# editor finds no shader media and draws nothing.
FLAT_RESOURCE_DIR = os.path.join("share", "orkige")

# the editor's own UI fonts (icon glyphs, the terminal/script mono face) and
# their licences, which ride in its resource root
EDITOR_UI_FONTS = ("fa-solid-900.ttf", "DejaVuSans.ttf",
                   "LICENSE-fontawesome.txt",
                   "LICENSE-iconfontcppheaders.txt", "LICENSE-dejavu.txt")


def log(message):
    print("orkige_nightly_package: " + message, flush=True)


def warn(message):
    print("orkige_nightly_package: WARNING: " + message, flush=True)


def fail(message):
    print("orkige_nightly_package: ERROR: " + message, flush=True)
    sys.exit(1)


# --- what a macOS build's signature is worth --------------------------------
#
# ONE vocabulary, used by everything that has to say it: the VERSION file's
# `signing:` line, the limitations record this build carries, the workflow's job
# output and the release notes. Three states, and no fourth - a build is either
# internally consistent (ad-hoc), identified (Developer ID) or identified AND
# vouched for by Apple (notarized + stapled).

SIGN_ADHOC = "ad-hoc"
SIGN_DEVELOPER_ID = "developer-id"
SIGN_NOTARIZED = "developer-id-notarized"


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
    applies to; PLATFORMS means all of them. `signing` narrows an entry to ONE
    macOS signature state (SIGN_*), so a record describing what an unsigned
    download does is absent from a signed one rather than lying to its reader;
    an entry that leaves it empty applies whatever the signature is worth."""

    def __init__(self, key, platforms, title, detail, workaround="",
                 signing=""):
        self.key = key
        self.platforms = tuple(platforms)
        self.title = title
        self.detail = detail
        self.workaround = workaround
        self.signing = signing

    def applies_to(self, platform, signing=SIGN_ADHOC):
        if self.signing and self.signing != signing:
            return False
        return platform in self.platforms


LIMITATIONS = (
    Limitation(
        key="project-export",
        platforms=PLATFORMS,
        title="Exporting a game needs the engine repository",
        detail="Build > Export packages a project by copying binaries and media "
               "out of an engine build tree and running Util/orkige_export.py, "
               "so it needs both that tree and python3 3.10+ on PATH.",
        workaround="Export from a clone of the engine repository."),
    Limitation(
        key="asset-cooks-need-python",
        platforms=PLATFORMS,
        title="Importing a Lottie animation needs python3 and the repository",
        detail="Importing a Lottie .json cooks it to an .oanim through "
               "Util/cook_vector_anim.py, which the editor runs from the engine "
               "source tree it was built against. Importing an .svg needs "
               "neither - that cook runs inside the editor.",
        workaround="Author .oanim text assets directly, or import the animation "
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
        key="unsigned-macos",
        platforms=("macos",),
        signing=SIGN_ADHOC,
        title="Neither the app nor the disk image is signed or notarized",
        detail="This build carries an ad-hoc signature, which makes the bundle "
               "internally consistent but names no developer. macOS refuses a "
               "downloaded app from an unidentified developer and reports it "
               "as damaged or unopenable. The .dmg is the install shape, not a "
               "trust shape: an unsigned image is blocked exactly like an "
               "unsigned .zip until a notarization ticket can be stapled to "
               "it.",
        workaround="Install first - open the .dmg and drag Orkige.app to "
                   "Applications - then remove the download quarantine flag "
                   "once:\n"
                   "    xattr -dr com.apple.quarantine /Applications/Orkige.app\n"
                   "Or right-click the installed app, choose Open, and "
                   "confirm."),
    Limitation(
        key="unnotarized-macos",
        platforms=("macos",),
        signing=SIGN_DEVELOPER_ID,
        title="The app is signed but not notarized",
        detail="This build carries a Developer ID signature with the hardened "
               "runtime and a secure timestamp, so it names its developer - but "
               "it was not submitted to Apple, so there is no notarization "
               "ticket stapled to it. macOS blocks a downloaded app it cannot "
               "check with Apple and says it cannot be verified.",
        workaround="Install first - open the .dmg and drag Orkige.app to "
                   "Applications - then right-click the installed app, choose "
                   "Open and confirm; or remove the download quarantine flag "
                   "once:\n"
                   "    xattr -dr com.apple.quarantine /Applications/Orkige.app"),
    Limitation(
        key="unsigned-windows",
        platforms=("windows",),
        title="Neither the installer nor the executable is signed",
        detail="SmartScreen warns that it does not recognise the publisher. On "
               "the installer that warning is the loudest one Windows has - a "
               "full-screen \"Windows protected your PC\" prompt whose default "
               "button is Don't run - because it is a program asking to "
               "install software rather than a file being unpacked.",
        workaround="Choose \"More info\" and then \"Run anyway\". The .zip "
                   "beside the installer needs no such confirmation: unpack it "
                   "anywhere and run orkige_editor.exe (right-click the .zip, "
                   "open Properties and tick Unblock if the download was "
                   "blocked)."),
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


def limitations_for(platform, signing=SIGN_ADHOC):
    return tuple(entry for entry in LIMITATIONS
                 if entry.applies_to(platform, signing))


def limitations_markdown(platform, identity_lines=(), signing=SIGN_ADHOC):
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
    for entry in limitations_for(platform, signing):
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


# --- the ORDERED version ---------------------------------------------------
#
# A commit sha names a tree but has no order, so "is this download newer than
# what I run" is unanswerable from one. The ordered identity answers it:
#
#     2.0.0-nightly.20260730+dea551f9e
#     ^^^^^ ^^^^^^^ ^^^^^^^^ ^^^^^^^^^
#     base  channel  date     commit (build metadata)
#
# It is semantic versioning 2.0.0, so the precedence rules are the standard
# ones: base first, then the date (a numeric prerelease identifier), with the
# build metadata carried but NEVER ordered - two builds of one day are the same
# version, which is exactly right, because a rebuild of today's tree is not an
# update. The comparison an updater performs lives in
# orkige_core/core_util/VersionOrder.h (pure, unit-tested); this is the
# composition side, and the two are pinned to the same literals by their tests.

VERSION_CHANNEL = "nightly"


def compact_date(date):
    """"2026-07-30" (or the already-compact "20260730") as the 8-digit date
    identifier the version orders by; "" when it is not one"""
    digits = (date or "").replace("-", "").strip()
    return digits if len(digits) == 8 and digits.isdigit() else ""


def nightly_version(date, commit, base=""):
    """the ordered identity of one nightly build - the ONE place it is
    composed. Returns "" when the stamp is incomplete (no usable date, no
    declared base version): an identity is never invented from half a stamp,
    and a caller with none falls back to naming things by the commit."""
    base = base or project_version()
    if not re.match(r"^\d+\.\d+\.\d+$", base or ""):
        return ""
    day = compact_date(date)
    if not day:
        return ""
    identity = "%s-%s.%s" % (base, VERSION_CHANNEL, day)
    commit = short_commit(commit)
    if commit:
        identity += "+" + commit
    return identity


def version_filename_token(version):
    """the same version rendered for a FILENAME: "+" becomes "_".

    Download paths and asset stores sanitise characters outside
    [A-Za-z0-9._-], which would silently rewrite the "+" and leave a client
    unable to match the file against the version it polled. "_" is not a
    semantic-versioning character at all, so the token reads back to the SAME
    version with no ambiguity (VersionOrder::parse accepts both)."""
    return (version or "").replace("+", "_")


def artifact_label(commit, version=""):
    """what names an artifact: the ordered version in its filename rendering,
    the short commit when there is no version (a hand-run with no date), and
    "unstamped" when there is neither."""
    return version_filename_token(version) or short_commit(commit) or "unstamped"


def artifact_stem(platform, commit, version=""):
    return "Orkige-%s-%s" % (platform, artifact_label(commit, version))


def artifact_name(platform, commit, version=""):
    return artifact_stem(platform, commit, version) + ARCHIVE_SUFFIX[platform]


def version_text(platform, commit, date, build_dir, extra=(), version=""):
    """the VERSION file: one `key: value` per line, so a human reads it and a
    script greps it. `version` is the ORDERED identity - the same string the
    archive is named after, the release notes carry and the binary reports; the
    commit and date it is composed from are spelled out beside it."""
    fields = [("product", "orkige editor"),
              ("version", version or project_version()),
              ("base-version", project_version()),
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


# --- the changelog ---------------------------------------------------------
#
# The range is the previous nightly's commit (exclusive) to tonight's
# (inclusive). That lower bound costs nothing: the publish job already writes a
# machine-readable marker into the release notes so the gate can skip an
# unchanged tree, and the same marker IS the changelog's lower bound.
#
# The entry text comes from this repository's commit subjects, which are one
# dense narrative line whose FIRST CLAUSE is a genuine headline, separated from
# the rest by ": ". Splitting there yields the headline and nothing else; the
# fallbacks below cover a subject shaped differently.
#
# The seam: git is asked for its log on one side (git_log_subjects), and every
# decision about what the output says is on the other (pure, so it is tested
# against synthetic log text rather than against whatever history a machine has).

CHANGELOG_FILE = "CHANGELOG.md"
# how many entries a changelog lists before it says "+N more" - a nightly after
# a busy fortnight must not render as an unbounded wall
CHANGELOG_MAX_ENTRIES = 20
# how far back to look when there is no previous marker at all (the first night,
# or a hand-deleted release): a bounded window, named in the output as such
CHANGELOG_WINDOW = 20
# the hard ceiling on one entry, for a subject with neither a clause nor a
# sentence boundary in reach
CHANGELOG_TEXT_CAP = 120


def changelog_headline(subject):
    """one commit subject reduced to its headline: the text before the first
    ": ", else the first sentence, else a hard character cap. Pure."""
    text = " ".join((subject or "").split())
    if not text:
        return ""
    clause = text.find(": ")
    if clause > 0:
        text = text[:clause]
    else:
        sentence = re.search(r"\.(\s|$)", text)
        if sentence and sentence.start() > 0:
            text = text[:sentence.start()]
    text = text.strip()
    if len(text) > CHANGELOG_TEXT_CAP:
        text = text[:CHANGELOG_TEXT_CAP - 1].rstrip() + "…"
    return text


def parse_log_subjects(text):
    """`<short sha>\\t<subject>` lines into [(sha, subject)], newest first (the
    order git emits). Pure; blank and malformed lines are skipped."""
    entries = []
    for line in (text or "").splitlines():
        if "\t" not in line:
            continue
        sha, subject = line.split("\t", 1)
        sha = sha.strip()
        if sha:
            entries.append((sha, subject))
    return entries


def changelog_markdown(entries, previous_commit="", note="",
                       max_entries=CHANGELOG_MAX_ENTRIES):
    """the changelog section both the release notes and the artifact's
    CHANGELOG.md carry. Pure - it never asks git anything."""
    lines = []
    if previous_commit:
        lines.append("## Changes since `%s`" % short_commit(previous_commit))
    else:
        lines.append("## Recent changes")
    lines.append("")
    if note:
        lines.append(note)
        lines.append("")
    if not entries:
        lines.append("No commits since the previous nightly."
                     if previous_commit else "No commit history to list.")
        return "\n".join(lines).rstrip() + "\n"
    shown = entries[:max_entries]
    for sha, subject in shown:
        lines.append("- %s (`%s`)"
                     % (changelog_headline(subject) or "(no subject)",
                        short_commit(sha)))
    remaining = len(entries) - len(shown)
    if remaining > 0:
        lines.append("- +%d more commit%s."
                     % (remaining, "" if remaining == 1 else "s"))
    return "\n".join(lines).rstrip() + "\n"


def git_log_subjects(repo, since="", head="HEAD", limit=0,
                     runner=subprocess.run):
    """the IMPURE half: ask git for `<short sha>\\t<subject>` lines. Returns
    (text, ok) and never raises - a changelog is worth degrading for and never
    worth failing a build over."""
    argv = ["git", "-C", repo, "log", "--no-merges",
            "--pretty=format:%h%x09%s"]
    if limit:
        argv.append("-n%d" % int(limit))
    argv.append("%s..%s" % (since, head) if since else head)
    try:
        result = runner(argv, capture_output=True, text=True)
    except OSError:
        return "", False
    if result.returncode != 0:
        return "", False
    return result.stdout or "", True


def collect_changelog(commit, since="", repo=REPO_ROOT, runner=subprocess.run):
    """the changelog section for this build, degrading honestly: no previous
    marker (the first night, or a release deleted by hand) and an unreachable
    one (a rewritten history) both fall back to a bounded recent window and SAY
    so in the output; no history at all says that too."""
    head = commit or "HEAD"
    note = ""
    if since:
        text, ok = git_log_subjects(repo, since, head, runner=runner)
        if ok:
            return changelog_markdown(parse_log_subjects(text),
                                      previous_commit=since)
        note = ("The previous nightly's commit `%s` is not in this history, so "
                "this lists the most recent %d commits."
                % (short_commit(since), CHANGELOG_WINDOW))
        warn("cannot reach the previous nightly's commit %s - falling back to "
             "the last %d commits" % (short_commit(since), CHANGELOG_WINDOW))
    else:
        note = ("No previous nightly to compare against, so this lists the "
                "most recent %d commits." % CHANGELOG_WINDOW)
    text, ok = git_log_subjects(repo, "", head, limit=CHANGELOG_WINDOW,
                                runner=runner)
    if not ok:
        warn("no commit history available - the changelog says so")
        return changelog_markdown(
            [], note="No commit history was available when this build was "
                     "packaged, so no changelog could be generated.")
    return changelog_markdown(parse_log_subjects(text), note=note)


def changelog_document(version, commit, date, section):
    """the artifact's CHANGELOG.md: the build it belongs to, then the section.
    The same section text goes into the release notes, so the two never
    disagree."""
    return ("# Changelog\n"
            "\n"
            "Orkige editor %s, built %s from `%s`.\n"
            "\n"
            "%s" % (version or project_version() or "(unversioned)",
                    date or "an unrecorded date",
                    short_commit(commit) or "an unrecorded commit", section))


# --- checksums -------------------------------------------------------------
#
# Every archive ships a `<archive>.sha256` sidecar in the standard one-line
# `sha256sum -c` format - the whole integrity story of a download: a person
# checks a file with the tool their machine already has, and an updater fetches
# the sidecar asset beside the archive and verifies the digest BEFORE trusting
# a byte of it (Docs/nightly-builds.md). The publish side checks its own
# sidecars against the assets that arrived, so bytes that changed on the way
# are refused rather than published under a digest that does not match them.

CHECKSUM_SUFFIX = ".sha256"


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def checksum_text(path):
    """the standard one-line `sha256sum -c` format, so a user verifies a
    download with the tool their machine already has"""
    return "%s  %s\n" % (sha256_file(path), os.path.basename(path))


def write_checksum(path):
    """write `<archive>.sha256` beside an archive; returns its path"""
    target = path + CHECKSUM_SUFFIX
    with open(target, "w") as handle:
        handle.write(checksum_text(path))
    return target


def checksum_mismatch(path):
    """compare a file against the digest its `.sha256` sidecar records. Returns
    "" when they agree (or when there is no sidecar to disagree with) and a
    complaint naming both digests when they do not."""
    sidecar = path + CHECKSUM_SUFFIX
    if not os.path.isfile(sidecar):
        return ""
    with open(sidecar, "r", errors="replace") as handle:
        recorded = handle.read().split()
    if not recorded:
        return "%s is empty" % os.path.basename(sidecar)
    digest = sha256_file(path)
    if recorded[0] == digest:
        return ""
    return ("%s does not match its checksum file (%s recorded, %s measured)"
            % (os.path.basename(path), recorded[0][:16], digest[:16]))


def verify_checksums(assets_dir):
    """check every archive in a directory of release assets against the
    checksum file that travelled beside it. A disagreement means the bytes
    changed on the way here, and publishing them is worse than failing."""
    assets_dir = os.path.abspath(assets_dir)
    if not os.path.isdir(assets_dir):
        fail("no assets directory at '%s'" % assets_dir)
    checked = []
    for name in sorted(os.listdir(assets_dir)):
        path = os.path.join(assets_dir, name)
        if not os.path.isfile(path) or name.endswith(CHECKSUM_SUFFIX):
            continue
        if not os.path.isfile(path + CHECKSUM_SUFFIX):
            continue
        complaint = checksum_mismatch(path)
        if complaint:
            fail(complaint)
        checked.append(name)
    if not checked:
        fail("no archive in '%s' carries a %s file - nothing was verified"
             % (assets_dir, CHECKSUM_SUFFIX))
    log("checksums verified: %s" % ", ".join(checked))
    log("OK " + assets_dir)
    return checked


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


def texcook_binary(build_dir, platform):
    """the texture cook tool the editor spawns; it ships beside the player"""
    name = "texcook.exe" if platform == "windows" else "texcook"
    return os.path.join(build_dir, "tools", "texcook", name)


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
    """macOS: the payload rides INSIDE the bundle - the executables in
    Contents/MacOS, the media in Contents/Resources/Media, which is what the
    editor's resource locator reads relative to SDL_GetBasePath. The build tree
    already stages an app in that shape; re-staging from the same sources keeps
    the packaging independent of whether the tree was fully built."""
    app = os.path.join(stage_root, MACOS_APP_NAME)
    shutil.copytree(editor, app, symlinks=True)
    macos_dir = os.path.join(app, "Contents", "MacOS")
    resources = os.path.join(app, "Contents", "Resources")
    strip_developer_settings(resources)
    shutil.copy2(player, os.path.join(macos_dir, os.path.basename(player)))
    tools = [os.path.basename(player)]
    cook = texcook_binary(build_dir, "macos")
    if os.path.isfile(cook):
        shutil.copy2(cook, os.path.join(macos_dir, os.path.basename(cook)))
        tools.append(os.path.basename(cook))
    media_root = os.path.join(resources, "Media")
    staged = stage_engine_media(build_dir, media_root)
    # cut every build-tree dylib rpath and carry the non-system closure inside
    # the bundle: a shipped binary must not reach into the machine that built it
    search_dirs = []
    triplet = orkige_export.vcpkg_triplet_dir(build_dir)
    if triplet:
        search_dirs.append(os.path.join(triplet, "lib"))
    frameworks = os.path.join(app, "Contents", "Frameworks")
    for name in ["Orkige"] + tools:
        orkige_export.macos_make_self_contained(os.path.join(macos_dir, name),
                                               frameworks, search_dirs)
    return staged, os.path.join(macos_dir, "Orkige")


# --- macOS signing, notarization and stapling ------------------------------
#
# The bundle is sealed the same way whatever it is signed WITH: inside-out (a
# bundle seal records the signatures beneath it), from ONE function, with the
# identity as the only difference. The two ends of that range:
#
# - the AD-HOC identity ("-") needs no certificate, so it runs on any machine
#   and on a fork, a pull request or a hand run. It makes the bundle internally
#   consistent and NOTHING more: the app names no developer, and macOS refuses
#   a downloaded one (KNOWN-LIMITATIONS says so, with the steps to open it).
# - a DEVELOPER ID identity additionally gets the hardened runtime
#   (--options runtime) and a secure timestamp (--timestamp), because
#   notarization accepts neither without them, and the artifacts are then
#   submitted to Apple and stapled.
#
# Nothing in between ships. A configured certificate that cannot sign, or a
# submission Apple does not accept, FAILS the build - a half-signed artifact is
# worse than an honestly ad-hoc one, and an artifact whose VERSION file claims
# a notarization it never got is worse than both.
#
# NO ENTITLEMENTS. The hardened runtime's default restrictions are all things
# this editor does not do: the scripting runtime is an interpreter and not a
# JIT (so no executable-memory exception), every dylib inside the bundle is
# signed by the same identity in the seal below (so library validation holds),
# and the tools it spawns - the player, the texture cook, cmake, git - are
# separate processes under their own policy rather than code loaded into this
# one. An entitlement that is not needed is signed-in permission nobody asked
# for, so the list stays empty until something genuinely refuses to run without
# one, at which point it belongs in a reviewed .entitlements file and in
# Docs/nightly-builds.md beside the reason.
#
# CREDENTIALS come from the ENVIRONMENT, never from a command line: an
# app-specific password on an argv is readable by every process on the machine.
# The one exception is the signing identity, which is a certificate's public
# name or its SHA-1 and is not a secret. Whatever does end up on a subprocess
# argv (notarytool takes its credentials that way and offers no alternative) is
# redacted out of the echoed command line by run_credentialed below.

MACOS_SIGNING_IDENTITY_ENV = "ORKIGE_MACOS_SIGNING_IDENTITY"
MACOS_KEYCHAIN_ENV = "ORKIGE_MACOS_KEYCHAIN"
# notarization, App Store Connect API key (the preferred route: a key file plus
# two identifiers, revocable on its own without touching an Apple ID)
NOTARY_KEY_ENV = "ORKIGE_NOTARY_KEY"
NOTARY_KEY_ID_ENV = "ORKIGE_NOTARY_KEY_ID"
NOTARY_ISSUER_ENV = "ORKIGE_NOTARY_ISSUER_ID"
# notarization, Apple ID + app-specific password (the alternative route)
NOTARY_APPLE_ID_ENV = "ORKIGE_NOTARY_APPLE_ID"
NOTARY_APP_PASSWORD_ENV = "ORKIGE_NOTARY_APP_PASSWORD"
NOTARY_TEAM_ID_ENV = "ORKIGE_NOTARY_TEAM_ID"

# how long one submission may take before notarytool gives up. Apple's service
# usually answers in minutes and occasionally takes far longer; a nightly can
# afford to wait, and a wait that ends in an artifact beats a timeout that ends
# in none.
NOTARY_TIMEOUT = "2h"


class NotaryCredentials:
    """how a notarization submission authenticates. Two methods, one shape:
    `api-key` (App Store Connect key file + key id + issuer id) and `apple-id`
    (Apple ID + app-specific password + team id). Pure data - the argv it
    composes is what notarytool takes, and `secrets()` is what must never reach
    a log."""

    def __init__(self, method="", key_path="", key_id="", issuer="",
                 apple_id="", app_password="", team_id=""):
        self.method = method
        self.key_path = key_path
        self.key_id = key_id
        self.issuer = issuer
        self.apple_id = apple_id
        self.app_password = app_password
        self.team_id = team_id

    def argv(self):
        if self.method == "api-key":
            return ["--key", self.key_path,
                    "--key-id", self.key_id,
                    "--issuer", self.issuer]
        if self.method == "apple-id":
            return ["--apple-id", self.apple_id,
                    "--password", self.app_password,
                    "--team-id", self.team_id]
        return []

    def secrets(self):
        """every credential VALUE, for redaction. The key file's PATH is not
        one (it names a file, it is not the key), but the identifiers and the
        password are."""
        return tuple(value for value in
                     (self.key_id, self.issuer, self.apple_id,
                      self.app_password, self.team_id) if value)


def resolve_notary_credentials(environ):
    """pick the notarization method from the environment and say what was
    missing. Returns (credentials, complaint): the API key wins when both are
    complete (it is revocable on its own), an incomplete set is NOT used and
    the complaint names exactly which values were absent, and nothing
    configured at all is no complaint - a build without notarization
    credentials is a legitimate, honestly-recorded state. Pure."""
    key_path = environ.get(NOTARY_KEY_ENV, "").strip()
    key_id = environ.get(NOTARY_KEY_ID_ENV, "").strip()
    issuer = environ.get(NOTARY_ISSUER_ENV, "").strip()
    apple_id = environ.get(NOTARY_APPLE_ID_ENV, "").strip()
    password = environ.get(NOTARY_APP_PASSWORD_ENV, "").strip()
    team_id = environ.get(NOTARY_TEAM_ID_ENV, "").strip()
    if key_path and key_id and issuer:
        return NotaryCredentials("api-key", key_path=key_path, key_id=key_id,
                                 issuer=issuer), ""
    if apple_id and password and team_id:
        return NotaryCredentials("apple-id", apple_id=apple_id,
                                 app_password=password, team_id=team_id), ""
    api_missing = [name for name, value in ((NOTARY_KEY_ENV, key_path),
                                            (NOTARY_KEY_ID_ENV, key_id),
                                            (NOTARY_ISSUER_ENV, issuer))
                   if not value]
    id_missing = [name for name, value in ((NOTARY_APPLE_ID_ENV, apple_id),
                                           (NOTARY_APP_PASSWORD_ENV, password),
                                           (NOTARY_TEAM_ID_ENV, team_id))
                  if not value]
    if len(api_missing) < 3:
        return NotaryCredentials(), ("an App Store Connect key is half "
                                     "configured - %s %s not set"
                                     % (", ".join(api_missing),
                                        "is" if len(api_missing) == 1 else "are"))
    if len(id_missing) < 3:
        return NotaryCredentials(), ("an Apple ID notarization login is half "
                                     "configured - %s %s not set"
                                     % (", ".join(id_missing),
                                        "is" if len(id_missing) == 1 else "are"))
    return NotaryCredentials(), ""


class MacosSigning:
    """what this run can sign and vouch for. `identity` empty means the ad-hoc
    seal; `notary` present means the artifacts are submitted and stapled.
    `notes` is what a degraded run has to SAY, in the log and, through the
    signature state, in the artifact's own KNOWN-LIMITATIONS.md."""

    def __init__(self, identity="", keychain="", notary=None, notes=()):
        self.identity = identity
        self.keychain = keychain
        self.notary = notary or NotaryCredentials()
        self.notes = tuple(notes)

    @property
    def real(self):
        """does this sign with a certificate (as opposed to ad-hoc)?"""
        return bool(self.identity)

    @property
    def notarizes(self):
        return self.real and bool(self.notary.method)

    @property
    def state(self):
        """the ONE signature vocabulary: what the VERSION file records, which
        limitations record applies, and what the release notes say"""
        if not self.real:
            return SIGN_ADHOC
        return SIGN_NOTARIZED if self.notarizes else SIGN_DEVELOPER_ID


def resolve_macos_signing(environ, identity_arg="", ad_hoc=False):
    """what this machine can do for a macOS build, and what it has to say about
    what it cannot. Pure: no keychain is opened and no file is read, so every
    branch is testable on a machine with no certificate at all.

    Degradation is never silent and never partial. Credentials with no
    certificate cannot notarize anything, so that combination falls all the way
    back to ad-hoc and says why; a certificate with half a credential set signs
    for real and records that it is not notarized."""
    identity = (identity_arg or environ.get(MACOS_SIGNING_IDENTITY_ENV, "")
                ).strip()
    keychain = environ.get(MACOS_KEYCHAIN_ENV, "").strip()
    notary, complaint = resolve_notary_credentials(environ)
    if ad_hoc:
        return MacosSigning(notes=("ad-hoc signing was asked for - this build "
                                   "names no developer",))
    if not identity:
        note = ("no Developer ID certificate (%s is not set) - the app is "
                "ad-hoc signed, which macOS refuses on a download"
                % MACOS_SIGNING_IDENTITY_ENV)
        if notary.method or complaint:
            note = ("notarization credentials are configured but no signing "
                    "certificate is (%s is not set) - nothing can be signed, "
                    "so nothing can be notarized; falling back to ad-hoc"
                    % MACOS_SIGNING_IDENTITY_ENV)
        return MacosSigning(notes=(note,))
    if notary.method:
        return MacosSigning(identity, keychain, notary)
    note = complaint or ("no notarization credentials (%s or %s) - the app is "
                         "Developer ID signed but not notarized"
                         % (NOTARY_KEY_ENV, NOTARY_APPLE_ID_ENV))
    return MacosSigning(identity, keychain, notes=(note,))


def codesign_argv(target, identity="", keychain="", entitlements="",
                  hardened=True):
    """the codesign invocation for ONE binary, bundle or disk image. Pure.

    The ad-hoc form is exactly the four-word command it has always been, so a
    run with no certificate produces the same signature it did before this seam
    existed. The real form adds the two flags notarization requires - the
    hardened runtime and a secure timestamp - and neither is optional: a
    submission missing either is rejected by Apple, not by us. `hardened` is
    off for a disk image, which is a container rather than code."""
    if not identity or identity == "-":
        return ["codesign", "--force", "--sign", "-", target]
    argv = ["codesign", "--force", "--sign", identity, "--timestamp"]
    if hardened:
        argv += ["--options", "runtime"]
    if entitlements:
        argv += ["--entitlements", entitlements]
    if keychain:
        argv += ["--keychain", keychain]
    argv.append(target)
    return argv


def codesign_verify_argv(target, strict=False):
    """read back what was just written. A real signature is verified STRICTLY -
    the check Gatekeeper applies - while the ad-hoc seal keeps the plain
    verification it has always had."""
    argv = ["codesign", "--verify"]
    if strict:
        argv += ["--strict", "--verbose=2"]
    argv.append(target)
    return argv


def notarytool_submit_argv(artifact, notary, timeout=NOTARY_TIMEOUT):
    """submit one artifact and WAIT for Apple's verdict, as JSON (the verdict
    is read from the payload rather than inferred from an exit code). Pure."""
    return (["xcrun", "notarytool", "submit", artifact] + notary.argv()
            + ["--wait", "--timeout", timeout, "--output-format", "json"])


def notarytool_log_argv(submission_id, notary):
    """the log of one submission. This is the ONLY thing that names the binary
    Apple objected to, so a rejection is worthless without it. Pure."""
    return ["xcrun", "notarytool", "log", submission_id] + notary.argv()


def stapler_argv(target):
    """attach the notarization ticket to the artifact, so the machine that
    opens it needs no network to learn that Apple vouched for it. Pure."""
    return ["xcrun", "stapler", "staple", target]


def stapler_validate_argv(target):
    return ["xcrun", "stapler", "validate", target]


def spctl_argv(target, kind="exec"):
    """the assessment Gatekeeper itself performs. `exec` is the app's verdict;
    a disk image is assessed as `open` against its primary signature, which is
    the check Apple documents for that container (`install` is the assessment
    for an installer package, which this pipeline does not produce). Pure."""
    argv = ["spctl", "--assess", "--type", kind, "--verbose=2"]
    if kind == "open":
        argv += ["--context", "context:primary-signature"]
    argv.append(target)
    return argv


def redact_argv(argv, secrets=()):
    """one command line as it may appear in a log: every credential VALUE
    replaced. Pure - and the only reason a credentialed command is echoed at
    all, because a step whose command nobody can see is a step nobody can
    debug."""
    hidden = set(value for value in secrets if value)
    return " ".join("<redacted>" if arg in hidden else arg for arg in argv)


def run_credentialed(argv, secrets=(), runner=subprocess.run):
    """run a command whose argv carries credentials. The echoed line is
    redacted; the output is captured so a failure can be reported without the
    command line that produced it. Never raises - the caller decides."""
    log("$ " + redact_argv(argv, secrets))
    try:
        return runner(argv, capture_output=True, text=True)
    except OSError as error:
        fail("could not run %s: %s" % (argv[0], error))


def notary_submission_verdict(stdout):
    """(submission id, status, accepted) out of `notarytool submit --wait
    --output-format json`. Pure, and deliberately strict: output that is not
    the expected payload reads as NOT accepted, because "we could not tell" and
    "Apple said yes" must never be the same answer."""
    try:
        payload = json.loads(stdout or "")
    except ValueError:
        return "", "", False
    if not isinstance(payload, dict):
        return "", "", False
    identifier = str(payload.get("id") or "")
    status = str(payload.get("status") or "")
    return identifier, status, status == "Accepted"


def notarize(artifact, notary, what="", runner=subprocess.run):
    """submit one artifact, wait for the verdict, and on a rejection fetch and
    print the notarization LOG - which names the offending binary and is the
    only way to diagnose one. Anything but "Accepted" fails the build."""
    what = what or os.path.basename(artifact)
    log("submitting %s for notarization (waiting up to %s)"
        % (what, NOTARY_TIMEOUT))
    result = run_credentialed(notarytool_submit_argv(artifact, notary),
                              notary.secrets(), runner)
    print(result.stdout or "", end="", flush=True)
    identifier, status, accepted = notary_submission_verdict(result.stdout)
    if accepted:
        log("Apple accepted %s (submission %s)" % (what, identifier))
        return identifier
    if (result.stderr or "").strip():
        print(result.stderr, end="", flush=True)
    if identifier:
        # the verdict alone says nothing actionable; the log names the binary
        log("fetching the notarization log for submission " + identifier)
        detail = run_credentialed(notarytool_log_argv(identifier, notary),
                                  notary.secrets(), runner)
        print(detail.stdout or "", end="", flush=True)
        print(detail.stderr or "", end="", flush=True)
    fail("notarization of %s came back '%s' - nothing is published from a "
         "submission Apple did not accept" % (what, status or "no verdict"))


def staple(artifact, what=""):
    """attach the ticket and prove it stuck"""
    what = what or os.path.basename(artifact)
    orkige_export.run(stapler_argv(artifact))
    orkige_export.run(stapler_validate_argv(artifact))
    log("stapled the notarization ticket into " + what)


def seal_macos_bundle(app, signing=None):
    """re-sign the finished bundle, inside-out. The linker's own ad-hoc
    signature covers the executable only; adding the player, the media and the
    text files leaves the bundle with no sealed resource directory, and a
    DOWNLOADED app is held to stricter rules than a locally built one.

    With no identity this signs ad-hoc ("-"), which needs no certificate and so
    runs on any machine - it makes the bundle internally consistent, NOT trusted
    (see KNOWN-LIMITATIONS). With a Developer ID identity the same inside-out
    walk signs for real, with the hardened runtime and a secure timestamp that
    notarization requires."""
    signing = signing or MacosSigning()
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
        orkige_export.run(codesign_argv(binary, signing.identity,
                                        signing.keychain))
    orkige_export.run(codesign_argv(app, signing.identity, signing.keychain))
    orkige_export.run(codesign_verify_argv(app, strict=signing.real))
    log("sealed %s (%s)" % (os.path.basename(app), signing.state))


def notarize_macos_app(app, signing):
    """notarize the APP itself, before any container is built from it.

    A ticket is issued for what was SUBMITTED, so the app has to be submitted on
    its own to end up with one of its own - and it has to, because the portable
    .zip is the updater's payload and a download that needs a network round trip
    to open is second-class. The submission container is a throwaway zip (the
    shape Apple's service takes an app in); the ticket is stapled into the app
    in the staging, so both artifacts built from that staging afterwards carry
    it."""
    submission = tempfile.mkdtemp(prefix="orkige-notarize-")
    try:
        payload = os.path.join(submission, "Orkige.zip")
        # ditto, not zipfile: the bundle's symlinks and executable bits have to
        # survive the trip or Apple assesses something that is not our app
        orkige_export.run(["ditto", "-c", "-k", "--sequesterRsrc",
                           "--keepParent", app, payload])
        notarize(payload, signing.notary, what=os.path.basename(app))
    finally:
        shutil.rmtree(submission, ignore_errors=True)
    staple(app, os.path.basename(app))
    orkige_export.run(spctl_argv(app, "exec"))


def stage_flat(build_dir, platform, stage_root, editor, player):
    """Linux and Windows: the executables beside each other and the resources
    under share/orkige/, which is the layout the editor's own resource locator
    reads relative to SDL_GetBasePath (its executable's directory) - the SAME
    layout the build stages into a tree, so a downloaded editor resolves its
    media exactly as a locally built one does."""
    shutil.copy2(editor, os.path.join(stage_root, os.path.basename(editor)))
    shutil.copy2(player, os.path.join(stage_root, os.path.basename(player)))
    # the texture cook tool the editor spawns, staged beside it like the player
    cook = texcook_binary(build_dir, platform)
    if os.path.isfile(cook):
        shutil.copy2(cook, os.path.join(stage_root, os.path.basename(cook)))
    resources = os.path.join(stage_root, FLAT_RESOURCE_DIR)
    os.makedirs(resources, exist_ok=True)
    # the editor's own fonts: it looks for these in its resource root before
    # falling back to the build tree, so a copied build keeps its icons and its
    # terminal/script mono glyphs (the macOS bundle already carries them)
    editor_media = os.path.join(REPO_ROOT, "tools", "editor", "media")
    for name in EDITOR_UI_FONTS:
        source = os.path.join(editor_media, name)
        if os.path.isfile(source):
            shutil.copy2(source, os.path.join(resources, name))
    # whatever shared libraries the build placed beside the executable (on
    # Windows the vcpkg DLLs the loader needs, e.g. the Vulkan loader)
    build_output = os.path.dirname(editor)
    library_suffix = ".dll" if platform == "windows" else ".so"
    for name in sorted(os.listdir(build_output)):
        source = os.path.join(build_output, name)
        if name.lower().endswith(library_suffix) and os.path.isfile(source):
            shutil.copy2(source, os.path.join(stage_root, name))
            log("bundled library %s" % name)
    staged = stage_engine_media(build_dir, os.path.join(resources, "Media"))
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


# --- the installable artifact each desktop platform expects -----------------
#
# Every platform gets TWO assets, because they answer two different questions.
#
# The INSTALLABLE one is what a person on that platform downloads: a .dmg on
# macOS, an installer on Windows. Both exist for reasons beyond familiarity:
#
# - macOS applies path randomization (app translocation) to a downloaded app
#   that is launched out of the folder it was unpacked into, so an app run from
#   an unzipped directory sees a read-only randomized path instead of its own.
#   Dragging it to /Applications first is what clears that, and a disk image
#   with an /Applications symlink beside the app is the layout that asks for
#   the drag. A notarization ticket also staples onto a .dmg directly, so the
#   container that fixes translocation today is the one that carries the
#   signature when there is one.
# - Windows has no drag-install: the expected artifact is an installer that
#   places the program, puts it in the Start menu, records it where Settings
#   lists installed programs, and can remove itself again.
#
# The PORTABLE one stays the .zip / .tar.gz: no image to mount, no installer to
# run, unpack anywhere - and it is the shape an updater can consume, because
# swapping files in place needs neither a mount nor an elevation-free installer
# run. Linux ships the .tar.gz alone: a distribution's own package formats are
# not interchangeable, and a tarball is what every one of them can unpack.

DMG_SUFFIX = ".dmg"
INSTALLER_SUFFIX = "-setup.exe"

# the NSIS script the Windows installer is compiled from; it carries no
# build-specific value, so the packager hands it everything with /D defines
NSIS_SCRIPT = os.path.join(REPO_ROOT, "Util", "orkige_installer.nsi")

# the drag target inside the disk image
APPLICATIONS_LINK = "Applications"


def dmg_name(platform, commit, version=""):
    return artifact_stem(platform, commit, version) + DMG_SUFFIX


def installer_name(platform, commit, version=""):
    return artifact_stem(platform, commit, version) + INSTALLER_SUFFIX


def dmg_volume_name(version=""):
    """the name the mounted image shows in the Finder sidebar. The BASE version
    only: a volume name is capped at 27 characters on the image's filesystem,
    and the whole ordered version does not fit - while "Orkige 2.0.0" reads as
    a product, keeps two nightlies of different base versions from colliding as
    "Orkige 1", and stays comfortably inside the cap."""
    base = ""
    match = re.match(r"^(\d+\.\d+\.\d+)", version or "")
    if match:
        base = match.group(1)
    base = base or project_version()
    name = ("Orkige %s" % base) if base else "Orkige"
    return name[:27]


def make_dmg(stage_root, dmg_path, volume_name):
    """the macOS disk image, built with hdiutil from the SAME staged directory
    the .zip is made from - so the two containers cannot hold different builds.

    The drag-to-Applications layout is one symlink: the image root holds the
    app, an `Applications` link to /Applications, and the same VERSION /
    CHANGELOG.md / KNOWN-LIMITATIONS.md the archive root carries (the
    limitations file is what tells a user how to open an unsigned app, so it
    has to be readable before installing). The link is added to the staged
    directory for the duration of the call rather than to a second copy of it:
    the app is well over a hundred megabytes, and a copy would be both slow and
    a chance for the two artifacts to diverge."""
    if not shutil.which("hdiutil"):
        fail("no hdiutil - a macOS disk image can only be built on macOS")
    link = os.path.join(stage_root, APPLICATIONS_LINK)
    if os.path.lexists(link):
        os.remove(link)
    os.symlink("/Applications", link)
    try:
        if os.path.exists(dmg_path):
            os.remove(dmg_path)
        orkige_export.run(["hdiutil", "create",
                           "-volname", volume_name,
                           "-srcfolder", stage_root,
                           "-fs", "HFS+",
                           "-format", "UDZO",   # compressed, read-only
                           "-ov", dmg_path])
    finally:
        if os.path.lexists(link):
            os.remove(link)
    return dmg_path


def make_macos_image(stage_root, dmg_path, volume_name, signing=None):
    """the disk image, and whatever trust this run can put behind it: the image
    is signed with the same identity the app carries (a container, so no
    hardened runtime - that flag describes code) and then notarized and stapled
    on its own, because a ticket belongs to the artifact that was submitted.

    The app inside is already stapled by the time the image is built, so the
    image carries a ticket for itself AND an app that carries one for itself -
    which is what makes the .zip built from the same staging equal to the
    .dmg rather than a lesser download."""
    signing = signing or MacosSigning()
    make_dmg(stage_root, dmg_path, volume_name)
    if not signing.real:
        return dmg_path
    orkige_export.run(codesign_argv(dmg_path, signing.identity,
                                    signing.keychain, hardened=False))
    if signing.notarizes:
        notarize(dmg_path, signing.notary)
        staple(dmg_path)
        orkige_export.run(spctl_argv(dmg_path, "open"))
    return dmg_path


def windows_file_version(version=""):
    """the ordered version as the numeric `a.b.c.d` the Windows VERSIONINFO
    resource accepts - it takes four numbers and nothing else, so the channel,
    the date and the commit cannot appear there. The full ordered version is
    recorded where it is readable: the installer's ProductVersion string and
    the DisplayVersion the installed-programs list shows."""
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)", version or "")
    if not match:
        match = re.match(r"^(\d+)\.(\d+)\.(\d+)", project_version() or "")
    if not match:
        return "0.0.0.0"
    return "%s.%s.%s.0" % match.groups()


def installer_command(stage_dir, out_file, version, size_kb, tool="makensis",
                      script=NSIS_SCRIPT):
    """the makensis invocation, as a pure argv - every build-specific value is
    a /D define, so the .nsi script stays a fixed, reviewable document"""
    return [tool,
            "/DSTAGE_DIR=" + stage_dir,
            "/DOUT_FILE=" + out_file,
            "/DORKIGE_VERSION=" + (version or "unversioned"),
            "/DFILE_VERSION=" + windows_file_version(version),
            "/DINSTALL_SIZE_KB=%d" % int(size_kb),
            script]


def make_windows_installer(stage_root, installer_path, version):
    """compile the Windows installer from the staged directory. Returns the
    installer path, or "" when makensis is not on this machine - a hand run
    without it still produces the .zip, and the pipeline fails loudly on its
    own toolchain check rather than shipping a night without an installer."""
    tool = shutil.which("makensis")
    if not tool:
        warn("no makensis on PATH - no Windows installer was built (the .zip "
             "is unaffected)")
        return ""
    if not os.path.isfile(NSIS_SCRIPT):
        fail("no installer script at '%s'" % NSIS_SCRIPT)
    if os.path.exists(installer_path):
        os.remove(installer_path)
    size_kb = max(1, orkige_export.directory_size(stage_root) // 1024)
    orkige_export.run(installer_command(os.path.abspath(stage_root),
                                        os.path.abspath(installer_path),
                                        version, size_kb, tool))
    if not os.path.isfile(installer_path):
        fail("makensis reported success but wrote no '%s'" % installer_path)
    return installer_path


def package(platform, build_dir, commit, date, output_dir, version="",
            since="", repo=REPO_ROOT, signing=None):
    """stage, describe and archive one platform's editor build. `version` is
    the ordered identity (composed here when the caller passes none, so a hand
    run needs no extra argument); `since` is the previous nightly's commit -
    the changelog's lower bound; `signing` is what this run can put behind a
    macOS build (ad-hoc when there is no certificate, and it says so)."""
    signing = signing or MacosSigning()
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
    version = version or nightly_version(date, commit)
    if not version:
        warn("no ordered version for this build (needs a date and the engine's "
             "declared version) - naming the artifact after the commit")
    stem = artifact_stem(platform, commit, version)
    stage_parent = os.path.join(output_dir, "stage")
    if os.path.isdir(stage_parent):
        shutil.rmtree(stage_parent)
    stage_root = os.path.join(stage_parent, stem)
    os.makedirs(stage_root)

    extra_fields = []
    if platform == "macos":
        # what this build's signature is worth, recorded where a person and a
        # script both read it. It is written before the signing runs, which is
        # honest because the alternative to the recorded state is not a lesser
        # one: a certificate that cannot sign or a submission Apple rejects
        # fails the build, so a VERSION file claiming a notarization is only
        # ever attached to an artifact that got one.
        extra_fields.append(("signing", signing.state))
        for note in signing.notes:
            warn(note)
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

    version_file = version_text(platform, commit, date, build_dir, extra_fields,
                                version)
    limitations = limitations_markdown(
        platform, ["%s %s" % (key, value) for key, value in
                   (line.split(": ", 1) for line in version_file.splitlines()
                    if line.startswith(("version:", "commit:", "built:")))],
        signing.state)
    changelog = changelog_document(version, commit, date,
                                   collect_changelog(commit, since, repo))
    targets = [stage_root]
    if platform == "macos":
        targets.append(os.path.join(stage_root, MACOS_APP_NAME, "Contents",
                                    "Resources"))
    for target in targets:
        with open(os.path.join(target, "VERSION"), "w") as handle:
            handle.write(version_file)
        with open(os.path.join(target, "KNOWN-LIMITATIONS.md"), "w") as handle:
            handle.write(limitations)
        with open(os.path.join(target, CHANGELOG_FILE), "w") as handle:
            handle.write(changelog)
    # the installable artifact and the portable one come from ONE staging, so
    # they can never hold different builds. On macOS the ORDER matters: the app
    # is sealed, notarized and stapled, THEN the disk image is built (and
    # notarized on its own), and only then is the .zip made - so the portable
    # archive carries the stapled app rather than a copy that predates the
    # ticket. Both get the same .sha256 treatment: every asset a person can
    # download carries its own integrity story, and the publish side re-checks
    # all of them the same way.
    installable = ""
    if platform == "macos":
        # the seal comes after every byte it covers is in place - the payload,
        # the media and the text files are all staged by now
        app = os.path.join(stage_root, MACOS_APP_NAME)
        seal_macos_bundle(app, signing)
        if signing.notarizes:
            notarize_macos_app(app, signing)
        installable = make_macos_image(
            stage_root,
            os.path.join(output_dir, dmg_name(platform, commit, version)),
            dmg_volume_name(version), signing)

    archive_path = os.path.join(output_dir,
                                artifact_name(platform, commit, version))
    if os.path.exists(archive_path):
        os.remove(archive_path)
    if ARCHIVE_SUFFIX[platform] == ".zip":
        make_zip(stage_root, archive_path)
    else:
        make_tar_gz(stage_root, archive_path)
    # the checksum beside the archive: what a person verifies a download with,
    # what an updater checks before trusting the bytes, and what the publish
    # side re-checks once the assets are gathered (a mismatch there means the
    # bytes changed in transit)
    write_checksum(archive_path)
    log("staged %s (%s), archive %s"
        % (stem, orkige_export.human_size(
            orkige_export.directory_size(stage_root)),
           orkige_export.human_size(os.path.getsize(archive_path))))

    if platform == "windows":
        installable = make_windows_installer(
            stage_root,
            os.path.join(output_dir, installer_name(platform, commit, version)),
            version)
    if installable:
        write_checksum(installable)
        log("installable %s (%s)"
            % (os.path.basename(installable),
               orkige_export.human_size(os.path.getsize(installable))))

    log("editor: %s" % os.path.relpath(staged_editor, stage_root))
    log("version: %s" % (version or "(unversioned)"))
    if platform == "macos":
        log("signing: %s" % signing.state)
    log("OK " + archive_path)
    return archive_path


# --- verification (the pipeline's smoke test) ------------------------------

def verify_layout(root, platform):
    """the structural half of the smoke test: every file a downloaded build is
    supposed to contain, checked on the UNPACKED tree. Returns the editor
    executable path; a list of complaints means the packaging is broken.

    The media and the sibling executables are checked AT THE PATHS THE EDITOR
    RESOLVES (Contents/Resources/Media + Contents/MacOS on macOS,
    share/orkige/Media + the executable's own directory elsewhere), and the
    shader library has to carry its flavor's marker subdirectory - so a build
    tree whose payload staging never ran, or an archive that puts the media
    somewhere the editor does not look, fails here instead of shipping an
    editor that opens a window and draws nothing."""
    problems = []
    if platform == "macos":
        app = os.path.join(root, MACOS_APP_NAME)
        editor = os.path.join(app, "Contents", "MacOS", "Orkige")
        resources = os.path.join(app, "Contents", "Resources")
        expected_files = [editor,
                          os.path.join(root, "VERSION"),
                          os.path.join(root, "KNOWN-LIMITATIONS.md"),
                          os.path.join(root, CHANGELOG_FILE),
                          os.path.join(resources, "VERSION")]
        media_root = os.path.join(resources, "Media")
        player_dir = os.path.join(app, "Contents", "MacOS")
        ui_font_dir = resources
    else:
        name = "orkige_editor.exe" if platform == "windows" else "orkige_editor"
        editor = os.path.join(root, name)
        expected_files = [editor,
                          os.path.join(root, "VERSION"),
                          os.path.join(root, "KNOWN-LIMITATIONS.md"),
                          os.path.join(root, CHANGELOG_FILE)]
        # the editor resolves its resources under share/orkige/ beside its own
        # executable, so that is where the check looks
        ui_font_dir = os.path.join(root, FLAT_RESOURCE_DIR)
        media_root = os.path.join(ui_font_dir, "Media")
        player_dir = root
    # forward slashes in the message on every platform: the complaint text is
    # what a job log shows and what the self-checks match against
    media_label = os.path.relpath(media_root, root).replace(os.sep, "/")
    for path in expected_files:
        if not os.path.isfile(path):
            problems.append("missing "
                            + os.path.relpath(path, root).replace(os.sep, "/"))
    if platform != "windows" and os.path.isfile(editor) \
            and not os.access(editor, os.X_OK):
        # an archive format that drops the executable bit turns a download into
        # a permission error nobody can diagnose
        problems.append("the editor is not executable")
    player = "orkige_player.exe" if platform == "windows" else "orkige_player"
    if not os.path.isfile(os.path.join(player_dir, player)):
        problems.append("missing the player (%s)" % player)
    cook = "texcook.exe" if platform == "windows" else "texcook"
    if not os.path.isfile(os.path.join(player_dir, cook)):
        # the export cook spawns it; the build stages it beside the player, so
        # its absence means a scoped build skipped the payload staging
        problems.append("missing the texture cook tool (%s)" % cook)
    if not os.path.isdir(media_root):
        problems.append("missing %s/" % media_label)
    else:
        # the shader library is the media without which nothing renders (the
        # editor's own interface draws through it), so its absence is a
        # packaging failure and not a soft note. Its presence is also the
        # MARKER the editor requires before believing a media root at all.
        shader = ("Hlms" if os.path.isdir(os.path.join(media_root, "Hlms"))
                  else "RTShaderLib")
        if not os.path.isdir(os.path.join(media_root, shader)):
            problems.append("missing the shader media (%s/Hlms or "
                            "%s/RTShaderLib)" % (media_label, media_label))
        for name in ("fonts", "water", "decals"):
            if not os.path.isdir(os.path.join(media_root, name)):
                problems.append("missing %s/%s" % (media_label, name))
    if not os.path.isfile(os.path.join(ui_font_dir, EDITOR_UI_FONTS[0])):
        problems.append("missing the editor's UI font (%s)" % EDITOR_UI_FONTS[0])
    for name in SETTINGS_FILES:
        # every directory a settings file could have ridden into, the macOS
        # bundle's Resources included
        for directory in {root, player_dir, ui_font_dir}:
            if os.path.isfile(os.path.join(directory, name)):
                problems.append("ships a build tree's %s" % name)
    return editor, problems


def verify(root, platform, commit="", runner=subprocess.run, version=""):
    """unpack-and-run verification: the layout above, plus the binary answering
    `--version` with the commit it was stamped with. A nightly that publishes a
    binary which cannot start is worse than no nightly.

    When `version` is given, the binary must report exactly that ordered
    identity. This is the anti-drift check between the two implementations of
    the version grammar - the packaging tooling's (nightly_version) and the
    binary's (core_util/VersionOrder.h) - so a change to one that the other
    does not follow fails the night's build instead of publishing an artifact
    whose file name and self-report disagree."""
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
    if version and version not in reported:
        fail("the binary reports '%s' - expected the ordered version %s (the "
             "packaged identity and the binary's own must be one value)"
             % (reported, version))
    log("OK " + root)
    return reported


def verify_dmg(dmg_path):
    """the disk image's own smoke test: MOUNT it and look, because a .dmg that
    builds is not a .dmg that carries a working install. It has to hold the
    complete app (the same layout check the unpacked archive gets) and the
    /Applications symlink that makes the drag an install rather than a copy
    into the download folder - an app launched from there is subject to path
    randomization, which is the whole reason the image exists.

    The binary is NOT run from the mounted volume: the archive's smoke test
    already proves the executable starts, and a read-only mount adds nothing to
    that verdict."""
    dmg_path = os.path.abspath(dmg_path)
    if not os.path.isfile(dmg_path):
        fail("no disk image at '%s'" % dmg_path)
    if not shutil.which("hdiutil"):
        fail("no hdiutil - a macOS disk image can only be inspected on macOS")
    mountpoint = tempfile.mkdtemp(prefix="orkige-dmg-")
    orkige_export.run(["hdiutil", "attach", dmg_path,
                       "-nobrowse", "-readonly", "-mountpoint", mountpoint])
    try:
        problems = []
        link = os.path.join(mountpoint, APPLICATIONS_LINK)
        if not os.path.islink(link):
            problems.append("no %s symlink - the image does not offer a "
                            "drag-to-Applications install" % APPLICATIONS_LINK)
        elif os.readlink(link) != "/Applications":
            problems.append("the %s symlink points at '%s', not /Applications"
                            % (APPLICATIONS_LINK, os.readlink(link)))
        _, layout_problems = verify_layout(mountpoint, "macos")
        problems.extend(layout_problems)
        for problem in problems:
            print("orkige_nightly_package: DMG: " + problem, flush=True)
        if problems:
            fail("the disk image is incomplete (%d problems)" % len(problems))
        log("the disk image carries %s and the %s link"
            % (MACOS_APP_NAME, APPLICATIONS_LINK))
    finally:
        # detaching can lose a race with whatever indexed the fresh mount, so
        # the retry is -force rather than a failure that leaves it mounted
        if subprocess.run(["hdiutil", "detach", mountpoint],
                          capture_output=True).returncode != 0:
            subprocess.run(["hdiutil", "detach", "-force", mountpoint],
                           capture_output=True)
        shutil.rmtree(mountpoint, ignore_errors=True)
    log("OK " + dmg_path)
    return True


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
    parser.add_argument("--version", default="",
                        help="the ordered version of this build (default: "
                             "composed from --commit and --date); when "
                             "verifying, the identity the binary must report")
    parser.add_argument("--since", default="",
                        help="the previous nightly's commit - the changelog's "
                             "lower bound (exclusive)")
    parser.add_argument("--repo", default=REPO_ROOT,
                        help="the repository to read the changelog from "
                             "(default this checkout)")
    parser.add_argument("--signing-identity", default="",
                        help="macOS: the Developer ID Application identity "
                             "(name or SHA-1) to sign with, else the env "
                             + MACOS_SIGNING_IDENTITY_ENV + ". Without one the "
                             "bundle is ad-hoc signed and says so. The "
                             "notarization credentials are ENVIRONMENT-only "
                             "(" + NOTARY_KEY_ENV + "/" + NOTARY_KEY_ID_ENV
                             + "/" + NOTARY_ISSUER_ENV + ", or "
                             + NOTARY_APPLE_ID_ENV + "/"
                             + NOTARY_APP_PASSWORD_ENV + "/"
                             + NOTARY_TEAM_ID_ENV + ") - a password on a "
                             "command line is readable by every process on the "
                             "machine")
    parser.add_argument("--ad-hoc-sign", action="store_true",
                        help="macOS: ad-hoc sign even where a certificate is "
                             "configured (a local packaging run that must not "
                             "reach Apple)")
    parser.add_argument("--verify", default="",
                        help="verify an UNPACKED artifact directory instead of "
                             "packaging one")
    parser.add_argument("--verify-dmg", default="",
                        help="mount a macOS disk image and verify what it "
                             "carries")
    parser.add_argument("--identity", action="store_true",
                        help="print this build's ordered version as "
                             "`key=value` lines (the pipeline feeds them to "
                             "every job, so the value is composed ONCE)")
    parser.add_argument("--changelog", action="store_true",
                        help="print the changelog section for --since..--commit")
    parser.add_argument("--changelog-out", default="",
                        help="write the changelog section to this file too")
    parser.add_argument("--verify-checksums", default="",
                        help="check every archive in a directory of release "
                             "assets against its %s file" % CHECKSUM_SUFFIX)
    parser.add_argument("--selftest", action="store_true",
                        help="run the packaging self-checks and exit")
    parser.add_argument("--selftest-dmg", action="store_true",
                        help="build and mount a real disk image from a "
                             "synthetic app (needs hdiutil; exits 77 without "
                             "it)")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return
    if args.selftest_dmg:
        selftest_dmg()
        return
    if args.verify_dmg:
        verify_dmg(args.verify_dmg)
        return
    if args.identity:
        # the ONE composition, printed for whoever stamps, names and publishes
        version = args.version or nightly_version(args.date or today(),
                                                  args.commit)
        print("version=%s" % version)
        print("version_token=%s" % version_filename_token(version))
        return
    if args.changelog:
        section = collect_changelog(args.commit, args.since, args.repo)
        if args.changelog_out:
            with open(args.changelog_out, "w") as handle:
                handle.write(section)
        print(section, end="")
        return
    if args.verify_checksums:
        verify_checksums(args.verify_checksums)
        return
    if not args.platform:
        parser.error("--platform is required")
    if args.verify:
        verify(args.verify, args.platform, args.commit, version=args.version)
        return
    if not args.build_dir:
        parser.error("--build-dir is required")
    output = args.output or os.path.join(args.build_dir, "nightly")
    package(args.platform, args.build_dir, args.commit,
            args.date or today(), output, args.version, args.since, args.repo,
            resolve_macos_signing(os.environ, args.signing_identity,
                                  args.ad_hoc_sign))


# --- reading the workflow's own shell --------------------------------------

def usable_bash():
    """is there a bash that actually RUNS a command? On Windows the name on
    PATH is usually the Subsystem-for-Linux launcher, which starts, prints that
    no distribution is installed and exits nonzero - so presence proves nothing
    and the probe has to ask it to do something."""
    if not shutil.which("bash"):
        return False
    try:
        probe = subprocess.run(["bash", "-c", "printf ok"],
                               capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return False
    return probe.returncode == 0 and probe.stdout.strip() == "ok"


def workflow_step_script(workflow_path, step_name):
    """the shell script of one workflow step, lifted out of the yaml as text
    (stdlib only, no yaml parser): everything indented under that step's
    `run: |`. Returns "" when the file, the step or its script is not there, so
    a caller degrades instead of failing on a copy outside the repository."""
    if not os.path.isfile(workflow_path):
        return ""
    with open(workflow_path, "r", errors="replace") as handle:
        lines = handle.read().split("\n")
    start = -1
    for index, line in enumerate(lines):
        if line.strip() == "- name: " + step_name:
            start = index
            break
    if start < 0:
        return ""
    for index in range(start + 1, len(lines)):
        stripped = lines[index].strip()
        if stripped.startswith("- name:"):
            break               # the next step began: this one runs no script
        if stripped not in ("run: |", "run: |-"):
            continue
        indent = len(lines[index]) - len(lines[index].lstrip()) + 2
        body = []
        for follow in lines[index + 1:]:
            if follow.strip() and not follow.startswith(" " * indent):
                break
            body.append(follow[indent:] if len(follow) >= indent else "")
        return "\n".join(body).rstrip() + "\n"
    return ""


# --- self-checks -----------------------------------------------------------

def selftest():
    """Exercises the parts that do not need a built editor: the ordered version
    and its filename rendering, the identity strings, the changelog extraction
    and formatting over synthetic git output, the checksum sidecar and its
    verification over real bytes, the release notes the publish job composes,
    the limitations table and its rendering, the media staging over a synthetic
    build tree, the archive round-trip, and every verdict the verifier can
    return (including a fake binary reporting the wrong stamp or a version the
    packaging did not compose)."""

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

    # --- the ORDERED version -------------------------------------------
    # THE cross-language literal: tests/core/VersionOrderTests.cpp asserts the
    # same string from the same inputs, so the two implementations of the
    # grammar (this composer and core_util/VersionOrder.h, which the binary
    # uses) cannot drift silently. End to end, the smoke test below matches a
    # binary's self-report against a composed version.
    assert nightly_version("2026-07-30", "dea551f9e0000", base="2.0.0") == \
        "2.0.0-nightly.20260730+dea551f9e"
    assert nightly_version("20260730", "dea551f9e", base="2.0.0") == \
        "2.0.0-nightly.20260730+dea551f9e"
    assert nightly_version("2026-07-30", "", base="2.0.0") == \
        "2.0.0-nightly.20260730"
    # an incomplete stamp invents nothing
    assert nightly_version("", "dea551f9e", base="2.0.0") == ""
    assert nightly_version("2026-7-3", "dea551f9e", base="2.0.0") == ""
    assert nightly_version("2026-07-30", "dea551f9e", base="2.0") == ""
    # the real base version composes a real identity for this tree
    assert nightly_version("2026-07-30", "dea551f9e").startswith(
        project_version() + "-nightly.20260730+")
    # the filename rendering: "+" is not safe in every download path, "_" is,
    # and it is not a version character at all - so it reads back unambiguously
    assert version_filename_token("2.0.0-nightly.20260730+dea551f9e") == \
        "2.0.0-nightly.20260730_dea551f9e"
    assert version_filename_token("") == ""
    for token in (version_filename_token("2.0.0-nightly.20260730+dea551f9e"),
                  "2.0.0-nightly.20260730"):
        assert re.match(r"^[A-Za-z0-9._-]+$", token), token
    # every surface is named from that ONE value
    ordered = "2.0.0-nightly.20260730+dea551f9e"
    assert artifact_stem("macos", "dea551f9e0", ordered) == \
        "Orkige-macos-2.0.0-nightly.20260730_dea551f9e"
    assert artifact_name("linux", "dea551f9e0", ordered) == \
        "Orkige-linux-2.0.0-nightly.20260730_dea551f9e.tar.gz"
    # ... and with no version to name it by, the commit still does
    assert artifact_stem("linux", "dea551f9e0", "") == "Orkige-linux-dea551f9e"

    # --- the changelog: extraction rules over synthetic subjects --------
    # this repository's subjects are one dense narrative line whose first
    # clause is the headline, separated by ": "
    assert changelog_headline(
        "Green CI becomes a download: the nightly grows a binary pipeline "
        "whose first job is a verdict, not a build") == \
        "Green CI becomes a download"
    # SEVERAL colons: the FIRST one bounds the headline
    assert changelog_headline(
        "The agent grows hands: one seam, two consumers: the editor and the "
        "tooling") == "The agent grows hands"
    # a colon with no space is part of the text, not a separator
    assert changelog_headline("Fix the 3:2 aspect fit") == "Fix the 3:2 aspect fit"
    # NO colon: the first sentence
    assert changelog_headline("Fix the tile hit-test. It clamped too late.") == \
        "Fix the tile hit-test"
    assert changelog_headline("Bump the pinned vcpkg commit") == \
        "Bump the pinned vcpkg commit"
    # neither clause nor sentence in reach: a hard cap with an ellipsis
    long_subject = "A subject with no punctuation at all " + "and more words " * 20
    capped = changelog_headline(long_subject)
    assert len(capped) <= CHANGELOG_TEXT_CAP, len(capped)
    assert len(capped) > CHANGELOG_TEXT_CAP - 12, len(capped)
    assert capped.endswith("…")
    # degenerate subjects say nothing rather than crash
    assert changelog_headline("") == ""
    assert changelog_headline(": leading separator") == ": leading separator"

    # the log parser tolerates whatever git hands back
    parsed = parse_log_subjects("aaaaaaaaa\tFirst: one\nbbbbbbbbb\tSecond\n\njunk")
    assert parsed == [("aaaaaaaaa", "First: one"), ("bbbbbbbbb", "Second")], parsed
    assert parse_log_subjects("") == []

    # --- the changelog: formatting -------------------------------------
    entries = [("aaaaaaaaa", "Green CI becomes a download: the nightly grows"),
               ("bbbbbbbbb", "The agent grows hands: one seam")]
    section = changelog_markdown(entries, previous_commit="ffffffff0123456")
    assert section.startswith("## Changes since `ffffffff0`"), section
    assert "- Green CI becomes a download (`aaaaaaaaa`)" in section
    assert "- The agent grows hands (`bbbbbbbbb`)" in section
    assert "more commit" not in section
    assert section.endswith("\n") and "\n\n\n" not in section
    # NEWEST FIRST: the order git emits is the order the section lists
    assert section.index("aaaaaaaaa") < section.index("bbbbbbbbb")
    # over the cap: a "+N more" line, never an unbounded wall
    many = [("%09d" % index, "Headline %d: detail" % index)
            for index in range(CHANGELOG_MAX_ENTRIES + 7)]
    capped_section = changelog_markdown(many, previous_commit="ffffffff0")
    listed = [line for line in capped_section.splitlines()
              if line.startswith("- ")]
    # exactly the cap, plus the one line that says how many were left out
    assert len(listed) == CHANGELOG_MAX_ENTRIES + 1, listed
    assert "- +7 more commits." in capped_section
    assert "- +1 more commit.\n" in changelog_markdown(
        many[:CHANGELOG_MAX_ENTRIES + 1], previous_commit="ffffffff0")
    # an EMPTY range says so (a manual dispatch of an unchanged tree)
    empty = changelog_markdown([], previous_commit="ffffffff0")
    assert "No commits since the previous nightly." in empty
    # NO previous marker: a bounded window, named as such
    unbounded = changelog_markdown(
        entries, note="No previous nightly to compare against, so this lists "
                      "the most recent %d commits." % CHANGELOG_WINDOW)
    assert unbounded.startswith("## Recent changes")
    assert "No previous nightly to compare against" in unbounded
    assert "- Green CI becomes a download (`aaaaaaaaa`)" in unbounded

    # --- the changelog: the git seam ------------------------------------
    class FakeLog:
        """a stand-in `git log` that records what it was asked for"""

        def __init__(self, stdout, returncode=0):
            self.stdout = stdout
            self.returncode = returncode
            self.argv = []

        def __call__(self, argv, **_kwargs):
            self.argv = argv
            return self

    # the marker gives the range: `<since>..<head>`, no window limit
    log_stub = FakeLog("aaaaaaaaa\tHeadline: rest\n")
    bounded = collect_changelog("dea551f9e", "ffffffff0", REPO_ROOT, log_stub)
    assert "ffffffff0..dea551f9e" in log_stub.argv, log_stub.argv
    assert not any(arg.startswith("-n") for arg in log_stub.argv), log_stub.argv
    assert "## Changes since `ffffffff0`" in bounded
    assert "- Headline (`aaaaaaaaa`)" in bounded
    # no marker at all: the bounded window, and the output SAYS so
    window_stub = FakeLog("aaaaaaaaa\tHeadline: rest\n")
    fallback = collect_changelog("dea551f9e", "", REPO_ROOT, window_stub)
    assert "-n%d" % CHANGELOG_WINDOW in window_stub.argv, window_stub.argv
    assert "No previous nightly to compare against" in fallback
    # a marker git cannot reach (a rewritten history): the same window, said
    # honestly, and never a failure
    class FailThenList:
        def __init__(self):
            self.calls = 0

        def __call__(self, argv, **_kwargs):
            self.calls += 1
            if self.calls == 1:
                return FakeLog("", returncode=128)
            return FakeLog("aaaaaaaaa\tHeadline: rest\n")

    unreachable = collect_changelog("dea551f9e", "ffffffff0", REPO_ROOT,
                                    FailThenList())
    assert "is not in this history" in unreachable
    assert "- Headline (`aaaaaaaaa`)" in unreachable
    # no history at all (packaging outside a repository)
    def always_fails(argv, **_kwargs):
        return FakeLog("", returncode=128)

    none_at_all = collect_changelog("dea551f9e", "", REPO_ROOT, always_fails)
    assert "No commit history was available" in none_at_all
    # against the REAL repository (when this checkout has history): the window
    # path produces entries with real short shas
    if os.path.exists(os.path.join(REPO_ROOT, ".git")):
        real = collect_changelog("HEAD", "", REPO_ROOT)
        assert real.startswith("## Recent changes"), real[:80]
        assert re.search(r"^- .+ \(`[0-9a-f]{7,}`\)$", real, re.M), real[:400]

    # the artifact's document wraps the same section
    document = changelog_document(ordered, "dea551f9e0", "2026-07-30", bounded)
    assert document.startswith("# Changelog")
    assert ordered in document and "`dea551f9e`" in document
    assert bounded in document

    # --- the checksum sidecars ------------------------------------------
    with tempfile.TemporaryDirectory() as temp:
        assets = os.path.join(temp, "downloads")
        os.makedirs(assets)
        names = []
        # EVERY asset a person can download, not just the archives: an
        # installer is exactly as much a download as a zip is, so it carries
        # the same sidecar and the publish side re-checks it the same way
        for name in (artifact_name("macos", "dea551f9e0", ordered),
                     artifact_name("linux", "dea551f9e0", ordered),
                     dmg_name("macos", "dea551f9e0", ordered),
                     installer_name("windows", "dea551f9e0", ordered)):
            path = os.path.join(assets, name)
            with open(path, "wb") as handle:
                handle.write(b"asset bytes for " + name.encode("ascii"))
            names.append(name)
            # the standard one-line `sha256sum -c` format: the digest of the
            # REAL bytes and the archive's own name, so a person verifies a
            # download with the tool their machine already has and an updater
            # verifies it before trusting a byte
            sidecar = write_checksum(path)
            recorded = open(sidecar).read().split()
            assert len(recorded) == 2, recorded
            assert recorded[0] == sha256_file(path), recorded
            assert len(recorded[0]) == 64, recorded
            assert recorded[1] == name, recorded
            assert not checksum_mismatch(path)
        # the publish side re-checks what actually arrived; a platform whose
        # build failed is simply not there to check
        assert verify_checksums(assets) == sorted(names)

    # a file that disagrees with its checksum is a REFUSAL: bytes that changed
    # on the way here must never be published under a digest that fits neither
    with tempfile.TemporaryDirectory() as temp:
        assets = os.path.join(temp, "downloads")
        os.makedirs(assets)
        path = os.path.join(assets, artifact_name("linux", "abc", ordered))
        with open(path, "wb") as handle:
            handle.write(b"bytes")
        with open(path + CHECKSUM_SUFFIX, "w") as handle:
            handle.write("%s  %s\n" % ("0" * 64, os.path.basename(path)))
        complaint = checksum_mismatch(path)
        assert "does not match its checksum file" in complaint, complaint
        try:
            verify_checksums(assets)
            raise AssertionError("expected a refusal for a wrong checksum")
        except SystemExit:
            pass
        # and a directory that is not there verifies nothing
        try:
            verify_checksums(os.path.join(temp, "empty-not-here"))
            raise AssertionError("expected a refusal for a missing directory")
        except SystemExit:
            pass

    # --- the release notes an updater reads ------------------------------
    # The notes ARE the client contract: one API call on the release returns a
    # body carrying the ordered version and the commit as machine-readable
    # markers (Docs/nightly-builds.md). That composition lives in the publish
    # job's shell, so the check drives THAT script - lifted out of the yaml and
    # run against stubbed job outputs, because a marker only exists once and it
    # must exist where the client looks.
    notes_script = workflow_step_script(NIGHTLY_WORKFLOW,
                                        "Compose the release notes")
    if not notes_script:
        log("no workflow to read - the release-notes leg is skipped")
    elif not usable_bash():
        # a Windows host resolves `bash` to the Subsystem-for-Linux stub, which
        # exists, runs, and reports that no distribution is installed - so the
        # probe asks the shell to DO something rather than trusting that a name
        # on PATH is a POSIX shell
        log("no working bash - the release-notes leg is skipped")
    else:
        with tempfile.TemporaryDirectory() as temp:
            downloads = os.path.join(temp, "downloads")
            os.makedirs(downloads)
            token = version_filename_token(ordered)
            for name in ("Orkige-macos-%s.zip" % token,
                         "Orkige-macos-%s.dmg" % token):
                open(os.path.join(downloads, name), "w").close()
            with open(os.path.join(temp, "changelog.md"), "w") as handle:
                handle.write(bounded)
            environ = dict(os.environ,
                           RESULT_MACOS="success", RESULT_LINUX="failure",
                           RESULT_WINDOWS="skipped",
                           SHA="dea551f9e0abcdef1234", SHORT_SHA="dea551f9e",
                           BUILD_DATE="2026-07-30", VERSION=ordered,
                           TOKEN=token, TRUST_MACOS=SIGN_ADHOC)
            run = subprocess.run(["bash", "-c", notes_script], cwd=temp,
                                 env=environ, capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            body = open(os.path.join(temp, "notes.md")).read()
            # BOTH markers, carrying the values the gate and a client read: the
            # commit bounds the next changelog and skips an unchanged tree, the
            # ordered version is what core_util/VersionOrder compares
            assert "<!-- orkige-nightly-commit: dea551f9e0abcdef1234 -->" \
                in body, body
            assert "<!-- orkige-nightly-version: %s -->" % ordered in body, body
            # BOTH of a platform's assets are named, and the table separates
            # the one a person installs from the one that unpacks anywhere -
            # a client picking an asset must not have to guess which is which
            assert "| Platform | Install | Portable | Build |" in body, body
            assert ("| macOS (Apple silicon) | `Orkige-macos-%s.dmg` | "
                    "`Orkige-macos-%s.zip` | success |" % (token, token)) \
                in body, body
            # a platform that produced nothing is called out with its job
            # result rather than silently missing, and one that HAS no
            # installable artifact says so instead of reading as a failure
            assert "| Linux (x86_64) | - | not produced | failure |" in body, body
            assert ("| Windows (x64) | not produced | not produced | skipped |"
                    in body), body
            assert ordered in body and bounded.strip() in body
            # the sidecar is the integrity story the notes point at
            assert CHECKSUM_SUFFIX in body, body
            # the ad-hoc build's notes send a reader through the quarantine
            # steps, because that is what an ad-hoc build needs
            assert "UNSIGNED, so macOS refuses" in body, body

            # ... and the SAME script, told what a signed build recorded, says
            # what THAT download does instead. A notarized artifact described as
            # unsigned sends people through steps they do not need; an unsigned
            # one described as notarized leaves them stuck.
            for state, expected, forbidden in (
                    (SIGN_NOTARIZED, "notarized by Apple and stapled",
                     "UNSIGNED, so macOS refuses"),
                    (SIGN_DEVELOPER_ID, "signed but NOT notarized",
                     "notarized by Apple and stapled")):
                run = subprocess.run(
                    ["bash", "-c", notes_script], cwd=temp, text=True,
                    env=dict(environ, TRUST_MACOS=state), capture_output=True)
                assert run.returncode == 0, run.stdout + run.stderr
                variant = open(os.path.join(temp, "notes.md")).read()
                assert expected in variant, variant
                assert forbidden not in variant, variant
                # Windows is unsigned whatever macOS managed, and says so
                assert "SmartScreen" in variant, variant
            # an unset value (a macOS job that never reported) reads as the
            # unsigned wording rather than as a claim nobody made
            unset = dict(environ)
            unset.pop("TRUST_MACOS")
            run = subprocess.run(["bash", "-c", notes_script], cwd=temp,
                                 env=unset, capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            assert "UNSIGNED, so macOS refuses" in \
                open(os.path.join(temp, "notes.md")).read()

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

    # the macOS trust records are mutually exclusive and follow the SIGNATURE
    # this build actually got: exactly one of them appears in an ad-hoc and a
    # signed-but-unnotarized build, and NEITHER in a notarized one - the record
    # a download carries has to describe the download in the reader's hands
    trust_records = {SIGN_ADHOC: "unsigned-macos",
                     SIGN_DEVELOPER_ID: "unnotarized-macos"}
    for state in (SIGN_ADHOC, SIGN_DEVELOPER_ID, SIGN_NOTARIZED):
        keys = [entry.key for entry in limitations_for("macos", state)]
        assert len(keys) == len(set(keys)), keys
        for candidate, key in trust_records.items():
            assert (key in keys) == (candidate == state), (state, key)
        # the shared, signature-independent gaps are there whatever it says
        assert "project-export" in keys, keys
        # ... and no macOS trust record ever leaks into another platform
        for other in ("linux", "windows"):
            assert not set(trust_records.values()) & set(
                entry.key for entry in limitations_for(other, state))
    assert "not notarized" in limitations_markdown("macos", (),
                                                   SIGN_DEVELOPER_ID)
    notarized_doc = limitations_markdown("macos", (), SIGN_NOTARIZED)
    assert "quarantine" not in notarized_doc, notarized_doc
    assert notarized_doc.startswith("# Known limitations")

    # --- what this machine can sign, and what it says when it cannot ----
    # Every branch below is decided WITHOUT a certificate, a keychain or a
    # network, which is why the whole degradation ladder is testable on a
    # machine that has none of them.
    api_env = {NOTARY_KEY_ENV: "/tmp/AuthKey.p8",
               NOTARY_KEY_ID_ENV: "KEYID12345",
               NOTARY_ISSUER_ENV: "1234-issuer-uuid"}
    id_env = {NOTARY_APPLE_ID_ENV: "builds@example.com",
              NOTARY_APP_PASSWORD_ENV: "abcd-efgh-ijkl-mnop",
              NOTARY_TEAM_ID_ENV: "ABCDE12345"}

    # nothing configured: ad-hoc, and the note says exactly why
    plan = resolve_macos_signing({})
    assert not plan.real and not plan.notarizes
    assert plan.state == SIGN_ADHOC
    assert plan.notes and MACOS_SIGNING_IDENTITY_ENV in plan.notes[0]

    # a certificate and nothing else: real signing, honestly not notarized
    plan = resolve_macos_signing({MACOS_SIGNING_IDENTITY_ENV: "DEADBEEF"})
    assert plan.real and not plan.notarizes
    assert plan.identity == "DEADBEEF" and plan.state == SIGN_DEVELOPER_ID
    assert "not notarized" in plan.notes[0], plan.notes

    # the CLI identity outranks the environment, and the keychain rides along
    plan = resolve_macos_signing({MACOS_SIGNING_IDENTITY_ENV: "from-env",
                                  MACOS_KEYCHAIN_ENV: "/tmp/build.keychain-db"},
                                 identity_arg="from-argv")
    assert plan.identity == "from-argv", plan.identity
    assert plan.keychain == "/tmp/build.keychain-db"

    # a certificate plus EITHER credential set notarizes
    signed = {MACOS_SIGNING_IDENTITY_ENV: "DEADBEEF"}
    plan = resolve_macos_signing(dict(signed, **api_env))
    assert plan.notarizes and plan.state == SIGN_NOTARIZED
    assert plan.notary.method == "api-key" and plan.notes == ()
    plan = resolve_macos_signing(dict(signed, **id_env))
    assert plan.notarizes and plan.notary.method == "apple-id"
    # BOTH configured: the API key wins - it is revocable on its own, without
    # touching the Apple ID a person signs in with
    plan = resolve_macos_signing(dict(signed, **dict(api_env, **id_env)))
    assert plan.notary.method == "api-key", plan.notary.method

    # HALF a credential set is not used, and the complaint NAMES what is
    # missing - a typo in one secret must not read as "notarization is off"
    half = dict(signed, **{NOTARY_KEY_ID_ENV: "KEYID12345"})
    plan = resolve_macos_signing(half)
    assert plan.real and not plan.notarizes
    assert plan.state == SIGN_DEVELOPER_ID
    assert NOTARY_KEY_ENV in plan.notes[0] and NOTARY_ISSUER_ENV in plan.notes[0]
    plan = resolve_macos_signing(dict(signed, **{
        NOTARY_APPLE_ID_ENV: "builds@example.com",
        NOTARY_APP_PASSWORD_ENV: "abcd-efgh-ijkl-mnop"}))
    assert not plan.notarizes and NOTARY_TEAM_ID_ENV in plan.notes[0]

    # credentials with NO certificate cannot notarize anything, so the whole
    # thing falls back to ad-hoc rather than half-arming
    plan = resolve_macos_signing(dict(api_env))
    assert plan.state == SIGN_ADHOC and not plan.real
    assert "no signing certificate" in plan.notes[0], plan.notes

    # and an explicit ad-hoc run ignores a configured certificate entirely
    plan = resolve_macos_signing(dict(signed, **api_env), ad_hoc=True)
    assert plan.state == SIGN_ADHOC and not plan.real and not plan.notarizes
    assert plan.notes, plan.notes

    # --- the argv every signing tool is driven with ---------------------
    # THE AD-HOC INVARIANT: with no identity the command is the four words it
    # has always been, so a run without a certificate signs exactly what it
    # signed before this seam existed
    for identity in ("", "-"):
        assert codesign_argv("/x/Orkige.app", identity) == \
            ["codesign", "--force", "--sign", "-", "/x/Orkige.app"]
    assert codesign_verify_argv("/x/Orkige.app") == \
        ["codesign", "--verify", "/x/Orkige.app"]

    # the real one carries BOTH flags notarization requires, and Apple - not
    # this script - is what rejects a submission missing either
    real = codesign_argv("/x/Orkige.app", "DEADBEEF",
                         keychain="/tmp/build.keychain-db")
    assert real[:5] == ["codesign", "--force", "--sign", "DEADBEEF",
                        "--timestamp"], real
    assert "--options" in real and real[real.index("--options") + 1] == "runtime"
    assert real[real.index("--keychain") + 1] == "/tmp/build.keychain-db"
    assert real[-1] == "/x/Orkige.app", real
    # a disk image is a container, not code: no hardened runtime on it
    image = codesign_argv("/x/Orkige.dmg", "DEADBEEF", hardened=False)
    assert "--options" not in image and "--timestamp" in image, image
    # entitlements are OPT-IN and nothing asks for them today; the argv still
    # has to carry one when something eventually does
    entitled = codesign_argv("/x/Orkige.app", "DEADBEEF",
                             entitlements="/x/orkige.entitlements")
    assert entitled[entitled.index("--entitlements") + 1] == \
        "/x/orkige.entitlements"
    strict = codesign_verify_argv("/x/Orkige.app", strict=True)
    assert "--strict" in strict and strict[-1] == "/x/Orkige.app"

    # the notarization argv, per credential method
    api = NotaryCredentials("api-key", key_path="/tmp/AuthKey.p8",
                            key_id="KEYID12345", issuer="1234-issuer-uuid")
    submit = notarytool_submit_argv("/out/Orkige.dmg", api)
    assert submit[:4] == ["xcrun", "notarytool", "submit", "/out/Orkige.dmg"]
    assert submit[submit.index("--key") + 1] == "/tmp/AuthKey.p8"
    assert submit[submit.index("--key-id") + 1] == "KEYID12345"
    assert submit[submit.index("--issuer") + 1] == "1234-issuer-uuid"
    # WAIT for the verdict, with a generous ceiling, and read it as JSON rather
    # than guess it from an exit code
    assert "--wait" in submit
    assert submit[submit.index("--timeout") + 1] == NOTARY_TIMEOUT
    assert submit[submit.index("--output-format") + 1] == "json"
    person = NotaryCredentials("apple-id", apple_id="builds@example.com",
                               app_password="abcd-efgh-ijkl-mnop",
                               team_id="ABCDE12345")
    submit_person = notarytool_submit_argv("/out/Orkige.dmg", person)
    assert submit_person[submit_person.index("--apple-id") + 1] == \
        "builds@example.com"
    assert submit_person[submit_person.index("--password") + 1] == \
        "abcd-efgh-ijkl-mnop"
    assert submit_person[submit_person.index("--team-id") + 1] == "ABCDE12345"
    assert NotaryCredentials().argv() == []
    # the LOG of a submission - the only thing that names the binary Apple
    # objected to - authenticates the same way
    detail = notarytool_log_argv("11111111-2222-3333", api)
    assert detail[:4] == ["xcrun", "notarytool", "log", "11111111-2222-3333"]
    assert detail[detail.index("--key-id") + 1] == "KEYID12345"

    # a credential never reaches a log, even though notarytool takes it on an
    # argv and the command line is still echoed (a command nobody can see is a
    # step nobody can debug)
    echoed = redact_argv(submit_person, person.secrets())
    for secret in ("abcd-efgh-ijkl-mnop", "builds@example.com", "ABCDE12345"):
        assert secret not in echoed, echoed
    assert "<redacted>" in echoed and "notarytool" in echoed
    assert "/out/Orkige.dmg" in echoed, echoed
    # the key FILE's path names a file, it is not the key - it stays readable
    assert "/tmp/AuthKey.p8" in redact_argv(submit, api.secrets())
    assert "KEYID12345" not in redact_argv(submit, api.secrets())
    # an empty secret must never redact every argument
    assert redact_argv(["a", "b"], ("",)) == "a b"

    # stapling and the assessment Gatekeeper itself performs
    assert stapler_argv("/out/Orkige.dmg") == \
        ["xcrun", "stapler", "staple", "/out/Orkige.dmg"]
    assert stapler_validate_argv("/out/Orkige.dmg") == \
        ["xcrun", "stapler", "validate", "/out/Orkige.dmg"]
    app_check = spctl_argv("/x/Orkige.app", "exec")
    assert app_check[:4] == ["spctl", "--assess", "--type", "exec"]
    assert app_check[-1] == "/x/Orkige.app"
    image_check = spctl_argv("/out/Orkige.dmg", "open")
    assert image_check[image_check.index("--context") + 1] == \
        "context:primary-signature", image_check

    # --- Apple's verdict, read strictly ---------------------------------
    accepted = json.dumps({"id": "11111111-2222-3333", "status": "Accepted",
                           "message": "Processing complete"})
    assert notary_submission_verdict(accepted) == \
        ("11111111-2222-3333", "Accepted", True)
    rejected = json.dumps({"id": "44444444", "status": "Invalid"})
    assert notary_submission_verdict(rejected) == ("44444444", "Invalid", False)
    # ... and everything that is not a verdict reads as NOT accepted: "we could
    # not tell" and "Apple said yes" must never be the same answer
    for unusable in ("", "not json at all", "[]", "null",
                     json.dumps({"status": "In Progress"}),
                     json.dumps({"id": "5"})):
        assert notary_submission_verdict(unusable)[2] is False, unusable

    # --- the submission, driven over an injected notarytool -------------
    class Reply:
        def __init__(self, stdout, returncode=0, stderr=""):
            self.stdout = stdout
            self.returncode = returncode
            self.stderr = stderr

    class FakeNotary:
        """a stand-in `xcrun notarytool` that records what it was asked"""

        def __init__(self, replies):
            self.replies = list(replies)
            self.calls = []

        def __call__(self, argv, **_kwargs):
            self.calls.append(argv)
            return Reply(self.replies.pop(0))

    # accepted: one call, and the submission id comes back
    tool = FakeNotary([accepted])
    assert notarize("/out/Orkige.dmg", api, runner=tool) == "11111111-2222-3333"
    assert len(tool.calls) == 1 and tool.calls[0][2] == "submit"

    # REJECTED: the verdict alone names nothing actionable, so the log is
    # fetched for the same submission - without it a rejection is undiagnosable
    tool = FakeNotary([rejected, "Team-ID missing on Orkige.app/.../texcook"])
    try:
        notarize("/out/Orkige.dmg", api, runner=tool)
        raise AssertionError("expected a refusal for a rejected submission")
    except SystemExit:
        pass
    assert len(tool.calls) == 2, tool.calls
    assert tool.calls[1][:4] == ["xcrun", "notarytool", "log", "44444444"], \
        tool.calls[1]

    # a submission that says nothing usable is a refusal too, and there is no
    # id to ask about - so exactly one call, and no artifact
    tool = FakeNotary(["notarytool: command not found"])
    try:
        notarize("/out/Orkige.dmg", api, runner=tool)
        raise AssertionError("expected a refusal for an unreadable verdict")
    except SystemExit:
        pass
    assert len(tool.calls) == 1, tool.calls

    # --- the inside-out seal, over a synthetic bundle -------------------
    # The ORDER is the load-bearing part (a bundle seal records the signatures
    # beneath it, so nested code has to be signed first) and it is the same walk
    # whatever the identity is - which is what keeps an ad-hoc run and a signed
    # one from being two code paths that can drift.
    with tempfile.TemporaryDirectory() as temp:
        app = os.path.join(temp, MACOS_APP_NAME)
        macos_dir = os.path.join(app, "Contents", "MacOS")
        frameworks = os.path.join(app, "Contents", "Frameworks")
        os.makedirs(macos_dir)
        os.makedirs(frameworks)
        for name in ("Orkige", "orkige_player", "texcook"):
            open(os.path.join(macos_dir, name), "w").close()
        open(os.path.join(frameworks, "libsomething.dylib"), "w").close()

        recorded = []
        real_run = orkige_export.run
        orkige_export.run = lambda argv, **_kwargs: recorded.append(argv)
        try:
            seal_macos_bundle(app)
            adhoc_calls = list(recorded)
            recorded.clear()
            seal_macos_bundle(app, MacosSigning("DEADBEEF", "/tmp/k.keychain-db"))
            signed_calls = list(recorded)
        finally:
            orkige_export.run = real_run

        # nested code first (frameworks, then the sibling tools), the bundle
        # last, then a verification - and the app's own executable is never
        # signed on its own, because the bundle seal covers it
        signed_order = [argv[-1] for argv in adhoc_calls]
        assert signed_order == [os.path.join(frameworks, "libsomething.dylib"),
                                os.path.join(macos_dir, "orkige_player"),
                                os.path.join(macos_dir, "texcook"),
                                app, app], signed_order
        assert [argv[-1] for argv in signed_calls] == signed_order
        # THE AD-HOC INVARIANT, end to end: every command a certificate-less run
        # issues is the four-word one, and the verification is the plain one
        for argv in adhoc_calls[:-1]:
            assert argv[:4] == ["codesign", "--force", "--sign", "-"], argv
        assert adhoc_calls[-1] == ["codesign", "--verify", app]
        # ... and the signed run adds exactly what notarization requires, to
        # every piece of code and not just the outer bundle
        for argv in signed_calls[:-1]:
            assert argv[:4] == ["codesign", "--force", "--sign", "DEADBEEF"]
            assert "--timestamp" in argv and "--options" in argv, argv
        assert "--strict" in signed_calls[-1], signed_calls[-1]

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

    # --- the installable artifacts --------------------------------------
    # Their NAMES first: an updater picks an asset by name, so the portable and
    # the installable one must be distinguishable without opening either
    assert dmg_name("macos", "dea551f9e0", ordered) == \
        "Orkige-macos-2.0.0-nightly.20260730_dea551f9e.dmg"
    assert installer_name("windows", "dea551f9e0", ordered) == \
        "Orkige-windows-2.0.0-nightly.20260730_dea551f9e-setup.exe"
    for name in (dmg_name("macos", "dea551f9e0", ordered),
                 installer_name("windows", "dea551f9e0", ordered)):
        # ... and neither may collide with the portable archive's name
        assert name != artifact_name(name.split("-")[1], "dea551f9e0", ordered)
        assert re.match(r"^[A-Za-z0-9._-]+$", name), name

    # the volume name a mounted image shows: the base version, inside the
    # 27-character cap the image filesystem enforces
    assert dmg_volume_name(ordered) == "Orkige 2.0.0", dmg_volume_name(ordered)
    assert dmg_volume_name("") == "Orkige " + project_version()
    for candidate in (ordered, "", "nonsense", "2.11.34-nightly.20260730"):
        assert len(dmg_volume_name(candidate)) <= 27, candidate

    # the Windows VERSIONINFO resource takes four numbers and nothing else, so
    # the ordered version reduces to its base there and is recorded in full
    # where a string is allowed (ProductVersion / DisplayVersion)
    assert windows_file_version(ordered) == "2.0.0.0"
    assert windows_file_version("2.11.34-nightly.20260730+abc") == "2.11.34.0"
    assert re.match(r"^\d+\.\d+\.\d+\.\d+$", windows_file_version(""))
    assert windows_file_version("not a version") == \
        windows_file_version(project_version())

    # the makensis invocation: every build-specific value travels as a /D
    # define, which is what keeps the .nsi script a fixed document
    argv = installer_command("C:\\stage", "C:\\out\\setup.exe", ordered, 4096,
                             tool="makensis.exe", script="C:\\Util\\x.nsi")
    assert argv[0] == "makensis.exe" and argv[-1] == "C:\\Util\\x.nsi", argv
    defines = dict(arg[2:].split("=", 1) for arg in argv if arg.startswith("/D"))
    assert defines["STAGE_DIR"] == "C:\\stage", defines
    assert defines["OUT_FILE"] == "C:\\out\\setup.exe", defines
    assert defines["ORKIGE_VERSION"] == ordered, defines
    assert defines["FILE_VERSION"] == "2.0.0.0", defines
    assert defines["INSTALL_SIZE_KB"] == "4096", defines
    # an unversioned hand run still compiles: no define is ever empty
    hand_run = dict(arg[2:].split("=", 1) for arg
                    in installer_command("s", "o", "", 1) if arg.startswith("/D"))
    assert all(value for value in hand_run.values()), hand_run

    # the installer script itself, read as text: the properties that make it a
    # per-user install a person can undo. makensis exists on no machine this
    # suite runs on, so these are the assertions that hold here - the compile
    # and a silent install/uninstall round trip are the pipeline's own step.
    with open(NSIS_SCRIPT, "r", errors="replace") as handle:
        nsi = handle.read()
    # NO elevation: a user install writes under the user's own roots only
    assert "RequestExecutionLevel user" in nsi
    assert "$LOCALAPPDATA\\Programs" in nsi
    assert "SetShellVarContext current" in nsi
    assert "HKLM" not in nsi, "a per-machine write would demand elevation"
    # listed where Windows shows installed programs, with the ordered version
    assert "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" in nsi
    for value in ("DisplayName", "DisplayVersion", "Publisher",
                  "UninstallString", "QuietUninstallString", "InstallLocation",
                  "EstimatedSize"):
        assert '"%s"' % value in nsi, value
    assert '"DisplayVersion" "${ORKIGE_VERSION}"' in nsi
    # a Start-menu entry and an uninstaller that removes it again
    assert "CreateShortCut \"$SMPROGRAMS" in nsi
    assert "WriteUninstaller" in nsi
    assert 'Section "Uninstall"' in nsi
    assert "DeleteRegKey" in nsi
    # the installed payload IS the staged tree - the same bytes the .zip holds,
    # app-local Visual C++ runtime included, extension-less files (VERSION)
    # included
    assert 'File /r "${STAGE_DIR}\\*"' in nsi
    # the two sides of the /D contract cannot drift: every define the packager
    # passes is required by the script, and every define the script requires is
    # passed
    required = set(re.findall(r"^!ifndef\s+(\w+)", nsi, re.M))
    assert required == set(defines), (required, set(defines))
    for name in required:
        assert "${%s}" % name in nsi, name

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
        # a stand-in editor that answers --version like the real one: the
        # ORDERED identity, composed from the same stamp the packaging uses
        staged_version = nightly_version("2026-07-30", "c0ffee1234567")
        editor_path = os.path.join(editor_dir, "orkige_editor")
        with open(editor_path, "w") as script:
            script.write("#!/bin/sh\n"
                         "echo 'orkige_editor %s [next, Release]'\n"
                         % staged_version)
        os.chmod(editor_path, 0o755)
        open(os.path.join(player_dir, "orkige_player"), "w").close()
        cook_dir = os.path.join(build, "tools", "texcook")
        os.makedirs(cook_dir)
        open(os.path.join(cook_dir, "texcook"), "w").close()
        # a build tree's leftover settings file must NOT ride along
        open(os.path.join(editor_dir, "orkige_editor_view.ini"), "w").close()
        # the vcpkg media layout the staging reads
        media = os.path.join(build, "vcpkg_installed", "x64-linux")
        os.makedirs(os.path.join(media, "include"))
        hlms = os.path.join(media, "share", "ogre-next", "Media", "Hlms", "Pbs")
        os.makedirs(hlms)
        open(os.path.join(hlms, "PixelShader_ps.glsl"), "w").close()

        output = os.path.join(temp, "out")
        # `repo` points at a directory with no history: the changelog degrades
        # honestly rather than failing the packaging
        archive = package("linux", build, "c0ffee1234567", "2026-07-30", output,
                          since="ffffffff0", repo=temp)
        stem = "Orkige-linux-" + version_filename_token(staged_version)
        assert archive.endswith(stem + ".tar.gz"), archive
        stage = os.path.join(output, "stage", stem)
        version = open(os.path.join(stage, "VERSION")).read()
        # the ORDERED version names the archive AND heads the VERSION file
        assert "version: %s\n" % staged_version in version, version
        assert "base-version: %s\n" % project_version() in version, version
        assert "commit: c0ffee1234567\n" in version, version
        assert "built: 2026-07-30\n" in version
        assert "flavor: next\n" in version
        assert "engine-abi-stamp: content.deadbeef1234\n" in version
        assert "platform: linux\n" in version
        # the changelog rides inside the artifact, and says what it could not do
        changelog = open(os.path.join(stage, CHANGELOG_FILE)).read()
        assert changelog.startswith("# Changelog"), changelog[:80]
        assert staged_version in changelog
        assert "No commit history was available" in changelog, changelog
        # the checksum sits beside the archive and matches its bytes
        checksum = archive + CHECKSUM_SUFFIX
        assert os.path.isfile(checksum)
        recorded = open(checksum).read().split()
        assert recorded == [sha256_file(archive), os.path.basename(archive)], \
            recorded
        # the real engine media (committed to the tree) rode along, staged AT
        # THE PATH the editor resolves relative to its own executable
        staged_media_root = os.path.join(stage, FLAT_RESOURCE_DIR, "Media")
        for name in ("Hlms", "fonts", "water", "decals"):
            assert os.path.isdir(os.path.join(staged_media_root, name)), name
        assert not os.path.exists(
            os.path.join(stage, "orkige_editor_view.ini"))
        # the editor's own fonts sit in its resource root, where it looks
        assert os.path.isfile(os.path.join(stage, FLAT_RESOURCE_DIR,
                                           "fa-solid-900.ttf"))
        # the sibling executables it spawns sit beside it
        assert os.path.isfile(os.path.join(stage, "orkige_player"))
        assert os.path.isfile(os.path.join(stage, "texcook"))

        # the archive really carries the tree under its one top-level dir
        with tarfile.open(archive) as tar:
            names = tar.getnames()
        assert stem + "/VERSION" in names, names[:5]
        assert stem + "/" + CHANGELOG_FILE in names, names[:5]
        assert any(name.startswith(stem + "/share/orkige/Media/Hlms")
                   for name in names), names[:5]

        # --- the verifier, on a real unpack ----------------------------
        unpacked = os.path.join(temp, "unpacked")
        os.makedirs(unpacked)
        with tarfile.open(archive) as tar:
            try:
                tar.extractall(unpacked, filter="data")
            except TypeError:  # the filter argument predates python 3.12
                tar.extractall(unpacked)
        os.chmod(os.path.join(unpacked, stem, "orkige_editor"), 0o755)
        if os.name == "nt":
            # the stand-in editor is a shell script, which Windows cannot
            # execute; the injected-runner legs below cover every verdict of the
            # run check on every platform, so only this leg sits out
            log("skipping the stand-in run leg on this platform")
        else:
            # pointed at the parent, it finds the single unpacked tree - and the
            # binary's own identity must be the packaged one, exactly
            reported = verify(unpacked, "linux", "c0ffee1234567",
                              version=staged_version)
            assert reported == ("orkige_editor %s [next, Release]"
                                % staged_version), reported

        # every layout verdict, on a tree with pieces removed
        tree = os.path.join(unpacked, stem)
        _, problems = verify_layout(tree, "linux")
        assert problems == [], problems
        media_root = os.path.join(tree, FLAT_RESOURCE_DIR, "Media")
        os.remove(os.path.join(tree, "orkige_player"))
        os.remove(os.path.join(tree, "texcook"))
        os.remove(os.path.join(tree, CHANGELOG_FILE))
        shutil.rmtree(os.path.join(media_root, "fonts"))
        open(os.path.join(tree, "orkige_editor_imgui.ini"), "w").close()
        _, problems = verify_layout(tree, "linux")
        assert any("player" in problem for problem in problems), problems
        assert any("texcook" in problem for problem in problems), problems
        assert any(CHANGELOG_FILE in problem for problem in problems), problems
        # the complaint names the path the editor reads, with forward slashes on
        # every platform
        assert any(problem.endswith("share/orkige/Media/fonts")
                   for problem in problems), problems
        assert any("imgui.ini" in problem for problem in problems), problems
        # a payload staging that never ran (or an archive that puts the media
        # where the editor does not look) is a REFUSAL, not a note: the shader
        # media is what the editor's own interface draws with
        shutil.rmtree(os.path.join(media_root, "Hlms"))
        _, problems = verify_layout(tree, "linux")
        assert any("shader media" in problem for problem in problems), problems
        shutil.rmtree(media_root)
        _, problems = verify_layout(tree, "linux")
        assert any(problem.startswith("missing share/orkige/Media")
                   for problem in problems), problems

    # --- a wrong stamp is a failure, not a shrug ------------------------
    class FakeResult:
        def __init__(self, stdout, returncode=0, stderr=""):
            self.stdout = stdout
            self.returncode = returncode
            self.stderr = stderr

    with tempfile.TemporaryDirectory() as temp:
        tree = os.path.join(temp, "Orkige-linux-abc")
        media_root = os.path.join(tree, FLAT_RESOURCE_DIR, "Media")
        os.makedirs(os.path.join(media_root, "Hlms"))
        for name in ("fonts", "water", "decals"):
            os.makedirs(os.path.join(media_root, name))
        open(os.path.join(tree, FLAT_RESOURCE_DIR, EDITOR_UI_FONTS[0]),
             "w").close()
        for name in ("orkige_editor", "orkige_player", "texcook", "VERSION",
                     "KNOWN-LIMITATIONS.md", CHANGELOG_FILE):
            open(os.path.join(tree, name), "w").close()
        # a non-executable editor is a packaging failure of its own - where
        # "executable" is a thing. Windows has no execute permission bit: every
        # readable file answers os.access(X_OK), so the check has nothing to
        # find and reporting a problem would be the wrong answer, not a
        # stricter one.
        _, problems = verify_layout(tree, "linux")
        if os.name == "nt":
            assert problems == [], problems
            log("no execute bit on this platform - the not-executable leg "
                "asserts the check stays quiet instead")
        else:
            assert problems == ["the editor is not executable"], problems
        os.chmod(os.path.join(tree, "orkige_editor"), 0o755)
        for stdout, expected in (
                ("orkige_editor 2.0.0-nightly.20260730+abcdef123 "
                 "[next, Release]", "stamp"),     # right shape, wrong commit
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
                "orkige_editor 2.0.0-nightly.20260730+abc1234 [next, Release]",
                returncode=134, stderr="abort"))
            raise AssertionError("expected a refusal for a nonzero exit")
        except SystemExit:
            pass
        # a binary whose ORDERED version is not the one the packaging composed
        # is a refusal too: the two implementations of the grammar have drifted,
        # and an artifact whose name and self-report disagree is unpublishable
        try:
            verify(tree, "linux", "abc1234",
                   runner=lambda *_a, **_k: FakeResult(
                       "orkige_editor 2.0.0 (abc1234, 2026-07-30) "
                       "[next, Release]"),
                   version="2.0.0-nightly.20260730+abc1234")
            raise AssertionError("expected a refusal for a drifted version")
        except SystemExit:
            pass
        # the matching stamp passes, with and without the version expectation
        matching = ("orkige_editor 2.0.0-nightly.20260730+abc1234 "
                    "[next, Release]")
        assert verify(tree, "linux", "abc1234",
                      runner=lambda *_a, **_k: FakeResult(matching))
        assert verify(tree, "linux", "abc1234",
                      runner=lambda *_a, **_k: FakeResult(matching),
                      version="2.0.0-nightly.20260730+abc1234")

    print("orkige_nightly_package: selftest OK")


def selftest_dmg():
    """The disk image, built and mounted for real over a synthetic app - the
    one check that cannot be a text assertion, because what has to hold is
    what a mounted volume actually contains.

    hdiutil exists only on macOS, so this SKIPS with 77 elsewhere rather than
    passing silently: a check that quietly does nothing on two of three CI
    platforms is worse than no check at all."""
    if not shutil.which("hdiutil"):
        log("no hdiutil on this platform - the disk-image self-check is "
            "skipped (it can only run on macOS)")
        sys.exit(77)
    with tempfile.TemporaryDirectory() as temp:
        stage_root = os.path.join(temp, "Orkige-macos-selftest")
        app = os.path.join(stage_root, MACOS_APP_NAME)
        macos_dir = os.path.join(app, "Contents", "MacOS")
        resources = os.path.join(app, "Contents", "Resources")
        media_root = os.path.join(resources, "Media")
        os.makedirs(macos_dir)
        for name in ("Hlms", "fonts", "water", "decals"):
            os.makedirs(os.path.join(media_root, name))
        # the shape verify_layout demands of a real artifact, in miniature
        for name in ("Orkige", "orkige_player", "texcook"):
            path = os.path.join(macos_dir, name)
            open(path, "w").close()
            os.chmod(path, 0o755)
        open(os.path.join(resources, EDITOR_UI_FONTS[0]), "w").close()
        open(os.path.join(resources, "VERSION"), "w").close()
        for name in ("VERSION", "KNOWN-LIMITATIONS.md", CHANGELOG_FILE):
            open(os.path.join(stage_root, name), "w").close()

        dmg = os.path.join(temp, "Orkige-macos-selftest.dmg")
        make_dmg(stage_root, dmg, dmg_volume_name("2.0.0-nightly.20260730"))
        assert os.path.isfile(dmg), dmg
        # the staged directory is handed back exactly as it was: the drag
        # target belongs in the image, never in the .zip the same stage feeds
        assert not os.path.lexists(os.path.join(stage_root, APPLICATIONS_LINK))
        # mount it and look - the app and the drag target both have to be there
        verify_dmg(dmg)

        # ... and the verifier has to REFUSE an image whose drag target lands
        # anywhere but /Applications, or it is not checking anything
        broken = os.path.join(temp, "broken.dmg")
        link = os.path.join(stage_root, APPLICATIONS_LINK)
        os.symlink("/tmp", link)
        try:
            orkige_export.run(["hdiutil", "create", "-volname", "Orkige",
                               "-srcfolder", stage_root, "-fs", "HFS+",
                               "-format", "UDZO", "-ov", broken])
        finally:
            os.remove(link)
        try:
            verify_dmg(broken)
            raise AssertionError("expected a refusal for a misdirected link")
        except SystemExit:
            pass
    print("orkige_nightly_package: disk-image selftest OK")


if __name__ == "__main__":
    main()
