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

    orkige_nightly_package.py --verify-appimage <image> [--commit <sha>]
                              [--version <ordered version>]

    orkige_nightly_package.py --identity --commit <sha> [--date YYYY-MM-DD]

    orkige_nightly_package.py --changelog --commit <sha> [--since <sha>]
                              [--changelog-out <file>]

    orkige_nightly_package.py --history [--commit <sha>] [--history-out <file>]

    orkige_nightly_package.py --verify-checksums <assets dir>

    orkige_nightly_package.py --prune-tags <file|-> [--keep N] [--protect <tag>]

    orkige_nightly_package.py --selftest [--selftest-dmg] [--selftest-appimage]

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
repeats VERSION + CHANGELOG.md + KNOWN-LIMITATIONS.md at the archive root so a
user reads them before installing. Linux and Windows keep it flat beside the
executable, which is where SDL_GetBasePath resolves, with the same three files
repeated under share/orkige/. Both repetitions serve the same rule: the archive
root is what a PERSON reads, the resource root is what the EDITOR reads (its
About box shows the changelog it shipped with, resolved through the one
resource locator).

Each platform ships the container its users expect. The PORTABLE archive is
.zip on macOS (through ditto, which preserves the bundle's symlinks and
executable bits) and Windows, .tar.gz on Linux. Every platform gets a second
asset from the SAME staging: a drag-to-Applications .dmg on macOS, a per-user
installer on Windows, and on Linux a single-file .AppImage that carries the
libraries a distribution may not have installed (see the block above make_dmg
for why each exists, and the one above make_appimage for what the Linux bundle
carries and what it deliberately leaves to the machine).

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
import fnmatch
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
        title="The .tar.gz needs system libraries the .AppImage carries",
        detail="The engine is linked statically, but the executable in the "
               "tarball still loads the distribution's own X11 or Wayland, "
               "OpenGL/Vulkan, ALSA or PulseAudio and D-Bus libraries - and "
               "several it needs (the Xaw, Xmu, Xpm, Xt, ICE and SM family) are "
               "not installed on a modern desktop until something asks for "
               "them, so unpacking the tarball can end in a loader error "
               "naming a library nobody recognises.",
        workaround="Download the .AppImage instead: it carries those libraries, "
                   "so chmod +x and run is the whole install. To use the "
                   "tarball on Debian or Ubuntu: apt install libx11-6 "
                   "libxrandr2 libxcursor1 libxi6 libxkbcommon0 libgl1 "
                   "libxaw7 libxmu6 libxpm4 libxt6 libice6 libsm6 libasound2 "
                   "libdbus-1-3."),
    Limitation(
        key="appimage-host-stack-linux",
        platforms=("linux",),
        title="The .AppImage still needs the machine's own driver stack",
        detail="The bundle carries the ordinary userspace libraries, but the "
               "GPU driver (libvulkan, libGL) and the C library come from the "
               "machine - a bundled copy of either would replace the driver "
               "matched to that kernel and hardware. So the image needs a "
               "working Vulkan or OpenGL driver installed, and a glibc at "
               "least as new as the one it was built against; the VERSION "
               "file's glibc-floor line records exactly which.",
        workaround="Install the distribution's Vulkan driver package (Mesa on "
                   "AMD and Intel, the vendor driver on NVIDIA). If the image "
                   "will not mount, run it with --appimage-extract-and-run: "
                   "AppImages use FUSE, which some distributions no longer "
                   "ship."),
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


# --- the dated archive releases --------------------------------------------
#
# A night publishes the SAME assets twice: once as the rolling `nightly`
# release, which is replaced wholesale so its download URLs stay the ones a
# document quotes and an updater fetches, and once as a dated
# `nightly-YYYYMMDD` release no later night touches - the archive a person
# browses to fetch the build from before a regression. The archive is bounded
# by DELETING the oldest dated releases past a keep count, which makes the
# selection rule below the sharp edge of the whole feature: it may only ever
# reach a tag of exactly that shape. The rolling `nightly`, a stable release
# tag, and anything a person made are not candidates and cannot become ones.

DATED_TAG_PREFIX = VERSION_CHANNEL + "-"                        # `nightly-`
DATED_TAG_RE = re.compile("^" + VERSION_CHANNEL + r"-(\d{8})$")

# how many dated releases survive a night. Two weeks is what the workflow-run
# artifacts keep as well (retention-days: 14), so "how far back can I go" has
# one answer on both surfaces.
DATED_RELEASES_KEPT = 14


def dated_release_tag(date):
    """the tag of one night's dated release: `nightly-YYYYMMDD`, from the SAME
    build date the ordered version orders by, so the two cannot name different
    days. "" when that is not a date - a caller with none publishes the rolling
    release alone rather than inventing a tag."""
    day = compact_date(date)
    return DATED_TAG_PREFIX + day if day else ""


def is_dated_release_tag(tag):
    """is this tag one this pipeline may delete? Exactly `nightly-YYYYMMDD` on
    a real calendar day - never the rolling `nightly`, never a version tag,
    never one that merely starts like one (`nightly-2026`,
    `nightly-20260731-rc1`), never a date that does not exist."""
    match = DATED_TAG_RE.match((tag or "").strip())
    if not match:
        return False
    day = match.group(1)
    try:
        datetime.date(int(day[:4]), int(day[4:6]), int(day[6:]))
    except ValueError:
        return False
    return True


def prune_dated_releases(tags, keep=DATED_RELEASES_KEPT, protect=()):
    """which of these release tags a night deletes: the dated ones past the
    newest `keep`, oldest first (the order they are logged and removed in).

    Everything that is not a dated tag is not a candidate at all, and any tag
    in `protect` - tonight's, always - survives whatever the count says. The
    fixed-width date sorts lexicographically, so "newest" needs no parsing."""
    protected = {(tag or "").strip() for tag in protect}
    dated = sorted({(tag or "").strip() for tag in tags
                    if is_dated_release_tag(tag)}, reverse=True)
    doomed = [tag for tag in dated[max(keep, 0):] if tag not in protected]
    doomed.reverse()
    return doomed


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


# --- the FULL history ------------------------------------------------------
#
# The section above answers "what is new tonight". The history answers "what
# has ever landed": every non-merge commit, newest first, grouped by the DAY it
# landed - the axis a daily channel makes obvious.
#
# A day that PUBLISHED a nightly build is headed by the ordered identity that
# build carries, composed by nightly_version - the same function the packaging
# pipeline names its artifacts with - from the very commit the build was made
# from; so a heading here and the version that binary reports are the same
# string by construction rather than by agreement.
#
# What proves a day published is the dated release tag the publish step leaves
# behind (`nightly-YYYYMMDD`, pointing at exactly the commit that was built).
# The honest limit is the pruning that bounds the archive: only the newest
# DATED_RELEASES_KEPT survive, tag and all, so a day older than that window
# carries no marker. An absent marker therefore records nothing either way -
# the document says so rather than implying that day published nothing.
#
# Every OTHER day is headed by its date, because a date is all such a day is:
# most of this history predates the nightly channel entirely, and composing a
# version identity for every day would present years of ordinary work as a
# release history that never happened. What those days DO carry is the version
# ERA they belong to (HISTORY_ERAS) - a retroactive reading aid over a long
# history, stated as such in the intro so no reader takes `1.0.0` for a release
# that was published at the time.
#
# Nothing generated here is committed back into the repository: git history IS
# the record, and this is rendered from it wherever it is needed - the release
# asset the publish job attaches, and the site's changelog page. A generated
# file committed back would be a commit, which would build, which would make
# the next nightly see a moved branch and rebuild - a loop with no reader.
#
# The seam is the same as the section's: git is asked for its log and its tags
# on one side (git_log_history, git_is_shallow, git_release_tags) and every
# decision about what the document says is on the other, pure and tested
# against synthetic log text and synthetic tag lists.

# what separates the parts of a day heading. A dash with spaces reads as a
# separator rather than as part of either side, which a plain hyphen would
# inside dates that are full of hyphens already.
ERA_HEADING_SEPARATOR = " — "

# The version ERAS this history reads in: the first day of each, the version it
# labels, and the short marker that names what that boundary is. An era runs
# until the next one opens; the last entry opens the nightly period, which
# carries no label at all, because a day in it either names the build it
# published or is simply a date.
#
# THE LABELS ARE RETROACTIVE. This repository carried no version number at all
# until the nightly channel began, so `0.1.0` and `1.0.0` were never published
# releases - they are a reading aid applied now, and the intro says as much in
# the one sentence it spends on it. A marker names what marks the BOUNDARY, not
# something built only inside the period after it: several titles were in flight
# at once and their work crosses these dates freely (the watermaze work runs to
# 2012-04-24, three boundaries past its own).
#
# Every date here is a documented CONSTANT, never anything derived at build
# time: the branches and the release tag that mark them live in the project's
# PRIVATE archive repository, which nothing that renders this document can read.
# A reader who goes hunting for them in THIS repository will not find them - the
# table is the record, and this is its provenance:
#
#   0.2.0  the `watermaze` branch, first commit 2010-11-05 ("orkige branch for
#          watermaze to not have the new ui and stuff")
#   0.3.0  the `ThinkBlue` branch, 2011-02-28 (the Volkswagen Think Blue title;
#          the heading carries the short form). The `CigaretteGame` branch
#          (2011-02-11) was a prototype and is deliberately given no era of its
#          own - it sits inside the period this boundary closes
#   1.0.0  the tag `PuddingPanic-Appstore-version-1.1` (commit 761e806de),
#          2011-06-01. It names version 1.1, so that game's first release is
#          EARLIER still: the tag is the earliest hard evidence of a shipped
#          title, not the release date, and nothing here claims more than that
#   2.0.0-pre  the commit that bootstrapped the current editor, 843529504
#
# Keeping all of it as ONE table is what keeps the claim checkable: the eras are
# read in one place instead of drifting into conditionals nobody can enumerate.
HISTORY_ERAS = (
    # first day,    version,      marker
    ("2010-09-16", "0.1.0",      ""),
    ("2010-11-05", "0.2.0",      "Watermaze"),
    ("2011-02-28", "0.3.0",      "Think Blue"),
    ("2011-06-01", "1.0.0",      "Pudding Panic"),
    ("2026-07-07", "2.0.0-pre",  "Editor"),
    ("2026-07-31", "",           ""),
)

# the day the nightly channel began - from here a day names a build or nothing
NIGHTLY_ERA_START = HISTORY_ERAS[-1][0]


def _era_label(version, marker):
    """an era's heading label: its version, and the marker that names the
    boundary where there is one"""
    if not version:
        return ""
    return version + ERA_HEADING_SEPARATOR + marker if marker else version


ERA_LABELS = tuple(_era_label(version, marker)
                   for _start, version, marker in HISTORY_ERAS
                   if _era_label(version, marker))

# Two sentences: what the document is, and the one thing a reader must not get
# wrong about it. Everything else - the evidence, the overlaps, why a boundary
# is a start and not a period - is a maintainer's concern and lives in the
# comment above, not on the page.
HISTORY_INTRO = (
    "The engine's history, grouped by day. Versions before %s are labels "
    "applied in hindsight; from that day on a published nightly heads its day "
    "with the real version it carries, and every other day is a plain date - "
    "including published days older than the %d the archive keeps."
    % (NIGHTLY_ERA_START, DATED_RELEASES_KEPT))

_ISO_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

# The grammar every day heading obeys, in ONE place so the checks on this
# document and on the site page that renders it read the same rule, and derived
# from the era table so a new era cannot leave it behind: either the ordered
# identity of a build that really was published, or a date - carrying its
# retroactive era label where an era applies.
HISTORY_HEADING_RE = re.compile(
    r"^(?:\d+\.\d+\.\d+-" + VERSION_CHANNEL + r"\.\d{8}\+[0-9a-f]{7,}"
    r"|(?:(?:" + "|".join(re.escape(label) for label in ERA_LABELS) + ")"
    + re.escape(ERA_HEADING_SEPARATOR) + r")?\d{4}-\d{2}-\d{2})$")

# the date every day repeats under its heading, whatever that heading names
HISTORY_SUMMARY_DATE_RE = re.compile(r"^\*(\d{4}-\d{2}-\d{2}) - \d+ commit",
                                     re.M)


