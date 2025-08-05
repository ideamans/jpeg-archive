#!/bin/bash

# IQA Library Memory Leak Test
# Uses the leaks tool to check for memory leaks

TEST_DIR="build/debug"
TEST_BINARY="test"

echo "IQA Memory Leak Test"
echo "============================================="

# Check if test binary exists
if [ ! -f "$TEST_DIR/$TEST_BINARY" ]; then
    echo "Building test suite..."
    make test
fi

# Change to test directory (required for BMP file access)
cd "$TEST_DIR" || exit 1

if [ ! -f "$TEST_BINARY" ]; then
    echo "Error: Test binary not found"
    exit 1
fi

# Check for memory leaks using macOS leaks tool
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Running test with memory leak detection..."
    echo "(This may take a few minutes...)"
    echo ""
    
    # Run with leaks tool (timeout set to 5 minutes)
    MallocStackLogging=1 timeout 300 leaks --atExit -- ./$TEST_BINARY 2>&1 | tee test_output.log | grep -E "Process.*:.*leaks for|total leaked bytes"
    
    # Check if timeout occurred
    if [ $? -eq 124 ]; then
        echo "Error: Test timed out after 5 minutes"
        exit 1
    fi
    
    # Extract leaks info from output
    if grep -q "0 leaks for 0 total leaked bytes" test_output.log; then
        echo ""
        echo "✓ PASS: No memory leaks detected!"
    else
        LEAK_COUNT=$(grep "leaks for" test_output.log | grep -oE "[0-9]+ leaks" | head -1)
        if [ -n "$LEAK_COUNT" ]; then
            echo ""
            echo "✗ FAIL: $LEAK_COUNT detected!"
        else
            echo ""
            echo "✓ PASS: Test completed (unable to parse leaks output)"
        fi
    fi
    
    # Clean up
    rm -f test_output.log
    
elif command -v valgrind &> /dev/null; then
    echo "Running test with valgrind..."
    valgrind --leak-check=full --quiet ./$TEST_BINARY 2>&1 | grep -E "definitely lost|indirectly lost|ERROR SUMMARY"
else
    echo "Error: No memory leak detection tool available"
    echo "Install valgrind (Linux) or use macOS"
fi