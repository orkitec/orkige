#!/usr/bin/env python3
"""Run a test command, failing it if it MODIFIED any committed tree it should
only read.

Editor self-checks open shipped sample/project trees (e.g. projects/benchmark)
to exercise the real assets. Those trees are source-controlled and, for the
benchmark project, GENERATOR-OWNED (Util/make_benchmark_assets.py). A self-check
must never write into them - but the visual .oui editor's save path
(GuiLayoutDoc::serialize via persist) and the scene autosave path both CAN write
a project file, so a future regression could quietly canonicalise a committed
asset (the observed drift: the .oui comment header stripped, fields reflowed).

This wrapper hashes the guarded paths before and after the command, propagates
the command's own exit code on success, and on drift OVERRIDES it with a failure
that NAMES every changed file - so the polluting test names itself in-run
instead of leaving an unattributed dirty working tree behind.

Usage:
    guard_tree_unchanged.py --path <file_or_dir> [--path ...] -- <command> [args...]
"""
import hashlib
import os
import subprocess
import sys


def _hash_file(path):
	h = hashlib.sha256()
	with open(path, "rb") as f:
		for chunk in iter(lambda: f.read(65536), b""):
			h.update(chunk)
	return h.hexdigest()


def _snapshot(paths):
	"""Map every guarded regular file to its hash (directories walked)."""
	snap = {}
	for p in paths:
		if os.path.isdir(p):
			for root, _dirs, files in os.walk(p):
				for name in files:
					fp = os.path.join(root, name)
					snap[fp] = _hash_file(fp)
		elif os.path.isfile(p):
			snap[p] = _hash_file(p)
		# a missing path contributes nothing; its later appearance is a change
	return snap


def main(argv):
	paths = []
	i = 1
	while i < len(argv) and argv[i] != "--":
		if argv[i] == "--path":
			paths.append(argv[i + 1])
			i += 2
		else:
			sys.stderr.write("guard_tree_unchanged: unexpected arg '%s'\n" % argv[i])
			return 2
	if i >= len(argv) or argv[i] != "--":
		sys.stderr.write("guard_tree_unchanged: missing '--' before the command\n")
		return 2
	command = argv[i + 1:]
	if not command:
		sys.stderr.write("guard_tree_unchanged: empty command\n")
		return 2

	before = _snapshot(paths)
	result = subprocess.run(command)
	after = _snapshot(paths)

	changed = sorted(
		p for p in (set(before) | set(after))
		if before.get(p) != after.get(p))
	if changed:
		sys.stderr.write(
			"\nguard_tree_unchanged: FAILED - this test wrote into a committed "
			"tree it must only read:\n")
		for p in changed:
			how = ("created" if p not in before else
				"deleted" if p not in after else "modified")
			sys.stderr.write("    %s (%s)\n" % (p, how))
		sys.stderr.write(
			"These files are source-controlled (benchmark assets are "
			"generator-owned by Util/make_benchmark_assets.py); restore them "
			"with 'git checkout' and fix the test so it copies to a temp dir "
			"before writing.\n")
		return 1
	return result.returncode


if __name__ == "__main__":
	sys.exit(main(sys.argv))
