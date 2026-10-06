#pragma once

// pprof — pprof.go:1-169
//
// CPU and heap profiling in Go's runtime/pprof output format: gzipped pprof
// protobuf profiles. CPU sampling is implemented with a 100 Hz ITIMER_PROF
// (SIGPROF) sampler + backtrace(3) — Go's runtime/pprof default rate.
// Heap/allocs profiles have no Go-runtime analog in C++: the port writes a
// well-formed pprof profile with the heap/allocs sample types and zero
// samples (documented — no allocation instrumentation exists in the port).

#include <chrono>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace tsc::pprof {

class ProfileSession;

// BeginProfiling starts CPU and memory profiling, writing the profiles to the
// specified directory. Panics (tscUnreachable) on filesystem errors like Go.
ProfileSession* beginProfiling(std::string_view profileDir, std::ostream* logWriter);

// CPUProfiler manages on-demand CPU profiling.
class CPUProfiler {
public:
    // Out-of-line ctor/dtor: session_ is unique_ptr to an incomplete type.
    CPUProfiler();
    ~CPUProfiler();

    // StartCPUProfile starts CPU profiling, writing to the specified
    // directory when stopped.
    std::string startCPUProfile(std::string_view profileDir);

    // StopCPUProfile stops CPU profiling and returns the profile file path.
    std::pair<std::string, std::string> stopCPUProfile();

private:
    std::mutex mu_;
    std::unique_ptr<ProfileSession> session_;
};

// SaveHeapProfile saves a heap profile to the specified directory.
// Returns (path, error).
std::pair<std::string, std::string> saveHeapProfile(std::string_view profileDir);

// SaveAllocProfile saves an allocation profile to the specified directory.
std::pair<std::string, std::string> saveAllocProfile(std::string_view profileDir);

// RunGC triggers garbage collection. (C++ port: no runtime GC — no-op.)
inline void runGC() {}

} // namespace tsc::pprof
