#!/usr/bin/env bash

set -e

mkdir -p test-output

# Download test files if needed
if [ ! -d test-files ]; then
    curl -O -L https://www.dropbox.com/s/hb3ah7p5hcjvhc1/jpeg-archive-test-files.zip
    
    # Try different unzip methods depending on the environment
    if command -v unzip >/dev/null 2>&1; then
        unzip jpeg-archive-test-files.zip
    elif command -v 7z >/dev/null 2>&1; then
        7z x jpeg-archive-test-files.zip
    elif command -v busybox >/dev/null 2>&1; then
        busybox unzip jpeg-archive-test-files.zip
    elif command -v python3 >/dev/null 2>&1; then
        python3 -m zipfile -e jpeg-archive-test-files.zip .
    elif command -v python >/dev/null 2>&1; then
        python -m zipfile -e jpeg-archive-test-files.zip .
    else
        echo "ERROR: No suitable unzip tool found (tried unzip, 7z, busybox, python)"
        exit 1
    fi
fi

# Run unit tests
echo "=== Running unit tests ==="
./test

# Run jpeg-recompress tests
echo ""
echo "=== Running jpeg-recompress tests ==="

# Determine the executable name (with .exe on Windows)
JPEG_RECOMPRESS="../jpeg-recompress"
if [ -f "../jpeg-recompress.exe" ]; then
    JPEG_RECOMPRESS="../jpeg-recompress.exe"
fi

for file in test-files/*; do
    # Skip directories and non-JPEG files
    if [ -d "$file" ]; then
        continue
    fi

    # Only process .jpg and .jpeg files
    case "$file" in
        *.jpg|*.jpeg|*.JPG|*.JPEG)
            "$JPEG_RECOMPRESS" "$file" "test-output/$(basename "$file")"
            ;;
        *)
            echo "Skipping non-JPEG file: $file"
            ;;
    esac
done

# Run libjpegarchive tests
echo ""
echo "=== Running libjpegarchive tests ==="
./libjpegarchive
