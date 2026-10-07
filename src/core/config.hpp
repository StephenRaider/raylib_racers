#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rr {

struct EntrySpec {
    std::string robot;   // name in the bots directory, or a path to a shared library
    std::string params;  // passed verbatim to the robot's create()
    std::string name;    // display name (defaults to the robot's name)
    std::string spec;    // car spec file or name in specs/ ("" = the built-in F1 car)
    std::string dev;     // development tokens, e.g. "top_speed=3,handling=-1"
    int tires = 0;       // starting compound chosen by the team (RR_TIRE_*), 0 = the robot decides
    float fuel = 0;      // starting fuel chosen by the team, litres; 0 = the robot decides
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
    float ambient = 25.0f;     // air and track temperature, C
    int twoCompounds = -1;     // two-compound rule: 1 on, 0 off, -1 automatic (races over 20 laps)
    std::string devRules = "development";  // rules for --dev, file or name in specs/
    float fuelLimit = 0;       // > 0: no car starts with more fuel than this (qualifying runs)
    bool pitsClosed = false;   // no pit stops (testing sessions)
    std::string testLog;       // save car 0's run to this test-run folder (see testlog.hpp)
    std::string jsonOut;
    std::string telemetryDir;
    bool quiet = false;
    bool coolDown = false;  // keep running after the flag until the cars have parked in the pit lane

    // viewer only
    int width = 1600, height = 900;
    bool fullscreen = false;
    float timeScale = 1.0f;
    std::string screenshot;
    float screenshotAt = -1;   // race time; the viewer exits after saving
    int camera = 0;
    int focus = -1;            // car index to follow (-1: the leader)
    bool noMenu = false;       // start racing straight away
    bool mute = false;
    std::string soundTest;     // render the focused car's engine to this WAV and exit
    bool test = false;         // open in the Testing session (with --no-menu or --at: start the run)
    int resultsView = 0;       // race-end window, 0-based
    int testView = 0;          // testing screen: 0 dashboard, 1 driving, 2 session, 3 track and events
    float scrubAt = -1;        // testing screen: show this moment of the run (screenshots)
    std::string page;          // setup page to open: stats, runs (screenshots)
};

// Parses the shared command line. Returns false and fills err on bad input;
// sets wantHelp for -h/--help.
bool parseArgs(int argc, char** argv, RaceConfig& cfg, bool viewer, bool& wantHelp, std::string* err);
std::string usage(const char* prog, bool viewer);

// Directory of the running executable.
std::string exeDir(const char* argv0);

}  // namespace rr
