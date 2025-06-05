# jpegarchive Go bindings

Go bindings for jpeg-archive library. This repository is forked from [danielgtaylor/jpeg-archive](https://github.com/danielgtaylor/jpeg-archive) and extended to provide jpeg-recompress core functionality as a library for multi-language bindings.

## Installation

```bash
go get github.com/ideamans/jpeg-archive/golang
```

## Prerequisites

- mozjpeg library installed
- C compiler (gcc or clang)
- Make
- `libjpegarchive.a` built (see Building section)

## Usage

```go
import jpegarchive "github.com/ideamans/jpeg-archive/golang"

result, err := jpegarchive.JpegRecompress("input.jpg", "output.jpg", jpegarchive.QualityMedium)
if err != nil {
    log.Fatal(err)
}
fmt.Printf("Quality: %d, SSIM: %f\n", result.Quality, result.SSIM)
```

## Quality Presets

- `QualityLow`: Low quality, smaller file size
- `QualityMedium`: Medium quality (default)
- `QualityHigh`: High quality
- `QualityVeryHigh`: Very high quality, larger file size

## mozjpeg Configuration

The library automatically detects mozjpeg based on the operating system:

- **Linux**: `/opt/mozjpeg` (checks both lib and lib64)
- **macOS**: `/usr/local/opt/mozjpeg` (Homebrew default)
- **FreeBSD**: `/usr/local/lib/mozjpeg`
- **Windows**: `../mozjpeg`

### Custom Installation Path

If mozjpeg is installed in a different location:

```bash
# Using MOZJPEG_PREFIX
export MOZJPEG_PREFIX=/custom/path/to/mozjpeg
export CGO_CFLAGS="-I$MOZJPEG_PREFIX/include"
export CGO_LDFLAGS="-L$MOZJPEG_PREFIX/lib -l:libjpeg.a"
go build

# For lib64 systems
export CGO_LDFLAGS="-L$MOZJPEG_PREFIX/lib64 -l:libjpeg.a"
```

## Building libjpegarchive.a

From the project root directory:

```bash
# Build the static library
make libjpegarchive.a

# Or build everything including CLI tools
make all
```

This creates:
- `libjpegarchive.a`: Static library containing jpeg-recompress functionality
- `jpegarchive.h`: C header file with API definitions
- `jpegarchive.c`: Implementation of the library API

## Testing

```bash
# Run all tests
make test-lib

# Run memory leak test
make test-memory-leaking

# Run tests directly with Go
cd golang
go test -v

# Run benchmarks
go test -bench=.
```

## Library Architecture

### C API

```c
// Quality presets
#define JPEG_RECOMPRESS_QUALITY_LOW      0
#define JPEG_RECOMPRESS_QUALITY_MEDIUM   1
#define JPEG_RECOMPRESS_QUALITY_HIGH     2
#define JPEG_RECOMPRESS_QUALITY_VERYHIGH 3

// Result structure
typedef struct {
    int exit_code;
    int quality;
    double ssim;
    char *error;
} jpeg_recompress_result;

// Main function
void jpeg_recompress(const char *input_path, const char *output_path, 
                    int quality_preset, jpeg_recompress_result *result);

// Free result
void jpeg_recompress_free_result(jpeg_recompress_result *result);
```

### Go Bindings Features

1. **Type Safety**: Go-friendly API with proper error handling
2. **Memory Management**: Automatic cleanup of C resources
3. **Platform Support**: Cross-platform build configuration
4. **Zero Memory Leaks**: Verified by automated tests

## Development

```bash
# Clean and rebuild
make clean
make libjpegarchive.a

# Development workflow
cd golang
go test -v              # Run tests
go test -run TestMemoryLeak  # Check memory leaks
go test -bench=.        # Run benchmarks
```

## License

Same as the original [jpeg-archive](https://github.com/danielgtaylor/jpeg-archive) project.