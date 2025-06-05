package jpegarchive_test

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"runtime/pprof"
	"testing"
	"time"

	jpegarchive "github.com/ideamans/jpeg-archive/golang"
)

func getTestImage(t *testing.T) string {
	// Use local test image
	testFile := "hina.jpg"

	// Check if test file exists
	if _, err := os.Stat(testFile); os.IsNotExist(err) {
		t.Skip("Test file hina.jpg does not exist, skipping test")
	}

	return testFile
}

func TestCompareWithOriginalTool(t *testing.T) {
	testFile := getTestImage(t)

	// Check if test file exists
	if _, err := os.Stat(testFile); os.IsNotExist(err) {
		t.Fatalf("Test file does not exist: %s", testFile)
	}
	t.Logf("Test file: %s", testFile)

	// Test with medium quality
	libOutput := filepath.Join(t.TempDir(), "lib_output.jpg")
	cmdOutput := filepath.Join(t.TempDir(), "cmd_output.jpg")

	// Use library
	libResult, err := jpegarchive.JpegRecompress(testFile, libOutput, jpegarchive.QualityMedium)
	if err != nil {
		t.Fatalf("Library failed: %v", err)
	}

	// Use command line tool
	cmd := exec.Command("../jpeg-recompress", "--quality", "medium", testFile, cmdOutput)
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Logf("Command output: %s", output)
		t.Fatalf("Command line tool failed: %v", err)
	}

	// Compare file sizes (should be similar)
	libInfo, _ := os.Stat(libOutput)
	cmdInfo, _ := os.Stat(cmdOutput)

	libSize := libInfo.Size()
	cmdSize := cmdInfo.Size()

	sizeDiff := float64(libSize-cmdSize) / float64(cmdSize) * 100
	if sizeDiff < 0 {
		sizeDiff = -sizeDiff
	}

	t.Logf("Library output size: %d bytes", libSize)
	t.Logf("Command output size: %d bytes", cmdSize)
	t.Logf("Size difference: %.2f%%", sizeDiff)
	t.Logf("Library SSIM: %f, Quality: %d", libResult.SSIM, libResult.Quality)

	// Allow up to 15% difference in file size (library and CLI may have slightly different optimizations)
	if sizeDiff > 1 {
		t.Errorf("File size difference too large: %.2f%%", sizeDiff)
	}
}

func TestMemoryLeak(t *testing.T) {
	testFile := getTestImage(t)

	// Create memory profile
	memFile, err := os.Create("mem.prof")
	if err != nil {
		t.Fatal("could not create memory profile: ", err)
	}
	defer memFile.Close()

	// Force GC to get a baseline
	runtime.GC()
	runtime.MemProfileRate = 1

	// Record initial memory
	var m1 runtime.MemStats
	runtime.ReadMemStats(&m1)

	// Run multiple iterations
	iterations := 20
	for i := 0; i < iterations; i++ {
		outputFile := filepath.Join(t.TempDir(), fmt.Sprintf("leak_test_%d.jpg", i))

		result, err := jpegarchive.JpegRecompress(testFile, outputFile, jpegarchive.QualityMedium)
		if err != nil {
			t.Fatalf("Iteration %d failed: %v", i, err)
		}

		if result.ExitCode != 0 {
			t.Fatalf("Iteration %d: unexpected exit code %d", i, result.ExitCode)
		}

		// Clean up output file
		os.Remove(outputFile)

		// Periodic GC
		if i%10 == 0 {
			runtime.GC()
		}
	}

	// Force final GC
	runtime.GC()
	time.Sleep(100 * time.Millisecond)
	runtime.GC()

	// Record final memory
	var m2 runtime.MemStats
	runtime.ReadMemStats(&m2)

	// Write heap profile
	if err := pprof.WriteHeapProfile(memFile); err != nil {
		t.Fatal("could not write memory profile: ", err)
	}

	// Calculate memory growth
	allocGrowth := int64(m2.Alloc) - int64(m1.Alloc)
	heapGrowth := int64(m2.HeapAlloc) - int64(m1.HeapAlloc)

	t.Logf("Memory after %d iterations:", iterations)
	t.Logf("  Alloc growth: %d bytes (%.2f MB)", allocGrowth, float64(allocGrowth)/1024/1024)
	t.Logf("  Heap growth: %d bytes (%.2f MB)", heapGrowth, float64(heapGrowth)/1024/1024)
	t.Logf("  Total allocations: %d", m2.TotalAlloc)
	t.Logf("  Number of GCs: %d", m2.NumGC)

	// Allow some memory growth but flag potential leaks
	maxGrowthMB := 10.0
	growthMB := float64(allocGrowth) / 1024 / 1024
	if growthMB > maxGrowthMB {
		t.Errorf("Excessive memory growth detected: %.2f MB (max allowed: %.2f MB)", growthMB, maxGrowthMB)
	}
}
