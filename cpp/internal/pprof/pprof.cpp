// pprof — pprof.go:1-169
//
// pprof protobuf writer (hand-rolled minimal proto marshal — profile.proto
// schema) + gzip via zlib + a SIGPROF/ITIMER_PROF sampler at Go's 100 Hz
// default CPU-profile rate.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <zlib.h>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/pprof/pprof.h"

namespace tsc::pprof {

namespace {

namespace fs = std::filesystem;

// --- minimal proto marshal ------------------------------------------------

struct ProtoBuf {
    std::string buf;

    void varint(uint64_t v) {
        char tmp[10];
        int n = 0;
        while (v >= 0x80) {
            tmp[n++] = static_cast<char>(v | 0x80);
            v >>= 7;
        }
        tmp[n++] = static_cast<char>(v);
        buf.append(tmp, n);
    }

    void tag(int field, int wire) { varint(static_cast<uint64_t>(field) << 3 | wire); }
    void i64(int field, int64_t v) { tag(field, 0); varint(static_cast<uint64_t>(v)); }
    void u64(int field, uint64_t v) { tag(field, 0); varint(v); }
    void str(int field, std::string_view s) {
        tag(field, 2);
        varint(s.size());
        buf.append(s.data(), s.size());
    }
    void msg(int field, const std::string& m) { str(field, m); }
    void packedU64(int field, const std::vector<uint64_t>& vs) {
        std::string inner;
        ProtoBuf p;
        for (uint64_t v : vs) p.varint(v);
        str(field, p.buf);
    }
    void packedI64(int field, const std::vector<int64_t>& vs) {
        std::string inner;
        ProtoBuf p;
        for (int64_t v : vs) p.varint(static_cast<uint64_t>(v));
        str(field, p.buf);
    }
};

// --- gzip writer over an fd -------------------------------------------------

// Writes gzip-compressed data (Go's gzip.NewWriter → zlib deflate,
// Z_DEFAULT_COMPRESSION, gzip header).
class GzipFdWriter {
public:
    explicit GzipFdWriter(int fd) : fd_(fd) {
        std::memset(&zs_, 0, sizeof(zs_));
        deflateInit2(&zs_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY);
    }
    ~GzipFdWriter() { deflateEnd(&zs_); }

    bool write(const std::string& data) {
        zs_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        zs_.avail_in = static_cast<uInt>(data.size());
        char out[64 * 1024];
        do {
            zs_.next_out = reinterpret_cast<Bytef*>(out);
            zs_.avail_out = sizeof(out);
            deflate(&zs_, Z_NO_FLUSH);
            size_t have = sizeof(out) - zs_.avail_out;
            if (have && !writeAll(out, have)) return false;
        } while (zs_.avail_out == 0);
        return true;
    }

    bool finish() {
        char out[64 * 1024];
        int ret;
        do {
            zs_.next_out = reinterpret_cast<Bytef*>(out);
            zs_.avail_out = sizeof(out);
            ret = deflate(&zs_, Z_FINISH);
            size_t have = sizeof(out) - zs_.avail_out;
            if (have && !writeAll(out, have)) return false;
        } while (ret == Z_OK);
        return ret == Z_STREAM_END;
    }

private:
    int fd_;
    z_stream zs_{};

