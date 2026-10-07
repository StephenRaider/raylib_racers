#include "robot_driver.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "bot_protocol.hpp"
#include "robot_loader.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#else
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rr {

#if defined(_WIN32)
double threadCpuSeconds() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
double threadCpuSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
#endif

namespace {

// ---------------------------------------------------------------- in process

class InProcessRobot : public RobotDriver {
public:
    explicit InProcessRobot(std::shared_ptr<RobotModule> m) : mod_(std::move(m)) {
        api_ = mod_->api();
        abi_ = api_->abi_version;
        name_ = api_->name ? api_->name : "";
    }
    ~InProcessRobot() override {
        if (self_ && api_->destroy) api_->destroy(self_);
    }
    bool create(const RRTrackInfo& track, const RRCarSpec& car, int index, const std::string& params,
                RRRobotConfig& cfg, std::string*) override {
        self_ = api_->create(&track, &car, index, params.c_str(), &cfg);
        return self_ != nullptr;
    }
    void beginDrive(const RRSensors& s, const RRControl& in) override {
        out_ = in;
        const double t0 = threadCpuSeconds();
        api_->drive(self_, &s, &out_);
        cpu_ = threadCpuSeconds() - t0;
    }
    DriveResult endDrive(RRControl& out) override {
        out = out_;
        DriveResult r;
        r.cpu = cpu_;
        return r;
    }
    void sessionEnd(const RRSessionSummary& s, std::vector<unsigned char>*) override {
        if (self_ && abi_ >= 8 && api_->session_end) api_->session_end(self_, &s);  // older RRRobotApi ends before it
    }
    bool hasDebugPath() const override { return api_->debug_path != nullptr; }
    int debugPath(float* xy, int maxPoints) override {
        return self_ && api_->debug_path ? api_->debug_path(self_, xy, maxPoints) : 0;
    }

private:
    std::shared_ptr<RobotModule> mod_;
    const RRRobotApi* api_ = nullptr;
    void* self_ = nullptr;
    RRControl out_{};
    double cpu_ = 0;
};

// ---------------------------------------------------------------- child process

// rr_bothost with its stdin and stdout as the two ends of the protocol.
class Child {
public:
    ~Child() { kill(); }

    bool start(const std::string& exe, const std::string& lib, std::string* err);
    bool write(const void* p, size_t n);
    // Reads exactly n bytes; false on end of file, error or after `timeout` s.
    bool read(void* p, size_t n, double timeout);
    void kill();
    bool alive() const { return alive_; }

private:
    bool alive_ = false;
#if defined(_WIN32)
    HANDLE proc_ = nullptr, job_ = nullptr, in_ = nullptr, out_ = nullptr;  // in_: we write; out_: we read
#else
    pid_t pid_ = -1;
    int in_ = -1, out_ = -1;
#endif
};

#if defined(_WIN32)

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// A copy of our own token at low integrity: the robot can't write to the
// user's files or talk to other programs' windows.
HANDLE lowIntegrityToken() {
    HANDLE tok = nullptr, low = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY,
                          &tok))
        return nullptr;
    if (!DuplicateTokenEx(tok, 0, nullptr, SecurityImpersonation, TokenPrimary, &low)) low = nullptr;
    CloseHandle(tok);
    if (!low) return nullptr;
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-16-4096", &sid)) {
        CloseHandle(low);
        return nullptr;
    }
    TOKEN_MANDATORY_LABEL tml{};
    tml.Label.Attributes = SE_GROUP_INTEGRITY;
    tml.Label.Sid = sid;
    const bool ok = SetTokenInformation(low, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(sid)) != 0;
    LocalFree(sid);
    if (!ok) {
        CloseHandle(low);
        return nullptr;
    }
    return low;
}

