package jpegarchive

// #cgo CFLAGS: -I../ -I../src/iqa/include
// #cgo LDFLAGS: -L../ -l:libjpegarchive.a -L/opt/mozjpeg/lib -L/opt/mozjpeg/lib64 -L/usr/local/opt/mozjpeg/lib -l:libjpeg.a -lm
// #include <stdlib.h>
// #include "jpeg-recompress-lib.h"
import "C"
import (
	"fmt"
	"unsafe"
)

const (
	QualityLow      = C.JPEG_RECOMPRESS_QUALITY_LOW
	QualityMedium   = C.JPEG_RECOMPRESS_QUALITY_MEDIUM
	QualityHigh     = C.JPEG_RECOMPRESS_QUALITY_HIGH
	QualityVeryHigh = C.JPEG_RECOMPRESS_QUALITY_VERYHIGH
)

type Result struct {
	ExitCode int
	Quality  int
	SSIM     float64
	Error    string
}

func JpegRecompress(inputPath, outputPath string, qualityPreset int) (*Result, error) {
	cInputPath := C.CString(inputPath)
	defer C.free(unsafe.Pointer(cInputPath))

	cOutputPath := C.CString(outputPath)
	defer C.free(unsafe.Pointer(cOutputPath))

	var cResult C.jpeg_recompress_result
	C.jpeg_recompress(cInputPath, cOutputPath, C.int(qualityPreset), &cResult)

	result := &Result{
		ExitCode: int(cResult.exit_code),
		Quality:  int(cResult.quality),
		SSIM:     float64(cResult.ssim),
	}

	if cResult.error != nil {
		result.Error = C.GoString(cResult.error)
		C.jpeg_recompress_free_result(&cResult)
	}

	if result.ExitCode != 0 {
		return result, fmt.Errorf("jpeg_recompress failed: %s", result.Error)
	}

	return result, nil
}