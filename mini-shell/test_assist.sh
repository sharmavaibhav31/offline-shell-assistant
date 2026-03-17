#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SHELL_BIN="${MYSHELL_BIN:-$BUILD_DIR/myshell}"

echo "=== Offline AI Assistant Test Suite ==="

if [ ! -x "$SHELL_BIN" ]; then
    echo "Error: Shell binary not found at $SHELL_BIN"
    exit 1
fi

PASS=0
FAIL=0

record_pass() {
    echo "✓ PASS ($1)"
    PASS=$((PASS + 1))
}

record_fail() {
    echo "✗ FAIL ($1)"
    FAIL=$((FAIL + 1))
}

# Test 1: Basic functionality
echo "Test 1: Basic assist command"
OUT=$(printf 'assist what is 2+2\nexit\n' | timeout 60 "$SHELL_BIN" 2>&1)
if echo "$OUT" | grep -q "AI:"; then
    record_pass "basic assist"
else
    record_fail "basic assist"
fi

# Test 2: Empty prompt
echo "Test 2: Empty prompt handling"
OUT=$(printf 'assist\nexit\n' | timeout 30 "$SHELL_BIN" 2>&1)
if echo "$OUT" | grep -q "Usage:"; then
    record_pass "empty prompt"
else
    record_fail "empty prompt"
fi

# Test 3: Long prompt (near limit, 500 words)
echo "Test 3: Long prompt"
LONG_PROMPT=$(python3 -c "print('word ' * 500)")
printf 'assist %s\nexit\n' "$LONG_PROMPT" | timeout 60 "$SHELL_BIN" > /dev/null 2>&1
RC=$?
if [ "$RC" -eq 0 ]; then
    record_pass "long prompt without crash"
else
    record_fail "long prompt exited $RC"
fi

# Test 4: Worker reset
echo "Test 4: Worker reset"
OUT=$(printf 'assist what is 1+1\nassist-reset\nassist what is 2+2\nexit\n' | timeout 60 "$SHELL_BIN" 2>&1)
AI_COUNT=$(echo "$OUT" | grep -c "AI:")
INIT_COUNT=$(echo "$OUT" | grep -c "AI assistant initialized")
if [ "$AI_COUNT" -eq 2 ] && [ "$INIT_COUNT" -eq 2 ]; then
    record_pass "worker reset respawned ($AI_COUNT replies, $INIT_COUNT inits)"
else
    record_fail "worker reset ($AI_COUNT replies, $INIT_COUNT inits)"
fi

# Test 5: Special characters
echo "Test 5: Special characters"
OUT=$(printf 'assist what is 2+2?\nexit\n' | timeout 60 "$SHELL_BIN" 2>&1)
if echo "$OUT" | grep -q "AI:"; then
    record_pass "special characters"
else
    record_fail "special characters"
fi

# Test 6: Model missing falls back to echo mode
echo "Test 6: Missing model falls back to echo mode"
TMP_HOME=$(mktemp -d)
OUT=$(HOME="$TMP_HOME" printf 'assist hello fallback\nexit\n' | HOME="$TMP_HOME" timeout 30 "$SHELL_BIN" 2>&1)
rm -rf "$TMP_HOME"
if echo "$OUT" | grep -q "AI: Echo: hello fallback"; then
    record_pass "echo fallback when model missing"
else
    record_fail "echo fallback (got: $OUT)"
fi

# Test 7: Killing the worker and then asking again respawns it
echo "Test 7: Killing worker and asking again respawns it"
FIFO=$(mktemp -u)
mkfifo "$FIFO"
OUT_FILE=$(mktemp)
ERR_FILE=$(mktemp)
"$SHELL_BIN" < "$FIFO" > "$OUT_FILE" 2> "$ERR_FILE" &
SHELL_PID=$!
exec 3>"$FIFO"

echo "assist what is 2+2" >&3
for _ in $(seq 1 60); do
    if grep -q "AI:" "$OUT_FILE"; then break; fi
    sleep 0.25
done
WORKER_PID=$(pgrep -P "$SHELL_PID" ai_worker | head -n 1)

if [ -n "$WORKER_PID" ]; then
    kill -9 "$WORKER_PID"
    # Wait briefly so kernel reaps/exits child before next prompt
    sleep 0.5
    echo "assist what is 3+3" >&3
    for _ in $(seq 1 60); do
        if [ "$(grep -c "AI:" "$OUT_FILE")" -ge 2 ]; then break; fi
        sleep 0.25
    done
    NEW_WORKER_PID=$(pgrep -P "$SHELL_PID" ai_worker | head -n 1)
    echo "exit" >&3
    exec 3>&-
    wait "$SHELL_PID" 2>/dev/null

    AI_COUNT=$(grep -c "AI:" "$OUT_FILE")
    if [ -n "$NEW_WORKER_PID" ] && [ "$NEW_WORKER_PID" != "$WORKER_PID" ] && [ "$AI_COUNT" -ge 2 ] && grep -q "killed by signal 9" "$ERR_FILE"; then
        record_pass "worker respawned ($WORKER_PID -> $NEW_WORKER_PID, signal 9 reported)"
    else
        record_fail "respawn check (old=$WORKER_PID new=$NEW_WORKER_PID replies=$AI_COUNT)"
    fi
else
    exec 3>&-
    kill "$SHELL_PID" 2>/dev/null || true
    wait "$SHELL_PID" 2>/dev/null || true
    record_fail "initial worker PID not found"
fi
rm -f "$FIFO" "$OUT_FILE" "$ERR_FILE"

# Test 8: 10 queries in a row inside a single worker
echo "Test 8: 10 consecutive queries in one worker"
OUT=$( (
    for i in $(seq 1 10); do
        echo "assist what is $i+$i"
    done
    echo "exit"
) | timeout 180 "$SHELL_BIN" 2>&1 )
AI_COUNT=$(echo "$OUT" | grep -c "AI:")
INIT_COUNT=$(echo "$OUT" | grep -c "AI assistant initialized")
if [ "$AI_COUNT" -eq 10 ] && [ "$INIT_COUNT" -eq 1 ]; then
    record_pass "10 consecutive queries ($AI_COUNT replies, $INIT_COUNT worker init)"
else
    record_fail "10 consecutive queries ($AI_COUNT replies, $INIT_COUNT worker inits)"
fi

echo "=== Test Suite Complete: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
