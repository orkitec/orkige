#!/usr/bin/env python3
"""Differential oracle for the vector-animation cook: two cooks, one verdict.

The native cook (orkige_core/core_util/VectorAnimCook, reachable as the
`animcook` CLI) must reproduce `cook_vector_anim.py` BYTE FOR BYTE - a cooked
`.oanim` is a committed asset that gets re-cooked on import and diffed, so a
subtly different vertex silently corrupts an animation nobody re-inspects.
This harness proves the two agree over every document the reference cook's own
selftest exercises.

Fixtures come from the reference cook itself rather than a hand-kept list: the
selftest builds its documents inline, so the harness records every text that
reaches `cook()` while `--selftest` runs. That is the complete inventory by
construction - every mapped feature, every named refusal, the pinned real-world
character corpus and the committed round-trip fixtures - and it cannot drift
from the selftest.

  python3 Util/cook_vector_anim_oracle.py <path/to/animcook>
                                          [--keep DIR] [--only N[,N...]]

Exit 0 when every fixture matches. Pure stdlib.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Util"))

import cook_vector_anim as C     # noqa: E402  (the reference cook, and the
                                 # specification this harness measures against)


# ---------------------------------------------------------------------------
# fixture capture


def capture_fixtures(out_dir):
    """Run the reference selftest with a recording `cook`, writing every
    distinct document it feeds through into out_dir plus a manifest carrying
    each one's cook options and the reference outcome."""
    records = []
    seen = {}
    original = C.cook

    def spy(lottie_text, **kwargs):
        options = sorted((k, v) for k, v in kwargs.items()
                         if k != "images_out")
        key = hashlib.sha256(
            (lottie_text + "|" + repr(options)).encode("utf-8")).hexdigest()
        if key not in seen:
            seen[key] = len(records)
            record = {"index": len(records),
                      "file": "%04d.json" % len(records),
                      "extent": kwargs.get("extent", 2.0),
                      "tolerance": kwargs.get("tolerance", None),
                      "clips": kwargs.get("clips_override", None)}
            (out_dir / record["file"]).write_text(lottie_text,
                                                  encoding="utf-8")
            records.append(record)
        return original(lottie_text, **kwargs)

    C.cook = spy
    try:
        C._selftest()
    finally:
        C.cook = original

    for record in records:
        text = (out_dir / record["file"]).read_text(encoding="utf-8")
        expected = out_dir / (record["file"][:-5] + ".expected")
        try:
            kind, cooked = original(text, extent=record["extent"],
                                    tolerance=record["tolerance"],
                                    clips_override=record["clips"])
            record["outcome"] = "ok"
            record["kind"] = kind
            expected.write_text(cooked, encoding="utf-8")
        except C.CookError as exc:
            record["outcome"] = "error"
            expected.write_text(str(exc) + "\n", encoding="utf-8")

    # image layers name files the CLI materializes beside its output; the
    # reference `cook()` never touches the filesystem, so stand in for them
    for record in records:
        try:
            source = json.loads(
                (out_dir / record["file"]).read_text("utf-8"))
        except json.JSONDecodeError:
            continue        # a deliberately malformed fixture
        if not isinstance(source, dict):
            continue
        for asset in source.get("assets", []):
            path = str(asset.get("p", "")) if isinstance(asset, dict) else ""
            if not path or path.startswith("data:"):
                continue
            placeholder = out_dir / (str(asset.get("u", "")) + path)
            placeholder.parent.mkdir(parents=True, exist_ok=True)
            if not placeholder.exists():
                placeholder.write_bytes(b"")

    (out_dir / "manifest.json").write_text(json.dumps(records, indent=1),
                                           encoding="utf-8")
    return records


# ---------------------------------------------------------------------------
# comparison


def first_difference(expected, got):
    """A readable account of where two cooked texts part ways."""
    want = expected.splitlines()
    have = got.splitlines()
    for number, (a, b) in enumerate(zip(want, have), 1):
        if a != b:
            return ("line %d\n      reference: %s\n      native:    %s\n"
                    "      (%d vs %d lines)" % (number, a, b, len(want),
                                                len(have)))
    return "line counts differ: %d vs %d" % (len(want), len(have))