    bool writeAll(const char* p, size_t n) {
        while (n > 0) {
            ssize_t w = ::write(fd_, p, n);
            if (w < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += w;
            n -= static_cast<size_t>(w);
        }
        return true;
    }
};

// --- SIGPROF sampler ---------------------------------------------------------

// 100 Hz — Go's default CPU profile rate.
constexpr int kProfHz = 100;
constexpr int64_t kPeriodNanos = 1'000'000'000 / kProfHz;
constexpr int kMaxDepth = 64;
constexpr size_t kMaxSamples = 1 << 20; // bounded ring; Go buffers unboundedly

struct SampleStack {
    uint16_t depth;
    void* pc[kMaxDepth];
};

std::atomic<SampleStack*> gSamples{nullptr};
std::atomic<size_t> gSampleCount{0};
std::atomic<bool> gProfiling{false};

#ifndef _WIN32
struct sigaction gOldAction{};
struct itimerval gOldTimer{};

void sigprofHandler(int /*sig*/, siginfo_t* /*info*/, void* /*ucontext*/) {
    // backtrace() in a signal handler is technically not async-signal-safe
    // (it may malloc on first call) — warm it up on start so the handler path
    // touches no allocator. This is the standard approach for profilers of
    // this kind.
    SampleStack* samples = gSamples.load(std::memory_order_relaxed);
    if (!samples) return;
    size_t i = gSampleCount.load(std::memory_order_relaxed);
    if (i >= kMaxSamples) return;
    SampleStack& s = samples[i];
    int n = ::backtrace(s.pc, kMaxDepth);
    s.depth = static_cast<uint16_t>(n);
    // relaxed store is fine: sampling tolerates a torn tail sample
    gSampleCount.store(i + 1, std::memory_order_relaxed);
}
#endif // !_WIN32

int64_t nowNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// field-tag + length-delim prepend helper
inline std::string wrapMsg(int field, const std::string& m) {
    ProtoBuf p;
    p.tag(field, 2);
    p.varint(m.size());
    return p.buf + m;
}

// StringTable for pprof — index 0 is "".
struct StringTable {
    std::vector<std::string> v{""};
    std::unordered_map<std::string, int64_t> index{{"", 0}};
    int64_t intern(std::string_view s) {
        auto it = index.find(std::string(s));
        if (it != index.end()) return it->second;
        int64_t id = static_cast<int64_t>(v.size());
        v.emplace_back(s);
        index.emplace(v.back(), id);
        return id;
    }
};

// runtime/pprof.StartCPUProfile — start sampling to a profile file descriptor.
struct CPUProfileState {
    int64_t startNanos;
    SampleStack* samples;
};

std::string startCPUProfile(int fd, CPUProfileState** out) {
    if (gProfiling.exchange(true)) {
        return "cpu profiling already in use";
    }

    auto* samples = new SampleStack[kMaxSamples];
    gSamples.store(samples, std::memory_order_release);
    gSampleCount.store(0, std::memory_order_release);

#ifdef _WIN32
    // No SIGPROF/setitimer on Windows: Go itself collects CPU profiles via
    // the runtime's own sampler, so a faithful Windows port would need
    // SuspendThread+StackWalk64 machinery. Instead we run the profile
    // lifecycle (valid gzip'd pprof header, 0 samples) and record the
    // divergence in cpp/WINDOWS_PARITY.md.
#else
    // warm up backtrace() outside the handler
    void* warm[8];
    ::backtrace(warm, 8);

    struct sigaction sa {};
    sa.sa_sigaction = sigprofHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (::sigaction(SIGPROF, &sa, &gOldAction) != 0) {
        gProfiling = false;
        delete[] samples;
        gSamples = nullptr;
        return "failed to install SIGPROF handler";
    }

    struct itimerval itv {};
    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = 1000000 / kProfHz; // 10000 µs = 10 ms
    itv.it_value = itv.it_interval;
    if (::setitimer(ITIMER_PROF, &itv, &gOldTimer) != 0) {
        ::sigaction(SIGPROF, &gOldAction, nullptr);
        gProfiling = false;
        delete[] samples;
        gSamples = nullptr;
        return "failed to start profiling timer";
    }
#endif

    *out = new CPUProfileState{nowNanos(), samples};
    // fd is owned by the session; sampling buffers are process-global like Go.
    (void)fd;
    return {};
}

// runtime/pprof.StopCPUProfile — stop sampling and write the gzipped profile.
void stopCPUProfile(int fd, CPUProfileState* state) {
#ifndef _WIN32
    // disarm the timer first
    struct itimerval off {};
    ::setitimer(ITIMER_PROF, &off, nullptr);
    ::sigaction(SIGPROF, &gOldAction, nullptr);
    ::setitimer(ITIMER_PROF, &gOldTimer, nullptr);
#endif
    gProfiling = false;

    int64_t endNanos = nowNanos();
    size_t n = gSampleCount.load(std::memory_order_relaxed);
    SampleStack* samples = state->samples;

    StringTable st;
    std::unordered_map<void*, uint64_t> locIds;
    std::unordered_map<std::string, uint64_t> fnIds;
    std::vector<ProtoBuf> locationMsgs;
    std::vector<ProtoBuf> functionMsgs;

    auto functionFor = [&](void* pc) -> uint64_t {
        // resolve symbol at stop time (not in the handler)
#ifdef _WIN32
        // No execinfo: use the raw address as the "symbol". Only reached
        // when samples exist — the Windows sampler records none today.
        char addrBuf[32];
        std::snprintf(addrBuf, sizeof(addrBuf), "0x%p", pc);
        std::string sym = addrBuf;
#else
        char** syms = ::backtrace_symbols(&pc, 1);
        std::string sym = syms ? syms[0] : "";
        if (syms) free(syms);
        // "path(mangled+0xoff) [0xaddr]" → keep the "path(mangled+0xoff)" part
        size_t sp = sym.rfind(" [");
        if (sp != std::string::npos) sym.resize(sp);
#endif
        auto it = fnIds.find(sym);
        if (it != fnIds.end()) return it->second;
        uint64_t id = static_cast<uint64_t>(fnIds.size()) + 1;
        fnIds.emplace(sym, id);
        ProtoBuf fn;
        fn.u64(1, id);                    // id
        fn.i64(2, st.intern(sym));        // name
        fn.i64(3, st.intern(sym));        // system_name
        fn.i64(4, st.intern(""));         // filename (unknown)
        functionMsgs.push_back(std::move(fn));
        return id;
    };

    auto locationFor = [&](void* pc) -> uint64_t {
        auto it = locIds.find(pc);
        if (it != locIds.end()) return it->second;
        uint64_t id = static_cast<uint64_t>(locIds.size()) + 1;
        locIds.emplace(pc, id);
        uint64_t fid = functionFor(pc);
        ProtoBuf line;
        line.u64(1, fid); // function_id
        line.i64(2, 0);   // line (unknown without DWARF)
        ProtoBuf loc;
        loc.u64(1, id);                            // id
        loc.u64(3, reinterpret_cast<uintptr_t>(pc)); // address
        loc.msg(4, line.buf);                      // line
        locationMsgs.push_back(std::move(loc));
        return id;
    };

    // aggregate identical stacks like Go's profile builder
    std::map<std::vector<uint64_t>, int64_t> sampleCounts;
    for (size_t i = 0; i < n; i++) {
        const SampleStack& s = samples[i];
        std::vector<uint64_t> locs;
        locs.reserve(s.depth);
        for (int f = 0; f < s.depth; f++) {
            locs.push_back(locationFor(s.pc[f])); // leaf-first order
        }
        sampleCounts[std::move(locs)]++;
    }

    ProtoBuf prof;
    {
        ProtoBuf vt;
        vt.i64(1, st.intern("samples"));
        vt.i64(2, st.intern("count"));
        prof.msg(1, vt.buf); // sample_type
        ProtoBuf vt2;
        vt2.i64(1, st.intern("cpu"));
        vt2.i64(2, st.intern("nanoseconds"));
        prof.msg(1, vt2.buf);
    }
    for (const auto& [locs, count] : sampleCounts) {
        ProtoBuf sample;
        sample.packedU64(1, locs);                          // location_id
        sample.packedI64(2, {count, count * kPeriodNanos}); // value
        prof.msg(2, sample.buf);
    }
    for (const auto& m : locationMsgs) prof.buf += wrapMsg(4, m.buf);
    for (const auto& m : functionMsgs) prof.buf += wrapMsg(5, m.buf);
    for (const auto& s : st.v) prof.str(6, s); // string_table
    prof.i64(9, state->startNanos);           // time_nanos
    prof.i64(10, endNanos - state->startNanos); // duration_nanos
    {
        ProtoBuf pt;
        pt.i64(1, st.intern("cpu"));
        pt.i64(2, st.intern("nanoseconds"));
        prof.msg(11, pt.buf); // period_type
    }
    prof.i64(12, kPeriodNanos); // period

    GzipFdWriter gz(fd);
    gz.write(prof.buf);
    gz.finish();

    delete[] samples;
    gSamples = nullptr;
    delete state;
}

// pprof.Lookup("heap"/"allocs").WriteTo analog: a valid pprof profile with
// heap sample types and no samples (no C++ analog for Go's allocator data).
std::string writeHeapProfileProto(bool alloc) {
    StringTable st;
    ProtoBuf prof;
    const char* types[][2] = {
        {"inuse_objects", "count"},
        {"inuse_space", "bytes"},
        {"alloc_objects", "count"},
        {"alloc_space", "bytes"},
    };
    for (auto& t : types) {
        ProtoBuf vt;
        vt.i64(1, st.intern(t[0]));
        vt.i64(2, st.intern(t[1]));
        prof.msg(1, vt.buf);
    }
    for (const auto& s : st.v) prof.str(6, s);
    prof.i64(9, nowNanos());
    ProtoBuf pt;
    pt.i64(1, st.intern("space"));
    pt.i64(2, st.intern("bytes"));
    prof.msg(11, pt.buf);
    prof.i64(12, 524288); // Go's default heap profile rate
    (void)alloc;
    return prof.buf;
}

std::string mkdirAll(std::string_view dir) {
    std::error_code ec;
    fs::create_directories(std::string(dir), ec);
    if (ec) return ec.message();
    return {};
}

std::string joinPath(std::string_view a, std::string_view b) {
    std::string r(a);
    if (!r.empty() && r.back() != '/') r += '/';
    r += b;
    return r;
}

int openCreate(const std::string& path) {
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

std::string errnoMessage(std::string_view prefix, int e) {
#ifdef _WIN32
    return std::string(prefix) + ": " + w32::errnoText(e);
#else
    return std::string(prefix) + ": " + std::strerror(e);
#endif
}

} // namespace

// --- ProfileSession -----------------------------------------------------------

class ProfileSession {
public:
    std::string cpuFilePath;
    std::string memFilePath;
    int cpuFd = -1;
    std::ostream* logWriter = nullptr;
    CPUProfileState* state = nullptr;

