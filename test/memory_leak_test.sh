#!/bin/bash

# Cross-platform memory leak testing script
# Supports: macOS (leaks), Linux (valgrind), FreeBSD (valgrind), Windows/MSYS2 (basic testing)

# Test configuration
ITERATIONS=3

# Detect platform and available tools
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

# Detect available memory leak detection tool
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
            # Windows typically doesn't have memory tools in CI
            echo "none"
            ;;
        *)
            echo "none"
            ;;
    esac
}

# Test with leaks (macOS)
test_with_leaks() {
    local test_name=$1
    local binary=$2
    shift 2
    local args="$@"
    
    echo "  Testing $test_name with leaks..."
    
    for i in $(seq 1 $ITERATIONS); do
        echo "    Iteration $i/$ITERATIONS"
        leaks --atExit -- $binary $args >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "      ✓ No leaks detected"
        else
            echo "      ✗ Leaks detected"
            return 1
        fi
    done
    return 0
}

# Test with valgrind (Linux/FreeBSD)
test_with_valgrind() {
    local test_name=$1
    local binary=$2
    shift 2
    local args="$@"
    
    echo "  Testing $test_name with valgrind..."
    
    for i in $(seq 1 $ITERATIONS); do
        echo "    Iteration $i/$ITERATIONS"
        valgrind --leak-check=full --error-exitcode=1 --quiet \
                 $binary $args >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "      ✓ No leaks detected"
        else
            echo "      ✗ Leaks detected"
            return 1
        fi
    done
    return 0
}

# Basic functionality test (when no memory tools available)
test_basic() {
    local test_name=$1
    local binary=$2
    shift 2
    local args="$@"
    
    echo "  Basic test for $test_name..."
    
    for i in $(seq 1 $ITERATIONS); do
        echo "    Iteration $i/$ITERATIONS"
        $binary $args >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "      ✓ Completed successfully"
        else
            echo "      ✗ Failed"
            return 1
        fi
    done
    return 0
}

# Main execution
main() {
    local platform=$(detect_platform)
    local memory_tool=$(detect_memory_tool "$platform")
    
    echo "==================================="
    echo "Memory Leak Testing"
    echo "==================================="
    echo "Platform: $platform"
    echo "Memory tool: $memory_tool"
    echo ""
    
    # Ensure we're in the project root
    if [ ! -f "jpeg-recompress" ]; then
        echo "Error: jpeg-recompress not found. Please run from project root after building."
        exit 1
    fi
    
    # Select test method based on available tool
    case "$memory_tool" in
        leaks)
            TEST_CMD="test_with_leaks"
            ;;
        valgrind)
            TEST_CMD="test_with_valgrind"
            ;;
        *)
            TEST_CMD="test_basic"
            if [ "$memory_tool" = "none" ]; then
                echo "Warning: No memory leak detection tool found. Running basic tests only."
                echo ""
            fi
            ;;
    esac
    
    local exit_code=0
    
    # Test jpeg-recompress
    echo "=== Testing jpeg-recompress ==="
    if [ -f "test/test-files/canon500d.jpg" ]; then
        output_file="/tmp/test_output_$$.jpg"
        $TEST_CMD "jpeg-recompress" ./jpeg-recompress \
                  -q medium -n 40 -x 95 -l 6 \
                  test/test-files/canon500d.jpg "$output_file"
        if [ $? -ne 0 ]; then exit_code=1; fi
        rm -f "$output_file"
    fi
    
    # Test jpeg-compare
    echo ""
    echo "=== Testing jpeg-compare ==="
    if [ -f "test/test-files/canon500d.jpg" ] && [ -f "test/test-files/hpc850.jpg" ]; then
        $TEST_CMD "jpeg-compare" ./jpeg-compare \
                  -m ssim test/test-files/canon500d.jpg test/test-files/hpc850.jpg
        if [ $? -ne 0 ]; then exit_code=1; fi
    fi
    
    # Test libjpegarchive
    if [ -f "test/libjpegarchive" ]; then
        echo ""
        echo "=== Testing libjpegarchive ==="
        $TEST_CMD "libjpegarchive" ./test/libjpegarchive
        if [ $? -ne 0 ]; then exit_code=1; fi
    fi
    
    # Test IQA
    if [ -f "src/iqa/build/release/test/test" ]; then
        echo ""
        echo "=== Testing IQA ==="
        (cd src/iqa/build/release && $TEST_CMD "iqa" test/test)
        if [ $? -ne 0 ]; then exit_code=1; fi
    fi
    
    echo ""
    echo "==================================="
    if [ $exit_code -eq 0 ]; then
        echo "Memory leak testing completed successfully"
    else
        echo "Memory leak testing completed with errors"
    fi
    echo "==================================="
    
    exit $exit_code
}

# Run main function
main "$@"