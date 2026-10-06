#include "robot_loader.hpp"

#include <filesystem>
#include <map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace rr {

namespace {

#if defined(_WIN32)
const char* kExt = ".dll";
void* openLib(const std::string& p) { return (void*)LoadLibraryA(p.c_str()); }
void* findSym(void* h, const char* s) { return (void*)GetProcAddress((HMODULE)h, s); }
void closeLib(void* h) { FreeLibrary((HMODULE)h); }
std::string libError() { return "LoadLibrary error " + std::to_string(GetLastError()); }
#else
#if defined(__APPLE__)
const char* kExt = ".dylib";
#else
const char* kExt = ".so";
#endif
void* openLib(const std::string& p) { return dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* findSym(void* h, const char* s) { return dlsym(h, s); }
void closeLib(void* h) { dlclose(h); }
std::string libError() { const char* e = dlerror(); return e ? e : "unknown dlopen error"; }
#endif

std::map<std::string, std::weak_ptr<RobotModule>>& cache() {
    static std::map<std::string, std::weak_ptr<RobotModule>> c;
    return c;
}

}  // namespace

RobotModule::~RobotModule() {
    if (handle_) closeLib(handle_);
}

std::shared_ptr<RobotModule> RobotModule::load(const std::string& nameOrPath,
                                               const std::vector<std::string>& searchDirs,
                                               std::string* err) {
    std::string path;
    std::error_code ec;
    if (fs::exists(nameOrPath, ec) && !fs::is_directory(nameOrPath, ec)) {
        path = nameOrPath;
    } else {
        for (const auto& d : searchDirs) {
            for (std::string cand : {nameOrPath + kExt, "lib" + nameOrPath + kExt, nameOrPath}) {
                fs::path p = fs::path(d) / cand;
                if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
                    path = p.string();
                    break;
                }
            }
            if (!path.empty()) break;
        }
    }
    if (path.empty()) {
        if (err) *err = "robot '" + nameOrPath + "' not found (looked in the bots directory and as a path)";
        return nullptr;
    }
    path = fs::absolute(path, ec).string();

    auto& c = cache();
    if (auto it = c.find(path); it != c.end())
        if (auto sp = it->second.lock()) return sp;

    void* h = openLib(path);
    if (!h) {
        if (err) *err = "cannot load " + path + ": " + libError();
        return nullptr;
    }
    auto entry = (RRRobotEntryFn)findSym(h, RR_ROBOT_ENTRY_SYMBOL);
    if (!entry) {
        closeLib(h);
        if (err) *err = path + " does not export " RR_ROBOT_ENTRY_SYMBOL "()";
        return nullptr;
    }
    const RRRobotApi* api = entry();
    if (!api || api->abi_version != RR_ABI_VERSION || !api->create || !api->drive) {
        closeLib(h);
        if (err)
            *err = path + ": incompatible robot (ABI " + std::to_string(api ? api->abi_version : -1) +
                   ", host expects " + std::to_string(RR_ABI_VERSION) + ")";
        return nullptr;
    }
    auto mod = std::shared_ptr<RobotModule>(new RobotModule());
    mod->handle_ = h;
    mod->api_ = api;
    mod->path_ = path;
    c[path] = mod;
    return mod;
}

}  // namespace rr
