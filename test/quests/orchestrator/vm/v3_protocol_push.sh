#!/bin/bash
# Push protocol work to v3 (docs/QUEST_ORCHESTRATOR.md "Changing the protocol or
# shared tooling") without checking out the whole tree and without ever
# committing a conflict.
#
#   v3_protocol_push.sh <patch file> "<commit message>" <path>...
#
# Makes a sparse v3 worktree holding only the dirs of <path>..., applies the
# patch with --3way, REFUSES if any file is left unmerged or carries a conflict
# marker (2026-10-02: a push of 240ef00ff committed markers into two authoring
# docs), commits the named paths and pushes HEAD:v3. On a rejected push it
# fetches and retries the whole apply once. Never forces.
set -euo pipefail
repo=$(git rev-parse --show-toplevel)
patch=$(realpath "$1"); msg=$2; shift 2
wt="$repo/build/orchestrator/worktrees/v3"
attempt() {
	git -C "$repo" fetch -q origin v3
	git -C "$repo" worktree remove --force "$wt" 2>/dev/null || true
	rm -rf "$wt"; git -C "$repo" worktree prune
	git -C "$repo" worktree add -q --no-checkout --detach "$wt" FETCH_HEAD
	local dirs=()
	for p in "$@"; do dirs+=("/$(dirname "$p")/"); done
	git -C "$wt" sparse-checkout set --no-cone "${dirs[@]}" >/dev/null
	git -C "$wt" checkout -q
	git -C "$wt" apply --3way "$patch" || true
	local unmerged
	unmerged=$(git -C "$wt" diff --name-only --diff-filter=U)
	if [ -n "$unmerged" ] || git -C "$wt" grep -n -E '^(<<<<<<< |>>>>>>> )' -- "$@" >/dev/null; then
		echo "v3_protocol_push: REFUSED -- unresolved conflict in: ${unmerged:-a file with markers}" >&2
		echo "resolve it by hand in $wt (keep both sides' work), then commit and push from there" >&2
		exit 2
	fi
	git -C "$wt" add -- "$@"
	git -C "$wt" commit -q -m "$msg" -- "$@"
	git -C "$wt" push origin HEAD:v3
}
if ! attempt "$@"; then
	echo "v3_protocol_push: push rejected or failed once; retrying on a fresh origin/v3" >&2
	attempt "$@"
fi
git -C "$repo" worktree remove --force "$wt"; git -C "$repo" worktree prune
