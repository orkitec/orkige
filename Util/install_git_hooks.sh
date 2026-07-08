#!/bin/bash
# install_git_hooks.sh - install the repo's git hooks (hooks are not
# versioned by git itself, so run this once per clone).
#
# pre-push: spawns Util/watch_ci.sh detached for the pushed commit, so every
# push gets its GitHub Actions runs watched in the background - the result
# arrives as a macOS notification and in ~/.orkige/ci-watch-<sha>.log.
# Disable for one push with ORKIGE_NO_CI_WATCH=1 git push.
set -eu

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOK="$REPO_ROOT/.git/hooks/pre-push"

cat > "$HOOK" <<'EOF'
#!/bin/bash
# auto-installed by Util/install_git_hooks.sh - background CI watcher
[ "${ORKIGE_NO_CI_WATCH:-0}" = "1" ] && exit 0
command -v gh >/dev/null 2>&1 || exit 0
repo_root="$(git rev-parse --show-toplevel)"
while read -r _local_ref local_sha _remote_ref _remote_sha; do
	case "$local_sha" in *[!0]*) ;; *) continue ;; esac  # skip deletes
	mkdir -p "$HOME/.orkige"
	log="$HOME/.orkige/ci-watch-$(git rev-parse --short "$local_sha").log"
	nohup "$repo_root/Util/watch_ci.sh" "$local_sha" \
		> "$log" 2>&1 < /dev/null &
	echo "[ci-watch] watching CI in the background (log: $log)"
done
exit 0
EOF
chmod +x "$HOOK"
echo "installed pre-push CI watcher hook at $HOOK"
