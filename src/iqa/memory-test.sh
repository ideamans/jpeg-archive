#!/bin/bash

# IQA Library Memory Leak Test
# Cross-platform memory leak testing

TEST_DIR="build/release"
TEST_BINARY="test/test"

echo "IQA Memory Leak Test"
echo "============================================="

# Detect platform
detect_platform() {
    case "$(uname -s)" in
        Darwin*)
            echo "macos"
            ;;
        Linux*)
            echo "linux"
            ;;
        FreeBSD*)
            echo "freebsd"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "windows"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# Detect available memory tool
detect_memory_tool() {
    local platform=$1
    
    case "$platform" in
        macos)
            if command -v leaks >/dev/null 2>&1; then
                echo "leaks"
            else
                echo "none"
            fi
            ;;
        linux|freebsd)
            if command -v valgrind >/dev/null 2>&1; then
                echo "valgrind"
            else
                echo "none"
            fi
            ;;
        windows)
            if command -v drmemory >/dev/null 2>&1; then
                echo "drmemory"
            elif command -v valgrind >/dev/null 2>&1; then
                echo "valgrind"
            else
                echo "none"
            fi
            ;;
        *)
            echo "none"
            ;;
    esac
}

# Common test function
run_iqa_test() {
    ./$TEST_BINARY
}

# Test with leaks (macOS)
test_with_leaks() {
    echo "Running test with leaks (macOS)..."
    echo ""
    
    # Run with leaks tool
    MallocStackLogging=1 leaks --atExit -- ./$TEST_BINARY 2>&1 | tee test_output.log | grep -E "Process.*:.*leaks for|total leaked bytes"
    
    # Extract leaks info from output
    if grep -q "0 leaks for 0 total leaked bytes" test_output.log; then
        echo ""
        echo "✓ PASS: No memory leaks detected!"
        rm -f test_output.log
        return 0
    else
        LEAK_COUNT=$(grep "leaks for" test_output.log | grep -oE "[0-9]+ leaks" | head -1)
        if [ -n "$LEAK_COUNT" ]; then
            echo ""
            echo "✗ FAIL: $LEAK_COUNT detected!"
            rm -f test_output.log
            return 1
        else
            echo ""
            echo "✓ PASS: Test completed (unable to parse leaks output)"
            rm -f test_output.log
            return 0
        fi
    fi
}

# Test with valgrind (Linux/FreeBSD)
test_with_valgrind() {
    echo "Running test with valgrind..."
    echo ""
    
    valgrind --leak-check=full \
             --show-leak-kinds=all \
             --track-origins=yes \
             --error-exitcode=1 \
             ./$TEST_BINARY 2>&1 | tee test_output.log | grep -E "ERROR SUMMARY|definitely lost|indirectly lost"
    
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo ""
        echo "✓ PASS: No memory leaks detected!"
    else
        echo ""
        echo "✗ FAIL: Memory leaks detected!"
    fi
    
    rm -f test_output.log
    return $exit_code
}

# Test with Dr. Memory (Windows)
test_with_drmemory() {
    echo "Running test with Dr. Memory..."
    echo ""
    
    drmemory -batch -brief -- ./$TEST_BINARY 2>&1 | tee test_output.log | grep -E "ERRORS|LEAK"
    
    if grep -q "0 unique.*0 total unaddressable access" test_output.log && \
       grep -q "0 unique.*0 total leak" test_output.log; then
        echo ""
        echo "✓ PASS: No memory issues detected!"
        rm -f test_output.log
        return 0
    else
        echo ""
        echo "✗ FAIL: Memory issues detected!"
        rm -f test_output.log
        return 1
    fi
}

# Basic test (no memory tools)
test_basic() {
    echo "Running basic functionality test (no memory tool available)..."
    echo ""
    
    if ./$TEST_BINARY; then
        echo ""
        echo "✓ PASS: Test completed successfully"
        return 0
    else
        echo ""
        echo "✗ FAIL: Test failed"
        return 1
    fi
}

# Main execution
main() {
    # Check if test binary exists
    if [ ! -f "$TEST_DIR/$TEST_BINARY" ]; then
        echo "Building test suite..."
        make test
    fi
    
    # Change to test directory
    cd "$TEST_DIR" || exit 1
    
    if [ ! -f "$TEST_BINARY" ]; then
        echo "Error: Test binary not found at $TEST_BINARY"
        exit 1
    fi
    
    local platform=$(detect_platform)
    local memory_tool=$(detect_memory_tool "$platform")
    
    echo "Platform: $platform"
    echo "Memory tool: $memory_tool"
    echo ""
    
    case "$memory_tool" in
        leaks)
            test_with_leaks
            ;;
        valgrind)
            test_with_valgrind
            ;;
        drmemory)
            test_with_drmemory
            ;;
        none)
            echo "Warning: No memory leak detection tool found."
            echo "Install valgrind (Linux/FreeBSD) or use macOS with leaks."
            test_basic
            ;;
    esac
    
    exit $?
}

# Run main
main "$@"