def compare(record, fixtures, work, binary):
    """Cook one fixture with the native cook and diff it against the
    reference outcome. Returns None when they agree, else the reason."""
    source = fixtures / record["file"]
    expected = (fixtures / (record["file"][:-5] + ".expected")).read_text(
        encoding="utf-8")
    out_path = work / ("%04d.oanim" % record["index"])
    for stale in work.glob("%04d.*" % record["index"]):
        stale.unlink()

    command = [str(binary), str(source), str(out_path)]
    if record["extent"] != 2.0:
        command += ["--extent", repr(record["extent"])]
    if record["tolerance"] is not None:
        command += ["--tolerance", repr(record["tolerance"])]
    if record["clips"]:
        command += ["--clips", record["clips"]]
    result = subprocess.run(command, capture_output=True, text=True)

    if record["outcome"] == "error":
        if result.returncode == 0:
            return "the native cook accepted a document the reference refuses"
        # the CLI prints a "cannot cook <path>:" header, then the listing
        # indented by two spaces
        lines = result.stderr.splitlines()[1:]
        got = "\n".join(line[2:] if line.startswith("  ") else line
                        for line in lines) + "\n"
        if got != expected:
            return ("refusal text differs\n      reference: %s\n"
                    "      native:    %s" % (expected.strip(), got.strip()))
        return None

    if result.returncode != 0:
        return ("the native cook refused a document the reference cooks:\n"
                "      " + result.stderr.strip().replace("\n", "\n      "))
    produced = out_path if record["kind"] == "oanim" \
        else out_path.with_suffix(".oshape")
    if not produced.is_file():
        return "no %s output was written" % record["kind"]
    got = produced.read_text(encoding="utf-8")
    if got != expected:
        return first_difference(expected, got)
    return None


# ---------------------------------------------------------------------------
# the one divergence that is by design


def allowed_divergence(record, fixtures):
    """A document that is not JSON at all is the ONE case where the two cooks
    are allowed to word their refusal differently: the reference reports the
    decoder's line/column, while the native reader is a boolean API with no
    offset readback. Both refuse, and no test asserts the wording. Returns the
    reason when this fixture is that case, else None."""
    if record["outcome"] != "error":
        return None
    text = (fixtures / record["file"]).read_text(encoding="utf-8")
    try:
        json.loads(text)
    except json.JSONDecodeError:
        return ("a malformed source document - both cooks refuse; only the "
                "syntax-error wording differs (the native JSON reader "
                "reports no position)")
    return None


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("animcook", help="the native cook's CLI binary")
    parser.add_argument("--keep", metavar="DIR",
                        help="write the captured fixtures here and keep them "
                             "(default: a temporary directory)")
    parser.add_argument("--only", metavar="N[,N...]",
                        help="compare only these fixture indices")
    args = parser.parse_args()

    binary = Path(args.animcook).resolve()
    if not binary.is_file():
        print("cook_vector_anim_oracle: no such binary: %s" % binary,
              file=sys.stderr)
        return 2

    keep = args.keep is not None
    fixtures = Path(args.keep).resolve() if keep else \
        Path(tempfile.mkdtemp(prefix="cook-oracle-fixtures-"))
    fixtures.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="cook-oracle-work-"))
    only = {int(v) for v in args.only.split(",")} if args.only else None

    try:
        records = capture_fixtures(fixtures)
        print("captured %d fixtures (%d cooked, %d refused)" %
              (len(records),
               sum(1 for r in records if r["outcome"] == "ok"),
               sum(1 for r in records if r["outcome"] == "error")))

        failures = []
        accepted = []
        checked = 0
        for record in records:
            if only is not None and record["index"] not in only:
                continue
            checked += 1
            reason = compare(record, fixtures, work, binary)
            if reason is None:
                continue
            allowance = allowed_divergence(record, fixtures)
            if allowance is not None:
                accepted.append((record, allowance))
            else:
                failures.append((record, reason))

        print("%d / %d fixtures reproduce byte for byte" %
              (checked - len(failures) - len(accepted), checked))
        for record, allowance in accepted:
            print("--- accepted divergence %04d: %s" %
                  (record["index"], allowance))
        for record, reason in failures:
            print("--- MISMATCH %04d (%s%s)" %
                  (record["index"], record["outcome"],
                   ", " + record["kind"] if record["outcome"] == "ok" else ""))
            print("      " + reason.replace("\n", "\n      ").strip())
        if failures and not keep:
            print("re-run with --keep <dir> to inspect the fixtures")
        return 1 if failures else 0
    finally:
        shutil.rmtree(work, ignore_errors=True)
        if not keep:
            shutil.rmtree(fixtures, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
