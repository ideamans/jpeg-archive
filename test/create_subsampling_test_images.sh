#!/bin/bash

# Create test images with different subsampling patterns using ImageMagick

OUTPUT_DIR="test-files/subsampling"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Use canon50d.jpg as base since it's a real photo (better for testing)
# First convert to 4:4:4 to have a clean base
BASE_SOURCE="test-files/canon50d.jpg"

# Check if canon50d.jpg exists, otherwise use another file
if [ ! -f "$BASE_SOURCE" ]; then
    # Try to find any suitable jpg file
    for f in test-files/canon*.jpg test-files/*.jpg; do
        if [ -f "$f" ] && [ "$(basename $f)" != "example.jpg" ]; then
            BASE_SOURCE="$f"
            echo "Using $BASE_SOURCE as base image"
            break
        fi
    done
fi

if [ ! -f "$BASE_SOURCE" ]; then
    echo "Error: No suitable test image found in test-files/"
    exit 1
fi

echo "Creating base 4:4:4 image from $BASE_SOURCE..."
# First create a high-quality 4:4:4 base image
magick "$BASE_SOURCE" -sampling-factor 1x1 -quality 95 "$OUTPUT_DIR/test_base_444.jpg"

# Create JPEGs with different subsampling patterns from the 4:4:4 base
echo "Creating JPEG with 4:4:4 subsampling..."
magick "$OUTPUT_DIR/test_base_444.jpg" -sampling-factor 1x1 -quality 90 "$OUTPUT_DIR/test_444.jpg"

echo "Creating JPEG with 4:2:2 subsampling..."
magick "$OUTPUT_DIR/test_base_444.jpg" -sampling-factor 2x1 -quality 90 "$OUTPUT_DIR/test_422.jpg"

echo "Creating JPEG with 4:2:0 subsampling..."
magick "$OUTPUT_DIR/test_base_444.jpg" -sampling-factor 2x2 -quality 90 "$OUTPUT_DIR/test_420.jpg"

echo "Creating JPEG with 4:1:1 subsampling..."
magick "$OUTPUT_DIR/test_base_444.jpg" -sampling-factor 4x1 -quality 90 "$OUTPUT_DIR/test_411.jpg"

# Verify the subsampling of created images using jpeginfo or custom tool
echo ""
echo "Verifying subsampling patterns with magick identify:"
for file in "$OUTPUT_DIR"/*.jpg; do
    echo ""
    echo "File: $(basename $file)"
    magick identify -format "Colorspace: %[colorspace]\nSampling-factor: %[jpeg:sampling-factor]\n" "$file" 2>/dev/null || echo "  Unable to get sampling info"
done

echo ""
echo "Test images created in $OUTPUT_DIR"