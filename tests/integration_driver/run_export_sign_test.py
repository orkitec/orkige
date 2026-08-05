#!/usr/bin/env python3
"""ctest driver for a SIGNED macOS project export.

    run_export_sign_test.py --repo <root> --project <dir>
                            --exporter <orkige_export>
                            --engine-build <dir> --output <dir>

Two legs, and only one of them needs a certificate:

1. THE REFUSAL, always run. `--sign` with no identity anywhere must exit
   nonzero, name the environment variable that carries one, and package
   NOTHING - a half-signed artifact is worse than an honestly ad-hoc one. This
   leg is the CI gate: it holds on every machine, credentials or not.
2. THE SIGNATURE, run only when this machine holds a Developer ID Application
   identity. The export is signed for real and the result is put to
   `codesign --verify --strict` (the check Gatekeeper applies) and read back
   for the hardened runtime flag. Without an identity the leg is SKIPPED (77),
   never faked - a signing test that passes with no certificate proves nothing.

Notarization is deliberately not exercised: it is a network round trip against
an Apple account, so its DECISIONS (the command sequence, every refusal, the
verdict parse, the redaction) are asserted in the unit suite instead
(tests/exporter/ExportMacosSignTests.cpp).

Exit codes: 0 pass, 77 skip, anything else fail.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

SKIP = 77


def log(message):
    print("run_export_sign_test: " + message, flush=True)


def fail(message):
    print("run_export_sign_test: FAILED - " + message, flush=True)
    sys.exit(1)


def skip(message):
    print("run_export_sign_test: SKIP - " + message, flush=True)
    sys.exit(SKIP)


def require(condition, message):
    if not condition:
        fail(message)
    log("ok: " + message)


def run(command, environment=None):
    log("$ " + " ".join(command))
    return subprocess.run(command, capture_output=True, text=True,
                          env=environment)


def credential_free_environment():
    """the process environment with every macOS signing credential removed, so
    the refusal leg is a refusal on a developer's own machine too"""
    environment = dict(os.environ)
    for name in list(environment):
        if name.startswith("ORKIGE_MACOS_") or name.startswith("ORKIGE_NOTARY_"):
            del environment[name]
    return environment


def developer_id_identity():
    """the first Developer ID Application identity in this machine's keychain,
    or "" when it holds none"""
    if not shutil.which("security"):
        return ""
    result = subprocess.run(["security", "find-identity", "-v",
                             "-p", "codesigning"],
                            capture_output=True, text=True)
    for line in (result.stdout or "").splitlines():
        match = re.search(r'"(Developer ID Application: [^"]+)"', line)
        if match:
            return match.group(1)
    return ""


def export_arguments(args, output):
    return [args.exporter,
            "--project", args.project,
            "--platform", "macos",
            "--engine-build", args.engine_build,
            "--repo", args.repo,
            "--output", output]


def refusal_leg(args):
    """a signed export with no credentials refuses, by name, and writes no app"""
    output = os.path.join(args.output, "refusal")
    shutil.rmtree(output, ignore_errors=True)
    result = run(export_arguments(args, output) + ["--sign"],
                 credential_free_environment())
    combined = (result.stdout or "") + (result.stderr or "")
    require(result.returncode != 0,
            "a signed export with no identity fails (exit %d)"
            % result.returncode)
    require("ORKIGE_MACOS_SIGNING_IDENTITY" in combined,
            "the refusal names the environment variable that carries an "
            "identity")
    require("--macos-identity" in combined,
            "the refusal names the command-line flag too")
    apps = []
    if os.path.isdir(output):
        for parent, directories, _ in os.walk(output):
            apps += [os.path.join(parent, name) for name in directories
                     if name.endswith(".app")]
    require(not apps, "nothing was packaged: no half-signed artifact exists")

    # ...and the same gate on the notarization credentials, one step further in
    result = run(export_arguments(args, output) +
                 ["--notarize", "--macos-identity",
                  "Developer ID Application: nobody"],
                 credential_free_environment())
    combined = (result.stdout or "") + (result.stderr or "")
    require(result.returncode != 0, "a notarized export with no notarization "
            "credentials fails")
    require("ORKIGE_NOTARY_KEY" in combined and
            "ORKIGE_NOTARY_APPLE_ID" in combined,
            "the refusal names BOTH credential routes Apple takes")


def signature_leg(args, identity):
    """the real thing: sign, then verify the way Gatekeeper does"""
    output = os.path.join(args.output, "signed")
    shutil.rmtree(output, ignore_errors=True)
    result = run(export_arguments(args, output) +
                 ["--sign", "--macos-identity", identity])
    combined = (result.stdout or "") + (result.stderr or "")
    if result.returncode != 0:
        print(combined, flush=True)
        fail("the signed export failed (exit %d)" % result.returncode)
    match = re.search(r"orkige_export: OK (.+)", combined)
    require(match is not None, "the export reported an artifact")
    app = match.group(1).strip()
    require(os.path.isdir(app), "the app bundle exists at " + app)

    verify = run(["codesign", "--verify", "--strict", "--verbose=2", app])
    if verify.returncode != 0:
        print((verify.stdout or "") + (verify.stderr or ""), flush=True)
        fail("codesign --verify --strict rejected the exported app")
    log("ok: the exported app passes a strict signature verification")

    display = run(["codesign", "--display", "--verbose=2", app])
    shown = (display.stdout or "") + (display.stderr or "")
    require("Authority=" + identity.split(":")[0] in shown or
            identity.split("(")[0].strip() in shown,
            "the signature names the Developer ID authority")
    # the hardened runtime is what notarization accepts nothing without
    flags = re.search(r"CodeDirectory v=[^\n]*flags=0x([0-9a-fA-F]+)", shown)
    require(flags is not None, "the code directory reports its flags")
    require(int(flags.group(1), 16) & 0x10000 != 0,
            "the hardened runtime flag is set")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if sys.platform != "darwin":
        skip("macOS signing is a macOS host operation")
    if not os.path.isfile(args.exporter):
        skip("no exporter at " + args.exporter)
    os.makedirs(args.output, exist_ok=True)

    refusal_leg(args)

    identity = developer_id_identity()
    if not identity:
        log("no Developer ID Application identity in this keychain - the "
            "refusal leg passed, the signature leg has nothing to sign with")
        skip("no Developer ID Application identity on this machine")
    log("signing with '%s'" % identity)
    signature_leg(args, identity)
    log("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