def history_era(date):
    """the retroactive era label a commit day carries ("<version>" or
    "<version> — <marker>"), "" for a day in the nightly period (which names
    real builds instead), for a day before the first era opens, and for anything
    that is not an ISO date. Pure.

    ISO dates compare as strings, so no parsing is needed; a day that IS a
    boundary belongs to the era that boundary opens."""
    day = (date or "").strip()
    if not _ISO_DATE_RE.match(day):
        return ""
    label = ""
    for start, version, marker in HISTORY_ERAS:
        if day < start:
            break
        label = _era_label(version, marker)
    return label


def published_days(tags):
    """which DAYS actually published a nightly build, and the commit each of
    those builds was made from: `{"YYYYMMDD": "<commit>"}` from tag lines of the
    form `<tag>` or `<tag>\\t<commit>[\\t<commit>]`. Pure - this is the whole
    decision, and the git call that feeds it is git_release_tags.

    The tag SHAPE is the entire test (is_dated_release_tag), so the rolling
    `nightly`, a version tag, a truncated look-alike like `nightly-2026` and an
    impossible calendar date are simply not days. A line naming no commit still
    marks its day: the tag is what proves the build existed, and the commit only
    sharpens the identity that day is headed by."""
    days = {}
    for line in (tags or ()):
        fields = str(line).split("\t")
        tag = fields[0].strip()
        if not is_dated_release_tag(tag):
            continue
        commit = ""
        for field in fields[1:]:
            if field.strip():
                commit = field.strip()
                break
        days[tag[len(DATED_TAG_PREFIX):]] = commit
    return days


def parse_log_history(text):
    """`<short sha>\\t<YYYY-MM-DD>\\t<subject>` lines into
    [(sha, date, subject)], newest first (the order git emits). Pure; blank and
    malformed lines are skipped."""
    entries = []
    for line in (text or "").splitlines():
        parts = line.split("\t", 2)
        if len(parts) < 3:
            continue
        sha, date, subject = parts[0].strip(), parts[1].strip(), parts[2]
        if sha and date:
            entries.append((sha, date, subject))
    return entries


class HistoryGroup:
    """one day of history: the commits that landed on it (newest first) and, for
    a day that PUBLISHED a nightly build, the ordered version identity that
    build carries. `version` is "" for every other day - the overwhelming
    majority, which published nothing and must not look as though they did."""

    def __init__(self, date, version, entries):
        self.date = date
        self.version = version
        self.entries = tuple(entries)
        self.era = history_era(date)

    @property
    def published(self):
        """did a nightly build of this day get published? A version identity is
        only ever composed where a dated release tag proved one."""
        return bool(self.version)

    @property
    def heading(self):
        """what heads the day: the published build's ordered identity where
        there is one, otherwise the date - carrying its retroactive era label
        where an era applies, so a long history reads as the eras it ran
        through without any of them claiming to be a release."""
        if self.version:
            return self.version
        if self.era:
            return self.era + ERA_HEADING_SEPARATOR + self.date
        return self.date


def history_groups(entries, base="", published=None):
    """[(sha, date, subject)] -> [HistoryGroup], newest day first, each day's
    commits in the order git emitted them. Pure.

    `published` is the {day -> commit} map of the days that actually published a
    nightly (published_days). A day in it is given the ordered identity of THAT
    build - composed by nightly_version from the day and the commit the build
    was made from, falling back to the day's newest commit when the tag named
    none. Every other day is given no version at all. First appearance decides a
    day's position, so the grouping never reorders what git said."""
    published = published or {}
    order = []
    by_date = {}
    for sha, date, subject in entries:
        if date not in by_date:
            by_date[date] = []
            order.append(date)
        by_date[date].append((sha, subject))
    groups = []
    for date in order:
        day = compact_date(date)
        version = ""
        if day in published:
            version = nightly_version(
                date, published[day] or by_date[date][0][0], base)
        groups.append(HistoryGroup(date, version, by_date[date]))
    return groups


def history_note(available, shallow, commits=0, tags_read=True):
    """what the document says about its OWN completeness - the honest half of
    generating a record from whatever history the machine happens to have.
    Pure; "" when nothing needs a caveat."""
    if not available:
        return ("No commit history was available where this was generated, so "
                "nothing is listed. This document is rendered from the "
                "repository's history when the site deploys and when a build "
                "is published.")
    notes = []
    if shallow:
        notes.append(
            "This was generated from a shallow checkout, so it lists only the "
            "%d commit%s that clone carried - not the whole history."
            % (commits, "" if commits == 1 else "s"))
    if not tags_read:
        notes.append(
            "The dated release tags could not be read where this was "
            "generated, so no day is marked as having published a build - "
            "including days that did.")
    return " ".join(notes)


def history_markdown(groups, note="", title="Changelog", intro=HISTORY_INTRO):
    """the full-history document: the release asset and the site page render
    from this ONE text. Pure - it never asks git anything."""
    lines = ["# " + title, ""]
    if intro:
        lines.append(intro)
        lines.append("")
    if note:
        lines.append(note)
        lines.append("")
    commits = sum(len(group.entries) for group in groups)
    if not commits:
        lines.append("No commits to list.")
        return "\n".join(lines).rstrip() + "\n"
    lines.append("%d commit%s across %d day%s."
                 % (commits, "" if commits == 1 else "s",
                    len(groups), "" if len(groups) == 1 else "s"))
    lines.append("")
    for group in groups:
        lines.append("## " + group.heading)
        lines.append("")
        # the date is repeated under every heading, published or not, so one
        # regular line answers "which day is this" whatever the heading names
        summary = ("*%s - %d commit%s"
                   % (group.date, len(group.entries),
                      "" if len(group.entries) == 1 else "s"))
        summary += " - published nightly build*" if group.published else "*"
        lines.append(summary)
        lines.append("")
        for sha, subject in group.entries:
            lines.append("- %s (`%s`)"
                         % (changelog_headline(subject) or "(no subject)",
                            short_commit(sha)))
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def git_log_history(repo, head="HEAD", limit=0, runner=subprocess.run):
    """the IMPURE half: ask git for `<short sha>\\t<date>\\t<subject>` lines
    over the WHOLE reachable history (or the newest @p limit of it, which is
    how a caller asks only what the tip is). Returns (text, ok), never
    raises."""
    argv = ["git", "-C", repo, "log", "--no-merges", "--date=short",
            "--pretty=format:%h%x09%cd%x09%s"]
    if limit:
        argv.append("-n%d" % int(limit))
    argv.append(head)
    try:
        result = runner(argv, capture_output=True, text=True)
    except OSError:
        return "", False
    if result.returncode != 0:
        return "", False
    return result.stdout or "", True


def git_is_shallow(repo, runner=subprocess.run):
    """is this checkout a SHALLOW clone? A truncated history renders as a
    perfectly plausible complete one, so the document has to be told."""
    argv = ["git", "-C", repo, "rev-parse", "--is-shallow-repository"]
    try:
        result = runner(argv, capture_output=True, text=True)
    except OSError:
        return False
    if result.returncode != 0:
        return False
    return (result.stdout or "").strip() == "true"


def git_release_tags(repo, runner=subprocess.run):
    """the IMPURE half of "which days published": the dated release tags this
    repository carries, one `<tag>\\t<dereferenced commit>\\t<commit>` line each.
    Returns (lines, ok), never raises.

    The glob is deliberately loose (`nightly-*`) and every judgement about what
    a line means belongs to published_days: a filter that decided anything here
    would be a second, untested copy of the tag rule."""
    argv = ["git", "-C", repo, "tag", "--list", DATED_TAG_PREFIX + "*",
            "--format=%(refname:strip=2)%09%(*objectname)%09%(objectname)"]
    try:
        result = runner(argv, capture_output=True, text=True)
    except OSError:
        return [], False
    if result.returncode != 0:
        return [], False
    return [line for line in (result.stdout or "").splitlines()
            if line.strip()], True


def collect_history(head="HEAD", repo=REPO_ROOT, runner=subprocess.run,
                    base="", title="Changelog", extra_tags=()):
    """the full-history document for @p repo, degrading honestly: no history at
    all says so, a shallow clone says the record is truncated rather than
    presenting what it has as the whole of it, and tags it could not read leave
    every day unmarked and SAY that rather than reading as "nothing published".

    `extra_tags` are dated release tag lines to count as published ON TOP of the
    ones the repository carries. The publish job needs exactly one of them: the
    document is an ASSET of tonight's release, so it is written before that
    release - and its tag - exists, and without this the one night a build
    describes would be the one night it left unmarked."""
    text, ok = git_log_history(repo, head or "HEAD", runner=runner)
    if not ok:
        warn("no commit history available - the changelog says so")
        return history_markdown([], note=history_note(False, False),
                                title=title)
    entries = parse_log_history(text)
    shallow = git_is_shallow(repo, runner=runner)
    if shallow:
        warn("this is a shallow checkout - the history document says it lists "
             "only the %d commits this clone carries" % len(entries))
    tags, tags_ok = git_release_tags(repo, runner=runner)
    if not tags_ok:
        warn("could not read the dated release tags - no day is marked as "
             "having published a build, and the document says so")
    return history_markdown(
        history_groups(entries, base,
                       published_days(list(tags) + list(extra_tags))),
        note=history_note(True, shallow, len(entries), tags_ok),
        title=title)


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
# - Linux has no package format the distributions share, but it has a
#   single-file convention every one of them runs: an AppImage. It carries the
#   libraries a distribution may not have installed (see the block above
#   make_appimage), so `chmod +x` and a double click is the whole install.
#
# The PORTABLE one stays the .zip / .tar.gz: no image to mount, no installer to
# run, unpack anywhere - and it is the shape an updater can consume, because
# swapping files in place needs neither a mount nor an elevation-free installer
# run. On Linux the tarball also stays the shape a person who wants the files
# themselves unpacks; what it does NOT carry is the library closure, which is
# exactly the difference between the two Linux assets.

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


# --- the Linux single-file bundle ------------------------------------------
#
# The tarball carries the editor but not the libraries it links, and that list
# is longer than a user expects: beside the X11 and GL/Vulkan libraries every
# graphical program needs, the editor pulls in libXaw, libXmu, libXpm, libXt,
# libICE and libSM - the Xt/Athena family nothing on a modern desktop installs
# on its own. Unpacking the tarball on a clean distribution therefore ends in a
# loader error naming a library its user has never heard of. The AppImage is one
# file that carries them: chmod +x, run, on any distribution.
#
# WHAT IS BUNDLED, AND WHAT MUST COME FROM THE HOST
#
# The rule is one sentence: every library the loader resolves for the editor is
# bundled EXCEPT the ones whose correct version is a property of the MACHINE
# rather than of our build. Four families qualify, and each is excluded for a
# reason that is not a preference:
#
#   driver         libvulkan, libGL/libEGL/libGLX/libGLdispatch/libOpenGL,
#                  libdrm, libgbm, libglapi. These are the front doors into the
#                  machine's own GPU driver, which is matched to its kernel and
#                  its hardware. A bundled copy either shadows that driver's
#                  entry point or is substituted into the driver's own
#                  dependency chain - both of which turn a working GPU into a
#                  software fallback or a crash.
#   libc           glibc and the dynamic loader (libc, libm, libdl, libpthread,
#                  librt, libresolv, libutil, the NSS modules, ld-linux). The
#                  process is started by the HOST's loader and resolves users
#                  and hosts through the HOST's NSS modules; a second glibc
#                  inside the image is a mismatch, not a fix. This is what
#                  makes the glibc floor a property of the BUILD image, which
#                  is why the packaging measures it (glibc_version_floor) and
#                  records it in VERSION instead of assuming it.
#   toolchain      libstdc++ and libgcc_s, and ONLY those two. Because glibc is
#                  not bundled, the machine that can run this image is already
#                  at least as new as the machine that built it, so its C++
#                  runtime can never be too old - while a bundled copy that is
#                  OLDER than the host's Mesa driver (which resolves its own
#                  libstdc++ through our search path) breaks that driver. The
#                  bundle would carry all of the risk and none of the benefit.
#                  libatomic is NOT in this family and IS bundled: it comes
#                  from the same compiler, but a distribution installs it only
#                  when something asks for it, so presence rather than version
#                  is what decides - and presence is what a download cannot
#                  assume. (libstdc++ and libgcc_s are on every machine that
#                  can show a window at all.)
#   server-client  libX11, libxcb, libwayland-*, libxshmfence, libasound,
#                  libpulse, libjack, libdbus-1, libudev. These talk to a
#                  server or a daemon that is part of the running system, and
#                  they load the host's own modules (X11 locale and input
#                  method data, ALSA plugins) by absolute path. They are also
#                  present on every machine that has a display at all, so
#                  bundling them buys nothing.
#
# Everything else IS bundled - the Xaw/Xmu/Xpm/Xt/ICE/SM family, libbsd, libmd,
# libuuid, libatomic, the Xext/Xrandr/Xrender/Xcursor/Xi leaf libraries and any
# shared library the build tree resolved for this binary. Those are ordinary
# userspace code with no coupling to the machine, and they are the actual
# failure this artifact exists to fix.
#
# HOW THE BUNDLED COPIES WIN. The AppRun exports LD_LIBRARY_PATH pointing at the
# image's own lib directory, which the loader searches BEFORE every system
# directory. So for a bundled name the host's copy is never consulted - which is
# what makes "the host does not have it" a non-event, and what verify_appimage
# asserts by resolving the editor's dependencies inside an extracted image.
# The same variable reaches the processes the editor spawns; the bundled set is
# deliberately kept to leaf libraries no other program's behaviour hinges on.
#
# FUSE. An AppImage mounts itself through libfuse at run time, and some current
# distributions no longer ship FUSE. `--appimage-extract-and-run` (or the
# APPIMAGE_EXTRACT_AND_RUN environment variable) unpacks to a temporary
# directory and runs from there instead, needing nothing. The packaging uses it
# for appimagetool itself, which is an AppImage too, and every check runs the
# produced image that way - so nothing here depends on FUSE being installed.

