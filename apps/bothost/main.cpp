// rr_bothost: runs one car's robot in its own process for the simulator
// (--sandbox). It loads the robot library, locks itself down, then answers
// the simulator's calls over stdin/stdout (see src/core/bot_protocol.hpp).
//
// Locked down means: on Linux a seccomp filter refuses every system call
// but memory, time and the two pipes (no files, no network, no processes or
// threads) and no file can be written; on Windows the simulator starts us at
// low integrity inside a job (no child processes, no writing to the user's
// files, limited memory). A crash or a hang here only loses this car.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/core/bot_protocol.hpp"
#include "../../src/core/robot_driver.hpp"
#include "../../src/core/robot_loader.hpp"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#if defined(__linux__)
#include <cstddef>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
#endif

namespace bp = rr::botproto;

namespace {

int protoIn = 0, protoOut = 1;

bool readAll(void* p, size_t n) {
    char* c = (char*)p;
    while (n > 0) {
#if defined(_WIN32)
        const int r = _read(protoIn, c, (unsigned)std::min<size_t>(n, 1 << 20));
#else
        const ssize_t r = read(protoIn, c, n);
#endif
        if (r <= 0) return false;
        c += r;
        n -= (size_t)r;
    }
    return true;
}

bool writeAll(const void* p, size_t n) {
    const char* c = (const char*)p;
    while (n > 0) {
#if defined(_WIN32)
        const int w = _write(protoOut, c, (unsigned)std::min<size_t>(n, 1 << 20));
#else
        const ssize_t w = write(protoOut, c, n);
#endif
        if (w <= 0) return false;
        c += w;
        n -= (size_t)w;
    }
    return true;
}

bool send(uint32_t type, const std::vector<unsigned char>& payload) {
    bp::Header h{type, (uint32_t)payload.size()};
    return writeAll(&h, sizeof h) && (payload.empty() || writeAll(payload.data(), payload.size()));
}

// The protocol gets its own descriptors; whatever the robot prints goes to stderr.
void takePipes() {
#if defined(_WIN32)
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
    protoIn = _dup(0);
    protoOut = _dup(1);
    _dup2(2, 1);
#else
    protoIn = dup(0);
    protoOut = dup(1);
    dup2(2, 1);
#endif
}

#if defined(__linux__)
// Only what a robot needs to think: memory, time, its two pipes and stderr.
// Anything else (open, socket, clone, execve, ...) fails with EPERM.
bool lockDown() {
    rlimit none{0, 0};
    setrlimit(RLIMIT_FSIZE, &none);  // no file grows
    rlimit mem{(rlim_t)2048 << 20, (rlim_t)2048 << 20};
    setrlimit(RLIMIT_AS, &mem);
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return false;
    static const int allowed[] = {
        SYS_read, SYS_write, SYS_writev, SYS_close, SYS_lseek, SYS_fstat,
#ifdef SYS_newfstatat
        SYS_newfstatat,
#endif
#ifdef SYS_fstatat64
        SYS_fstatat64,
#endif
#ifdef SYS_statx
        SYS_statx,
#endif
        SYS_mmap, SYS_munmap, SYS_mprotect, SYS_mremap, SYS_madvise, SYS_brk,
        SYS_futex, SYS_clock_gettime, SYS_clock_getres, SYS_gettimeofday, SYS_nanosleep, SYS_clock_nanosleep,
        SYS_sched_yield, SYS_getpid, SYS_gettid, SYS_getrandom, SYS_rt_sigreturn, SYS_rt_sigprocmask,
        SYS_exit, SYS_exit_group,
#ifdef SYS_time
        SYS_time,
#endif
    };
#if defined(__x86_64__)
    const unsigned arch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    const unsigned arch = AUDIT_ARCH_AARCH64;
#elif defined(__i386__)
    const unsigned arch = AUDIT_ARCH_I386;
#else
#error "rr_bothost: add this architecture's AUDIT_ARCH"
#endif
    std::vector<sock_filter> f;
    // calls through another architecture's syscall table (int 0x80, x32) have other numbers: refuse them
    f.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)));
    f.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, arch, 1, 0));
    f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    f.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)));
#if defined(__x86_64__)
    f.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000u, 0, 1));  // x32
    f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