    ~ProfileSession() {
        if (cpuFd >= 0) ::close(cpuFd);
    }

    // Stop — pprof.go:49
    void stop() {
        if (state) {
            tsc::pprof::stopCPUProfile(cpuFd, state);
            state = nullptr;
        }
        if (cpuFd >= 0) {
            ::close(cpuFd);
            cpuFd = -1;
        }

        if (!memFilePath.empty()) {
            int memFd = openCreate(memFilePath);
            if (memFd < 0) {
                tscUnreachable(errnoMessage("create memprofile", errno).c_str());
            }
            std::string proto = writeHeapProfileProto(/*alloc=*/true);
            GzipFdWriter gz(memFd);
            gz.write(proto);
            gz.finish();
            ::close(memFd);
            if (logWriter) {
                *logWriter << "Memory profile: " << memFilePath << "\n";
            }
        }

        if (logWriter) {
            *logWriter << "CPU profile: " << cpuFilePath << "\n";
        }
    }
};

// BeginProfiling — pprof.go:23
ProfileSession* beginProfiling(std::string_view profileDir, std::ostream* logWriter) {
    std::string err = mkdirAll(profileDir);
    if (!err.empty()) {
        tscUnreachable(err.c_str());
    }

    pid_t pid = ::getpid();
    std::string dir(profileDir);
    std::string pidStr = std::to_string(pid);

    std::string cpuProfilePath = joinPath(dir, pidStr + "-cpuprofile.pb.gz");
    std::string memProfilePath = joinPath(dir, pidStr + "-memprofile.pb.gz");
    int cpuFd = openCreate(cpuProfilePath);
    if (cpuFd < 0) {
        tscUnreachable(errnoMessage("create cpuprofile", errno).c_str());
    }

    auto* session = new ProfileSession();
    CPUProfileState* state = nullptr;
    err = tsc::pprof::startCPUProfile(cpuFd, &state);
    if (!err.empty()) {
        ::close(cpuFd);
        delete session;
        tscUnreachable(err.c_str());
    }
    session->state = state;
    session->cpuFilePath = cpuProfilePath;
    session->memFilePath = memProfilePath;
    session->cpuFd = cpuFd;
    session->logWriter = logWriter;
    return session;
}

// CPUProfiler::StartCPUProfile — pprof.go:75
std::string CPUProfiler::startCPUProfile(std::string_view profileDir) {
    std::lock_guard<std::mutex> lk(mu_);

    if (session_ != nullptr) {
        return "CPU profiling already in progress";
    }

    if (std::string err = mkdirAll(profileDir); !err.empty()) {
        return "failed to create profile directory: " + err;
    }

    std::string cpuProfilePath =
        joinPath(profileDir, std::to_string(::getpid()) + "-" +
                                 std::to_string(nowMillis()) + "-cpuprofile.pb.gz");
    int cpuFd = openCreate(cpuProfilePath);
    if (cpuFd < 0) {
        return errnoMessage("failed to create CPU profile file", errno);
    }

    CPUProfileState* state = nullptr;
    if (std::string err = tsc::pprof::startCPUProfile(cpuFd, &state); !err.empty()) {
        ::close(cpuFd);
        std::error_code ec;
        fs::remove(cpuProfilePath, ec);
        return "failed to start CPU profile: " + err;
    }

    auto session = std::make_unique<ProfileSession>();
    session->cpuFilePath = cpuProfilePath;
    session->cpuFd = cpuFd;
    session->state = state;
    session->logWriter = nullptr; // io.Discard
    session_ = std::move(session);
    return {};
}

// CPUProfiler::StopCPUProfile — pprof.go:108
CPUProfiler::CPUProfiler() = default;
CPUProfiler::~CPUProfiler() = default;

std::pair<std::string, std::string> CPUProfiler::stopCPUProfile() {
    std::lock_guard<std::mutex> lk(mu_);

    if (session_ == nullptr) {
        return {"", "CPU profiling not in progress"};
    }

    std::string filePath = session_->cpuFilePath;
    session_->stop();
    session_.reset();

    return {filePath, {}};
}

// SaveHeapProfile — pprof.go:124
std::pair<std::string, std::string> saveHeapProfile(std::string_view profileDir) {
    if (std::string err = mkdirAll(profileDir); !err.empty()) {
        return {"", "failed to create profile directory: " + err};
    }

    std::string heapProfilePath =
        joinPath(profileDir, std::to_string(::getpid()) + "-" +
                                 std::to_string(nowMillis()) + "-heapprofile.pb.gz");
    int heapFd = openCreate(heapProfilePath);
    if (heapFd < 0) {
        return {"", errnoMessage("failed to create heap profile file", errno)};
    }

    runGC(); // runtime.GC() — no-op in the C++ port
    {
        std::string proto = writeHeapProfileProto(/*alloc=*/false);
        GzipFdWriter gz(heapFd);
        gz.write(proto);
        gz.finish();
        ::close(heapFd);
    }

    return {heapProfilePath, {}};
}

// SaveAllocProfile — pprof.go:146
std::pair<std::string, std::string> saveAllocProfile(std::string_view profileDir) {
    if (std::string err = mkdirAll(profileDir); !err.empty()) {
        return {"", "failed to create profile directory: " + err};
    }

    std::string allocProfilePath =
        joinPath(profileDir, std::to_string(::getpid()) + "-" +
                                 std::to_string(nowMillis()) + "-allocprofile.pb.gz");
    int allocFd = openCreate(allocProfilePath);
    if (allocFd < 0) {
        return {"", errnoMessage("failed to create alloc profile file", errno)};
    }

    {
        std::string proto = writeHeapProfileProto(/*alloc=*/true);
        GzipFdWriter gz(allocFd);
        gz.write(proto);
        gz.finish();
        ::close(allocFd);
    }

    return {allocProfilePath, {}};
}

} // namespace tsc::pprof
