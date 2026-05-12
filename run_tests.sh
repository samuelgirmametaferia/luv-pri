#!/bin/bash

# Configuration
LUVC="./bin/luvc"
TEST_DIR="tests"
TIMEOUT=10s
LOG_FILE="test_results.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}Starting Luv Compiler Tests...${NC}"
echo "Running tests in $TEST_DIR"
echo "Timeout: $TIMEOUT"
echo "---------------------------------------" > "$LOG_FILE"

TOTAL=0
PASSED=0
FAILED=0
TIMED_OUT=0
SEGFAULTED=0

for test_file in "$TEST_DIR"/*.lv; do
    ((TOTAL++))
    filename=$(basename "$test_file")
    echo -n "Testing $filename... "

    # Check if this test is expected to fail
    EXPECTED_TO_FAIL=0
    if grep -q "should error" "$test_file"; then
        EXPECTED_TO_FAIL=1
    fi

    # Run luvc with timeout and capture output
    start_time=$(date +%s%N)
    output=$(timeout --signal=KILL "$TIMEOUT" "$LUVC" "$test_file" 2>&1)
    exit_code=$?
    end_time=$(date +%s%N)
    
    duration=$(( (end_time - start_time) / 1000000 )) # in milliseconds

    echo "--- Test: $filename ---" >> "$LOG_FILE"
    echo "$output" >> "$LOG_FILE"
    echo "Exit Code: $exit_code" >> "$LOG_FILE"
    echo "Duration: ${duration}ms" >> "$LOG_FILE"
    echo "---------------------------------------" >> "$LOG_FILE"

    if [ $exit_code -gt 128 ]; then
        echo -e "${RED}SEGFAULT/CRASH ($exit_code)${NC} (${duration}ms)"
        ((SEGFAULTED++))
        ((FAILED++))
    elif [ $exit_code -eq 124 ] || [ $exit_code -eq 137 ]; then
        echo -e "${YELLOW}TIMEOUT${NC} (${duration}ms)"
        ((TIMED_OUT++))
    elif [ $EXPECTED_TO_FAIL -eq 1 ]; then
        if [ $exit_code -ne 0 ]; then
            echo -e "${GREEN}PASS (Expected Error)${NC} (${duration}ms)"
            ((PASSED++))
        else
            echo -e "${RED}FAIL (Expected Error but it Passed)${NC} (${duration}ms)"
            ((FAILED++))
        fi
    else
        if [ $exit_code -eq 0 ]; then
            echo -e "${GREEN}PASS${NC} (${duration}ms)"
            ((PASSED++))
        else
            echo -e "${RED}FAIL ($exit_code)${NC} (${duration}ms)"
            ((FAILED++))
        fi
    fi
done

echo "---------------------------------------"
echo -e "Total: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"
echo -e "${YELLOW}Timed Out: $TIMED_OUT${NC}"
echo -e "${RED}Segfaulted: $SEGFAULTED${NC}"

if [ $FAILED -gt 0 ] || [ $TIMED_OUT -gt 0 ]; then
    echo "Check $LOG_FILE for details."
    exit 1
fi

exit 0
