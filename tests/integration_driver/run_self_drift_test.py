#!/usr/bin/env python3
"""The self-drift gate: no pixel of a flavor moves without a commit saying so.

The cross-flavor gates compare classic against next. They answer "do the two
flavors agree", and they answer it well - but two flavors can agree perfectly
on the WRONG image. A change that moves both of them the same way, or moves
one inside the cross-flavor corridor, passes every one of them. That is not a
hypothetical: a water surface once stopped refracting the scene behind it and
travelled through the whole matrix green, because nothing in it compared a
flavor against ITS OWN PREVIOUS OUTPUT.

This driver is that missing comparison. Given two capture directories of the
SAME flavor - the previous green run's and this run's - it compares every
frame pair and requires each MOVER to be NAMED in the declaration file
`look_changes.json`. Same code, same rasterizer, same scene on both sides, so
the honest expectation is that nothing moves at all; anything that does is
either a deliberate look change (declare it) or a regression (fix it).

  previous captures ---\\
                        >--- per-shot movement ---> declared? -> pass/fail
  current captures  ---/                           moved?

WHAT COUNTS AS A SHOT

Every `.png` under the capture root, named by its root-relative path with
forward slashes: `render_facade/selfcheck_window.png`, `lake/classic.png`.
The whole uploaded tree is in scope - the facade selfcheck's frames and the
benchmark vignettes alike - because "no pixel of either flavor changes" is
the property, not "no pixel of the frames somebody remembered to list".
Diagnosis images the comparison gates themselves write (`*.diff.png`,
`*.drift.png`) are not captures and are skipped.

A shot present on one side only is a movement too, reported as `added` or
`removed`. Adding a frame to the selfcheck and deleting one are both changes
to what the flavor draws, and both belong in the commit that makes them.

THE NOISE FLOOR

Both sides come from the same rasterizer, so the floor is not a corridor
between two implementations - it is the residue of true nondeterminism (a
clock-driven effect, a thread-order-dependent accumulation). Measured by
capturing the facade selfcheck twice on one machine and comparing: every one
of its frames came back BYTE-IDENTICAL, zero pixels differing at all. The
floor below is therefore margin, not measurement, and deliberately tight:

  * `NOISE_DELTA` - a per-pixel worst-channel delta at or below this is not
    counted at all.
  * `MOVED_FRACTION` - the fraction of pixels that may exceed it before the
    shot counts as moved.
  * `LOUD_DELTA` - any single pixel above this moves the shot regardless of
    how few there are. A change can be small in area and unmistakable in
    kind, and a fraction-only rule would let a wrong object through as long
    as it was small enough.

THE DECLARATION

`tests/integration_driver/look_changes.json`:

    {
      "note": "...",
      "changes": [
        {"pattern": "render_facade/selfcheck_shadow_*.png",
         "reason": "the PSSM split distances move - Docs/materials.md",
         "flavors": ["classic"]}
      ]
    }

  * `pattern` - a shot name. With no glob metacharacter it is an EXACT
    root-relative name. With one it is a glob in the usual shell vocabulary
    (`*` any run of characters INCLUDING `/`, `?` one character, `[seq]` a
    set), which is what makes a family declarable in one line:
    `render_facade/*` for the whole facade sweep, `*water*` for every frame
    whose path names water. Case-sensitive, forward slashes always.
  * `reason` - required and non-empty. The point of the gate is that a moved
    pixel arrives with a sentence explaining it; an entry without one
    declares nothing.
  * `flavors` - optional, `["classic"]` / `["next"]` / both (the default). A
    fix that lands in one backend moves one flavor's frames, and the other
    flavor's run must not then fail on an entry that was never about it.

RESET SEMANTICS - WHY AN OLD DECLARATION FAILS

The declaration must match the moved set EXACTLY:

  * a shot that moved and is covered by no active pattern FAILS as
    UNDECLARED, named;
  * an active pattern that covers no moved shot FAILS as STALE, named.

The second half is what keeps this honest. A declaration is consumed by the
landing it belongs to: once that commit is the previous green run, the frames
it moved no longer move, and the entry that described them covers nothing.
The next run therefore fails as stale until the entry is removed - so the
cleanup is forced by the gate instead of remembered by a person, and no entry
can quietly become a permanent allowance for a frame nobody watches any more.
The steady state of `look_changes.json` is an empty `changes` list, and a
commit that changes no pixel is a commit that leaves it empty.

CAPTURES THAT CANNOT BE COMPARED

A missing or empty capture directory FAILS, on both sides - the same doctrine
the cross-flavor gates carry. A gate that compared nothing must never report
that nothing moved. What is NOT a failure is having no previous run to
compare against at all (the first run, an artifact past its retention): that
is a decision for the CALLER, which reports it as a skip with a notice naming
the reason. This gate guards drift, not archaeology.

Pure stdlib. Frames are decoded by the parity gates' own decoder and scored
through `parity_diff`, so the drift verdict and the flavor verdict are read
off the same pixels by the same code.
"""

