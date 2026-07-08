#!/usr/bin/env bash
# Audit every interface in the cache by running interfacex in isolated processes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

TIMEOUT_SEC="${TIMEOUT_SEC:-30}"
OUT_DIR="${OUT_DIR:-audit_out}"
REPORT="${REPORT:-audit_report.txt}"

mkdir -p "$OUT_DIR"

echo "building interfacex..."
make -s

IDS_FILE="$OUT_DIR/interface_ids.txt"
./interfacex --list > "$IDS_FILE"
TOTAL=$(wc -l < "$IDS_FILE" | tr -d ' ')
echo "auditing $TOTAL interfaces (timeout=${TIMEOUT_SEC}s each)..."

PASS=0
FAIL=0
FAIL_UNKNOWN=0
FAIL_SCRIPT=0
FAIL_HOST=0
FAIL_DECODE=0
FAIL_CRASH=0
FAIL_TIMEOUT=0
FAIL_OTHER=0

run_with_timeout() {
    local sec="$1"
    shift
    perl -e '
        use POSIX qw(:sys_wait_h);
        my $t = shift @ARGV;
        my $pid = fork();
        die "fork failed\n" unless defined $pid;
        if ($pid == 0) {
            exec @ARGV or exit 127;
        }
        eval {
            local $SIG{ALRM} = sub { kill "TERM", $pid; die "timeout\n" };
            alarm $t;
            waitpid $pid, 0;
            alarm 0;
        };
        if ($@) {
            waitpid $pid, 0;
            exit 124;
        }
        exit($? >> 8);
    ' "$sec" "$@"
}

classify_failure() {
    local log="$1"
    local exit_code="$2"
    local timed_out="$3"

    if [[ "$timed_out" == "1" ]]; then
        echo "timeout"
        return
    fi
    if [[ "$exit_code" -ge 128 ]] || [[ "$exit_code" -eq 134 ]] || [[ "$exit_code" -eq 139 ]]; then
        echo "crash"
        return
    fi
    if grep -q "unknown opcode:" "$log" 2>/dev/null; then
        echo "unknown-opcode"
        return
    fi
    if grep -q "error running script" "$log" 2>/dev/null; then
        echo "script-error"
        return
    fi
    if grep -qE "VMHost: unknown request kind|failed to resolve node|failed to resolve script" "$log" 2>/dev/null; then
        echo "host-error"
        return
    fi
    if grep -q "failed to decode component" "$log" 2>/dev/null; then
        echo "decode-error"
        return
    fi
    if [[ "$exit_code" -ne 0 ]]; then
        echo "exit-$exit_code"
        return
    fi
    echo "pass"
}

: > "$REPORT"
echo "interfacex interface audit" >> "$REPORT"
echo "date: $(date)" >> "$REPORT"
echo "total: $TOTAL" >> "$REPORT"
echo "" >> "$REPORT"

while IFS= read -r id; do
  [[ -z "$id" ]] && continue
  log="$OUT_DIR/${id}.log"
  timed_out=0
  exit_code=0

  if ! run_with_timeout "$TIMEOUT_SEC" ./interfacex --no-bmp "$id" >"$log" 2>&1; then
    exit_code=$?
    if [[ "$exit_code" -eq 124 ]]; then
      timed_out=1
      exit_code=124
    fi
  fi

  category=$(classify_failure "$log" "$exit_code" "$timed_out")

  if [[ "$category" == "pass" ]]; then
    PASS=$((PASS + 1))
    rm -f "$log"
  else
    FAIL=$((FAIL + 1))
    case "$category" in
      unknown-opcode) FAIL_UNKNOWN=$((FAIL_UNKNOWN + 1)) ;;
      script-error) FAIL_SCRIPT=$((FAIL_SCRIPT + 1)) ;;
      host-error) FAIL_HOST=$((FAIL_HOST + 1)) ;;
      decode-error) FAIL_DECODE=$((FAIL_DECODE + 1)) ;;
      crash) FAIL_CRASH=$((FAIL_CRASH + 1)) ;;
      timeout) FAIL_TIMEOUT=$((FAIL_TIMEOUT + 1)) ;;
      *) FAIL_OTHER=$((FAIL_OTHER + 1)) ;;
    esac
    first_line=$(grep -m1 -E "unknown opcode:|error running script|VMHost: unknown|failed to resolve|failed to decode|Assertion|Segmentation|Abort" "$log" 2>/dev/null || head -1 "$log")
    echo "FAIL id=$id category=$category exit=$exit_code: $first_line" >> "$REPORT"
  fi

  done_count=$((PASS + FAIL))
  if (( done_count % 50 == 0 )); then
    echo "progress: $done_count / $TOTAL (pass=$PASS fail=$FAIL)"
  fi
done < "$IDS_FILE"

echo "" >> "$REPORT"
echo "summary:" >> "$REPORT"
echo "  pass: $PASS" >> "$REPORT"
echo "  fail: $FAIL" >> "$REPORT"
echo "  unknown-opcode: $FAIL_UNKNOWN" >> "$REPORT"
echo "  script-error: $FAIL_SCRIPT" >> "$REPORT"
echo "  host-error: $FAIL_HOST" >> "$REPORT"
echo "  decode-error: $FAIL_DECODE" >> "$REPORT"
echo "  crash: $FAIL_CRASH" >> "$REPORT"
echo "  timeout: $FAIL_TIMEOUT" >> "$REPORT"
echo "  other: $FAIL_OTHER" >> "$REPORT"

echo ""
echo "done: pass=$PASS fail=$FAIL"
echo "report: $SCRIPT_DIR/$REPORT"
cat "$REPORT"
