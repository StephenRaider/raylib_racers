#include "config.hpp"

#include <filesystem>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace rr {

std::string usage(const char* prog, bool viewer) {
    std::string u = std::string("usage: ") + prog + " [options] --car ROBOT [--car ROBOT ...]\n\n" +
        "Race setup\n"
        "  --track NAME|FILE      track name in tracks/ or a .trk file (default circuit)\n"
        "  --laps N               race length (default 3)\n"
        "  --car ROBOT            add a car; ROBOT is a name in bots/ or a path to a robot library\n"
        "  --params STR           parameters for the last --car, e.g. \"speed=1.1,line=0.3\"\n"
        "  --name STR             display name for the last --car\n"
        "  --seed N               random seed (sensor noise)\n"
        "  --noise X              range-finder noise, relative std-dev (default 0)\n"
        "  --dt SECONDS           physics step (default 0.002)\n"
        "  --robot-hz N           how often robots drive (default 50)\n"
        "  --max-time SECONDS     abort the race after this long (default: automatic)\n"
        "  --fuel-rate X          fuel consumption multiplier (default 1)\n"
        "  --wear-rate X          tyre wear multiplier (default 1; raise it to force stops in short races)\n"
        "Output\n"
        "  --json FILE            write results as JSON\n"
        "  --telemetry DIR        write one CSV per car at the robot rate\n"
        "  --quiet                print only the results\n";
    if (viewer) {
        u += "Viewer\n"
             "  --width N --height N  window size (default 1600x900)\n"
             "  --fullscreen\n"
             "  --speed X             start at X times real time\n"
             "  --camera N            0 follow, 1 cinematic, 2 TV, 3 helicopter, 4 top down, 5 orbit, 6 overview\n"
             "  --focus N             follow car N (0 = first --car); default: whoever leads\n"
             "  --screenshot FILE     save a screenshot at --at seconds of race time, then exit\n"
             "  --at SECONDS\n"
             "Keys: Tab/Left/Right focus car, L follow leader, C or F2-F8 camera, Space pause, +/- speed, R restart, H HUD, F1 help\n";
    }
    return u;
}

bool parseArgs(int argc, char** argv, RaceConfig& cfg, bool viewer, bool& wantHelp, std::string* err) {
    wantHelp = false;
    auto need = [&](int& i, const std::string& opt) -> const char* {
        if (i + 1 >= argc) throw std::runtime_error(opt + " needs a value");
        return argv[++i];
    };
    try {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-h" || a == "--help") { wantHelp = true; return true; }
            else if (a == "--track") cfg.track = need(i, a);
            else if (a == "--laps") cfg.laps = std::stoi(need(i, a));
            else if (a == "--car") cfg.entries.push_back({need(i, a), "", ""});
            else if (a == "--params" || a == "--name") {
                const char* v = need(i, a);
                if (cfg.entries.empty()) throw std::runtime_error(a + " must follow a --car");
                (a == "--params" ? cfg.entries.back().params : cfg.entries.back().name) = v;
            }
            else if (a == "--seed") cfg.seed = std::stoull(need(i, a));
            else if (a == "--noise") cfg.sensorNoise = std::stof(need(i, a));
            else if (a == "--dt") cfg.dt = std::stof(need(i, a));
            else if (a == "--robot-hz") cfg.robotHz = std::stoi(need(i, a));
            else if (a == "--fuel-rate") cfg.fuelRate = std::stof(need(i, a));
            else if (a == "--wear-rate") cfg.wearRate = std::stof(need(i, a));
            else if (a == "--max-time") cfg.maxTime = std::stof(need(i, a));
            else if (a == "--json") cfg.jsonOut = need(i, a);
            else if (a == "--telemetry") cfg.telemetryDir = need(i, a);
            else if (a == "--quiet") cfg.quiet = true;
            else if (viewer && a == "--width") cfg.width = std::stoi(need(i, a));
            else if (viewer && a == "--height") cfg.height = std::stoi(need(i, a));
            else if (viewer && a == "--fullscreen") cfg.fullscreen = true;
            else if (viewer && a == "--speed") cfg.timeScale = std::stof(need(i, a));
            else if (viewer && a == "--camera") cfg.camera = std::stoi(need(i, a));
            else if (viewer && a == "--focus") cfg.focus = std::stoi(need(i, a));
            else if (viewer && a == "--screenshot") cfg.screenshot = need(i, a);
            else if (viewer && a == "--at") cfg.screenshotAt = std::stof(need(i, a));
            else throw std::runtime_error("unknown option " + a);
        }
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
    if (cfg.laps < 1) { if (err) *err = "--laps must be at least 1"; return false; }
    if (cfg.dt <= 0 || cfg.dt > 0.02f) { if (err) *err = "--dt must be in (0, 0.02]"; return false; }
    if (cfg.fuelRate < 0 || cfg.wearRate < 0) { if (err) *err = "--fuel-rate and --wear-rate must not be negative"; return false; }
    if (cfg.robotHz < 1) { if (err) *err = "--robot-hz must be positive"; return false; }
    return true;
}

std::string exeDir(const char* argv0) {
    std::error_code ec;
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0) return fs::path(std::string(buf, n)).parent_path().string();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::canonical(buf, ec).parent_path().string();
#else
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path().string();
#endif
    return fs::absolute(argv0, ec).parent_path().string();
}

}  // namespace rr