import argparse
import fnmatch
import json
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_backend_screenshots import decode_png              # noqa: E402
import parity_diff                                              # noqa: E402

#: a per-pixel worst-channel delta at or below this is not counted
NOISE_DELTA = 2

#: how much of a frame may exceed NOISE_DELTA before the shot counts as moved
MOVED_FRACTION = 0.0002        # 0.02% - 103 pixels of a 960x540 frame

#: one pixel above this moves the shot on its own, however few there are
LOUD_DELTA = 64

#: the declaration lives beside this driver
DECLARATION = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "look_changes.json")

#: the flavor names a declaration entry may scope itself to
FLAVORS = ("classic", "next")

#: written by the comparison gates, not by a flavor - never a capture
DIAGNOSIS_SUFFIXES = (".diff.png", ".drift.png")

#: delta -> 1 above the floor, 0 at or below it; see measure_pair
OVER_FLOOR_TABLE = bytes(1 if value > NOISE_DELTA else 0 for value in range(256))


class CaptureUnusable(Exception):
    """A capture directory carries nothing to compare - refuse, never pass."""


class DeclarationInvalid(Exception):
    """The declaration file cannot be read as one - refuse, never ignore."""


# --- the declaration --------------------------------------------------------

class Change:
    """One declared look change: a pattern, its reason, its flavors."""

    def __init__(self, pattern, reason, flavors):
        self.pattern = pattern
        self.reason = reason
        self.flavors = flavors

    def applies_to(self, flavor):
        return flavor in self.flavors

    def matches(self, shot):
        """Exact name when the pattern carries no glob, else a glob match."""
        if any(ch in self.pattern for ch in "*?["):
            return fnmatch.fnmatchcase(shot, self.pattern)
        return shot == self.pattern

    def __repr__(self):
        return f"Change({self.pattern!r}, flavors={sorted(self.flavors)})"


