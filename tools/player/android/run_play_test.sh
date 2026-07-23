#!/usr/bin/env bash
#
# Run the Android emulator Play test as a chain of BOUNDED phases so a wedged
# emulator, install or test fails fast with the offending phase named, instead
# of silently burning the whole job. This is the payload the CI emulator step
# hands to the emulator action AFTER the emulator has booted; the fresh-AVD
# self-heal (one cold retry of this whole chain) lives at the workflow step
# level, so this script does not retry - it reports the failed phase and exits.
#
# Usage:
#   run_play_test.sh <apk-path> -- <test command...>
#
# Phases and their deadlines (seconds, env-overridable):
#   ready    the emulator is booted + package manager up   ORKIGE_READY_DEADLINE (180, via emulator_wait_ready.sh)
#   install  adb install -r <apk> + a pm-path sanity check  ORKIGE_INSTALL_DEADLINE (240)
#   test     the passed test command (the ctest invocation) ORKIGE_TEST_DEADLINE (600)
#
# Other env:
#   ANDROID_SERIAL         target device/emulator (else the first adb device)
#   ORKIGE_APK_PACKAGE     app package for the pm-path check (default com.orkitec.orkigeplayer)
#   ORKIGE_ADB             explicit adb binary (else SDK path, else PATH)
#   ORKIGE_WAIT_READY      path to emulator_wait_ready.sh (default: alongside this script)
#   ORKIGE_SKIP_INSTALL=1  skip the install phase (local mechanics tests without an APK)
#
# Exit codes: 0 all phases passed; 1 a phase failed (name printed via
# `::error::` so it surfaces in the GitHub log and the step annotation).

set -u

INSTALL_DEADLINE="${ORKIGE_INSTALL_DEADLINE:-240}"
TEST_DEADLINE="${ORKIGE_TEST_DEADLINE:-600}"
APK_PACKAGE="${ORKIGE_APK_PACKAGE:-com.orkitec.orkigeplayer}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
WAIT_READY="${ORKIGE_WAIT_READY:-$SCRIPT_DIR/emulator_wait_ready.sh}"

log() { printf '[play] %s\n' "$*"; }
fail_phase() {
	# GitHub renders ::error:: as a step annotation; the plain line is what a
	# log-tail greps. Name the phase so a red job says WHERE it broke.
	printf '::error::android emulator Play test failed in phase '\''%s'\''\n' "$1"
	log "FAILED phase=$1"
	exit 1
}

resolve_adb() {
	if [ -n "${ORKIGE_ADB:-}" ]; then printf '%s' "$ORKIGE_ADB"; return 0; fi
	local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
	if [ -x "$sdk/platform-tools/adb" ]; then printf '%s' "$sdk/platform-tools/adb"; return 0; fi
	command -v adb >/dev/null 2>&1 && { printf 'adb'; return 0; }
	return 1
}

# --- arguments -------------------------------------------------------------
APK=""
if [ "$#" -gt 0 ] && [ "$1" != "--" ]; then
	APK="$1"
	shift
fi
if [ "${1:-}" = "--" ]; then
	shift
fi
# everything remaining is the test command
if [ "$#" -eq 0 ]; then
	log "no test command given (expected: run_play_test.sh <apk> -- <cmd...>)"
	exit 2
fi

ADB="$(resolve_adb)" || { log "no adb binary found"; exit 2; }

# --- phase: ready ----------------------------------------------------------
log "phase ready: waiting for the emulator to finish booting"
ORKIGE_ADB="$ADB" bash "$WAIT_READY" "${ANDROID_SERIAL:-}" || fail_phase ready

# --- phase: install --------------------------------------------------------
if [ "${ORKIGE_SKIP_INSTALL:-0}" = "1" ]; then
	log "phase install: skipped (ORKIGE_SKIP_INSTALL=1)"
elif [ -n "$APK" ]; then
	log "phase install: adb install -r $APK (deadline ${INSTALL_DEADLINE}s)"
	timeout "$INSTALL_DEADLINE" "$ADB" install -r "$APK" || fail_phase install
	# the Play test skips (77) if the package is not resolvable - verify the
	# install actually took before spending the test budget on it.
	timeout 30 "$ADB" shell pm path "$APK_PACKAGE" >/dev/null 2>&1 || fail_phase install
	log "phase install: $APK_PACKAGE present"
else
	log "phase install: no APK argument given, skipping install"
fi

# --- phase: test -----------------------------------------------------------
log "phase test: $* (deadline ${TEST_DEADLINE}s)"
timeout "$TEST_DEADLINE" "$@" || fail_phase test

log "all phases passed"
exit 0
