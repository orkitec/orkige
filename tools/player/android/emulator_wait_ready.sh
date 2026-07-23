#!/usr/bin/env bash
#
# Wait, with a hard deadline, for an adb-connected emulator (or device) to
# finish booting and become ready to install and launch an app. Polls the
# device's own boot state rather than sleeping a fixed guess, so a healthy
# fast boot returns as soon as it is ready and a wedged one fails at the
# deadline instead of hanging its caller forever.
#
# Usage:
#   emulator_wait_ready.sh [serial]
#
# The serial defaults to $ANDROID_SERIAL, then to the first `adb devices`
# entry. Tunables (seconds) come from the environment so callers and tests
# can bound the wait:
#   ORKIGE_READY_DEADLINE   overall deadline for the whole readiness wait (default 180)
#   ORKIGE_READY_POLL       poll interval between checks (default 3)
#   ORKIGE_ADB              explicit adb binary (else the SDK path, else PATH)
#
# Exit codes:
#   0  the device reported sys.boot_completed=1 and the package manager is up
#   7  the deadline elapsed before the device was ready (the caller decides
#      whether to kill + cold-boot and retry)
#   2  no adb / no device to wait for (a setup error, not a timeout)

set -u

READY_DEADLINE="${ORKIGE_READY_DEADLINE:-180}"
POLL="${ORKIGE_READY_POLL:-3}"

log() { printf '[wait-ready] %s\n' "$*"; }

# Resolve adb: an explicit override wins, then the SDK layout package_apk.sh
# uses, then whatever is on PATH.
resolve_adb() {
	if [ -n "${ORKIGE_ADB:-}" ]; then
		# an explicit override must actually be runnable; a bad path is a
		# setup error, not something to wait out.
		if [ -x "$ORKIGE_ADB" ] || command -v "$ORKIGE_ADB" >/dev/null 2>&1; then
			printf '%s' "$ORKIGE_ADB"
			return 0
		fi
		return 1
	fi
	local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
	if [ -x "$sdk/platform-tools/adb" ]; then
		printf '%s' "$sdk/platform-tools/adb"
		return 0
	fi
	if command -v adb >/dev/null 2>&1; then
		printf 'adb'
		return 0
	fi
	return 1
}

ADB="$(resolve_adb)" || { log "no usable adb binary (set ORKIGE_ADB or ANDROID_HOME)"; exit 2; }

# Absolute wall-clock deadline, started before we even look for a device so the
# whole readiness wait - device attach + OS boot - shares the one budget.
# `date +%s` is portable across the macOS and Linux shells this runs under.
START="$(date +%s)"
deadline_reached() {
	local now
	now="$(date +%s)"
	[ "$((now - START))" -ge "$READY_DEADLINE" ]
}

SERIAL="${1:-${ANDROID_SERIAL:-}}"
if [ -z "$SERIAL" ]; then
	# No serial named: wait (bounded) for a device/emulator to ATTACH, then
	# target the first one. Called right after an emulator launch, the device
	# node takes a few seconds to appear - poll for it rather than bailing.
	log "no serial given; waiting for a device to attach (deadline ${READY_DEADLINE}s)"
	while :; do
		SERIAL="$("$ADB" devices 2>/dev/null | awk 'NR>1 && $2=="device"{print $1; exit}')"
		[ -n "$SERIAL" ] && break
		if deadline_reached; then
			log "TIMEOUT: no adb device/emulator attached within ${READY_DEADLINE}s"
			exit 7
		fi
		sleep "$POLL"
	done
fi

log "waiting for $SERIAL to be ready (deadline ${READY_DEADLINE}s, poll ${POLL}s)"

# Phase 1: the device node exists at all (emulator process attached to adb).
# `adb wait-for-device` itself can block forever, so bound it with a short
# per-probe timeout and loop against our own deadline.
while :; do
	if timeout 10 "$ADB" -s "$SERIAL" wait-for-device >/dev/null 2>&1; then
		break
	fi
	if deadline_reached; then
		log "TIMEOUT: $SERIAL never attached within ${READY_DEADLINE}s"
		exit 7
	fi
	sleep "$POLL"
done

# Phase 2: the OS finished booting (sys.boot_completed) AND the package
# manager answers - `pm path` needs to work before an install/launch will.
while :; do
	booted="$(timeout 10 "$ADB" -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' | tr -d '[:space:]')"
	if [ "$booted" = "1" ]; then
		if timeout 10 "$ADB" -s "$SERIAL" shell pm get-install-location >/dev/null 2>&1; then
			elapsed="$(( $(date +%s) - START ))"
			log "READY: $SERIAL booted in ${elapsed}s"
			exit 0
		fi
	fi
	if deadline_reached; then
		log "TIMEOUT: $SERIAL not ready within ${READY_DEADLINE}s (boot_completed='${booted:-}')"
		exit 7
	fi
	sleep "$POLL"
done
