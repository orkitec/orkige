#!/usr/bin/env bash
# CI configure wrapper: vcpkg classifies some genuinely transient download
# failures (HTTP/2 framing errors from the GitHub archive endpoint, the
# occasional 404 blip) as permanent and refuses to retry - re-running the
# whole configure IS the retry, and already-downloaded/built artifacts are
# reused so a second attempt only redoes the failed download. Three
# attempts with a pause; a real failure still fails on the last.
set -uo pipefail
# one-line forensics for the distfile seed: what THIS process resolves
echo "ci_configure: HOME=$HOME VCPKG_ROOT=${VCPKG_ROOT:-unset}"
ls -la "$HOME/Development/vcpkg/downloads/" 2>/dev/null | head -5 || echo "ci_configure: no downloads dir at $HOME/Development/vcpkg"
for attempt in 1 2 3; do
	cmake "$@" && exit 0
	if [ "$attempt" -lt 3 ]; then
		echo "ci_configure: attempt $attempt failed - retrying in 20s (transient download guard)"
		sleep 20
	fi
done
exit 1
