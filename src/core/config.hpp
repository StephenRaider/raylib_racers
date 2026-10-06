#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rr {

struct EntrySpec {
    std::string robot;   // name in the bots directory, or a path to a shared library
    std::string params;  // passed verbatim to the robot's create()
    std::string name;    // display name (defaults to the robot's name)
};

struct RaceConfig {
    std::string track = "circuit";
    int laps = 3;
    std::vector<EntrySpec> entries;
    uint64_t seed = 1;
    float sensorNoise = 0.0f;  // relative std-dev on range finders
    float dt = 0.002f;         // physics step (500 Hz)
    int robotHz = 50;
    float maxTime = 0;         // 0 = automatic
    float fuelRate = 1.0f;     // fuel consumption multiplier
    float wearRate = 1.0f;     // tyre wear multiplier
    std::string jsonOut;
    std::string telemetryDir;
    bool quiet = false;

    // viewer only
    int width = 1600, height = 900;
    bool fullscreen = false;
    float timeScale = 1.0f;
    std::string screenshot;
    float screenshotAt = -1;   // race time; the viewer exits after saving
    int camera = 0;
    int focus = -1;            // car index to follow (-1: the leader)
};

// Parses the shared command line. Returns false and fills err on bad input;
// sets wantHelp for -h/--help.
bool parseArgs(int argc, char** argv, RaceConfig& cfg, bool viewer, bool& wantHelp, std::string* err);
std::string usage(const char* prog, bool viewer);

// Directory of the running executable.
std::string exeDir(const char* argv0);

}  // namespace rr