#endif
    for (int nr : allowed) {
        f.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)nr, 0, 1));
        f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }
    f.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 1 /* EPERM */));
    sock_fprog prog{(unsigned short)f.size(), f.data()};
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) == 0;
}
#else
bool lockDown() { return true; }  // Windows: the simulator starts us locked down
#endif

}  // namespace

int main(int argc, char** argv) {
    takePipes();
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s ROBOT_LIBRARY (started by the simulator with --sandbox)\n", argv[0]);
        return 2;
    }
    std::string err;
    auto mod = rr::RobotModule::load(argv[1], {}, &err);
    if (!mod) {
        bp::Writer w;
        w.str(err);
        send(bp::FAILED, w.b);
        return 1;
    }
    const RRRobotApi* api = mod->api();
    const bool hasSessionEnd = api->abi_version >= 8 && api->session_end;
    // Warm the C runtime (stdio buffers, time zone) before the filter goes on.
    std::fflush(stderr);
    (void)rr::threadCpuSeconds();
    if (!lockDown()) {
        bp::Writer w;
        w.str("cannot lock the robot host down");
        send(bp::FAILED, w.b);
        return 1;
    }
    {
        bp::Writer w;
        w.pod((int32_t)api->abi_version);
        w.pod((int32_t)(api->debug_path != nullptr));
        w.pod((int32_t)hasSessionEnd);
        w.str(api->name ? api->name : "");
        w.str(api->author ? api->author : "");
        if (!send(bp::HELLO, w.b)) return 1;
    }

    void* self = nullptr;
    bp::TrackCopy track;
    std::vector<unsigned char> memory, msg;
    std::vector<float> path;
    for (;;) {
        bp::Header h{};
        if (!readAll(&h, sizeof h)) break;
        msg.resize(h.size);
        if (h.size && !readAll(msg.data(), h.size)) break;
        bp::Reader r(msg);
        if (h.type == bp::CREATE) {
            const int index = r.pod<int32_t>();
            const std::string params = r.str();
            const RRCarSpec car = r.pod<RRCarSpec>();
            RRRobotConfig cfg = r.pod<RRRobotConfig>();
            const bool ok = bp::readTrack(r, track);
            memory = r.bytes();
            cfg.memory = memory.empty() ? nullptr : memory.data();
            cfg.memory_size = (int)memory.size();
            if (ok && r.ok && !self) self = api->create(&track.info, &car, index, params.c_str(), &cfg);
            bp::Writer w;
            w.pod((int32_t)(self != nullptr));
            w.pod(cfg);
            if (!send(bp::CREATED, w.b)) break;
        } else if (h.type == bp::DRIVE) {
            const RRSensors s = r.pod<RRSensors>();
            RRControl c = r.pod<RRControl>();
            const double t0 = rr::threadCpuSeconds();
            if (self && r.ok) api->drive(self, &s, &c);
            const double cpu = rr::threadCpuSeconds() - t0;
            bp::Writer w;
            w.pod(c);
            w.pod(cpu);
            if (!send(bp::CONTROL, w.b)) break;
        } else if (h.type == bp::DEBUG_PATH) {
            const int max = std::max(0, std::min(r.pod<int32_t>(), 1 << 16));
            path.assign(2 * (size_t)max, 0.0f);
            const int n = self && api->debug_path ? api->debug_path(self, path.data(), max) : 0;
            bp::Writer w;
            w.pod((int32_t)n);
            w.raw(path.data(), sizeof(float) * 2 * (size_t)std::max(0, std::min(n, max)));
            if (!send(bp::PATH, w.b)) break;
        } else if (h.type == bp::SESSION_END) {
            const RRSessionSummary s = r.pod<RRSessionSummary>();
            if (self && hasSessionEnd && r.ok) api->session_end(self, &s);
            bp::Writer w;
            w.bytes(memory.data(), memory.size());
            if (!send(bp::MEMORY, w.b)) break;
        } else if (h.type == bp::DESTROY) {
            if (self && api->destroy) api->destroy(self);
            self = nullptr;
            bp::Writer w;
            w.bytes(memory.data(), memory.size());
            send(bp::MEMORY, w.b);
            break;
        } else {
            break;
        }
    }
    if (self && api->destroy) api->destroy(self);
    return 0;
}