APPIMAGE_SUFFIX = ".AppImage"

# appimagetool is not on a runner and is not a dependency this repository can
# declare, so it is resolved the way bundletool is (Docs/store-release.md): an
# explicit path, else the environment, else a launcher on PATH. The nightly job
# downloads a pinned release and points this at it; nothing here reaches the
# network.
APPIMAGETOOL_ENV = "ORKIGE_APPIMAGETOOL"

# inside the AppDir: the desktop entry and the icon it names (both at the root,
# where the AppImage runtime and desktop integration look, and again under
# usr/share where a desktop that installs the file expects them)
APPIMAGE_DESKTOP_FILE = "orkige.desktop"
APPIMAGE_ICON_NAME = "orkige"
APPIMAGE_ICON_FILE = APPIMAGE_ICON_NAME + ".png"
APPIMAGE_ICON_SIZE = 256
# Where the bundled libraries land, and what the AppRun puts on
# LD_LIBRARY_PATH. Spelled with a FORWARD SLASH deliberately and not through
# os.path.join: an AppDir is a Linux layout and the AppRun is a POSIX shell
# script, so this separator is part of the FORMAT rather than a property of the
# machine composing it - joining it on Windows wrote "$HERE/usr\lib" into the
# script. Python accepts forward slashes in filesystem paths on every platform,
# so the same constant still serves the os.path.join calls below.
APPIMAGE_LIB_DIR = "usr/lib"

# the editor icon is drawn by the same generator the macOS .icns comes from, so
# the Linux launcher shows the same artwork
EDITOR_ICON_SCRIPT = os.path.join(REPO_ROOT, "Util", "make_editor_icon.py")


class HostLibraryFamily:
    """one family of libraries that must come from the host, with the reason
    stated where the decision is made. `patterns` match the SONAME's stem (the
    part before `.so`), so `libGLX*` covers libGLX.so.0 and libGLX_mesa.so.0."""

    def __init__(self, key, reason, patterns):
        self.key = key
        self.reason = reason
        self.patterns = tuple(patterns)


HOST_LIBRARY_FAMILIES = (
    HostLibraryFamily(
        "driver",
        "the entry point into the machine's own GPU driver",
        ("libvulkan", "libGL", "libGLX*", "libGLdispatch", "libOpenGL",
         "libEGL", "libGLESv*", "libglapi", "libdrm*", "libgbm")),
    HostLibraryFamily(
        "libc",
        "glibc and the loader that started this process",
        ("ld-linux*", "ld64", "libc", "libm", "libdl", "libpthread", "librt",
         "libresolv", "libutil", "libnsl", "libanl", "libcrypt", "libnss_*",
         "libthread_db", "libBrokenLocale", "linux-vdso", "linux-gate")),
    HostLibraryFamily(
        "toolchain",
        "the C++ runtime the host's own driver stack also resolves",
        ("libstdc++", "libgcc_s")),
    HostLibraryFamily(
        "server-client",
        "a client of a server or daemon that is part of the running system",
        ("libX11", "libX11-xcb", "libxcb", "libxcb-*", "libxshmfence",
         "libwayland-*", "libdecor-*", "libasound", "libpulse*", "libjack*",
         "libpipewire-*", "libdbus-1", "libudev", "libsystemd")),
)


def library_stem(soname):
    """`libXaw.so.7` -> `libXaw` (the name the exclusion families match on)"""
    name = os.path.basename((soname or "").strip())
    index = name.find(".so")
    return name[:index] if index > 0 else name


def host_library_family(soname):
    """the family key that keeps this library OUT of the bundle, or "" when it
    is ordinary userspace code the image carries. This IS the inclusion rule -
    everything the loader resolves and this function does not name is bundled."""
    stem = library_stem(soname)
    for family in HOST_LIBRARY_FAMILIES:
        for pattern in family.patterns:
            if fnmatch.fnmatchcase(stem, pattern):
                return family.key
    return ""


def host_library_reason(key):
    for family in HOST_LIBRARY_FAMILIES:
        if family.key == key:
            return family.reason
    return ""


def parse_ldd(text):
    """`ldd` output -> [(soname, resolved path)], the path empty when the loader
    found nothing. Pure, so the planning below is testable without a Linux
    binary to point at: the three shapes are `name => path (addr)`, a bare
    `name (addr)` (the vDSO and the loader itself) and `name => not found`."""
    entries = []
    for line in (text or "").splitlines():
        line = line.strip()
        if not line:
            continue
        if "=>" in line:
            name, _, target = line.partition("=>")
            name = name.strip()
            target = target.strip()
            if target.startswith("not found"):
                entries.append((name, ""))
                continue
            # drop the trailing load address
            if target.endswith(")") and " (" in target:
                target = target[:target.rfind(" (")].strip()
            entries.append((name, target))
        else:
            name = line[:line.rfind(" (")].strip() if " (" in line else line
            if name:
                entries.append((os.path.basename(name), ""))
    return entries


def plan_bundled_libraries(ldd_text):
    """the inclusion rule applied to one binary's resolved dependencies.

    Returns (bundle, host, missing): the (soname, path) pairs the image carries,
    the (soname, family) pairs it deliberately leaves to the machine, and the
    sonames the loader could not resolve at all - which is a broken build tree
    and never something to package around."""
    bundle = []
    host = []
    missing = []
    seen = set()
    for soname, path in parse_ldd(ldd_text):
        if soname in seen:
            continue
        seen.add(soname)
        family = host_library_family(soname)
        if family:
            host.append((soname, family))
        elif not path:
            missing.append(soname)
        else:
            bundle.append((soname, path))
    return sorted(bundle), sorted(host), sorted(missing)


def glibc_version_floor(objdump_text):
    """the oldest glibc this binary can run on, read out of the binary itself:
    the highest GLIBC_x.y symbol version it references. `objdump -p` prints them
    under "Version References"; the parse is pure so the ordering (2.9 is older
    than 2.34, not newer) is tested rather than assumed. Returns "" when the
    output names none - an honest unknown, never a guess."""
    best = ()
    best_text = ""
    for match in re.finditer(r"GLIBC_(\d+(?:\.\d+)+)", objdump_text or ""):
        text = match.group(1)
        parts = tuple(int(piece) for piece in text.split("."))
        if parts > best:
            best = parts
            best_text = text
    return best_text


def binary_glibc_floor(path, runner=subprocess.run):
    """the floor above, measured on a real binary. objdump is part of binutils
    and therefore of any machine that linked this build; without it the floor is
    reported as unknown rather than invented."""
    if not shutil.which("objdump"):
        warn("no objdump - this build's glibc floor is not recorded")
        return ""
    result = runner(["objdump", "-p", path], capture_output=True, text=True)
    if result.returncode != 0:
        warn("objdump could not read '%s' - the glibc floor is not recorded"
             % path)
        return ""
    return glibc_version_floor(result.stdout or "")


def binary_libraries(path, extra_library_path="", runner=subprocess.run):
    """`ldd` output for one binary, optionally with a library directory put in
    front of the loader's search path (which is how an extracted image is asked
    where it resolves its dependencies FROM)"""
    if not shutil.which("ldd"):
        fail("no ldd - the AppImage's library closure can only be resolved on "
             "Linux")
    environment = dict(os.environ)
    if extra_library_path:
        existing = environment.get("LD_LIBRARY_PATH", "")
        environment["LD_LIBRARY_PATH"] = (
            extra_library_path + (":" + existing if existing else ""))
    result = runner(["ldd", path], capture_output=True, text=True,
                    env=environment)
    # a binary ldd refuses to read at all is a packaging failure; "not found"
    # lines come back with returncode 0 and are handled by the planner
    if result.returncode != 0 and not (result.stdout or "").strip():
        fail("ldd could not read '%s': %s"
             % (path, (result.stderr or "").strip() or "no output"))
    return result.stdout or ""


def desktop_entry_text(version=""):
    """the .desktop entry the image carries: what a desktop shows once the file
    is integrated (a name, the icon below and a category), plus the version so
    a file manager can tell two downloads apart. Exec names the executable
    without a path - the AppImage runtime rewrites it to the mounted AppRun."""
    return "\n".join([
        "[Desktop Entry]",
        "Type=Application",
        "Name=Orkige",
        "GenericName=Game Editor",
        "Comment=Build and play games with the Orkige engine",
        "Exec=orkige_editor %F",
        "Icon=" + APPIMAGE_ICON_NAME,
        "Terminal=false",
        "Categories=Development;IDE;",
        "Keywords=game;engine;editor;3D;2D;",
        "StartupWMClass=orkige_editor",
        "X-AppImage-Version=" + (version or "unversioned"),
        ""])


def apprun_text(editor_name, lib_dir=APPIMAGE_LIB_DIR):
    """the AppDir's entry point. Two jobs: put the bundled libraries in front of
    the system ones (the loader reads LD_LIBRARY_PATH before every system
    directory, which is what makes a host without them a non-event), and exec
    the editor so it inherits the process rather than running under a shell that
    would swallow its exit code and its signals."""
    return "\n".join([
        "#!/bin/sh",
        "# Orkige editor - AppImage entry point.",
        "# HERE is the mounted (or extracted) image root.",
        'HERE=$(dirname "$(readlink -f "$0")")',
        'export LD_LIBRARY_PATH="$HERE/' + lib_dir
        + '${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"',
        'exec "$HERE/' + editor_name + '" "$@"',
        ""])


def appimage_name(platform, commit, version=""):
    return artifact_stem(platform, commit, version) + APPIMAGE_SUFFIX


def appimage_arch(machine=""):
    """the architecture name appimagetool stamps into the image. AppImage
    runtimes are per-architecture, so this is passed explicitly rather than
    guessed by the tool from whatever binary it happens to look at first."""
    if not machine:
        # os.uname exists wherever an AppImage can be built at all
        machine = os.uname().machine if hasattr(os, "uname") else ""
    machine = machine.strip()
    return {"x86_64": "x86_64", "amd64": "x86_64",
            "aarch64": "aarch64", "arm64": "aarch64",
            "i686": "i686", "i386": "i686",
            "armv7l": "armhf"}.get(machine, machine)


