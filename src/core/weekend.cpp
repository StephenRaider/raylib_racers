#include "weekend.hpp"

#include <algorithm>

#include "race.hpp"

namespace rr {

void startWeekend(std::vector<EntrySpec>& entries) {
    for (EntrySpec& e : entries) e.memory = newWeekendMemory();
}

RaceConfig practiceConfig(const RaceConfig& race, const EntrySpec& e, int laps) {
    RaceConfig c = race;
    c.entries = {e};
    c.entries[0].tires = 0;  // practice: the robot picks its tyres
    c.entries[0].fuel = 0;
    c.laps = std::max(1, laps);
    c.session = RR_SESSION_PRACTICE;
    c.twoCompounds = 0;
    c.pitsClosed = false;
    c.fuelLimit = 0;
    c.coolDown = false;
    c.jsonOut.clear();
    c.telemetryDir.clear();
    c.testLog.clear();
    return c;
}

RaceConfig qualiConfig(const RaceConfig& race, const EntrySpec& e, float fuelLimit) {
    RaceConfig c = practiceConfig(race, e, kQualiLaps);
    c.entries[0].tires = e.tires;
    c.session = RR_SESSION_QUALIFYING;
    c.fuelLimit = fuelLimit;
    return c;
}

float runAlone(const RaceConfig& rc, const std::vector<std::string>& botDirs,
               const std::vector<std::string>& trackDirs, std::string* err) {
    Race r;
    if (!r.setup(rc, botDirs, trackDirs, err)) return -1;
    while (!r.isOver()) r.step();
    return r.cars()[0].bestLap;
}

std::vector<int> gridOrder(const std::vector<float>& times) {
    std::vector<int> idx(times.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        const bool ta = times[a] > 0, tb = times[b] > 0;
        if (ta != tb) return ta;
        return ta && times[a] < times[b];
    });
    return idx;
}

}  // namespace rr
