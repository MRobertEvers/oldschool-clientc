#!/bin/sh
# The Check job's tool (editor/builder/builder_ssc_main.c) end to end over a
# SYNTHETIC tree with a stand-in sscompile (no OSRS-Content, no compiler):
# an unsaved script the server left in <work>/puts is staged over the tree's
# and its error is a job line with the stage-relative path; the put is gone
# afterwards; the next run reads the disk's script again and is clean; a work
# dir inside the content tree is refused.
#
#   maped_script_check_test.sh <maped_script_check> <scratch-dir>
set -u
tool="$1"
t="$2"
fails=0
checks=0

check() {
    checks=$((checks + 1))
    if [ "$1" != 0 ]; then
        fails=$((fails + 1))
        echo "FAIL: $2"
    fi
}

rm -rf "$t"
mkdir -p "$t/content/server/scripts/areas/x/scripts" "$t/content/pack" "$t/content/configs" \
    "$t/work/puts/areas/x/scripts"
printf '[opnpc1,hans]\nmes("a");\n' > "$t/content/server/scripts/areas/x/scripts/hans.rs2"
printf '[opnpc1,hans]\nmes2("a");\n' > "$t/work/puts/areas/x/scripts/hans.rs2"
cat > "$t/fake_sscompile" <<'EOF'
#!/bin/sh
# sscompile --src <stage> ...: an error for an unknown command, else clean.
src=$2
if grep -q mes2 "$src/areas/x/scripts/hans.rs2"; then
    echo "$src/areas/x/scripts/hans.rs2:2: unknown command mes2"
    echo "sscompile: 1 compiled, 0 reused"
    exit 1
fi
echo "sscompile: 1 compiled, 0 reused"
EOF
chmod +x "$t/fake_sscompile"

out=$("$tool" "$t/fake_sscompile" "$t/content" "$t/work")
code=$?
[ "$code" = 1 ]
check $? "an unsaved script with an error exits 1 (exit $code)"
echo "$out" | grep -qx 'areas/x/scripts/hans.rs2:2: unknown command mes2'
check $? "its error is a line with the stage-relative path"
echo "$out" | grep -qx 'check: 1 unsaved script(s) over the stage'
check $? "the put is said"
echo "$out" | tail -n 1 | grep -q '^check: 1 error(s), 0 warning(s), '
check $? "the last line is the summary"
[ -z "$(find "$t/work/puts" -type f)" ]
check $? "the put is gone once staged"

out=$("$tool" "$t/fake_sscompile" "$t/content" "$t/work")
code=$?
[ "$code" = 0 ]
check $? "the next run reads the disk's script and is clean (exit $code)"
echo "$out" | tail -n 1 | grep -q '^check: ok, '
check $? "and says ok"

out=$("$tool" "$t/fake_sscompile" "$t/content" "$t/content/work")
code=$?
[ "$code" = 2 ]
check $? "a work dir inside the content tree is refused (exit $code)"
echo "$out" | grep -q 'inside the content tree'
check $? "and says why"
[ ! -e "$t/content/work" ]
check $? "and nothing is written there"

echo "maped_script_check_test: $checks checks, $fails failed"
[ "$fails" = 0 ]
