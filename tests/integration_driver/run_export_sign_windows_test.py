#!/usr/bin/env python3
"""ctest driver for a SIGNED Windows project export.

    run_export_sign_windows_test.py --repo <root> --project <dir>
                                    --exporter <orkige_export>
                                    --engine-build <dir> --output <dir>

The Windows sibling of run_export_sign_test.py, and split the same way -
because only one of the two legs needs a certificate:

1. THE REFUSALS, always run. Each must exit nonzero, name what is missing, and
   package NOTHING - a half-signed artifact is worse than an honestly unsigned
   one. Four shapes, and every one of them holds on a machine that has never
   held a certificate:
     - `--sign` with no credential at all names both routes and their
       variables;
     - a certificate file with no password names the password variable rather
       than letting signtool stop and ASK for one, which on a build server is
       a job that hangs instead of a job that fails;
     - a credential named without `--sign` refuses instead of quietly
       producing an unsigned package;
     - `--notarize` refuses by name: nothing on Windows corresponds to it.
   This leg is the CI gate. It runs credential-free on every Windows machine.
2. THE SIGNATURE, run only when this machine holds a code-signing certificate
   AND has signtool.exe. The export is signed for real and the result is put
   to `signtool verify /pa` - the Authenticode policy an operating system
   applies to a program. Without a certificate the leg is SKIPPED (77), never
   faked: a signing test that passes with no certificate proves nothing.

What is deliberately NOT exercised here is the timestamp round trip - it is a
network call to an authority - so the DECISIONS around it (the command shapes,
the RFC 3161 flag, every refusal, the redaction, the whole signtool search)
are asserted in the unit suite instead
(tests/exporter/ExportWindowsSignTests.cpp).

Exit codes: 0 pass, 77 skip, anything else fail.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

SKIP = 77

# the machine-store certificate the signature leg would use, named by its SHA-1
# thumbprint. Nothing here creates one: a test that manufactured a certificate
# would prove that signtool signs with a certificate this test made, which is
# not the question.
THUMBPRINT_VARIABLE = "ORKIGE_TEST_WINDOWS_THUMBPRINT"


def log(message):
    print("run_export_sign_windows_test: " + message, flush=True)


def fail(message):
    print("run_export_sign_windows_test: FAILED - " + message, flush=True)
    sys.exit(1)


def skip(message):
    print("run_export_sign_windows_test: SKIP - " + message, flush=True)
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
    """the process environment with every Windows signing credential removed,
    so the refusal legs are refusals on a developer's own machine too"""
    environment = dict(os.environ)
    for name in list(environment):
        if name.startswith("ORKIGE_WINDOWS_SIGNING_"):
            del environment[name]
    return environment


def export_arguments(args, output):
    return [args.exporter,
            "--project", args.project,
            "--platform", "windows",
            "--engine-build", args.engine_build,
            "--repo", args.repo,
            "--output", output]


def packaged_executables(output):
    found = []
    if os.path.isdir(output):
        for parent, _, files in os.walk(output):
            found += [os.path.join(parent, name) for name in files
                      if name.lower().endswith(".exe")]
    return found


def refuses(args, extra, output_name, environment):
    """run an export that must refuse, and prove it packaged nothing"""
    output = os.path.join(args.output, output_name)
    # a leftover from an earlier run would make "nothing was packaged"
    # meaningless
    shutil.rmtree(output, ignore_errors=True)
    result = run(export_arguments(args, output) + extra, environment)
    combined = (result.stdout or "") + (result.stderr or "")
    require(result.returncode != 0,
            "'%s' fails (exit %d)" % (" ".join(extra), result.returncode))
    require(not packaged_executables(output),
            "nothing was packaged: no half-signed artifact exists")
    return combined


def refusal_leg(args):
    environment = credential_free_environment()

    # 1. no credential at all: both routes named, with both their variables
    combined = refuses(args, ["--sign"], "refusal-none", environment)
    require("ORKIGE_WINDOWS_SIGNING_THUMBPRINT" in combined,
            "the refusal names the machine-store route's variable")
    require("ORKIGE_WINDOWS_SIGNING_CERTIFICATE" in combined,
            "the refusal names the certificate-file route's variable")
    require("--windows-thumbprint" in combined and
            "--windows-certificate" in combined,
            "the refusal names the command-line flags too")

    # 2. a certificate file with no password: named, rather than a prompt that
    #    would hang the job
    combined = refuses(args,
                       ["--sign", "--windows-certificate",
                        os.path.join(args.output, "nobody.pfx")],
                       "refusal-password", environment)
    require("ORKIGE_WINDOWS_SIGNING_PASSWORD" in combined,
            "the refusal names the password variable")
    require("ORKIGE_WINDOWS_SIGNING_THUMBPRINT" in combined,
            "...and points at the route that needs no password at all")

    # 3. a credential with no --sign would silently produce an unsigned package
    combined = refuses(args,
                       ["--windows-thumbprint", "A1B2C3D4E5F6"],
                       "refusal-dangling", environment)
    require("--sign" in combined,
            "a credential named without --sign refuses, naming the flag")

    # 4. --notarize has no meaning here, and saying so beats ignoring it
    combined = refuses(args, ["--notarize"], "refusal-notarize", environment)
    require("notariz" in combined.lower(),
            "--notarize on a Windows package refuses by name")


def signature_leg(args, thumbprint):
    output = os.path.join(args.output, "signed")
    shutil.rmtree(output, ignore_errors=True)
    result = run(export_arguments(args, output) +
                 ["--sign", "--windows-thumbprint", thumbprint])
    combined = (result.stdout or "") + (result.stderr or "")
    if result.returncode != 0:
        print(combined, flush=True)
        fail("the signed export failed (exit %d)" % result.returncode)
    match = re.search(r"orkige_export: OK (.+)", combined)
    require(match is not None, "the export reported an artifact")
    package = match.group(1).strip()
    require(os.path.isdir(package), "the package exists at " + package)

    executables = [name for name in os.listdir(package)
                   if name.lower().endswith(".exe")]
    require(len(executables) == 1,
            "the package holds exactly one executable")
    executable = os.path.join(package, executables[0])

    # the export echoed the tool it found, which is also what this leg verifies
    # with - a second search here could disagree with the one that signed
    tool = re.search(r"signing with '([^']+)'", combined)
    require(tool is not None, "the export named the signing tool it located")
    verify = run([tool.group(1), "verify", "/pa", executable])
    if verify.returncode != 0:
        print((verify.stdout or "") + (verify.stderr or ""), flush=True)
        fail("signtool verify /pa rejected the exported executable")
    log("ok: the exported executable passes an Authenticode verification")

    shown = (verify.stdout or "") + (verify.stderr or "")
    # the countersignature is what makes the signature outlive the certificate
    require("imestamp" in shown or "ountersignature" in shown,
            "the signature carries a timestamp countersignature")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if not sys.platform.startswith("win"):
        skip("a Windows package is packaged on Windows")
    if not os.path.isfile(args.exporter):
        skip("no exporter at " + args.exporter)
    os.makedirs(args.output, exist_ok=True)

    refusal_leg(args)

    thumbprint = os.environ.get(THUMBPRINT_VARIABLE, "").strip()
    if not thumbprint:
        log("no code-signing certificate named in %s - the refusal legs "
            "passed, the signature leg has nothing to sign with"
            % THUMBPRINT_VARIABLE)
        skip("no code-signing certificate on this machine")
    log("signing with the store certificate %s" % thumbprint)
    signature_leg(args, thumbprint)
    log("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
