// rr_race: runs a race without graphics as fast as the CPU allows.
#include <chrono>
#include <cstdio>

#include "../../src/core/race.hpp"

#ifndef RR_SOURCE_DIR
#define RR_SOURCE_DIR "."
#endif

int main(int argc, char** argv) {
    rr::RaceConfig cfg;
    bool help = false;
    std::string err;
    if (!rr::parseArgs(argc, argv, cfg, false, help, &err)) {
        std::fprintf(stderr, "error: %s\n\n%s", err.c_str(), rr::usage(argv[0], false).c_str());
        return 2;
    }
    if (help) {
        std::printf("%s", rr::usage(argv[0], false).c_str());
        return 0;
    }
    if (cfg.entries.empty()) {
        cfg.entries = {{"racingline", "", "", "", ""}, {"gapfollow", "", "", "", ""}, {"simple", "", "", "", ""}};
        if (!cfg.quiet) std::printf("no --car given, racing the example robots\n");
    }

    const std::string dir = rr::exeDir(argv[0]);
    rr::Race race;
    if (!race.setup(cfg, {dir + "/bots", dir, "bots", "."}, {dir + "/tracks", RR_SOURCE_DIR "/tracks", "tracks"}, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (const auto& w : race.track().warnings()) std::fprintf(stderr, "track warning: %s\n", w.c_str());
    if (!cfg.quiet) {
        std::printf("%s: %.0f m, %zu cars, %d laps\n", race.track().name().c_str(), race.track().length(),
                    race.cars().size(), cfg.laps);
    }

    auto t0 = std::chrono::steady_clock::now();
    double nextReport = 30.0;
    while (!race.isOver()) {
        race.step();
        if (!cfg.quiet && race.time() >= nextReport) {
            const auto& lead = race.cars()[race.order()[0]];
            std::printf("  t=%5.0fs  leader %-16s lap %d/%d\n", race.time(), lead.name.c_str(),
                        lead.currentLap(cfg.laps), cfg.laps);
            nextReport += 30.0;
        }
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    race.printResults(stdout);
    if (!cfg.quiet)
        std::printf("\nsimulated %.1f s in %.2f s of CPU (%.0fx real time)\n", race.time(), wall,
                    wall > 0 ? race.time() / wall : 0.0);
    if (!cfg.jsonOut.empty() && !race.writeJson(cfg.jsonOut, wall)) {
        std::fprintf(stderr, "error: cannot write %s\n", cfg.jsonOut.c_str());
        return 1;
    }
    return 0;
}
