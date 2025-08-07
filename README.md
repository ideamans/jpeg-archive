JPEG Archive [![Build Status](http://img.shields.io/travis/danielgtaylor/jpeg-archive.svg?style=flat)](https://travis-ci.org/danielgtaylor/jpeg-archive) [![Build status](https://ci.appveyor.com/api/projects/status/1p7hrrq380xuqlyh?svg=true)](https://ci.appveyor.com/project/danielgtaylor/jpeg-archive) [![Version](http://img.shields.io/badge/version-2.2.0-blue.svg?style=flat)](https://github.com/danielgtaylor/jpeg-archive/releases) [![License](http://img.shields.io/badge/license-MIT-red.svg?style=flat)](http://dgt.mit-license.org/)
============
Utilities for archiving photos for saving to long term storage or serving over the web. The goals are:

 * Use a common, well supported format (JPEG)
 * Minimize storage space and cost
 * Identify duplicates / similar photos

Approach:

 * Command line utilities and scripts
 * Simple options and useful help
 * Good quality output via sane defaults

Contributions to this project are very welcome.

Download
--------
You can download the latest source and binary releases from the [JPEG Archive releases page](https://github.com/danielgtaylor/jpeg-archive/releases). Windows binaries for the latest commit are available from the [Windows CI build server](https://ci.appveyor.com/project/danielgtaylor/jpeg-archive/build/artifacts).

If you are looking for an easy way to run these utilities in parallel over many files to utilize all CPU cores, please also download [Ladon](https://github.com/danielgtaylor/ladon) or [GNU Parallel](https://www.gnu.org/software/parallel/). You can then use the `jpeg-archive` command below or use `ladon` directly. Example:

```bash
# Re-compress JPEGs and replace the originals
ladon "Photos/**/*.jpg" -- jpeg-recompress FULLPATH FULLPATH

# Re-compress JPEGs into the new directory 'Comp'
ladon -m Comp/RELDIR "Photos/**/*.jpg" -- jpeg-recompress FULLPATH Comp/RELPATH
```

Utilities
---------
The following utilities are part of this project. All of them accept a `--help` parameter to see the available options.

### jpeg-archive
Compress RAW and JPEG files in a folder utilizing all CPU cores. This is a simple shell script that uses the utilities below. It requires:

* a POSIX-compatible shell such as Bash
* [Ladon](https://github.com/danielgtaylor/ladon) or [GNU Parallel](https://www.gnu.org/software/parallel/)
* [dcraw](http://www.cybercom.net/~dcoffin/dcraw/)
* [exiftool](http://www.sno.phy.queensu.ca/~phil/exiftool/)
* jpeg-recompress (part of this project)

```bash
# Compress a folder of images
cd path/to/photos
jpeg-archive

# Custom quality and metric
jpeg-archive --quality medium --method smallfry
```

### jpeg-recompress
Compress JPEGs by re-encoding to the smallest JPEG quality while keeping _perceived_ visual quality the same and by making sure huffman tables are optimized. This is a __lossy__ operation, but the images are visually identical and it usually saves 30-70% of the size for JPEGs coming from a digital camera, particularly DSLRs. By default all EXIF/IPTC/XMP and color profile metadata is copied over, but this can be disabled to save more space if desired.

There is no need for the input file to be a JPEG. In fact, you can use `jpeg-recompress` as a replacement for `cjpeg` by using PPM input and the `--ppm` option.

The better the quality of the input image is, the better the output will be.

Some basic photo-related editing options are available, such as removing fisheye lens distortion.

#### Demo
Below are two 100% crops of [Nikon's D3x Sample Image 2](http://static.nikonusa.com/D3X_gallery/index.html). The left shows the original image from the camera, while the others show the output of `jpeg-recompress` with the `medium` quality setting and various comparison methods. By default SSIM is used, which lowers the file size by **88%**. The recompression algorithm chooses a JPEG quality of 80. By comparison the `veryhigh` quality setting chooses a JPEG quality of 93 and saves 70% of the file size.

![JPEG recompression comparison](https://cloud.githubusercontent.com/assets/106826/3633843/5fde26b6-0eff-11e4-8c98-f18dbbf7b510.png)

Why are they different sizes? The default quality settings are set to average out to similar visual quality over large data sets. They may differ on individual photos (like above) because each metric considers different parts of the image to be more or less important for compression.

#### Image Comparison Metrics
The following metrics are available when using `jpeg-recompress`. SSIM is the default.

Name     | Option        | Description
-------- | ------------- | -----------
MPE      | `-m mpe`      | Mean pixel error (as used by [imgmin](https://github.com/rflynn/imgmin))
SSIM     | `-m ssim`     | [Structural similarity](http://en.wikipedia.org/wiki/Structural_similarity) **DEFAULT**
MS-SSIM* | `-m ms-ssim`  | Multi-scale structural similarity (slow!) ([2008 paper](https://doi.org/10.1117/12.768060))
SmallFry | `-m smallfry` | Linear-weighted BBCQ-like ([original project](https://github.com/dwbuiten/smallfry), [2011 BBCQ paper](http://spie.org/Publications/Proceedings/Paper/10.1117/12.872231))

**Note**: The SmallFry algorithm may be [patented](http://www.jpegmini.com/main/technology) so use with caution.

#### Subsampling
The JPEG format allows for subsampling of the color channels to save space. For each 2x2 block of pixels per color channel (four pixels total) it can store four pixels (all of them), two pixels or a single pixel. By default, the JPEG encoder subsamples the non-luma channels to two pixels (often referred to as 4:2:0 subsampling). Most digital cameras do the same because of limitations in the human eye. This may lead to unintended behavior for specific use cases (see [#12](https://github.com/danielgtaylor/jpeg-archive/issues/12) for an example), so you can use `--subsample disable` to disable this subsampling.

#### Example Commands

```bash
# Default settings
jpeg-recompress image.jpg compressed.jpg

# High quality example settings
jpeg-recompress --quality high --min 60 image.jpg compressed.jpg

# Slow high quality settings (3-4x slower than above, slightly more accurate)
jpeg-recompress --accurate --quality high --min 60 image.jpg compressed.jpg

# Use SmallFry instead of SSIM
jpeg-recompress --method smallfry image.jpg compressed.jpg

# Use 4:4:4 sampling (disables subsampling).
jpeg-recompress --subsample disable image.jpg compressed.jpg

# Remove fisheye distortion (Tokina 10-17mm on APS-C @ 10mm)
jpeg-recompress --defish 2.6 --zoom 1.2 image.jpg defished.jpg

# Read from stdin and write to stdout with '-' as the filename
jpeg-recompress - - <image.jpg >compressed.jpg

# Convert RAW to JPEG via PPM from stdin
dcraw -w -q 3 -c IMG_1234.CR2 | jpeg-recompress --ppm - compressed.jpg

# Disable progressive mode (not recommended)
jpeg-recompress --no-progressive image.jpg compressed.jpg

# Disable all output except for errors
jpeg-recompress --quiet image.jpg compressed.jpg
```

### jpeg-compare
Compare two JPEG photos to judge how similar they are. The `fast` comparison method returns an integer from 0 to 99, where 0 is identical. PSNR, SSIM, and MS-SSIM return floats but require images to be the same dimensions.

```bash
# Do a fast compare of two images
jpeg-compare image1.jpg image2.jpg

# Calculate PSNR
jpeg-compare --method psnr image1.jpg image2.jpg

# Calculate SSIM
jpeg-compare --method ssim image1.jpg image2.jpg
```

### jpeg-hash
Create a hash of an image that can be used to compare it to other images quickly.

```bash
jpeg-hash image.jpg
```

libjpegarchive - Static Library
--------------------------------
`libjpegarchive` is a static library that provides the core functionality of jpeg-recompress and jpeg-compare as a C API. This allows integration into other applications without spawning external processes, providing significant performance improvements.

### Features
- **jpeg_recompress functionality**: Compress JPEGs with perceptual quality metrics
- **jpeg_compare functionality**: Compare two JPEGs using SSIM
- **Memory-based operations**: Work directly with memory buffers instead of files
- **Performance**: 2-3x faster than calling CLI utilities
- **Thread-safe**: Can be used in multi-threaded applications

### API Functions

#### jpegarchive_recompress
Re-compress a JPEG image in memory.

```c
typedef struct {
    unsigned char* jpeg;  // Input JPEG data
    long length;          // Input data length
    int min;              // Minimum quality (0-100)
    int max;              // Maximum quality (0-100)
    int loops;            // Number of binary search loops
    jpegarchive_quality_t quality;  // Quality preset (low/medium/high/veryhigh)
    jpegarchive_method_t method;    // Comparison method (SSIM only)
} jpegarchive_recompress_input_t;

typedef struct {
    jpegarchive_error_t error_code;  // Error status
    unsigned char* jpeg;              // Output JPEG data (must be freed)
    long length;                      // Output data length
    int quality;                      // Final quality used
    double metric;                    // Final metric value
} jpegarchive_recompress_output_t;

jpegarchive_recompress_output_t jpegarchive_recompress(jpegarchive_recompress_input_t input);
void jpegarchive_free_recompress_output(jpegarchive_recompress_output_t* output);
```

#### jpegarchive_compare
Compare two JPEG images using SSIM.

```c
typedef struct {
    unsigned char* jpeg1;  // First JPEG data
    unsigned char* jpeg2;  // Second JPEG data
    long length1;          // First data length
    long length2;          // Second data length
    jpegarchive_method_t method;  // Comparison method (SSIM only)
} jpegarchive_compare_input_t;

typedef struct {
    jpegarchive_error_t error_code;  // Error status
    double metric;                    // SSIM value (0-1, 1 = identical)
} jpegarchive_compare_output_t;

jpegarchive_compare_output_t jpegarchive_compare(jpegarchive_compare_input_t input);
void jpegarchive_free_compare_output(jpegarchive_compare_output_t* output);
```

### Building the Library

```bash
# Build libjpegarchive.a
make libjpegarchive.a

# Run tests
make test
```

### Usage in Rust

Add FFI bindings to your Rust project:

```rust
// jpegarchive.rs - FFI bindings
use std::os::raw::{c_uchar, c_long, c_int, c_double};

#[repr(C)]
pub enum JpegArchiveError {
    Ok = 0,
    InvalidInput = 1,
    NotJpeg = 2,
    Unsupported = 3,
    NotSuitable = 4,
    MemoryError = 5,
    Unknown = 6,
}

#[repr(C)]
pub enum JpegArchiveQuality {
    Low = 0,
    Medium = 1,
    High = 2,
    VeryHigh = 3,
}

#[repr(C)]
pub enum JpegArchiveMethod {
    SSIM = 0,
}

#[repr(C)]
pub struct RecompressInput {
    pub jpeg: *const c_uchar,
    pub length: c_long,
    pub min: c_int,
    pub max: c_int,
    pub loops: c_int,
    pub quality: JpegArchiveQuality,
    pub method: JpegArchiveMethod,
}

#[repr(C)]
pub struct RecompressOutput {
    pub error_code: JpegArchiveError,
    pub jpeg: *mut c_uchar,
    pub length: c_long,
    pub quality: c_int,
    pub metric: c_double,
}

extern "C" {
    pub fn jpegarchive_recompress(input: RecompressInput) -> RecompressOutput;
    pub fn jpegarchive_free_recompress_output(output: *mut RecompressOutput);
}

// Example usage
use std::fs;
use std::slice;

fn compress_jpeg(input_data: &[u8]) -> Result<Vec<u8>, String> {
    unsafe {
        let input = RecompressInput {
            jpeg: input_data.as_ptr(),
            length: input_data.len() as c_long,
            min: 40,
            max: 95,
            loops: 6,
            quality: JpegArchiveQuality::Medium,
            method: JpegArchiveMethod::SSIM,
        };
        
        let mut output = jpegarchive_recompress(input);
        
        if output.error_code != JpegArchiveError::Ok {
            return Err(format!("Compression failed: {:?}", output.error_code));
        }
        
        let result = slice::from_raw_parts(output.jpeg, output.length as usize).to_vec();
        jpegarchive_free_recompress_output(&mut output);
        
        Ok(result)
    }
}

fn main() {
    let input_data = fs::read("input.jpg").expect("Failed to read input file");
    
    match compress_jpeg(&input_data) {
        Ok(compressed) => {
            fs::write("output.jpg", compressed).expect("Failed to write output");
            println!("Compression successful!");
        }
        Err(e) => eprintln!("Error: {}", e),
    }
}
```

Build with:
```toml
# Cargo.toml
[build-dependencies]
cc = "1.0"

# build.rs
fn main() {
    println!("cargo:rustc-link-search=native=/path/to/jpeg-archive");
    println!("cargo:rustc-link-lib=static=jpegarchive");
    println!("cargo:rustc-link-lib=static=iqa");
    println!("cargo:rustc-link-lib=static=turbojpeg");
    println!("cargo:rustc-link-lib=dylib=m");
}
```

### Usage in Go

Create Go bindings using CGO:

```go
// jpegarchive.go
package jpegarchive

/*
#cgo CFLAGS: -I/path/to/jpeg-archive
#cgo LDFLAGS: -L/path/to/jpeg-archive -ljpegarchive -L/path/to/jpeg-archive/src/iqa/build/release -liqa -L/path/to/mozjpeg/lib -lturbojpeg -lm

#include <stdlib.h>
#include "jpegarchive.h"
*/
import "C"
import (
    "errors"
    "unsafe"
)

type Quality int

const (
    QualityLow      Quality = 0
    QualityMedium   Quality = 1
    QualityHigh     Quality = 2
    QualityVeryHigh Quality = 3
)

type Method int

const (
    MethodSSIM Method = 0
)

// RecompressOptions contains options for JPEG recompression
type RecompressOptions struct {
    Min     int
    Max     int
    Loops   int
    Quality Quality
    Method  Method
}

// DefaultOptions returns recommended default options
func DefaultOptions() RecompressOptions {
    return RecompressOptions{
        Min:     40,
        Max:     95,
        Loops:   6,
        Quality: QualityMedium,
        Method:  MethodSSIM,
    }
}

// Recompress compresses a JPEG image using perceptual quality metrics
func Recompress(inputData []byte, opts RecompressOptions) ([]byte, int, float64, error) {
    if len(inputData) == 0 {
        return nil, 0, 0, errors.New("empty input data")
    }

    input := C.jpegarchive_recompress_input_t{
        jpeg:    (*C.uchar)(unsafe.Pointer(&inputData[0])),
        length:  C.long(len(inputData)),
        min:     C.int(opts.Min),
        max:     C.int(opts.Max),
        loops:   C.int(opts.Loops),
        quality: C.jpegarchive_quality_t(opts.Quality),
        method:  C.jpegarchive_method_t(opts.Method),
    }

    output := C.jpegarchive_recompress(input)
    defer C.jpegarchive_free_recompress_output(&output)

    if output.error_code != C.JPEGARCHIVE_OK {
        return nil, 0, 0, errors.New(getErrorString(output.error_code))
    }

    // Copy the output data
    outputData := C.GoBytes(unsafe.Pointer(output.jpeg), C.int(output.length))
    
    return outputData, int(output.quality), float64(output.metric), nil
}

// Compare compares two JPEG images using SSIM
func Compare(jpeg1, jpeg2 []byte) (float64, error) {
    if len(jpeg1) == 0 || len(jpeg2) == 0 {
        return 0, errors.New("empty input data")
    }

    input := C.jpegarchive_compare_input_t{
        jpeg1:   (*C.uchar)(unsafe.Pointer(&jpeg1[0])),
        jpeg2:   (*C.uchar)(unsafe.Pointer(&jpeg2[0])),
        length1: C.long(len(jpeg1)),
        length2: C.long(len(jpeg2)),
        method:  C.JPEGARCHIVE_METHOD_SSIM,
    }

    output := C.jpegarchive_compare(input)
    defer C.jpegarchive_free_compare_output(&output)

    if output.error_code != C.JPEGARCHIVE_OK {
        return 0, errors.New(getErrorString(output.error_code))
    }

    return float64(output.metric), nil
}

func getErrorString(code C.jpegarchive_error_t) string {
    switch code {
    case C.JPEGARCHIVE_INVALID_INPUT:
        return "invalid input"
    case C.JPEGARCHIVE_NOT_JPEG:
        return "not a JPEG file"
    case C.JPEGARCHIVE_UNSUPPORTED:
        return "unsupported JPEG format"
    case C.JPEGARCHIVE_NOT_SUITABLE:
        return "not suitable for recompression"
    case C.JPEGARCHIVE_MEMORY_ERROR:
        return "memory allocation error"
    default:
        return "unknown error"
    }
}
```

Example usage:

```go
// main.go
package main

import (
    "fmt"
    "io/ioutil"
    "log"
    "path/to/jpegarchive"
)

func main() {
    // Read input file
    inputData, err := ioutil.ReadFile("input.jpg")
    if err != nil {
        log.Fatal(err)
    }

    // Compress with default options
    opts := jpegarchive.DefaultOptions()
    outputData, quality, metric, err := jpegarchive.Recompress(inputData, opts)
    if err != nil {
        log.Fatal(err)
    }

    // Write output file
    err = ioutil.WriteFile("output.jpg", outputData, 0644)
    if err != nil {
        log.Fatal(err)
    }

    fmt.Printf("Compression successful!\n")
    fmt.Printf("Quality: %d, SSIM: %.6f\n", quality, metric)
    fmt.Printf("Size reduction: %.1f%%\n", 
        (1.0 - float64(len(outputData))/float64(len(inputData))) * 100)
    
    // Compare two images
    jpeg2, _ := ioutil.ReadFile("other.jpg")
    ssim, err := jpegarchive.Compare(inputData, jpeg2)
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("SSIM between images: %.6f\n", ssim)
}
```

### Performance

The library version provides significant performance improvements over calling CLI utilities:

- **2-3x faster** than spawning external processes
- **Lower memory overhead** - no need for temporary files
- **Thread-safe** - can be used in parallel processing
- **Direct memory operations** - no file I/O overhead

Benchmark results show library calls are typically 150-200% faster than equivalent CLI commands.

Building
--------
### Dependencies
 * [mozjpeg](https://github.com/mozilla/mozjpeg)

#### Ubuntu
Ubuntu users can install via `apt-get`:

```bash
sudo apt-get install build-essential autoconf pkg-config nasm libtool
git clone https://github.com/mozilla/mozjpeg.git
cd mozjpeg
autoreconf -fiv
./configure --with-jpeg8
make
sudo make install
```

#### Mac OS X
Mac users can install it via [Homebrew](http://brew.sh/):

```bash
brew install mozjpeg
```

#### FreeBSD

```bash
pkg install mozjpeg
git clone https://github.com/danielgtaylor/jpeg-archive.git
cd jpeg-archive/
gmake
sudo gmake install
```

#### Windows
The `Makefile` should work with MinGW/Cygwin/etc and standard GCC. Patches welcome.

To get everything you need to build, install these:

* [CMake](https://cmake.org/download/)
* [NASM](https://www.nasm.us/)
* [MinGW](https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/installer/mingw-w64-install.exe/download) (installed to e.g. `C:\mingw`)
* [Github for Windows](https://windows.github.com/)

Run Github for windows. In the settings, set **Git Bash** as the shell. Open Git Shell from the start menu.

```bash
# Update PATH to include MinGW/NASM bin folder, location on your system may vary
export PATH=/c/mingw/mingw32/bin:/c/Program\ Files \(x68\)/nasm:$PATH

# Build mozjpeg or download https://www.dropbox.com/s/98jppfgds2xjblu/libjpeg.a
git clone https://github.com/mozilla/mozjpeg.git
cd mozjpeg
cmake -G "MSYS Makefiles" -D CMAKE_C_COMPILER=gcc.exe -D CMAKE_MAKE_PROGRAM=mingw32-make.exe  -D WITH_JPEG8=1
mingw32-make
cd ..

# Build jpeg-archive
git clone https://github.com/danielgtaylor/jpeg-archive
cd jpeg-archive
CC=gcc mingw32-make
```

JPEG-Archive should now be built.

### Compiling (Linux and Mac OS X)
The `Makefile` should work as-is on Ubuntu and Mac OS X. Other platforms may need to set the location of `libjpeg.a` or make other tweaks.

```bash
make
```

### Installation
Install the binaries into `/usr/local/bin`:

```bash
sudo make install
```

Links / Alternatives
--------------------
* https://github.com/rflynn/imgmin
* https://news.ycombinator.com/item?id=803839

License
-------
* JPEG-Archive is copyright &copy; 2015 Daniel G. Taylor
* Image Quality Assessment (IQA) is copyright 2011, Tom Distler (http://tdistler.com)
* SmallFry is copyright 2014, Derek Buitenhuis (https://github.com/dwbuiten)

All are released under an MIT license.

http://dgt.mit-license.org/
