#!/usr/bin/env bash
# Runs EndfieldFGSwitch.exe in a test mode, waits for it and prints its output file and exit code.
EXE="$(cd "$(dirname "$0")" && pwd)/build/EndfieldFGSwitch.exe"
OUT="$LOCALAPPDATA/EndfieldFGSwitch/cli-output.txt"
rm -f "$OUT"
"$EXE" "$@"
code=$?
cat "$OUT" 2>/dev/null | sed 's/^\xEF\xBB\xBF//'
echo "exit=$code"
