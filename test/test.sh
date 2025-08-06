#!/bin/bash

set -e

mkdir -p test-output

if [ ! -d test-files ]; then
    curl -O -L https://www.dropbox.com/s/hb3ah7p5hcjvhc1/jpeg-archive-test-files.zip
    unzip jpeg-archive-test-files.zip
fi

echo "=== Running jpeg-recompress tests ==="
for file in test-files/*; do
    ../jpeg-recompress "$file" "test-output/`basename $file`"
done

echo ""
echo "=== Building and running libjpegarchive tests ==="
cd ..
make test-libjpegarchive
cd test
./libjpegarchive