bool Child::start(const std::string& exe, const std::string& lib, std::string* err) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE childIn = nullptr, childOut = nullptr;
    if (!CreatePipe(&childIn, &in_, &sa, 1 << 20) || !CreatePipe(&out_, &childOut, &sa, 1 << 20)) {
        if (err) *err = "cannot create pipes";
        return false;
    }
    SetHandleInformation(in_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childIn;
    si.hStdOutput = childOut;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + widen(exe) + L"\" \"" + widen(lib) + L"\"";
    const DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
    HANDLE low = lowIntegrityToken();
    BOOL ok = FALSE;
    if (low) ok = CreateProcessAsUserW(low, nullptr, cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) {
        static bool warned = false;
        if (!warned) std::fprintf(stderr, "warning: sandbox: robots run at normal integrity (error %lu)\n", GetLastError());
        warned = true;
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
    }
    if (low) CloseHandle(low);
    CloseHandle(childIn);
    CloseHandle(childOut);
    if (!ok) {
        if (err) *err = "cannot start " + exe + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    // One process, no children, 2 GB of memory, no desktop tricks; killed with us.
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                              JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        li.BasicLimitInformation.ActiveProcessLimit = 1;
        li.ProcessMemoryLimit = (SIZE_T)2048 << 20;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &li, sizeof li);
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
        ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                                 JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                                 JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                                 JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
        SetInformationJobObject(job_, JobObjectBasicUIRestrictions, &ui, sizeof ui);
        AssignProcessToJobObject(job_, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    proc_ = pi.hProcess;
    alive_ = true;
    return true;
}

bool Child::write(const void* p, size_t n) {
    const char* c = (const char*)p;
    while (alive_ && n > 0) {
        DWORD w = 0;
        if (!WriteFile(in_, c, (DWORD)std::min<size_t>(n, 1 << 20), &w, nullptr)) return alive_ = false;
        c += w;
        n -= w;
    }
    return alive_;
}

bool Child::read(void* p, size_t n, double timeout) {
    char* c = (char*)p;
    const auto t0 = std::chrono::steady_clock::now();
    int spins = 0;
    while (alive_ && n > 0) {
        DWORD avail = 0;
        if (!PeekNamedPipe(out_, nullptr, 0, nullptr, &avail, nullptr)) return alive_ = false;
        if (avail == 0) {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout) return false;
            if (++spins < 2000) SwitchToThread();
            else Sleep(1);
            continue;
        }
        DWORD r = 0;
        if (!ReadFile(out_, c, (DWORD)std::min<size_t>(n, avail), &r, nullptr) || r == 0) return alive_ = false;
        c += r;
        n -= r;
    }
    return alive_;
}

void Child::kill() {
    if (proc_) {
        TerminateProcess(proc_, 1);
        WaitForSingleObject(proc_, 2000);
        CloseHandle(proc_);
        proc_ = nullptr;
    }
    if (job_) { CloseHandle(job_); job_ = nullptr; }
    if (in_) { CloseHandle(in_); in_ = nullptr; }
    if (out_) { CloseHandle(out_); out_ = nullptr; }
    alive_ = false;
}

#else

bool Child::start(const std::string& exe, const std::string& lib, std::string* err) {
    std::signal(SIGPIPE, SIG_IGN);  // a dead robot must not take the simulator with it
    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0) {
        if (err) *err = "cannot create pipes";
        return false;
    }
    if (pipe(fromChild) != 0) {
        close(toChild[0]);
        close(toChild[1]);
        if (err) *err = "cannot create pipes";
        return false;
    }
    pid_ = fork();
    if (pid_ < 0) {
        if (err) *err = "cannot fork";
        return false;
    }
    if (pid_ == 0) {
        dup2(toChild[0], 0);
        dup2(fromChild[1], 1);
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        execl(exe.c_str(), exe.c_str(), lib.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(toChild[0]);
    close(fromChild[1]);
    in_ = toChild[1];
    out_ = fromChild[0];
    fcntl(in_, F_SETFD, FD_CLOEXEC);
    fcntl(out_, F_SETFD, FD_CLOEXEC);
    alive_ = true;
    return true;
}

bool Child::write(const void* p, size_t n) {
    const char* c = (const char*)p;
    while (alive_ && n > 0) {
        const ssize_t w = ::write(in_, c, n);
        if (w <= 0) return alive_ = false;
        c += w;
        n -= (size_t)w;
    }
    return alive_;
}

bool Child::read(void* p, size_t n, double timeout) {
    char* c = (char*)p;
    const auto t0 = std::chrono::steady_clock::now();
    while (alive_ && n > 0) {
        const double left = timeout - std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (left <= 0) return false;
        pollfd pf{out_, POLLIN, 0};
        const int k = poll(&pf, 1, (int)(left * 1000) + 1);
        if (k < 0) return alive_ = false;
        if (k == 0) continue;
        const ssize_t r = ::read(out_, c, n);
        if (r <= 0) return alive_ = false;
        c += r;
        n -= (size_t)r;
    }
    return alive_;
}

void Child::kill() {
    if (in_ >= 0) { close(in_); in_ = -1; }
    if (out_ >= 0) { close(out_); out_ = -1; }
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
    }
    alive_ = false;
}

#endif

// ---------------------------------------------------------------- sandboxed

namespace bp = botproto;

class SandboxRobot : public RobotDriver {
public:
    bool start(const std::string& hostPath, const std::string& libPath, std::string* err) {
        if (!child_.start(hostPath, libPath, err)) return false;
        std::vector<unsigned char> m;
        uint32_t type = 0;
        if (!recv(type, m, kStartTimeout) || (type != bp::HELLO && type != bp::FAILED)) {
            if (err) *err = libPath + ": the robot host did not start";
            return false;
        }
        bp::Reader r(m);
        if (type == bp::FAILED) {
            if (err) *err = r.str();
            return false;
        }
        abi_ = r.pod<int32_t>();
        hasPath_ = r.pod<int32_t>() != 0;
        r.pod<int32_t>();  // has session_end: the host checks
        name_ = r.str();
        return r.ok;
    }
    ~SandboxRobot() override {
        if (child_.alive()) {
            send(bp::DESTROY, {});
            std::vector<unsigned char> m;
            uint32_t type = 0;
            recv(type, m, 1.0);
        }
    }
    bool sandboxed() const override { return true; }

