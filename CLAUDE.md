# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JPEG Archive is a set of command-line utilities for optimizing JPEG images while maintaining visual quality. The main programs are:
- `jpeg-recompress`: Lossy compression using visual quality metrics
- `jpeg-compare`: Compare two images for similarity
- `jpeg-hash`: Generate perceptual hashes for duplicate detection
- `jpeg-archive`: Batch processing script for RAW and JPEG files

## Build Commands

```bash
# Build all utilities
make

# Install to /usr/local/bin
sudo make install

# Run unit tests
make test

# Clean build artifacts
make clean

# Run integration tests (downloads test images)
cd test && ./test.sh

# Run visual comparison tests (requires ImageMagick)
cd test && ./comparison.sh
```

## Architecture

The codebase follows a straightforward C architecture:

1. **Main utilities** (top-level .c files):
   - Each utility is a standalone program with its own main()
   - All use common functionality from src/

2. **Core modules** (src/):
   - `util.c/h`: Image I/O, command parsing, mozjpeg integration
   - `hash.c/h`: Perceptual hashing algorithms
   - `edit.c/h`: Image transformations (defish, etc.)
   - `smallfry.c/h`: SmallFry quality metric implementation
   - `iqa/`: External Image Quality Assessment library providing SSIM, MS-SSIM, MSE, PSNR

3. **Key architectural decisions**:
   - Uses mozjpeg for JPEG encoding/decoding
   - All image processing works on RGB buffers (3 bytes per pixel)
   - Quality metrics operate on 8-bit grayscale converted images
   - Progressive search for optimal quality level using binary search

## Testing Approach

- Unit tests use custom test framework with DESCRIBE/IT macros in `test/test.c`
- Integration tests compare output against expected results
- No specific linting tools configured - follow existing C style
- Windows builds tested via AppVeyor, Linux/macOS via Travis CI

## Development Notes

- mozjpeg library must be installed before building
- Platform-specific mozjpeg paths are hardcoded in Makefile
- When modifying quality algorithms, test with various image types (photos, screenshots, graphics)
- The `--accurate` flag trades speed for better quality assessment
- For text/screenshots, use `--subsample disable` to prevent color artifacts