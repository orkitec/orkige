#!/bin/bash
# watch_ci.sh [commit-sha] - watch the GitHub Actions runs for a commit and
# report the outcome. Designed to run detached in the background after a push
# (the pre-push hook installed by Util/install_git_hooks.sh spawns it):
#
#   Util/watch_ci.sh &            # watch HEAD's runs
#   Util/watch_ci.sh <sha>        # watch a specific commit's runs
#
# Needs the gh CLI (authenticated). Polls until every workflow run for the
# commit completes (cold vcpkg builds take 1-2h, so the timeout is generous),
# then prints a per-run verdict; on failure it appends the failing steps' log
# tail. On macOS a notification pops in every case. Exit code: 0 = all runs
# green, 1 = something failed, 2 = gave up waiting.
set -u

SHA="${1:-$(git rev-parse HEAD)}"
SHORT_SHA="$(git rev-parse --short "$SHA" 2>/dev/null || echo "$SHA")"
POLL_SECONDS=60
APPEAR_TIMEOUT_SECONDS=300     # how long to wait for runs to be created
TOTAL_TIMEOUT_SECONDS=10800    # 3h: covers cold vcpkg builds

notify() { # $1 = title, $2 = body (best-effort, macOS only)
	command -v osascript >/dev/null 2>&1 && osascript -e \
		"display notification \"$2\" with title \"$1\"" >/dev/null 2>&1
}

runs_json() {
	gh run list --commit "$SHA" \
		--json databaseId,name,status,conclusion,url 2>/dev/null
}

echo "[ci-watch] watching CI for $SHORT_SHA ..."
waited=0
while true; do
	count=$(runs_json | /usr/bin/python3 -c \
		'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)
	[ "$count" -gt 0 ] && break
	if [ "$waited" -ge "$APPEAR_TIMEOUT_SECONDS" ]; then
		echo "[ci-watch] no runs appeared for $SHORT_SHA after ${waited}s - giving up"
		notify "Orkige CI" "no runs appeared for $SHORT_SHA"
		exit 2
	fi
	sleep 15; waited=$((waited + 15))
done

waited=0
while true; do
	json=$(runs_json)
	pending=$(echo "$json" | /usr/bin/python3 -c 'import json,sys
runs = json.load(sys.stdin)
print(sum(1 for r in runs if r["status"] != "completed"))')
	[ "$pending" -eq 0 ] && break
	if [ "$waited" -ge "$TOTAL_TIMEOUT_SECONDS" ]; then
		echo "[ci-watch] still running after ${waited}s - giving up (check manually)"
		notify "Orkige CI" "$SHORT_SHA still running after $((waited / 60))min"
		exit 2
	fi
	sleep "$POLL_SECONDS"; waited=$((waited + POLL_SECONDS))
done

echo "$json" | /usr/bin/python3 -c 'import json,sys
runs = json.load(sys.stdin)
for r in runs:
    print("[ci-watch] %-12s %s  %s" % (r["conclusion"], r["name"], r["url"]))'

failed_ids=$(echo "$json" | /usr/bin/python3 -c 'import json,sys
runs = json.load(sys.stdin)
print("\n".join(str(r["databaseId"]) for r in runs
                if r["conclusion"] not in ("success", "skipped")))')

if [ -n "$failed_ids" ]; then
	for id in $failed_ids; do
		echo "[ci-watch] ---- failing steps of run $id (log tail) ----"
		gh run view "$id" --log-failed 2>/dev/null | tail -n 60
	done
	notify "Orkige CI FAILED" "commit $SHORT_SHA - see terminal/log"
	exit 1
fi
notify "Orkige CI passed" "commit $SHORT_SHA is green"
echo "[ci-watch] all green for $SHORT_SHA"
exit 0
