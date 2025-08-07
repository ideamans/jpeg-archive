#!/bin/bash

set -e

mkdir -p test-output

# Run unit tests
echo "=== Running unit tests ==="
./test

# Download test files if needed
if [ ! -d test-files ]; then
    curl -O -L https://www.dropbox.com/s/hb3ah7p5hcjvhc1/jpeg-archive-test-files.zip
    unzip jpeg-archive-test-files.zip
fi

# Run jpeg-recompress tests
echo ""
echo "=== Running jpeg-recompress tests ==="
for file in test-files/*; do
    ../jpeg-recompress "$file" "test-output/`basename $file`"
done

# Run libjpegarchive tests
echo ""
echo "=== Running libjpegarchive tests ==="
./libjpegarchive
