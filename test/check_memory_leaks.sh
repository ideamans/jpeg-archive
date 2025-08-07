#!/bin/bash

# Memory leak test script for jpeg-archive using macOS leaks command
# Tests test/test and test/libjpegarchive binaries 3 times each

set -e

echo "=== Memory Leak Test for jpeg-archive ==="
echo "Testing with macOS leaks command (3 iterations per binary)"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if leaks command is available
if ! command -v leaks &> /dev/null; then
    echo -e "${RED}Error: 'leaks' command not found. This script requires macOS.${NC}"
    exit 1
fi

# Build binaries if needed
cd ..
if [ ! -f test/test ]; then
    echo "Building test binary..."
    make test 2>&1 | tail -3
fi

if [ ! -f test/libjpegarchive ]; then
    echo "Building libjpegarchive test binary..."
    make test-libjpegarchive 2>&1 | tail -3
fi

cd test

# Summary variables
test_result="PASS"
libjpegarchive_result="PASS"

echo "=== Testing test/test ==="
if [ -f "./test" ]; then
    for i in {1..3}; do
        echo -n "  Run $i/3: "
        if leaks --atExit -- ./test 2>&1 | grep -q "0 leaks for 0 total leaked bytes"; then
            echo -e "${GREEN}OK - No leaks${NC}"
        else
            echo -e "${RED}LEAK DETECTED${NC}"
            leaks --atExit -- ./test 2>&1 | grep "leaks for" || true
            test_result="FAIL"
        fi
    done
else
    echo -e "${RED}test/test not found${NC}"
    test_result="NOT FOUND"
fi

echo ""
echo "=== Testing test/libjpegarchive ==="
if [ -f "./libjpegarchive" ]; then
    for i in {1..3}; do
        echo -n "  Run $i/3: "
        if timeout 30 leaks --atExit -- ./libjpegarchive 2>&1 | grep -q "0 leaks for 0 total leaked bytes"; then
            echo -e "${GREEN}OK - No leaks${NC}"
        else
            echo -e "${YELLOW}LEAK DETECTED or TIMEOUT${NC}"
            timeout 30 leaks --atExit -- ./libjpegarchive 2>&1 | grep "leaks for" || true
            libjpegarchive_result="FAIL"
        fi
    done
else
    echo -e "${RED}test/libjpegarchive not found${NC}"
    libjpegarchive_result="NOT FOUND"
fi

echo ""
echo "=== FINAL SUMMARY ==="
echo -n "test/test:          "
if [ "$test_result" = "PASS" ]; then
    echo -e "${GREEN}PASS - No memory leaks${NC}"
elif [ "$test_result" = "NOT FOUND" ]; then
    echo -e "${YELLOW}NOT FOUND${NC}"
else
    echo -e "${RED}FAIL - Memory leaks detected${NC}"
fi

echo -n "test/libjpegarchive: "
if [ "$libjpegarchive_result" = "PASS" ]; then
    echo -e "${GREEN}PASS - No memory leaks${NC}"
elif [ "$libjpegarchive_result" = "NOT FOUND" ]; then
    echo -e "${YELLOW}NOT FOUND${NC}"
else
    echo -e "${RED}FAIL - Memory leaks detected${NC}"
fi

echo ""
if [ "$test_result" = "PASS" ] && [ "$libjpegarchive_result" = "PASS" ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed or had issues${NC}"
    exit 1
fi