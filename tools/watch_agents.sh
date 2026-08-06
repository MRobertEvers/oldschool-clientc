#!/usr/bin/env bash
# Watch subagent / workflow transcripts live.
#
#   tools/watch_agents.sh                 follow the most recently active agent
#   tools/watch_agents.sh -l              list sessions, workflows and agents
#   tools/watch_agents.sh -n 200          print the last N records, no follow
#   tools/watch_agents.sh wf_a61688bd-61d follow a specific workflow
#   tools/watch_agents.sh -a agent-a1352… follow one specific agent
#   tools/watch_agents.sh -s <session-id> pick a session other than the newest
#
# Why this exists: a workflow's task-output file (tasks/<id>.output) stays EMPTY
# until the whole workflow finishes, so tailing it shows nothing for however long
# the work takes. The live stream is the per-agent JSONL under the session's
# subagents/ directory. This finds the most recently written one and prints
# assistant text plus the tool calls, which is what is actually worth watching.
#
# Records are JSON per line; a tool RESULT is deliberately not printed (they are
# huge and mostly noise). Use -v to include them truncated.
set -uo pipefail

ROOT=~/.claude/projects/-Users-matthewevers-Documents-git-repos-3draster

FOLLOW=1
LINES=60
RUN=""
AGENT=""
SESSION=""
VERBOSE=0

while [ $# -gt 0 ]; do
    case "$1" in
        -l|--list) LIST=1; shift ;;
        -n) LINES=$2; FOLLOW=0; shift 2 ;;
        -a) AGENT=$2; shift 2 ;;
        -s) SESSION=$2; shift 2 ;;
        -v) VERBOSE=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        wf_*) RUN=$1; shift ;;
        *) echo "unknown argument: $1 (try -h)" >&2; exit 2 ;;
    esac
done

[ -d "$ROOT" ] || { echo "no session data under $ROOT" >&2; exit 1; }

# Newest session that actually has subagents, not merely the newest session.
if [ -z "$SESSION" ]; then
    SESSION=$(for d in "$ROOT"/*/subagents; do
                  [ -d "$d" ] && echo "$(stat -f '%m' "$d") $(basename "$(dirname "$d")")"
              done 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2)
fi
[ -n "$SESSION" ] || { echo "no session with subagents found" >&2; exit 1; }
SUB="$ROOT/$SESSION/subagents"

if [ "${LIST:-0}" = 1 ]; then
    echo "session $SESSION"
    for d in $(ls -td "$SUB"/workflows/wf_* "$SUB"/* 2>/dev/null | grep -v '/workflows$'); do
        [ -d "$d" ] || continue
        n=$(ls "$d"/agent-*.jsonl 2>/dev/null | wc -l | tr -d ' ')
        [ "$n" = 0 ] && continue
        printf '\n  %s  (%s agents, last write %s)\n' \
            "$(basename "$d")" "$n" "$(date -r "$d" '+%m-%d %H:%M')"
        for f in $(ls -t "$d"/agent-*.jsonl 2>/dev/null); do
            printf '      %-34s %8s KB  %s\n' "$(basename "$f")" \
                "$(( $(stat -f '%z' "$f") / 1024 ))" "$(date -r "$f" '+%H:%M:%S')"
        done
    done
    exit 0
fi

if [ -n "$AGENT" ]; then
    FILE=$(find "$SUB" -name "${AGENT}*.jsonl" 2>/dev/null | head -1)
elif [ -n "$RUN" ]; then
    FILE=$(ls -t "$SUB"/workflows/"$RUN"/agent-*.jsonl 2>/dev/null | head -1)
else
    FILE=$(ls -t "$SUB"/workflows/wf_*/agent-*.jsonl "$SUB"/*/agent-*.jsonl 2>/dev/null | head -1)
fi
[ -n "${FILE:-}" ] && [ -f "$FILE" ] || {
    echo "no agent transcript found (try -l)" >&2; exit 1; }

echo "# $(dirname "$FILE" | xargs basename) / $(basename "$FILE")" >&2
echo "# $(( $(stat -f '%z' "$FILE") / 1024 )) KB, last write $(date -r "$FILE" '+%H:%M:%S')" >&2

export WATCH_VERBOSE=$VERBOSE
FILTER='
import sys, json, os
verbose = os.environ.get("WATCH_VERBOSE") == "1"
for line in sys.stdin:
    try:
        j = json.loads(line)
    except Exception:
        continue
    m = j.get("message") or {}
    c = m.get("content")
    if isinstance(c, str):
        if c.strip():
            print(". " + c[:200], flush=True)
        continue
    if not isinstance(c, list):
        continue
    for b in c:
        if not isinstance(b, dict):
            continue
        kind = b.get("type")
        if kind == "text" and b.get("text", "").strip():
            print("\n=== " + (m.get("role") or "?").upper(), flush=True)
            print(b["text"][:1500], flush=True)
        elif kind == "tool_use":
            i = b.get("input", {}) or {}
            d = i.get("description") or i.get("command") or i.get("file_path") or ""
            print("  -> %-8s %s" % (b.get("name", "?"),
                                    str(d).replace("\n", " ")[:120]), flush=True)
        elif kind == "tool_result" and verbose:
            r = b.get("content")
            if isinstance(r, list):
                r = " ".join(x.get("text", "") for x in r if isinstance(x, dict))
            if isinstance(r, str) and r.strip():
                print("     | " + r.replace("\n", " ")[:200], flush=True)
'

if [ "$FOLLOW" = 1 ]; then
    tail -f -n 40 "$FILE" | python3 -u -c "$FILTER"
else
    tail -n "$LINES" "$FILE" | python3 -u -c "$FILTER"
fi