def resolve_appimagetool(tool_arg="", environ=None, which=shutil.which):
    """the tool: an explicit path wins, else the environment, else a launcher on
    PATH. The same precedence the Android bundle's bundletool follows, and for
    the same reason - the tool is a separate download this repository does not
    vendor. `which` is injected so the precedence is testable on a machine that
    has no appimagetool at all."""
    environ = os.environ if environ is None else environ
    explicit = (tool_arg or environ.get(APPIMAGETOOL_ENV, "")).strip()
    if explicit:
        return explicit
    return which("appimagetool") or ""


def appimagetool_command(tool, appdir, out_file):
    """the appimagetool invocation as a pure argv. --no-appstream because the
    editor ships no AppStream metadata and the validator is a separate tool;
    everything else it needs is inside the AppDir."""
    return [tool, "--no-appstream", appdir, out_file]


def render_editor_icon(destination, size=APPIMAGE_ICON_SIZE,
                       runner=subprocess.run):
    """the launcher icon, drawn by the generator that also draws the macOS
    .icns - one piece of artwork, two containers."""
    result = runner([sys.executable, EDITOR_ICON_SCRIPT, "--png", str(size),
                     destination], capture_output=True, text=True)
    if result.returncode != 0 or not os.path.isfile(destination):
        fail("could not render the editor icon: %s"
             % ((result.stderr or "").strip() or "no output"))
    return destination


def build_appdir(stage_root, appdir, editor_name, version="",
                 runner=subprocess.run):
    """assemble the AppDir: the staged tree exactly as the tarball carries it
    (so the editor resolves share/orkige beside its own executable, one layout
    for both Linux assets), the library closure under usr/lib, and the three
    files the AppImage format itself needs - AppRun, a desktop entry and the
    icon it names, at the root where the runtime looks and again under usr/share
    where a desktop that integrates the file expects them.

    Returns (bundled, host): what the image carries and what it leaves to the
    machine, so the caller can log and record both."""
    if os.path.isdir(appdir):
        shutil.rmtree(appdir)
    # hardlink the payload where the filesystem allows it: the staged tree is
    # hundreds of megabytes and neither container modifies it
    def link_or_copy(source, destination, *, follow_symlinks=True):
        try:
            os.link(source, destination)
        except OSError:
            shutil.copy2(source, destination, follow_symlinks=follow_symlinks)
    shutil.copytree(stage_root, appdir, copy_function=link_or_copy,
                    symlinks=True)

    editor = os.path.join(appdir, editor_name)
    bundle, host, missing = plan_bundled_libraries(
        binary_libraries(editor, runner=runner))
    if missing:
        fail("the editor's dependencies do not resolve on this machine (%s) - "
             "the build tree is broken, and an image built from it would be too"
             % ", ".join(missing))
    lib_dir = os.path.join(appdir, APPIMAGE_LIB_DIR)
    os.makedirs(lib_dir, exist_ok=True)
    for soname, path in bundle:
        # copy the resolved FILE under its SONAME: a distribution's real file is
        # usually libFoo.so.1.2.3 with the soname a symlink, and a symlink into
        # a directory the image does not carry resolves to nothing
        shutil.copy2(os.path.realpath(path), os.path.join(lib_dir, soname))

    apprun = os.path.join(appdir, "AppRun")
    with open(apprun, "w") as handle:
        handle.write(apprun_text(editor_name))
    os.chmod(apprun, 0o755)
    desktop = desktop_entry_text(version)
    applications = os.path.join(appdir, "usr", "share", "applications")
    icons = os.path.join(appdir, "usr", "share", "icons", "hicolor",
                         "%dx%d" % (APPIMAGE_ICON_SIZE, APPIMAGE_ICON_SIZE),
                         "apps")
    os.makedirs(applications, exist_ok=True)
    os.makedirs(icons, exist_ok=True)
    for target in (os.path.join(appdir, APPIMAGE_DESKTOP_FILE),
                   os.path.join(applications, APPIMAGE_DESKTOP_FILE)):
        with open(target, "w") as handle:
            handle.write(desktop)
    root_icon = render_editor_icon(os.path.join(appdir, APPIMAGE_ICON_FILE),
                                   runner=runner)
    shutil.copy2(root_icon, os.path.join(icons, APPIMAGE_ICON_FILE))
    # .DirIcon is the icon a file manager reads straight out of the image
    dir_icon = os.path.join(appdir, ".DirIcon")
    if os.path.lexists(dir_icon):
        os.remove(dir_icon)
    shutil.copy2(root_icon, dir_icon)
    return bundle, host


def make_appimage(stage_root, appimage_path, editor_name, version="",
                  tool="", runner=subprocess.run):
    """build the Linux single-file bundle from the SAME staged directory the
    tarball is made from, so the two assets can never hold different builds.

    Returns the image path, or "" when appimagetool is not on this machine - a
    hand run without it still produces the tarball, and the pipeline resolves
    the tool in a step of its own so a night can never publish without the
    image."""
    tool = tool or resolve_appimagetool()
    if not tool:
        warn("no appimagetool (pass --appimagetool, set " + APPIMAGETOOL_ENV
             + ", or put one on PATH - a pinned release is downloaded by the "
               "nightly job; see Docs/nightly-builds.md) - no AppImage was "
               "built (the .tar.gz is unaffected)")
        return ""
    appdir_parent = tempfile.mkdtemp(prefix="orkige-appdir-",
                                     dir=os.path.dirname(
                                         os.path.abspath(appimage_path)))
    appdir = os.path.join(appdir_parent, "Orkige.AppDir")
    try:
        bundle, host = build_appdir(stage_root, appdir, editor_name, version,
                                    runner=runner)
        log("AppImage carries %d librar%s: %s"
            % (len(bundle), "y" if len(bundle) == 1 else "ies",
               ", ".join(soname for soname, _ in bundle) or "none"))
        log("AppImage leaves %d to the host: %s"
            % (len(host), ", ".join("%s (%s)" % pair for pair in host)
               or "none"))
        if os.path.exists(appimage_path):
            os.remove(appimage_path)
        environment = dict(os.environ)
        # appimagetool is itself an AppImage: run it the way a machine without
        # FUSE has to, so the packaging never depends on FUSE either
        environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
        environment["ARCH"] = appimage_arch()
        orkige_export.run(appimagetool_command(tool, appdir, appimage_path),
                          env=environment)
    finally:
        shutil.rmtree(appdir_parent, ignore_errors=True)
    if not os.path.isfile(appimage_path):
        fail("appimagetool reported success but wrote no '%s'" % appimage_path)
    os.chmod(appimage_path, 0o755)
    return appimage_path


def package(platform, build_dir, commit, date, output_dir, version="",
            since="", repo=REPO_ROOT, signing=None, appimagetool=""):
    """stage, describe and archive one platform's editor build. `version` is
    the ordered identity (composed here when the caller passes none, so a hand
    run needs no extra argument); `since` is the previous nightly's commit -
    the changelog's lower bound; `signing` is what this run can put behind a
    macOS build (ad-hoc when there is no certificate, and it says so);
    `appimagetool` is the Linux bundle's packer (resolved from the environment
    when the caller names none)."""
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
    if platform == "linux":
        # the oldest glibc this build can run on, MEASURED on the binary rather
        # than assumed from the build image. It is the AppImage's real floor -
        # the one library family the image deliberately does not carry - and the
        # limitations file points its reader at this line.
        floor = binary_glibc_floor(staged_editor)
        extra_fields.append(("glibc-floor", floor or "unknown"))
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
    # the archive ROOT carries these for the person who downloaded it, and the
    # editor's RESOURCE root carries the same three files for the editor
    # itself: the About box reads CHANGELOG.md through the one resource
    # locator, which resolves relative to SDL_GetBasePath (the bundle's
    # Resources on macOS, share/orkige elsewhere), so both layouts have to hold
    # a copy or a downloaded editor has nothing to show
    targets = [stage_root]
    if platform == "macos":
        targets.append(os.path.join(stage_root, MACOS_APP_NAME, "Contents",
                                    "Resources"))
    else:
        targets.append(os.path.join(stage_root, FLAT_RESOURCE_DIR))
    for target in targets:
        os.makedirs(target, exist_ok=True)
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
    if platform == "linux":
        installable = make_appimage(
            stage_root,
            os.path.join(output_dir, appimage_name(platform, commit, version)),
            os.path.basename(staged_editor), version, appimagetool)
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
                          os.path.join(resources, "VERSION"),
                          # what the editor's About box reads back
                          os.path.join(resources, CHANGELOG_FILE)]
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
        # what the editor's About box reads back
        expected_files.append(os.path.join(ui_font_dir, CHANGELOG_FILE))
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
    reported = check_reported_identity([editor, "--version"], commit, version,
                                       runner)
    log("OK " + root)
    return reported


