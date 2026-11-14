#!/bin/sh
# Runs every tests/*.slip program and compares stdout+stderr and the exit code
# with tests/<name>.out (and tests/<name>.code when the exit code is not 0).
# A tests/<name>.in file, when present, is fed on stdin.
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
SLIP=$ROOT/slip
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0
for t in tests/*.slip; do
    name=${t%.slip}
    input=/dev/null
    [ -f "$name.in" ] && input=$ROOT/$name.in
    # run inside a scratch directory so file IO tests can use relative paths
    (cd "$TMP" && "$SLIP" "$ROOT/$t" arg1 arg2 <"$input" >out 2>&1; echo $? >code)
    want_code=0
    [ -f "$name.code" ] && want_code=$(cat "$name.code")
    if cmp -s "$TMP/out" "$name.out" && [ "$(cat "$TMP/code")" = "$want_code" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $t (exit $(cat "$TMP/code"), want $want_code)"
        diff "$name.out" "$TMP/out" | head -20
    fi
    rm -f "$TMP"/*
done
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