    bool create(const RRTrackInfo& track, const RRCarSpec& car, int index, const std::string& params,
                RRRobotConfig& cfg, std::string* err) override {
        bp::Writer w;
        w.pod((int32_t)index);
        w.str(params);
        w.pod(car);
        w.pod(cfg);
        bp::writeTrack(w, track);
        w.bytes(cfg.memory, cfg.memory ? (size_t)cfg.memory_size : 0);
        std::vector<unsigned char> m;
        uint32_t type = 0;
        if (!send(bp::CREATE, w.b) || !recv(type, m, kStartTimeout) || type != bp::CREATED) {
            if (err) *err = name_ + ": the robot crashed or hung in create()";
            return false;
        }
        bp::Reader r(m);
        const int ok = r.pod<int32_t>();
        RRRobotConfig back = r.pod<RRRobotConfig>();
        if (!r.ok || !ok) return false;
        back.memory = cfg.memory;  // the pointer is ours, not the host's
        back.memory_size = cfg.memory_size;
        cfg = back;
        return true;
    }
    void beginDrive(const RRSensors& s, const RRControl& in) override {
        bp::Writer w;
        w.pod(s);
        w.pod(in);
        sent_ = send(bp::DRIVE, w.b);
        last_ = in;
    }
    DriveResult endDrive(RRControl& out) override {
        DriveResult res;
        out = last_;
        std::vector<unsigned char> m;
        uint32_t type = 0;
        if (!sent_ || !recv(type, m, timeout_) || type != bp::CONTROL) {
            res.failed = true;
            res.why = child_.alive() ? "robot hung (no answer in " + std::to_string((int)timeout_) + " s)" : "robot crashed";
            child_.kill();
            return res;
        }
        bp::Reader r(m);
        out = r.pod<RRControl>();
        res.cpu = r.pod<double>();
        if (!r.ok) {
            res.failed = true;
            res.why = "robot host sent a bad answer";
        }
        return res;
    }
    void sessionEnd(const RRSessionSummary& s, std::vector<unsigned char>* memory) override {
        bp::Writer w;
        w.pod(s);
        std::vector<unsigned char> m;
        uint32_t type = 0;
        if (!send(bp::SESSION_END, w.b) || !recv(type, m, 5.0) || type != bp::MEMORY) return;
        bp::Reader r(m);
        std::vector<unsigned char> mem = r.bytes();
        if (r.ok && memory && mem.size() == memory->size()) *memory = mem;
    }
    bool hasDebugPath() const override { return hasPath_; }
    int debugPath(float* xy, int maxPoints) override {
        if (!hasPath_ || !child_.alive()) return 0;
        bp::Writer w;
        w.pod((int32_t)maxPoints);
        std::vector<unsigned char> m;
        uint32_t type = 0;
        if (!send(bp::DEBUG_PATH, w.b) || !recv(type, m, 1.0) || type != bp::PATH) return 0;
        bp::Reader r(m);
        int n = r.pod<int32_t>();
        n = std::max(0, std::min(n, maxPoints));
        r.raw(xy, sizeof(float) * 2 * (size_t)n);
        return r.ok ? n : 0;
    }
    void setHangTimeout(double s) override { timeout_ = s; }

private:
    static constexpr double kStartTimeout = 10.0;
    Child child_;
    bool hasPath_ = false, sent_ = false;
    RRControl last_{};
    double timeout_ = 2.0;  // s a drive call may take before the robot counts as hung

    bool send(uint32_t type, const std::vector<unsigned char>& payload) {
        bp::Header h{type, (uint32_t)payload.size()};
        return child_.write(&h, sizeof h) && (payload.empty() || child_.write(payload.data(), payload.size()));
    }
    bool recv(uint32_t& type, std::vector<unsigned char>& payload, double timeout) {
        bp::Header h{};
        if (!child_.read(&h, sizeof h, timeout)) return false;
        if (h.size > (64u << 20)) return false;
        payload.resize(h.size);
        if (h.size && !child_.read(payload.data(), h.size, timeout)) return false;
        type = h.type;
        return true;
    }
};

}  // namespace

std::shared_ptr<RobotDriver> RobotDriver::inProcess(const std::shared_ptr<RobotModule>& module) {
    return std::make_shared<InProcessRobot>(module);
}

std::shared_ptr<RobotDriver> RobotDriver::sandbox(const std::string& hostPath, const std::string& libPath,
                                                  std::string* err) {
    auto r = std::make_shared<SandboxRobot>();
    if (!r->start(hostPath, libPath, err)) return nullptr;
    return r;
}

}  // namespace rr