def check_reported_identity(argv, commit="", version="",
                            runner=subprocess.run):
    """run a binary (or a container that starts one) and hold its self-report
    to the identity this packaging composed. The ONE place that comparison is
    made, so the archive and the single-file bundle are held to it equally."""
    result = runner(argv, capture_output=True, text=True)
    reported = (result.stdout or "").strip()
    if result.returncode != 0:
        fail("'%s --version' exited %d: %s"
             % (argv[0], result.returncode,
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


def verify_appimage(appimage_path, commit="", version="",
                    runner=subprocess.run):
    """the single-file bundle's own smoke test, and the one that proves the
    reason it exists.

    Three verdicts, none of them "the file is there":

    1. it RUNS: the image is executed and asked for its identity, which has to
       be the commit and the ordered version this packaging composed. An
       AppImage that cannot start is a download that cannot start.
    2. it is COMPLETE: the image is extracted and put through the same layout
       check the unpacked tarball gets, plus the three files the format itself
       needs (AppRun, the desktop entry and the icon it names).
    3. it is SELF-CONTAINED: the editor inside the extracted image is asked
       where it resolves each of its libraries FROM, with the image's own lib
       directory in front of the loader's path exactly as the AppRun puts it.
       Every library the inclusion rule bundles has to resolve INSIDE the
       image - which is what makes a host that does not have it a non-event,
       because the host's copy is never consulted - and no library the rule
       leaves to the machine may resolve inside it, because a bundled driver
       or libc would be the opposite failure.

    Everything runs through --appimage-extract-and-run, so no step here needs
    FUSE, which some current distributions no longer ship."""
    appimage_path = os.path.abspath(appimage_path)
    if not os.path.isfile(appimage_path):
        fail("no AppImage at '%s'" % appimage_path)
    if not os.access(appimage_path, os.X_OK):
        # a download nobody can start: the format's whole contract is chmod +x
        fail("'%s' is not executable" % appimage_path)
    reported = check_reported_identity(
        [appimage_path, "--appimage-extract-and-run", "--version"],
        commit, version, runner)
    workspace = tempfile.mkdtemp(prefix="orkige-appimage-")
    try:
        orkige_export.run([appimage_path, "--appimage-extract"], cwd=workspace)
        root = os.path.join(workspace, "squashfs-root")
        if not os.path.isdir(root):
            fail("--appimage-extract wrote no squashfs-root")
        problems = []
        for name in ("AppRun", APPIMAGE_DESKTOP_FILE, APPIMAGE_ICON_FILE):
            if not os.path.isfile(os.path.join(root, name)):
                problems.append("missing %s" % name)
        apprun = os.path.join(root, "AppRun")
        if os.path.isfile(apprun) and not os.access(apprun, os.X_OK):
            problems.append("AppRun is not executable")
        editor, layout_problems = verify_layout(root, "linux")
        problems.extend(layout_problems)
        for problem in problems:
            print("orkige_nightly_package: APPIMAGE: " + problem, flush=True)
        if problems:
            fail("the AppImage is incomplete (%d problems)" % len(problems))
        lib_dir = os.path.join(root, APPIMAGE_LIB_DIR)
        resolved = parse_ldd(binary_libraries(editor, lib_dir, runner))
        inside = []
        outside = []
        for soname, path in resolved:
            family = host_library_family(soname)
            in_image = bool(path) and os.path.realpath(path).startswith(
                os.path.realpath(root) + os.sep)
            if family:
                if in_image:
                    outside.append("%s is bundled but must come from the host "
                                   "(%s)" % (soname, host_library_reason(family)))
                continue
            if in_image:
                inside.append(soname)
            else:
                outside.append("%s resolves to '%s' - outside the image, so a "
                               "machine without it cannot start this download"
                               % (soname, path or "nothing"))
        for problem in outside:
            print("orkige_nightly_package: APPIMAGE: " + problem, flush=True)
        if outside:
            fail("the AppImage is not self-contained (%d problems)"
                 % len(outside))
        log("the AppImage resolves %d librar%s from inside itself: %s"
            % (len(inside), "y" if len(inside) == 1 else "ies",
               ", ".join(inside) or "none"))
    finally:
        shutil.rmtree(workspace, ignore_errors=True)
    log("OK " + appimage_path)
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
    parser.add_argument("--appimagetool", default="",
                        help="linux: the appimagetool binary that packs the "
                             "single-file bundle, else the env "
                             + APPIMAGETOOL_ENV + " or one on PATH")
    parser.add_argument("--verify-appimage", default="",
                        help="run a Linux AppImage, extract it and verify that "
                             "it is complete AND resolves its bundled "
                             "libraries from inside itself")
    parser.add_argument("--identity", action="store_true",
                        help="print this build's ordered version as "
                             "`key=value` lines (the pipeline feeds them to "
                             "every job, so the value is composed ONCE)")
    parser.add_argument("--changelog", action="store_true",
                        help="print the changelog section for --since..--commit")
    parser.add_argument("--changelog-out", default="",
                        help="write the changelog section to this file too")
    parser.add_argument("--history", action="store_true",
                        help="print the FULL-history changelog: every commit "
                             "grouped by the day it landed, a day that "
                             "published a nightly headed by that build's "
                             "ordered version and every other day by its date")
    parser.add_argument("--history-out", default="",
                        help="write the full-history changelog to this file "
                             "too")
    parser.add_argument("--published-tag", default="",
                        help="a dated release tag to count as published on top "
                             "of the ones this repository carries, made from "
                             "--commit. The publish job passes tonight's: the "
                             "document is an asset of that release, so it is "
                             "written before the release and its tag exist")
    parser.add_argument("--checksum", default="",
                        help="write the %s sidecar for one file (the same "
                             "writer every archive's sidecar comes from)"
                             % CHECKSUM_SUFFIX)
    parser.add_argument("--verify-checksums", default="",
                        help="check every archive in a directory of release "
                             "assets against its %s file" % CHECKSUM_SUFFIX)
    parser.add_argument("--prune-tags", default="",
                        help="read release tags (one per line, `-` for stdin) "
                             "and print the DATED nightly releases past the "
                             "keep count, oldest first - nothing else is ever "
                             "a candidate")
    parser.add_argument("--keep", type=int, default=DATED_RELEASES_KEPT,
                        help="--prune-tags: how many dated releases survive "
                             "(default %d)" % DATED_RELEASES_KEPT)
    parser.add_argument("--protect", default="",
                        help="--prune-tags: a tag that survives whatever the "
                             "count says (the night's own)")
    parser.add_argument("--selftest", action="store_true",
                        help="run the packaging self-checks and exit")
    parser.add_argument("--selftest-dmg", action="store_true",
                        help="build and mount a real disk image from a "
                             "synthetic app (needs hdiutil; exits 77 without "
                             "it)")
    parser.add_argument("--selftest-appimage", action="store_true",
                        help="assemble and pack a real AppImage from a "
                             "synthetic AppDir, then unpack it and check where "
                             "it resolves its libraries (needs Linux and "
                             "appimagetool; exits 77 without either)")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return
    if args.selftest_dmg:
        selftest_dmg()
        return
    if args.selftest_appimage:
        selftest_appimage()
        return
    if args.verify_dmg:
        verify_dmg(args.verify_dmg)
        return
    if args.verify_appimage:
        verify_appimage(args.verify_appimage, args.commit, args.version)
        return
    if args.identity:
        # the ONE composition, printed for whoever stamps, names and publishes
        date = args.date or today()
        version = args.version or nightly_version(date, args.commit)
        print("version=%s" % version)
        print("version_token=%s" % version_filename_token(version))
        # the dated release's tag, off the SAME date - so the archive entry and
        # the version it is titled with can never name different days
        print("dated_tag=%s" % dated_release_tag(date))
        return
    if args.prune_tags:
        if args.prune_tags == "-":
            tags = sys.stdin.read().split("\n")
        else:
            with open(args.prune_tags, errors="replace") as handle:
                tags = handle.read().split("\n")
        doomed = prune_dated_releases(tags, args.keep, [args.protect])
        # the verdict goes to stderr, so the caller's `> file` holds nothing but
        # the tags it is about to delete
        sys.stderr.write("orkige_nightly_package: keeping the newest %d dated "
                         "release(s), pruning %d\n" % (args.keep, len(doomed)))
        for tag in doomed:
            print(tag)
        return
    if args.changelog:
        section = collect_changelog(args.commit, args.since, args.repo)
        if args.changelog_out:
            with open(args.changelog_out, "w") as handle:
                handle.write(section)
        print(section, end="")
        return
    if args.history:
        # the handed-in tag names the commit that was built - when --commit is
        # a real sha. A symbolic one (`HEAD`) names no commit, and the day's own
        # newest is the right identity for it anyway, so the tag is left bare.
        built = (args.commit or "").strip()
        built = built if re.match(r"^[0-9a-f]{7,40}$", built) else ""
        extra = ["%s\t\t%s" % (args.published_tag, built)] \
            if args.published_tag else []
        document = collect_history(args.commit, args.repo, extra_tags=extra)
        if args.history_out:
            with open(args.history_out, "w") as handle:
                handle.write(document)
        print(document, end="")
        return
    if args.checksum:
        log("OK " + write_checksum(args.checksum))
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
                                  args.ad_hoc_sign),
            resolve_appimagetool(args.appimagetool))


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

    # --- the dated archive: which tags may EVER be deleted ---------------
    # the tag comes off the same build date the version orders by
    assert dated_release_tag("2026-07-30") == "nightly-20260730"
    assert dated_release_tag("20260730") == "nightly-20260730"
    assert dated_release_tag("2026-7-3") == ""
    assert dated_release_tag("") == ""
    # THE SHARP EDGE. Only `nightly-YYYYMMDD` on a real calendar day is a
    # candidate - the rolling release, a stable version tag, a tag that merely
    # starts like ours and anything a person made are not, and no keep count
    # can turn them into one.
    assert is_dated_release_tag("nightly-20260730")
    assert is_dated_release_tag(" nightly-20260101 ")   # a listing's whitespace
    for decoy in ("nightly", "v2.0.0", "v2.0.0-rc1", "nightly-2026",
                  "nightly-20260731-rc1", "nightly-latest", "release-20260731",
                  "nightly-2026073", "nightly-202607311", "nightly_20260731",
                  "Nightly-20260731", "milestone", "", "   "):
        assert not is_dated_release_tag(decoy), decoy
    # a date-shaped tag that names no real day is somebody else's, not ours
    assert not is_dated_release_tag("nightly-20261332")
    assert not is_dated_release_tag("nightly-20260230")

    # a realistic listing: eighteen nights, the rolling release, a stable tag
    # and two things a person made
    listing = ["nightly", "v2.0.0", "v1.9.0", "nightly-2026", "docs-freeze",
               "nightly-20260731-rc1"]
    nights = ["nightly-202607%02d" % day for day in range(14, 32)]
    listing.extend(reversed(nights))          # newest first, as gh lists them
    doomed = prune_dated_releases(listing, keep=14, protect=["nightly-20260731"])
    # exactly the four oldest dated ones, oldest FIRST (the deletion order the
    # job logs), and nothing else in the listing is even considered
    assert doomed == ["nightly-20260714", "nightly-20260715",
                      "nightly-20260716", "nightly-20260717"], doomed
    for survivor in ("nightly", "v2.0.0", "v1.9.0", "nightly-2026",
                     "docs-freeze", "nightly-20260731-rc1"):
        assert survivor not in doomed, survivor
    # the newest 14 stay, tonight's among them
    kept = [tag for tag in nights if tag not in doomed]
    assert len(kept) == 14 and kept[-1] == "nightly-20260731", kept
    # an archive not yet at the count loses nothing
    assert prune_dated_releases(listing[:6] + nights[-3:], keep=14) == []
    # tonight's entry is REPLACED, never pruned: a second run of the same day
    # sees its own tag already listed and still keeps it
    same_day = prune_dated_releases(listing, keep=1,
                                    protect=["nightly-20260731"])
    assert "nightly-20260731" not in same_day, same_day
    assert len(same_day) == 17, len(same_day)
    # ... and it survives even a keep count of zero, which is the whole point
    # of protecting it explicitly rather than trusting the ordering
    assert "nightly-20260731" not in prune_dated_releases(
        listing, keep=0, protect=["nightly-20260731"])
    # a duplicate listing entry is one release
    assert prune_dated_releases(["nightly-20260101", "nightly-20260101"],
                                keep=0) == ["nightly-20260101"]
    # no protection asked for is no protection given - and still only ever
    # reaches dated tags
    assert prune_dated_releases(listing, keep=0) == sorted(nights)

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

    # --- the FULL history: the eras -------------------------------------
    # ONE boundary list, so both sides of every boundary are checkable here
    assert history_era("2010-09-15") == ""          # before the first commit
    assert history_era("2010-09-16") == "0.1.0"     # the FIRST commit
    assert history_era("2010-11-04") == "0.1.0"
    assert history_era("2010-11-05") == "0.2.0 — Watermaze"
    assert history_era("2011-02-27") == "0.2.0 — Watermaze"
    assert history_era("2011-02-28") == "0.3.0 — Think Blue"
    assert history_era("2011-05-31") == "0.3.0 — Think Blue"
    assert history_era("2011-06-01") == "1.0.0 — Pudding Panic"
    assert history_era("2012-11-23") == "1.0.0 — Pudding Panic"
    assert history_era("2026-07-06") == "1.0.0 — Pudding Panic"
    assert history_era("2026-07-07") == "2.0.0-pre — Editor"
    assert history_era("2026-07-30") == "2.0.0-pre — Editor"
    # from the day the channel began a day names a real build or nothing: no
    # era label is invented over a period that has actual version identities
    assert history_era(NIGHTLY_ERA_START) == ""     # 2026-07-31
    assert history_era("2026-08-01") == ""
    assert history_era("not-a-date") == "" and history_era("") == ""
    assert history_era(None) == ""
    # every label the table composes is one the heading grammar accepts
    assert ERA_LABELS[0] == "0.1.0", ERA_LABELS
    for label in ERA_LABELS:
        assert HISTORY_HEADING_RE.match(label + ERA_HEADING_SEPARATOR
                                        + "2011-04-12"), label
    # the two ends of the REAL history, read from git rather than assumed
    if os.path.exists(os.path.join(REPO_ROOT, ".git")):
        ends, ends_ok = git_log_history(REPO_ROOT, "HEAD")
        real_days = [date for _sha, date, _subject in parse_log_history(ends)]
        if ends_ok and real_days:
            # the newest commit's day always resolves to a known label (the
            # nightly period's "" among them); the OLDEST is only the first
            # commit in a clone that carries the whole history
            assert history_era(real_days[0]) in ("",) + ERA_LABELS, real_days[0]
            if not git_is_shallow(REPO_ROOT):
                assert real_days[-1] == HISTORY_ERAS[0][0], real_days[-1]
                assert history_era(real_days[-1]) == "0.1.0", real_days[-1]

    # --- the FULL history: which days published --------------------------
    # the tag shape is the whole test, so a listing full of realistic decoys
    # yields exactly the two real archive entries
    tag_lines = ["nightly",                     # the ROLLING tag, not a day
                 "nightly-20260731\t\tbbbbbbbbb",     # a lightweight tag
                 "nightly-20260730\tddddddddd\tttttttttt",   # an annotated one
                 "v2.0.0",                      # a stable release tag
                 "nightly-2026",                # merely starts like one
                 "nightly-20260731-rc1",        # ...and so does this
                 "nightly-20260230",            # a day that does not exist
                 "PuddingPanic-Appstore-version-1.1",
                 ""]
    days = published_days(tag_lines)
    assert sorted(days) == ["20260730", "20260731"], days
    assert days["20260731"] == "bbbbbbbbb"      # the commit that was built
    # an annotated tag reports the commit it dereferences to, never the tag
    # object - the identity must name the build's commit
    assert days["20260730"] == "ddddddddd", days
    # a tag with no commit beside it still marks its day: the tag is what
    # proves the build, the commit only sharpens the identity
    assert published_days(["nightly-20260731"]) == {"20260731": ""}
    assert published_days([]) == {} and published_days(None) == {}

    # --- the FULL history: parsing, grouping, the version per group -----
    history_lines = ("ccccccccc\t2026-08-01\tFourth: rest\n"
                     "bbbbbbbbb\t2026-07-31\tThird: rest\n"
                     "ddddddddd\t2026-07-31\tSecond: rest\n"
                     "aaaaaaaaa\t2012-11-23\tFirst: rest\n"
                     "\n"
                     "malformed line with no tabs\n")
    parsed = parse_log_history(history_lines)
    assert parsed == [("ccccccccc", "2026-08-01", "Fourth: rest"),
                      ("bbbbbbbbb", "2026-07-31", "Third: rest"),
                      ("ddddddddd", "2026-07-31", "Second: rest"),
                      ("aaaaaaaaa", "2012-11-23", "First: rest")], parsed
    # only 2026-07-31 published, and its build was made from `ddddddddd` - an
    # older commit of that day, because two more landed after the build ran
    published = {"20260731": "ddddddddd"}
    groups = history_groups(parsed, base="2.0.0", published=published)
    assert [group.date for group in groups] == ["2026-08-01", "2026-07-31",
                                                "2012-11-23"]
    assert [len(group.entries) for group in groups] == [1, 2, 1]
    # the published day's version is composed by the SAME function the pipeline
    # names its artifacts with, from the commit the TAG names - so a heading
    # here and that binary's own self-report are the same string
    assert groups[1].published
    assert groups[1].version == nightly_version("2026-07-31", "ddddddddd",
                                                "2.0.0")
    assert groups[1].version == "2.0.0-nightly.20260731+ddddddddd"
    assert groups[1].heading == groups[1].version
    # a day that published NOTHING composes no version at all - it is a date,
    # carrying its retroactive era where one applies
    assert not groups[0].published and groups[0].version == ""
    assert groups[0].heading == "2026-08-01"            # the nightly period
    assert not groups[2].published
    assert groups[2].era == "1.0.0 — Pudding Panic"
    assert groups[2].heading == "1.0.0 — Pudding Panic — 2012-11-23"
    # with no tag evidence at all, not even that day is marked
    unmarked = history_groups(parsed, base="2.0.0")
    assert not any(group.published for group in unmarked)
    assert unmarked[1].heading == "2026-07-31"
    # a group whose date is no date at all is headed by whatever git said
    undated = history_groups([("aaaaaaaaa", "not-a-date", "Subject: rest")])
    assert undated[0].version == "" and undated[0].era == ""
    assert undated[0].heading == "not-a-date"

    # --- the FULL history: the document ---------------------------------
    rendered = history_markdown(groups)
    assert rendered.startswith("# Changelog")
    assert HISTORY_INTRO in rendered
    # the intro says out loud that the era labels are retroactive - the whole
    # point of the document is that it never invents a release
    assert "applied in hindsight" in HISTORY_INTRO
    assert "4 commits across 3 days." in rendered, rendered
    assert "## 2.0.0-nightly.20260731+ddddddddd" in rendered, rendered
    assert "*2026-07-31 - 2 commits - published nightly build*" in rendered, \
        rendered
    assert "## 2026-08-01" in rendered and "*2026-08-01 - 1 commit*" in rendered
    assert "## 1.0.0 — Pudding Panic — 2012-11-23" in rendered, rendered
    assert "*2012-11-23 - 1 commit*" in rendered, rendered
    # a day that published nothing never says it did
    assert rendered.count("published nightly build") == 1, rendered
    assert "- Third (`bbbbbbbbb`)" in rendered
    assert "- First (`aaaaaaaaa`)" in rendered
    # newest day first, and a day's commits in the order git emitted them
    assert rendered.index("## 2026-08-01") < \
        rendered.index("## 2.0.0-nightly.20260731") < \
        rendered.index("## 1.0.0 — Pudding Panic — 2012-11-23")
    assert rendered.index("- Third (") < rendered.index("- Second (")
    # the completeness note: silent on a full history, explicit otherwise
    assert history_note(True, False, 3) == ""
    assert "shallow checkout" in history_note(True, True, 3)
    assert "3 commits" in history_note(True, True, 3)
    assert "1 commit that clone carried" in history_note(True, True, 1)
    assert "No commit history was available" in history_note(False, False)
    empty = history_markdown([], note=history_note(False, False))
    assert "No commits to list." in empty
    assert "No commit history was available" in empty

    # --- the FULL history: the git seam ---------------------------------
    class FakeHistoryGit:
        """a stand-in git answering all three questions the history asks"""

        def __init__(self, log_out, shallow=False, returncode=0, tags=(),
                     tags_returncode=0):
            self.log_out = log_out
            self.shallow = shallow
            self.returncode = returncode
            self.tags = tuple(tags)
            self.tags_returncode = tags_returncode
            self.log_argv = []
            self.tag_argv = []

        def __call__(self, argv, **_kwargs):
            if "rev-parse" in argv:
                return FakeLog("true\n" if self.shallow else "false\n")
            if "tag" in argv:
                self.tag_argv = argv
                return FakeLog("".join(line + "\n" for line in self.tags),
                               returncode=self.tags_returncode)
            self.log_argv = argv
            return FakeLog(self.log_out, returncode=self.returncode)

    full_git = FakeHistoryGit(history_lines,
                              tags=["nightly", "nightly-20260731\t\tddddddddd"])
    full = collect_history("HEAD", REPO_ROOT, full_git)
    assert "%cd" in " ".join(full_git.log_argv), full_git.log_argv
    # the whole history, never a window: this document IS the complete record
    assert not any(arg.startswith("-n") for arg in full_git.log_argv), \
        full_git.log_argv
    # the tags are asked for by the dated prefix, and the answer carries the
    # commit each tag names (the dereferenced one for an annotated tag)
    assert DATED_TAG_PREFIX + "*" in full_git.tag_argv, full_git.tag_argv
    assert any("objectname" in arg for arg in full_git.tag_argv), \
        full_git.tag_argv
    # ...though a caller may ask for just the tip (the site's staleness stamp)
    tip_git = FakeHistoryGit(history_lines)
    git_log_history(REPO_ROOT, "HEAD", limit=1, runner=tip_git)
    assert "-n1" in tip_git.log_argv, tip_git.log_argv
    assert "4 commits across 3 days." in full
    assert "shallow" not in full
    # the ONE day the tags prove is headed by its build; the others are dates
    assert "## 2.0.0-nightly.20260731+ddddddddd" in full, full
    assert "## 2026-08-01" in full and \
        "## 1.0.0 — Pudding Panic — 2012-11-23" in full, full
    assert full.count("published nightly build") == 1, full
    # tags git could not answer for: no day is marked, and the document SAYS so
    # rather than reading as "nothing was ever published"
    untagged = collect_history("HEAD", REPO_ROOT,
                               FakeHistoryGit(history_lines,
                                              tags_returncode=128))
    assert "dated release tags could not be read" in untagged, untagged
    assert "published nightly build" not in untagged, untagged
    assert "## 2026-07-31" in untagged, untagged
    # no tags at all is not an error: a repository whose archive was pruned
    # empty simply marks nothing, silently
    pruned = collect_history("HEAD", REPO_ROOT, FakeHistoryGit(history_lines))
    assert "dated release tags could not be read" not in pruned, pruned
    assert "published nightly build" not in pruned, pruned
    # tonight's own tag does not exist yet when the publish job writes this
    # asset, so it is handed in - the one night a build describes must not be
    # the one night it leaves unmarked
    tonight = collect_history("HEAD", REPO_ROOT, FakeHistoryGit(history_lines),
                              extra_tags=["nightly-20260801\t\tccccccccc"])
    assert "## 2.0.0-nightly.20260801+ccccccccc" in tonight, tonight
    assert tonight.count("published nightly build") == 1, tonight
    shallow = collect_history("HEAD", REPO_ROOT,
                              FakeHistoryGit(history_lines, shallow=True))
    assert "shallow checkout" in shallow, shallow
    assert "4 commits across 3 days." in shallow
    absent = collect_history("HEAD", REPO_ROOT, always_fails)
    assert "No commit history was available" in absent
    assert "No commits to list." in absent
    # against the REAL repository: every day heading obeys the ONE grammar -
    # an ordered identity only where a dated release tag proved a build, a
    # dated heading with its era otherwise - and the days run newest first
    if os.path.exists(os.path.join(REPO_ROOT, ".git")):
        real_history = collect_history("HEAD", REPO_ROOT)
        headings = re.findall(r"^## (.+)$", real_history, re.M)
        assert headings, real_history[:200]
        for heading in headings:
            assert HISTORY_HEADING_RE.match(heading), heading
        # a version identity may appear ONLY for a day the tags name
        marked = published_days(git_release_tags(REPO_ROOT)[0])
        versions = [h for h in headings if "+" in h]
        assert len(versions) <= len(marked), (versions, sorted(marked))
        for version in versions:
            assert version.split(".")[-1].split("+")[0] in marked, version
        # the date under each heading is what orders the document
        dates = HISTORY_SUMMARY_DATE_RE.findall(real_history)
        assert len(dates) == len(headings), (len(dates), len(headings))
        assert dates == sorted(dates, reverse=True), dates[:5]
        # MORE than one day is only a fair expectation of a clone that carries
        # the history: every build job checks out shallow (only the site jobs
        # ask for the full log, because only they publish it), and a clone with
        # one day in it says so rather than being wrong
        if not git_is_shallow(REPO_ROOT):
            assert len(headings) > 1, headings[:5]
            # this history predates the nightly channel by years, so a full
            # clone always carries era-headed days
            assert any(ERA_HEADING_SEPARATOR in h for h in headings), \
                headings[:5]
        else:
            log("shallow checkout - the multi-day leg of the real-history "
                "check is skipped, the rest still holds")

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
                         "Orkige-macos-%s.dmg" % token,
                         "Orkige-linux-%s.tar.gz" % token,
                         "Orkige-linux-%s.AppImage" % token):
                open(os.path.join(downloads, name), "w").close()
            with open(os.path.join(temp, "changelog.md"), "w") as handle:
                handle.write(bounded)
            environ = dict(os.environ,
                           RESULT_MACOS="success", RESULT_LINUX="success",
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
            # Linux ships the same pair: the single-file bundle a person runs
            # and the tarball an updater unpacks
            assert ("| Linux (x86_64) | `Orkige-linux-%s.AppImage` | "
                    "`Orkige-linux-%s.tar.gz` | success |" % (token, token)) \
                in body, body
            # a platform that produced nothing is called out with its job
            # result rather than silently missing
            assert ("| Windows (x64) | not produced | not produced | skipped |"
                    in body), body
            assert ordered in body and bounded.strip() in body
            # the sidecar is the integrity story the notes point at
            assert CHECKSUM_SUFFIX in body, body
            # the full history rides as its own asset, and the notes say so:
            # "what has ever landed" must not need a download and an unpack
            assert "FULL history" in body, body

            # ... and the step that produces it does, run from THIS repository
            # against a scratch assets directory, with the sidecar the publish
            # step is about to verify
            history_script = workflow_step_script(
                NIGHTLY_WORKFLOW, "Generate the full-history changelog asset")
            assert history_script, "the publish job must generate the asset"
            run = subprocess.run(["bash", "-c", history_script], cwd=REPO_ROOT,
                                 env=dict(environ, SHA="HEAD",
                                          DATED_TAG=dated_release_tag(today()),
                                          ASSETS=downloads),
                                 capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            asset = os.path.join(downloads, CHANGELOG_FILE)
            assert os.path.isfile(asset), sorted(os.listdir(downloads))
            assert not checksum_mismatch(asset), checksum_mismatch(asset)
            with open(asset) as handle:
                attached = handle.read()
            assert attached.startswith("# Changelog"), attached[:80]
            if os.path.exists(os.path.join(REPO_ROOT, ".git")):
                # the asset carries the same day headings the site page does:
                # every one obeys the ONE grammar, and none of them invents a
                # version for a day that published nothing
                attached_headings = re.findall(r"^## (.+)$", attached, re.M)
                assert attached_headings, attached[:400]
                for heading in attached_headings:
                    assert HISTORY_HEADING_RE.match(heading), heading
                # tonight's tag was handed in, so the night this build
                # describes is marked in it - the release does not attach a
                # document that leaves its own day blank
                if compact_date(today()) in \
                        {compact_date(date) for _sha, date, _subject
                         in parse_log_history(git_log_history(REPO_ROOT)[0])}:
                    assert "published nightly build" in attached, \
                        attached[:600]
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

    # --- publishing the two releases, and pruning the archive -------------
    # Both are shell in the publish job, and a mistake in either deletes
    # something on a real repository - so the checks drive THOSE scripts,
    # lifted out of the yaml and run against a `gh` that records the exact argv
    # it was handed instead of talking to GitHub. What is asserted is the
    # sequence: which tag is replaced, which is added, and - the sharp edge -
    # that no tag outside the dated shape is ever passed to a delete.
    publish_script = workflow_step_script(
        NIGHTLY_WORKFLOW, "Create the rolling and dated prereleases")
    prune_script = workflow_step_script(NIGHTLY_WORKFLOW,
                                        "Prune the older dated releases")
    assert publish_script, "the publish job must create the releases"
    assert prune_script, "the publish job must prune the dated archive"
    if not usable_bash():
        log("no working bash - the publish/prune leg is skipped")
    else:
        GH_STUB = """#!/bin/sh
# a stand-in for the GitHub CLI: it records the exact argv it was handed - one
# argument per line, `===` after each call - and answers a listing from a file
for arg in "$@"; do printf '%s\\n' "$arg" >> "$GH_LOG"; done
printf '===\\n' >> "$GH_LOG"
if [ "${2:-}" = "list" ]; then
  [ -z "${GH_LIST_FAILS:-}" ] || exit 1
  cat "$GH_TAGS"
  exit 0
fi
if [ "${2:-}" = "delete" ] && [ -n "${GH_DELETE_FAILS:-}" ]; then
  echo "gh: the release could not be deleted" >&2
  exit 1
fi
exit 0
"""

        def gh_calls(log_path):
            """the recorded invocations, each as its argv list"""
            if not os.path.isfile(log_path):
                return []
            with open(log_path) as handle:
                text = handle.read()
            calls = []
            for chunk in text.split("===\n"):
                if not chunk:
                    continue
                argv = chunk.split("\n")
                if argv and argv[-1] == "":
                    argv.pop()      # the trailing newline, not an empty arg
                calls.append(argv)
            return calls

        with tempfile.TemporaryDirectory() as temp:
            bindir = os.path.join(temp, "bin")
            os.makedirs(bindir)
            stub = os.path.join(bindir, "gh")
            with open(stub, "w") as handle:
                handle.write(GH_STUB)
            os.chmod(stub, 0o755)
            base = dict(os.environ,
                        PATH=bindir + os.pathsep + os.environ.get("PATH", ""),
                        GITHUB_REPOSITORY="orkitec/orkige",
                        GITHUB_STEP_SUMMARY=os.path.join(temp, "summary.md"))

            # --- the two releases, from ONE set of assets -----------------
            stage = os.path.join(temp, "publish")
            downloads = os.path.join(stage, "downloads")
            os.makedirs(downloads)
            token = version_filename_token(ordered)
            names = ["Orkige-linux-%s.tar.gz" % token,
                     "Orkige-linux-%s.tar.gz%s" % (token, CHECKSUM_SUFFIX)]
            for name in names:
                open(os.path.join(downloads, name), "w").close()
            with open(os.path.join(stage, "notes.md"), "w") as handle:
                handle.write("notes\n")
            gh_log = os.path.join(temp, "publish.log")
            environ = dict(base, GH_LOG=gh_log, GH_TAGS="",
                           SHA="dea551f9e0abcdef1234", SHORT_SHA="dea551f9e",
                           BUILD_DATE="2026-07-31", VERSION=ordered,
                           DATED_TAG="nightly-20260731", ASSETS="downloads")
            run = subprocess.run(["bash", "-c", publish_script], cwd=stage,
                                 env=environ, capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            calls = gh_calls(gh_log)
            assets = sorted(os.path.join("downloads", name) for name in names)
            # two releases, each replaced then created - and nothing else
            assert len(calls) == 4, calls
            # the ROLLING release is replaced wholesale and keeps its tag, so
            # the URLs a document quotes and an updater fetches do not move
            assert calls[0] == ["release", "delete", "nightly", "--yes",
                                "--cleanup-tag"], calls[0]
            assert calls[1] == ["release", "create", "nightly", "--prerelease",
                                "--target", "dea551f9e0abcdef1234", "--title",
                                "Orkige " + ordered, "--notes-file",
                                "notes.md"] + assets, calls[1]
            # ... and tonight's ARCHIVE entry carries the same assets under the
            # dated tag, replacing a same-day rerun rather than duplicating it
            assert calls[2] == ["release", "delete", "nightly-20260731",
                                "--yes", "--cleanup-tag"], calls[2]
            assert calls[3] == ["release", "create", "nightly-20260731",
                                "--prerelease", "--target",
                                "dea551f9e0abcdef1234", "--title",
                                "Orkige " + ordered, "--notes-file",
                                "notes.md"] + assets, calls[3]
            # the two releases hold the SAME files - that is what makes them one
            # download under two names
            assert calls[1][10:] == calls[3][10:]
            summary = open(base["GITHUB_STEP_SUMMARY"]).read()
            assert "releases/tag/nightly\n" in summary, summary
            assert "releases/tag/nightly-20260731" in summary, summary

            # a build whose date composed no tag still publishes the rolling
            # release, and says out loud that it left no archive entry
            os.remove(gh_log)
            run = subprocess.run(["bash", "-c", publish_script], cwd=stage,
                                 env=dict(environ, DATED_TAG=""),
                                 capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            assert "::warning::" in run.stdout, run.stdout
            calls = gh_calls(gh_log)
            assert len(calls) == 2 and calls[1][2] == "nightly", calls

            # --- the prune, over a realistic listing ---------------------
            # eighteen nights plus the decoys a real repository carries. The
            # script runs from the repository (it calls the packager for the
            # decision); everything it writes goes to WORK.
            work = os.path.join(temp, "prune")
            os.makedirs(work)
            tags_file = os.path.join(temp, "tags.txt")
            protected = ("nightly", "v2.0.0", "v1.9.0", "nightly-2026",
                         "nightly-20260731-rc1", "docs-freeze")
            with open(tags_file, "w") as handle:
                handle.write("\n".join(list(protected)
                                       + list(reversed(nights))) + "\n")
            gh_log = os.path.join(temp, "prune.log")
            environ = dict(base, GH_LOG=gh_log, GH_TAGS=tags_file, WORK=work,
                           DATED_TAG="nightly-20260731")
            run = subprocess.run(["bash", "-c", prune_script], cwd=REPO_ROOT,
                                 env=environ, capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            calls = gh_calls(gh_log)
            assert calls[0] == ["release", "list", "--limit", "200", "--json",
                                "tagName", "--jq", ".[].tagName"], calls[0]
            deleted = [call[2] for call in calls[1:]]
            assert deleted == ["nightly-20260714", "nightly-20260715",
                               "nightly-20260716", "nightly-20260717"], deleted
            # THE INVARIANT: nothing outside the dated shape is ever handed to
            # a delete - not the rolling release, not a version tag, not a tag
            # a person made, and not tonight's own entry
            for call in calls[1:]:
                assert call[:2] == ["release", "delete"], call
                assert is_dated_release_tag(call[2]), call
                assert call[2] != "nightly-20260731", call
                for forbidden in protected:
                    assert forbidden not in call, call
            # every deletion is named in the log, so the job's output says
            # exactly what was removed
            for tag in deleted:
                assert "pruning the dated release " + tag in run.stdout, \
                    run.stdout

            # --- housekeeping never fails the night ----------------------
            # the artifacts are already published by the time this runs, so a
            # listing that cannot be fetched is a loud annotation and an exit 0
            os.remove(gh_log)
            run = subprocess.run(["bash", "-c", prune_script], cwd=REPO_ROOT,
                                 env=dict(environ, GH_LIST_FAILS="1"),
                                 capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            assert "::warning::could not list the releases" in run.stdout
            assert [call for call in gh_calls(gh_log)
                    if "delete" in call] == []
            # ... and a deletion GitHub refuses leaves that release in the
            # archive, warns, and still tries the rest
            os.remove(gh_log)
            run = subprocess.run(["bash", "-c", prune_script], cwd=REPO_ROOT,
                                 env=dict(environ, GH_DELETE_FAILS="1"),
                                 capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            assert run.stdout.count("::warning::could not delete") == 4, \
                run.stdout
            assert [call[2] for call in gh_calls(gh_log)[1:]] == \
                ["nightly-20260714", "nightly-20260715", "nightly-20260716",
                 "nightly-20260717"]

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

    # --- the Linux single-file bundle -----------------------------------
    # appimagetool runs on no machine this suite runs on, so what is asserted
    # here is every decision the packaging makes BEFORE the tool is called -
    # which is where the whole value of the artifact is decided. Building and
    # running a real image is the pipeline's own step.
    assert appimage_name("linux", "dea551f9e0", ordered) == \
        "Orkige-linux-2.0.0-nightly.20260730_dea551f9e.AppImage"
    # ... and it must not collide with the tarball an updater picks by name
    assert appimage_name("linux", "dea551f9e0", ordered) != \
        artifact_name("linux", "dea551f9e0", ordered)
    assert re.match(r"^[A-Za-z0-9._-]+$",
                    appimage_name("linux", "dea551f9e0", ordered))

    # THE INCLUSION RULE. Every library the loader resolves is bundled unless
    # it belongs to a family whose correct version is a property of the
    # machine - so these two lists are the decision, asserted by name.
    for host_only, family in (
            ("libvulkan.so.1", "driver"), ("libGL.so.1", "driver"),
            ("libGLX_mesa.so.0", "driver"), ("libGLdispatch.so.0", "driver"),
            ("libEGL.so.1", "driver"), ("libdrm.so.2", "driver"),
            ("libgbm.so.1", "driver"), ("libglapi.so.0", "driver"),
            ("libc.so.6", "libc"), ("libm.so.6", "libc"),
            ("libpthread.so.0", "libc"), ("libdl.so.2", "libc"),
            ("ld-linux-x86-64.so.2", "libc"),
            ("ld-linux-aarch64.so.1", "libc"),
            ("libnss_files.so.2", "libc"), ("linux-vdso.so.1", "libc"),
            ("libstdc++.so.6", "toolchain"), ("libgcc_s.so.1", "toolchain"),
            ("libX11.so.6", "server-client"),
            ("libX11-xcb.so.1", "server-client"),
            ("libxcb.so.1", "server-client"),
            ("libxcb-randr.so.0", "server-client"),
            ("libwayland-client.so.0", "server-client"),
            ("libasound.so.2", "server-client"),
            ("libdbus-1.so.3", "server-client")):
        assert host_library_family(host_only) == family, host_only
        assert host_library_reason(family), family
    # the family this artifact exists for: the Xt/Athena set nothing on a
    # modern desktop installs, plus the leaf libraries beside it
    for bundled in ("libXaw.so.7", "libXmu.so.6", "libXpm.so.4",
                    "libXt.so.6", "libICE.so.6", "libSM.so.6",
                    "libbsd.so.0", "libmd.so.0", "libuuid.so.1",
                    "libXext.so.6", "libXrandr.so.2", "libXrender.so.1",
                    "libXcursor.so.1", "libXi.so.6", "libXau.so.6",
                    "libXdmcp.so.6", "libxkbcommon.so.0",
                    # from the same compiler as libstdc++, but a distribution
                    # installs it only on demand - and a bare one that has
                    # every other host family still has no libatomic, which is
                    # a loader error before main() runs
                    "libatomic.so.1"):
        assert host_library_family(bundled) == "", bundled
    # a prefix is not a match: libXcursor is not libX11, libmd is not libm
    assert host_library_family("libmd.so.0") == ""
    assert host_library_family("libcurl.so.4") == ""
    assert library_stem("/usr/lib/x86_64-linux-gnu/libXaw.so.7") == "libXaw"

    # ldd's three output shapes, and the plan that comes out of them
    ldd_sample = "\n".join([
        "\tlinux-vdso.so.1 (0x00007ffd8b3f8000)",
        "\tlibXaw.so.7 => /lib/x86_64-linux-gnu/libXaw.so.7 (0x00007f1a00000000)",
        "\tlibvulkan.so.1 => /build/vcpkg/lib/libvulkan.so.1 (0x00007f1a10000000)",
        "\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f1a20000000)",
        "\tlibXaw.so.7 => /lib/x86_64-linux-gnu/libXaw.so.7 (0x00007f1a00000000)",
        "\tlibmissing.so.9 => not found",
        "\t/lib64/ld-linux-x86-64.so.2 (0x00007f1a30000000)"])
    parsed = dict(parse_ldd(ldd_sample))
    assert parsed["libXaw.so.7"] == "/lib/x86_64-linux-gnu/libXaw.so.7", parsed
    assert parsed["libmissing.so.9"] == "", parsed
    assert parsed["linux-vdso.so.1"] == "", parsed
    assert parsed["ld-linux-x86-64.so.2"] == "", parsed
    bundle, host, missing = plan_bundled_libraries(ldd_sample)
    assert bundle == [("libXaw.so.7", "/lib/x86_64-linux-gnu/libXaw.so.7")], \
        bundle
    assert [soname for soname, _ in host] == [
        "ld-linux-x86-64.so.2", "libc.so.6", "libvulkan.so.1",
        "linux-vdso.so.1"], host
    # a dependency nothing on the build machine resolves is a broken tree, not
    # something to package around - it comes back named
    assert missing == ["libmissing.so.9"], missing

    # the glibc floor, read out of the binary rather than assumed from the
    # build image - and ORDERED as versions, not as strings (2.9 < 2.34)
    objdump_sample = "\n".join([
        "Version References:",
        "  required from libm.so.6:",
        "    0x0d696910 0x00 12 GLIBC_2.29",
        "  required from libc.so.6:",
        "    0x09691a75 0x00 05 GLIBC_2.2.5",
        "    0x069691b4 0x00 09 GLIBC_2.9",
        "    0x0d696913 0x00 07 GLIBC_2.34",
        "    0x06969194 0x00 04 GLIBC_PRIVATE"])
    assert glibc_version_floor(objdump_sample) == "2.34", \
        glibc_version_floor(objdump_sample)
    assert glibc_version_floor("") == ""
    assert glibc_version_floor("no version references here") == ""

    # the tool: an explicit path wins, then the environment, then PATH - the
    # same precedence the Android bundle's separate download follows. `which`
    # is injected, so this holds on a machine with no appimagetool at all.
    assert resolve_appimagetool("/tmp/t.AppImage", {},
                                which=lambda _: None) == "/tmp/t.AppImage"
    assert resolve_appimagetool("", {APPIMAGETOOL_ENV: "/env/t"},
                                which=lambda _: None) == "/env/t"
    assert resolve_appimagetool("", {},
                                which=lambda _: "/usr/bin/appimagetool") == \
        "/usr/bin/appimagetool"
    assert resolve_appimagetool("", {}, which=lambda _: None) == ""
    argv = appimagetool_command("/opt/appimagetool", "/tmp/Orkige.AppDir",
                                "/out/Orkige.AppImage")
    assert argv == ["/opt/appimagetool", "--no-appstream",
                    "/tmp/Orkige.AppDir", "/out/Orkige.AppImage"], argv
    # AppImage runtimes are per-architecture, so the arch is passed rather
    # than guessed from whatever binary the tool looks at first
    assert appimage_arch("amd64") == "x86_64"
    assert appimage_arch("arm64") == "aarch64"
    assert appimage_arch("aarch64") == "aarch64"

    # the AppDir's entry point: the bundled libraries go in FRONT of the
    # system ones (which is what makes a host without them a non-event) and
    # the editor replaces the shell, so its exit code and its signals are the
    # process's own
    apprun = apprun_text("orkige_editor")
    assert apprun.startswith("#!/bin/sh\n"), apprun
    assert 'export LD_LIBRARY_PATH="$HERE/usr/lib' in apprun, apprun
    assert '${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"' in apprun, apprun
    assert 'exec "$HERE/orkige_editor" "$@"' in apprun, apprun

    # the desktop entry: what a desktop shows once the file is integrated
    entry = desktop_entry_text(ordered)
    fields = dict(line.split("=", 1) for line in entry.splitlines()
                  if "=" in line)
    assert entry.startswith("[Desktop Entry]"), entry
    assert fields["Type"] == "Application", fields
    assert fields["Name"] == "Orkige", fields
    assert fields["Icon"] == APPIMAGE_ICON_NAME, fields
    assert fields["Categories"].endswith(";"), fields
    assert "Development" in fields["Categories"], fields
    assert fields["Exec"].startswith("orkige_editor"), fields
    assert fields["X-AppImage-Version"] == ordered, fields
    # an unversioned hand run still writes a valid entry
    assert "X-AppImage-Version=unversioned" in desktop_entry_text(""), \
        desktop_entry_text("")

    # the launcher icon comes from the generator that draws the macOS one, so
    # both containers show the same artwork
    assert os.path.isfile(EDITOR_ICON_SCRIPT), EDITOR_ICON_SCRIPT
    with tempfile.TemporaryDirectory() as temp:
        icon = render_editor_icon(os.path.join(temp, APPIMAGE_ICON_FILE), 64)
        with open(icon, "rb") as handle:
            header = handle.read(24)
        assert header[:8] == b"\x89PNG\r\n\x1a\n", header
        assert (int.from_bytes(header[16:20], "big"),
                int.from_bytes(header[20:24], "big")) == (64, 64), header

    # without the tool there is a WARNING and no image - a hand run still
    # produces the tarball, and the pipeline resolves the tool in a step of
    # its own so a night can never publish without one
    with tempfile.TemporaryDirectory() as temp:
        stage = os.path.join(temp, "Orkige-linux-abc")
        os.makedirs(stage)
        assert make_appimage(stage, os.path.join(temp, "x.AppImage"),
                             "orkige_editor", ordered, tool="",
                             runner=lambda *_a, **_k: None) == ""

    # a verdict nobody can act on is not a verdict: pointing the check at a
    # file that is not there, or at one nobody can execute, refuses by name
    with tempfile.TemporaryDirectory() as temp:
        try:
            verify_appimage(os.path.join(temp, "nothing.AppImage"))
            raise AssertionError("expected a refusal for a missing image")
        except SystemExit:
            pass
        # "not executable" is a POSIX state: Windows has no execute bit, so
        # chmod cannot take one away and the check would RUN the file instead
        # of refusing it. The leg is skipped there rather than asserting a
        # refusal the platform cannot produce.
        if os.name == "nt":
            log("no execute bit on this platform - the non-executable "
                "AppImage leg is skipped")
        else:
            unreadable = os.path.join(temp, "Orkige.AppImage")
            open(unreadable, "w").close()
            os.chmod(unreadable, 0o644)
            try:
                verify_appimage(unreadable)
                raise AssertionError(
                    "expected a refusal for a non-executable image")
            except SystemExit:
                pass

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
        # the AppImage's real floor is the one library family it does not
        # carry, so a Linux build records it - measured where a binary can be
        # measured, an honest "unknown" where the stand-in cannot
        assert re.search(r"^glibc-floor: (\d+\.\d+(\.\d+)?|unknown)$", version,
                         re.M), version
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
        # the About box reads the changelog out of the RESOURCE root, so the
        # layout check wants a copy there as well as at the archive root
        open(os.path.join(tree, FLAT_RESOURCE_DIR, CHANGELOG_FILE),
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
        for name in ("VERSION", CHANGELOG_FILE):
            # the resource root carries what the EDITOR reads back (its About
            # box shows the changelog it shipped with)
            open(os.path.join(resources, name), "w").close()
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


def selftest_appimage():
    """The Linux bundle, ASSEMBLED AND PACKED for real over a synthetic AppDir -
    the half of it that cannot be a text assertion, because what has to hold is
    what the produced image actually contains and where the loader finds it.

    The stand-in editor is a real dynamic executable off this machine (`ls`),
    which is the point: it has a genuine library closure, so the inclusion rule
    runs against something the loader really resolved instead of a fixture.

    It needs Linux AND appimagetool, and SKIPS with 77 without either rather
    than passing silently - a check that quietly does nothing is worse than no
    check at all."""
    if not sys.platform.startswith("linux") or not shutil.which("ldd"):
        log("not Linux - the AppImage self-check is skipped")
        sys.exit(77)
    tool = resolve_appimagetool()
    if not tool:
        log("no appimagetool (set " + APPIMAGETOOL_ENV + ") - the AppImage "
            "self-check is skipped")
        sys.exit(77)
    stand_in = shutil.which("ls")
    if not stand_in:
        log("no dynamic stand-in binary - the AppImage self-check is skipped")
        sys.exit(77)
    with tempfile.TemporaryDirectory() as temp:
        stage_root = os.path.join(temp, "Orkige-linux-selftest")
        resources = os.path.join(stage_root, FLAT_RESOURCE_DIR)
        os.makedirs(resources)
        editor = os.path.join(stage_root, "orkige_editor")
        shutil.copy2(stand_in, editor)
        os.chmod(editor, 0o755)
        expected, _, _ = plan_bundled_libraries(binary_libraries(editor))

        appdir = os.path.join(temp, "Orkige.AppDir")
        bundle, host = build_appdir(stage_root, appdir, "orkige_editor",
                                    "2.0.0-nightly.20260730+dea551f9e")
        assert bundle == expected, (bundle, expected)
        assert host, "every binary resolves something the host has to provide"
        # the format's own three files, and the payload beside them
        for name in ("AppRun", APPIMAGE_DESKTOP_FILE, APPIMAGE_ICON_FILE,
                     ".DirIcon", "orkige_editor"):
            assert os.path.isfile(os.path.join(appdir, name)), name
        assert os.access(os.path.join(appdir, "AppRun"), os.X_OK)
        # every bundled library is a REAL file under its soname, never the
        # symlink a distribution keeps beside the versioned file
        for soname, _ in bundle:
            path = os.path.join(appdir, APPIMAGE_LIB_DIR, soname)
            assert os.path.isfile(path) and not os.path.islink(path), soname
            assert os.path.getsize(path) > 0, soname

        image = os.path.join(temp, "Orkige-linux-selftest.AppImage")
        assert make_appimage(stage_root, image, "orkige_editor",
                             "2.0.0-nightly.20260730+dea551f9e", tool) == image
        assert os.access(image, os.X_OK), image
        # unpack what was packed - through the FUSE-free path, because that is
        # the one a machine without FUSE has - and hold it to the same
        # resolution rule verify_appimage applies to a real one: the bundled
        # names come from INSIDE, the host families do not
        workspace = os.path.join(temp, "extract")
        os.makedirs(workspace)
        orkige_export.run([image, "--appimage-extract"], cwd=workspace)
        root = os.path.join(workspace, "squashfs-root")
        assert os.path.isdir(root), root
        entry = open(os.path.join(root, APPIMAGE_DESKTOP_FILE)).read()
        assert entry == desktop_entry_text("2.0.0-nightly.20260730+dea551f9e")
        resolved = dict(parse_ldd(binary_libraries(
            os.path.join(root, "orkige_editor"),
            os.path.join(root, APPIMAGE_LIB_DIR))))
        inside = os.path.realpath(root) + os.sep
        for soname, _ in bundle:
            where = os.path.realpath(resolved.get(soname, ""))
            assert where.startswith(inside), (soname, where)
        for soname, _ in host:
            where = resolved.get(soname, "")
            assert not os.path.realpath(where).startswith(inside), \
                (soname, where)
        # the staged directory is handed back exactly as it was: the AppDir is
        # built beside it, never inside the tree the tarball is made from
        assert sorted(os.listdir(stage_root)) == ["orkige_editor", "share"], \
            os.listdir(stage_root)
    print("orkige_nightly_package: AppImage selftest OK")


if __name__ == "__main__":
    main()
