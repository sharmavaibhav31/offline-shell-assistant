#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SHELL_BIN="${MYSHELL_BIN:-$BUILD_DIR/myshell}"

echo "=== Stress Test ==="

if [ ! -x "$SHELL_BIN" ]; then
    echo "Error: Shell binary not found at $SHELL_BIN"
    exit 1
fi

PASS=0
FAIL=0

# Test 1: 10 rapid consecutive queries
echo "Test 1: 10 consecutive queries"
STRESS_OUT=$(mktemp)
(
for i in $(seq 1 10); do
    echo "assist what is $i+$i"
done
echo "exit"
) | timeout 180 "$SHELL_BIN" > "$STRESS_OUT" 2>&1

RESPONSES=$(grep -c "AI:" "$STRESS_OUT")
INITS=$(grep -c "AI assistant initialized" "$STRESS_OUT")
echo "Received $RESPONSES responses ($INITS worker init)"
rm -f "$STRESS_OUT"

if [ "$RESPONSES" -eq 10 ] && [ "$INITS" -eq 1 ]; then
    echo "✓ PASS (10/10 responses in single worker)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL ($RESPONSES/10 responses, $INITS inits)"
    FAIL=$((FAIL + 1))
fi

# Test 2: Worker crash recovery via FIFO
echo "Test 2: Worker crash recovery"
FIFO=$(mktemp -u)
mkfifo "$FIFO"
OUT_FILE=$(mktemp)
ERR_FILE=$(mktemp)
"$SHELL_BIN" < "$FIFO" > "$OUT_FILE" 2> "$ERR_FILE" &
SHELL_PID=$!
exec 3>"$FIFO"

echo "assist test1" >&3
for _ in $(seq 1 60); do
    if grep -q "AI:" "$OUT_FILE"; then break; fi
    sleep 0.25
done

WORKER_PID=$(pgrep -P "$SHELL_PID" ai_worker | head -n 1)
if [ -n "$WORKER_PID" ]; then
    echo "Killing worker PID $WORKER_PID"
    kill -9 "$WORKER_PID"
    sleep 0.5

    echo "assist test2" >&3
    for _ in $(seq 1 60); do
        if [ "$(grep -c "AI:" "$OUT_FILE")" -ge 2 ]; then break; fi
        sleep 0.25
    done

    NEW_WORKER_PID=$(pgrep -P "$SHELL_PID" ai_worker | head -n 1)
    echo "exit" >&3
    exec 3>&-
    wait "$SHELL_PID" 2>/dev/null

    if [ -n "$NEW_WORKER_PID" ] && [ "$NEW_WORKER_PID" != "$WORKER_PID" ]; then
        echo "✓ PASS (worker respawned: $WORKER_PID -> $NEW_WORKER_PID)"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL (worker did not respawn)"
        FAIL=$((FAIL + 1))
    fi
else
    exec 3>&-
    kill "$SHELL_PID" 2>/dev/null || true
    wait "$SHELL_PID" 2>/dev/null || true
    echo "✗ FAIL (worker did not start)"
    FAIL=$((FAIL + 1))
fi
rm -f "$FIFO" "$OUT_FILE" "$ERR_FILE"

# Test 3: Memory leak check (requires valgrind)
echo "Test 3: Memory leak check"
if command -v valgrind >/dev/null 2>&1; then
    TMP_HOME=$(mktemp -d)
    HOME="$TMP_HOME" valgrind --leak-check=full --error-exitcode=1 \
        "$SHELL_BIN" < <(printf 'assist test\nexit\n') > /dev/null 2>&1
    RC=$?
    rm -rf "$TMP_HOME"
    if [ "$RC" -eq 0 ]; then
        echo "✓ PASS (no memory leaks detected)"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL (valgrind reported leaks/errors)"
        FAIL=$((FAIL + 1))
    fi
else
    echo "- SKIP (valgrind not installed)"
fi

echo "=== Stress Test Complete: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
