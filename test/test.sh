#!/bin/bash

set -e

# Change to test directory if not already there
if [ "$(basename $(pwd))" != "test" ]; then
    cd test
fi

mkdir -p test-output

# Build and run unit tests
echo "=== Building and running unit tests ==="
cd ..
make test-build
cd test
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

# Build and run libjpegarchive tests
echo ""
echo "=== Building and running libjpegarchive tests ==="
cd ..
make test-libjpegarchive-build
cd test
./libjpegarchive