def load_declaration(path):
    """Read look_changes.json into Changes, refusing every malformed shape.

    A declaration is the one place a moved pixel is allowed to pass, so a
    typo in it must be a loud refusal rather than an entry that silently
    covers nothing (which would read as a stale entry) or everything.
    """
    if not os.path.exists(path):
        raise DeclarationInvalid(f"declaration file missing: {path}")
    try:
        with open(path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
    except (OSError, ValueError) as error:
        raise DeclarationInvalid(f"{path}: {error}") from error
    if not isinstance(document, dict):
        raise DeclarationInvalid(
            f"{path}: the declaration is an object with a 'changes' list")
    unknown = sorted(set(document) - {"note", "changes"})
    if unknown:
        raise DeclarationInvalid(
            f"{path}: unknown top-level key(s): {', '.join(unknown)}")
    entries = document.get("changes", [])
    if not isinstance(entries, list):
        raise DeclarationInvalid(f"{path}: 'changes' is a list")
    changes = []
    for index, entry in enumerate(entries):
        where = f"{path}: changes[{index}]"
        if not isinstance(entry, dict):
            raise DeclarationInvalid(f"{where}: an entry is an object")
        unknown = sorted(set(entry) - {"pattern", "reason", "flavors"})
        if unknown:
            raise DeclarationInvalid(
                f"{where}: unknown key(s): {', '.join(unknown)}")
        pattern = entry.get("pattern")
        if not isinstance(pattern, str) or not pattern.strip():
            raise DeclarationInvalid(f"{where}: 'pattern' is a non-empty string")
        reason = entry.get("reason")
        if not isinstance(reason, str) or not reason.strip():
            raise DeclarationInvalid(
                f"{where}: 'reason' is a non-empty string - a declared pixel "
                f"arrives with the sentence that explains it")
        flavors = entry.get("flavors", list(FLAVORS))
        if not isinstance(flavors, list) or not flavors:
            raise DeclarationInvalid(
                f"{where}: 'flavors' is a non-empty list of "
                f"{'/'.join(FLAVORS)}")
        for flavor in flavors:
            if flavor not in FLAVORS:
                raise DeclarationInvalid(
                    f"{where}: unknown flavor {flavor!r} "
                    f"(expected {'/'.join(FLAVORS)})")
        changes.append(Change(pattern.strip(), reason.strip(), set(flavors)))
    return changes


# --- the captures -----------------------------------------------------------

def collect_shots(root):
    """Every capture under `root`, keyed by its root-relative posix name."""
    shots = {}
    for directory, _subdirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(".png"):
                continue
            if name.endswith(DIAGNOSIS_SUFFIXES):
                continue
            full = os.path.join(directory, name)
            relative = os.path.relpath(full, root).replace(os.sep, "/")
            shots[relative] = full
    return shots


def verify_capture_dir(label, root):
    """Refuse a capture directory that cannot carry a verdict.

    The failure this exists to prevent is the quiet one: a comparison handed
    an absent or empty tree finds no movement and reports that nothing
    drifted. Silence is not evidence.
    """
    if not os.path.isdir(root):
        raise CaptureUnusable(
            f"{label} capture directory does not exist: {root}")
    shots = collect_shots(root)
    if not shots:
        raise CaptureUnusable(
            f"{label} capture directory carries no screenshots: {root}")
    return shots


# --- movement ---------------------------------------------------------------

class Movement:
    """What one shot did between the two captures."""

    def __init__(self, shot, moved, kind, summary):
        self.shot = shot
        self.moved = moved
        self.kind = kind            # same | pixels | resized | added | removed
        self.summary = summary
        self.dmap = None            # set for a pixel movement, for the picture

    def __repr__(self):
        return f"Movement({self.shot!r}, {self.kind}, moved={self.moved})"


def measure_pair(shot, previous_path, current_path):
    """Score one frame pair against the noise floor."""
    try:
        previous = decode_png(previous_path)
        current = decode_png(current_path)
    except Exception as error:      # noqa: BLE001 - see below
        # An unreadable frame is not evidence of stillness, and a decoder
        # meeting a truncated file raises whatever the damage happens to
        # produce (a short read, a bad chunk, a missing header field), so the
        # catch is deliberately by consequence rather than by exception type.
        return Movement(shot, True, "unreadable", f"cannot be compared: {error}")
    if (previous[0], previous[1]) != (current[0], current[1]):
        return Movement(shot, True, "resized",
                        f"{previous[0]}x{previous[1]} -> "
                        f"{current[0]}x{current[1]}")
    dmap = parity_diff.delta_map(previous, current)
    total = dmap.width * dmap.height
    # scored at byte speed rather than pixel by pixel: the delta map is one
    # byte per pixel, so the worst is a max() over it and the over-the-floor
    # count is a translate to 0/1 followed by a count
    worst = max(dmap.deltas) if dmap.deltas else 0
    over = dmap.deltas.translate(OVER_FLOOR_TABLE).count(1)
    fraction = over / float(total) if total else 0.0
    moved = worst > LOUD_DELTA or fraction > MOVED_FRACTION
    summary = (f"max delta {worst}, {over}px "
               f"({100.0 * fraction:.4f}%) over {NOISE_DELTA}")
    movement = Movement(shot, moved, "pixels" if moved else "same", summary)
    movement.dmap = dmap
    return movement


def compare_captures(previous_root, current_root):
    """Every shot on either side, scored. Raises CaptureUnusable per doctrine."""
    previous = verify_capture_dir("previous", previous_root)
    current = verify_capture_dir("current", current_root)
    movements = []
    for shot in sorted(set(previous) | set(current)):
        if shot not in current:
            movements.append(Movement(shot, True, "removed",
                                      "present before, gone now"))
        elif shot not in previous:
            movements.append(Movement(shot, True, "added",
                                      "not in the previous capture"))
        else:
            movements.append(measure_pair(shot, previous[shot], current[shot]))
    return movements


# --- the verdict ------------------------------------------------------------

class Verdict:
    """The gate's decision plus everything a reader needs to act on it."""

    def __init__(self, movements, changes, flavor):
        self.movements = movements
        self.flavor = flavor
        self.active = [change for change in changes if change.applies_to(flavor)]
        self.inactive = [change for change in changes
                         if not change.applies_to(flavor)]
        self.moved = [move for move in movements if move.moved]
        covered = {}
        self.undeclared = []
        self.declared = []
        for move in self.moved:
            hit = next((change for change in self.active
                        if change.matches(move.shot)), None)
            if hit is None:
                self.undeclared.append(move)
            else:
                self.declared.append((move, hit))
                covered.setdefault(id(hit), 0)
                covered[id(hit)] += 1
        self.stale = [change for change in self.active
                      if covered.get(id(change), 0) == 0]

    @property
    def ok(self):
        return not self.undeclared and not self.stale


def report(verdict, stream=None):
    """Print the whole picture: what moved, what covered it, what is stale."""
    write = (stream or sys.stdout).write
    write(f"self-drift gate ({verdict.flavor}): "
          f"{len(verdict.movements)} shot(s) compared, "
          f"{len(verdict.moved)} moved\n")
    for move in verdict.moved:
        declared = next((change for covered, change in verdict.declared
                         if covered is move), None)
        if declared is None:
            write(f"FAIL {move.shot}: {move.kind} - {move.summary}\n")
        else:
            write(f"ok   {move.shot}: {move.kind} - {move.summary} "
                  f"[declared: {declared.reason}]\n")
    if verdict.undeclared:
        write("\nUNDECLARED MOVEMENT - a pixel changed with nothing saying "
              "why:\n")
        for move in verdict.undeclared:
            write(f"  {move.shot}  ({move.kind}: {move.summary})\n")
        write("\nEither this is a regression - the frame is the evidence, and "
              "the drift image beside it\nshows where - or it is a look change "
              "this commit means to make, in which case name it\nin "
              "tests/integration_driver/look_changes.json with the sentence "
              "that explains it.\n")
    if verdict.stale:
        write("\nSTALE DECLARATION - an entry covering nothing that moved:\n")
        for change in verdict.stale:
            write(f"  {change.pattern}  ({change.reason})\n")
        write("\nA declaration is consumed by the landing it belongs to: once "
              "that commit is the previous\ngreen run its frames no longer "
              "move, so the entry has served and must be removed. An empty\n"
              "'changes' list is the steady state.\n")
    if verdict.ok:
        write(f"ok   nothing moved undeclared "
              f"({len(verdict.declared)} declared, "
              f"{len(verdict.inactive)} entr(ies) scoped to the other "
              f"flavor)\n")
    return verdict.ok


def write_drift_images(verdict, previous_root, current_root, diff_dir):
    """A picture per undeclared mover: the delta painted over the old frame.

    Written into a directory of the caller's choosing (the parity job's
    uploaded tree), under a flattened name, so the evidence rides out with
    the failure. Never raises - a diagnosis must not invent a failure mode.
    """
    written = []
    for move in verdict.undeclared:
        if move.dmap is None:
            continue
        flat = move.shot.replace("/", "_")
        target = os.path.join(diff_dir, f"{verdict.flavor}_{flat}.drift.png")
        reference = None
        try:
            reference = decode_png(os.path.join(previous_root, move.shot))
        except (OSError, ValueError):
            reference = None
        if parity_diff.try_write_diff(target, move.dmap, reference):
            written.append(target)
    return written


# --- the command line -------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Gate a flavor's captures against its own previous ones")
    parser.add_argument("--previous",
                        help="capture directory from the previous green run")
    parser.add_argument("--current",
                        help="capture directory from this run")
    parser.add_argument("--flavor", choices=FLAVORS,
                        help="which flavor these captures are")
    parser.add_argument("--declaration", default=DECLARATION,
                        help="the look-change declaration (default: "
                             "tests/integration_driver/look_changes.json)")
    parser.add_argument("--diff-dir",
                        help="where to write a drift image per undeclared "
                             "mover")
    parser.add_argument("--selftest", action="store_true",
                        help="run the driver's own assertions and exit")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    if args.selftest:
        selftest()
        print("run_self_drift_test.py selftest: ok")
        return 0
    missing = [name for name, value in (("--previous", args.previous),
                                        ("--current", args.current),
                                        ("--flavor", args.flavor))
               if not value]
    if missing:
        print(f"FAIL: {', '.join(missing)} required")
        return 2
    try:
        changes = load_declaration(args.declaration)
    except DeclarationInvalid as error:
        print(f"FAIL: {error}")
        return 2
    try:
        movements = compare_captures(args.previous, args.current)
    except CaptureUnusable as error:
        print(f"FAIL: {error}")
        return 2
    verdict = Verdict(movements, changes, args.flavor)
    ok = report(verdict)
    if args.diff_dir and verdict.undeclared:
        for path in write_drift_images(verdict, args.previous, args.current,
                                       args.diff_dir):
            print(f"drift image: {path}")
    return 0 if ok else 1


# --- selftest ---------------------------------------------------------------

def write_png(path, width, height, fill, poke=None, block=None):
    """A flat frame, optionally with one poked pixel or one solid block."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    image = parity_diff.Image(width, height)
    pixels = image.pixels
    for index in range(width * height):
        base = index * 4
        pixels[base] = fill[0]
        pixels[base + 1] = fill[1]
        pixels[base + 2] = fill[2]
        pixels[base + 3] = 255
    if poke is not None:
        x, y, colour = poke
        base = (y * width + x) * 4
        pixels[base] = colour[0]
        pixels[base + 1] = colour[1]
        pixels[base + 2] = colour[2]
    if block is not None:
        x0, y0, x1, y1, colour = block
        for y in range(y0, y1):
            for x in range(x0, x1):
                base = (y * width + x) * 4
                pixels[base] = colour[0]
                pixels[base + 1] = colour[1]
                pixels[base + 2] = colour[2]
    parity_diff.encode_png(image, path)


def write_declaration(path, entries, note="fixture"):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"note": note, "changes": entries}, handle, indent=2)


def run_gate(previous, current, flavor, declaration, diff_dir=None):
    """Drive main() the way a caller does, returning the exit code."""
    argv = ["--previous", previous, "--current", current,
            "--flavor", flavor, "--declaration", declaration]
    if diff_dir:
        argv += ["--diff-dir", diff_dir]
    return main(argv)


def capture_output(function):
    """Run `function`, returning (result, everything it printed)."""
    import io
    buffer = io.StringIO()
    saved = sys.stdout
    sys.stdout = buffer
    try:
        result = function()
    finally:
        sys.stdout = saved
    return result, buffer.getvalue()


def selftest_declaration(scratch):
    """The declaration reader refuses every shape that declares nothing."""
    path = os.path.join(scratch, "decl.json")

    write_declaration(path, [])
    assert load_declaration(path) == []

    write_declaration(path, [{"pattern": "a.png", "reason": "because"}])
    changes = load_declaration(path)
    assert len(changes) == 1
    assert changes[0].flavors == set(FLAVORS), changes[0].flavors
    assert changes[0].applies_to("classic") and changes[0].applies_to("next")

    write_declaration(path, [{"pattern": "a.png", "reason": "r",
                              "flavors": ["classic"]}])
    change = load_declaration(path)[0]
    assert change.applies_to("classic") and not change.applies_to("next")

    def refuses(entries, fragment, top=None):
        if top is None:
            write_declaration(path, entries)
        else:
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(top, handle)
        try:
            load_declaration(path)
        except DeclarationInvalid as error:
            assert fragment in str(error), (fragment, str(error))
            return
        raise AssertionError(f"accepted what it must refuse: {fragment}")

    refuses([{"pattern": "a.png"}], "'reason' is a non-empty string")
    refuses([{"pattern": "a.png", "reason": "  "}], "'reason' is a non-empty")
    refuses([{"reason": "r"}], "'pattern' is a non-empty string")
    refuses([{"pattern": "a.png", "reason": "r", "flavors": ["metal"]}],
            "unknown flavor")
    refuses([{"pattern": "a.png", "reason": "r", "flavors": []}],
            "'flavors' is a non-empty list")
    refuses([{"pattern": "a.png", "reason": "r", "why": "no"}], "unknown key")
    refuses(None, "unknown top-level key", top={"changes": [], "extra": 1})
    refuses(None, "'changes' is a list", top={"changes": "all"})
    refuses(None, "the declaration is an object", top=["a.png"])
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("{not json")
    try:
        load_declaration(path)
        raise AssertionError("accepted a file that is not JSON")
    except DeclarationInvalid:
        pass
    try:
        load_declaration(os.path.join(scratch, "absent.json"))
        raise AssertionError("accepted a missing declaration")
    except DeclarationInvalid as error:
        assert "missing" in str(error)


def selftest_patterns():
    """The pattern vocabulary: exact names, globs, the `/`-crossing star."""
    exact = Change("render_facade/selfcheck_window.png", "r", set(FLAVORS))
    assert exact.matches("render_facade/selfcheck_window.png")
    assert not exact.matches("render_facade/selfcheck_window2.png")
    assert not exact.matches("selfcheck_window.png")

    group = Change("render_facade/*", "r", set(FLAVORS))
    assert group.matches("render_facade/selfcheck_window.png")
    assert group.matches("render_facade/nested/deep.png")   # * crosses /
    assert not group.matches("lake/classic.png")

    family = Change("*water*", "r", set(FLAVORS))
    assert family.matches("render_facade/selfcheck_water_on.png")
    assert family.matches("watertest/shot.png")
    assert not family.matches("lake/classic.png")

    single = Change("lake/classi?.png", "r", set(FLAVORS))
    assert single.matches("lake/classic.png")
    assert not single.matches("lake/classicx.png")

    # case-sensitive, so a rename that only changes case is a movement
    assert not Change("A.png", "r", set(FLAVORS)).matches("a.png")


def selftest_noise_floor(scratch):
    """Identical captures move nothing; a copy of a real tree is still."""
    previous = os.path.join(scratch, "floor-prev")
    current = os.path.join(scratch, "floor-cur")
    write_png(os.path.join(previous, "render_facade", "a.png"), 64, 64,
              (30, 60, 90))
    write_png(os.path.join(previous, "lake", "classic.png"), 64, 64,
              (10, 10, 10), block=(8, 8, 40, 40, (200, 30, 30)))
    shutil.copytree(previous, current)
    movements = compare_captures(previous, current)
    assert len(movements) == 2, movements
    assert all(not move.moved for move in movements), movements
    assert all(move.kind == "same" for move in movements), movements

    # a delta at the floor is not a movement; a loud single pixel is
    quiet = os.path.join(scratch, "floor-quiet")
    write_png(os.path.join(quiet, "render_facade", "a.png"), 64, 64,
              (30 + NOISE_DELTA, 60, 90))
    write_png(os.path.join(quiet, "lake", "classic.png"), 64, 64,
              (10, 10, 10), block=(8, 8, 40, 40, (200, 30, 30)))
    assert all(not move.moved
               for move in compare_captures(previous, quiet)), "floor too low"

    loud = os.path.join(scratch, "floor-loud")
    shutil.copytree(previous, loud)
    write_png(os.path.join(loud, "render_facade", "a.png"), 64, 64,
              (30, 60, 90), poke=(5, 5, (30, 60, 90 + LOUD_DELTA + 1)))
    moved = [move for move in compare_captures(previous, loud) if move.moved]
    assert [move.shot for move in moved] == ["render_facade/a.png"], moved
    assert moved[0].kind == "pixels", moved[0]


def selftest_gate(scratch):
    """The four verdicts, end to end through main()."""
    previous = os.path.join(scratch, "prev")
    write_png(os.path.join(previous, "render_facade", "window.png"), 64, 64,
              (30, 60, 90))
    write_png(os.path.join(previous, "render_facade", "shadow.png"), 64, 64,
              (40, 40, 40))
    write_png(os.path.join(previous, "lake", "classic.png"), 64, 64,
              (10, 40, 70))
    declaration = os.path.join(scratch, "gate.json")

    still = os.path.join(scratch, "still")
    shutil.copytree(previous, still)

    moved = os.path.join(scratch, "moved")
    shutil.copytree(previous, moved)
    write_png(os.path.join(moved, "render_facade", "shadow.png"), 64, 64,
              (40, 40, 40), block=(0, 0, 32, 32, (200, 200, 40)))

    # 1. an undeclared mover fails, NAMING the shot
    write_declaration(declaration, [])
    code, output = capture_output(
        lambda: run_gate(previous, moved, "next", declaration))
    assert code == 1, output
    assert "UNDECLARED" in output, output
    assert "render_facade/shadow.png" in output, output
    assert "render_facade/window.png" not in output, output

    # 2. declared, it passes - and the reason is echoed
    write_declaration(declaration, [
        {"pattern": "render_facade/shadow.png",
         "reason": "the shadow split moves"}])
    code, output = capture_output(
        lambda: run_gate(previous, moved, "next", declaration))
    assert code == 0, output
    assert "the shadow split moves" in output, output

    # 3. the SAME declaration left in place on clean captures fails as stale
    code, output = capture_output(
        lambda: run_gate(previous, still, "next", declaration))
    assert code == 1, output
    assert "STALE DECLARATION" in output, output
    assert "render_facade/shadow.png" in output, output

    # 4. emptied again, the clean pair passes
    write_declaration(declaration, [])
    code, output = capture_output(
        lambda: run_gate(previous, still, "next", declaration))
    assert code == 0, output
    assert "nothing moved undeclared" in output, output

    # a group pattern covers the mover, and one entry may cover several
    two = os.path.join(scratch, "two")
    shutil.copytree(previous, two)
    write_png(os.path.join(two, "render_facade", "shadow.png"), 64, 64,
              (90, 90, 90))
    write_png(os.path.join(two, "render_facade", "window.png"), 64, 64,
              (90, 90, 90))
    write_declaration(declaration, [
        {"pattern": "render_facade/*", "reason": "the whole facade sweep"}])
    code, output = capture_output(
        lambda: run_gate(previous, two, "next", declaration))
    assert code == 0, output

    # ... but the same group is stale for a run where nothing under it moved
    code, output = capture_output(
        lambda: run_gate(previous, still, "next", declaration))
    assert code == 1 and "STALE" in output, output

    # a flavor-scoped entry is inert for the other flavor: declared on
    # classic, the same movement is undeclared on next
    write_declaration(declaration, [
        {"pattern": "render_facade/shadow.png", "reason": "classic only",
         "flavors": ["classic"]}])
    code, output = capture_output(
        lambda: run_gate(previous, moved, "classic", declaration))
    assert code == 0, output
    code, output = capture_output(
        lambda: run_gate(previous, moved, "next", declaration))
    assert code == 1 and "UNDECLARED" in output, output
    # ... and it is not reported stale on next either - it was never about it
    assert "STALE" not in output, output


def selftest_added_removed(scratch):
    """A frame appearing or disappearing is a movement, and says which."""
    previous = os.path.join(scratch, "ar-prev")
    write_png(os.path.join(previous, "render_facade", "a.png"), 32, 32,
              (10, 20, 30))
    write_png(os.path.join(previous, "render_facade", "gone.png"), 32, 32,
              (10, 20, 30))
    current = os.path.join(scratch, "ar-cur")
    write_png(os.path.join(current, "render_facade", "a.png"), 32, 32,
              (10, 20, 30))
    write_png(os.path.join(current, "render_facade", "new.png"), 32, 32,
              (10, 20, 30))
    kinds = {move.shot: move.kind for move in
             compare_captures(previous, current) if move.moved}
    assert kinds == {"render_facade/gone.png": "removed",
                     "render_facade/new.png": "added"}, kinds

    # a resized frame is a movement of its own kind, not a pixel score
    resized = os.path.join(scratch, "ar-resized")
    write_png(os.path.join(resized, "render_facade", "a.png"), 48, 32,
              (10, 20, 30))
    write_png(os.path.join(resized, "render_facade", "gone.png"), 32, 32,
              (10, 20, 30))
    moves = [move for move in compare_captures(previous, resized) if move.moved]
    assert [move.kind for move in moves] == ["resized"], moves
    assert "32x32 -> 48x32" in moves[0].summary, moves[0].summary


def selftest_capture_refusals(scratch):
    """A capture that cannot be compared FAILS - the parity doctrine."""
    good = os.path.join(scratch, "ref-good")
    write_png(os.path.join(good, "render_facade", "a.png"), 32, 32, (1, 2, 3))
    empty = os.path.join(scratch, "ref-empty")
    os.makedirs(empty, exist_ok=True)
    absent = os.path.join(scratch, "ref-absent")
    # a tree holding nothing but diagnosis images is empty of CAPTURES
    diagnosis = os.path.join(scratch, "ref-diffs")
    write_png(os.path.join(diagnosis, "a.diff.png"), 32, 32, (1, 2, 3))
    write_png(os.path.join(diagnosis, "b.drift.png"), 32, 32, (1, 2, 3))

    declaration = os.path.join(scratch, "ref.json")
    write_declaration(declaration, [])
    for label, bad in (("previous", absent), ("previous", empty),
                       ("previous", diagnosis)):
        code, output = capture_output(
            lambda bad=bad: run_gate(bad, good, "next", declaration))
        assert code == 2, (bad, output)
        assert label in output, output
    for bad in (absent, empty, diagnosis):
        code, output = capture_output(
            lambda bad=bad: run_gate(good, bad, "next", declaration))
        assert code == 2, (bad, output)
        assert "current" in output, output

    # an unreadable frame is a movement, never a silent pass
    broken = os.path.join(scratch, "ref-broken")
    os.makedirs(os.path.join(broken, "render_facade"), exist_ok=True)
    with open(os.path.join(broken, "render_facade", "a.png"), "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\nnot really")
    moves = [move for move in compare_captures(good, broken) if move.moved]
    assert [move.kind for move in moves] == ["unreadable"], moves


def selftest_drift_image(scratch):
    """An undeclared mover leaves a picture where the caller asked for one."""
    previous = os.path.join(scratch, "img-prev")
    write_png(os.path.join(previous, "render_facade", "a.png"), 32, 32,
              (10, 20, 30))
    current = os.path.join(scratch, "img-cur")
    write_png(os.path.join(current, "render_facade", "a.png"), 32, 32,
              (10, 20, 30), block=(4, 4, 20, 20, (200, 20, 20)))
    declaration = os.path.join(scratch, "img.json")
    write_declaration(declaration, [])
    diff_dir = os.path.join(scratch, "img-diffs")
    code, output = capture_output(
        lambda: run_gate(previous, current, "next", declaration, diff_dir))
    assert code == 1, output
    written = os.path.join(diff_dir, "next_render_facade_a.png.drift.png")
    assert os.path.exists(written), os.listdir(diff_dir)
    assert written in output, output
    width, height, _channels, _data = decode_png(written)
    assert (width, height) == (32, 32), (width, height)


def selftest_shipped_declaration():
    """The declaration this repository ships parses, and is honest.

    Its steady state is empty. A non-empty one is legitimate for exactly as
    long as the landing it belongs to is in flight, so this asserts it PARSES
    rather than that it is empty - the stale rule is what empties it.
    """
    changes = load_declaration(DECLARATION)
    for change in changes:
        assert change.reason, change
        assert change.flavors <= set(FLAVORS), change


def selftest():
    scratch = tempfile.mkdtemp(prefix="orkige-self-drift-")
    try:
        selftest_declaration(scratch)
        selftest_patterns()
        selftest_noise_floor(scratch)
        selftest_gate(scratch)
        selftest_added_removed(scratch)
        selftest_capture_refusals(scratch)
        selftest_drift_image(scratch)
        selftest_shipped_declaration()
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
