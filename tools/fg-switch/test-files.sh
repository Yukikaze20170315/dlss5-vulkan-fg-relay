#!/usr/bin/env bash
# File-switch tests on a scratch copy (never the real game folder). Usage: test-files.sh NEW_DIR
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cli() { bash "$ROOT/tools/fg-switch/cli.sh" "$@"; }
SETS="$ROOT/evidence/sl-compat-20261002"
D="$1"; [ -e "$D" ] && { echo "refusing existing $D"; exit 2; }
tasklist 2>/dev/null | grep -qi '^Endfield.exe' && { echo "the real game is running; close it first"; exit 2; }
mkdir -p "$D/game"; G="$D/game"; W="$(cygpath -w "$G")"
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=$((fail+1)); fi; }
state() { cli --state "$W" | grep -o 'whole=-\?[0-9]*'; }

cp "$SETS"/sl-2.10.3/*.dll "$G/"
check "not a game folder is refused" 'cli --switch "$W" 1 | grep -q "refused"'
echo dummy > "$G/sl.interposer.dll"; cp /c/Windows/System32/PING.EXE "$G/Endfield.exe"
check "stock set recognised" '[ "$(state)" = whole=0 ]'
check "switch to 2.14.1" 'cli --switch "$W" 1 | grep -q "switch: ok" && [ "$(state)" = whole=1 ]'
check "upgraded files match archive" '(cd "$G" && sed "s/ \*/  /" "$SETS/sl-2.14.1/SHA256SUMS.txt" | sha256sum -c --quiet)'
check "interposer untouched" '[ "$(cat "$G/sl.interposer.dll")" = dummy ]'
check "switch back to 2.10.3" 'cli --switch "$W" 0 | grep -q "switch: ok" && [ "$(state)" = whole=0 ]'
check "stock files match archive" '(cd "$G" && sed "s/ \*/  /" "$SETS/sl-2.10.3/SHA256SUMS.txt" | sha256sum -c --quiet)'
cp "$SETS"/sl-2.14.1-stockfg/*.dll "$G/"
check "mixed set recognised" '[ "$(state)" = whole=-1 ]'
check "mixed set completes to 2.14.1" 'cli --switch "$W" 1 | grep -q "switch: ok" && [ "$(state)" = whole=1 ]'
before=$(sha256sum "$G"/*.dll | sha256sum)
printf x >> "$G/sl.dlss_g.dll"; tampered=$(sha256sum "$G"/*.dll | sha256sum)
check "unknown version refused" 'cli --switch "$W" 0 | grep -q "refused"'
check "unknown version left unchanged" '[ "$(sha256sum "$G"/*.dll | sha256sum)" = "$tampered" ]'
cp "$SETS/sl-2.14.1/sl.dlss_g.dll" "$G/"
check "restored after tamper" '[ "$(sha256sum "$G"/*.dll | sha256sum)" = "$before" ]'
"$G/Endfield.exe" -n 15 127.0.0.1 >/dev/null & pid=$!; sleep 1
check "running game refused" 'cli --switch "$W" 0 | grep -q "refused"'
check "running game left unchanged" '[ "$(state)" = whole=1 ]'
wait $pid 2>/dev/null
check "no leftover temp files" '! ls "$G" | grep -q fgswitch-new'
echo "file tests: failures=$fail"
exit $fail
