#pragma once
// A race weekend: practice and qualifying, each car alone on track, then the
// race. Each car gets one weekend memory (RRRobotConfig.memory) that carries
// from session to session and is thrown away after the race.
#include <string>
#include <vector>

#include "config.hpp"

namespace rr {

class Race;

constexpr int kPracticeLaps = 15;  // default practice lap limit
constexpr int kQualiLaps = 3;      // out lap and two flying laps

// Gives each entry a fresh, zeroed weekend memory.
void startWeekend(std::vector<EntrySpec>& entries);

// One car alone: a practice session of `laps` laps (pits open, any tyres).
RaceConfig practiceConfig(const RaceConfig& race, const EntrySpec& e, int laps);
// One car alone: qualifying. fuelLimit > 0 caps the starting fuel.
RaceConfig qualiConfig(const RaceConfig& race, const EntrySpec& e, float fuelLimit);

// Runs a one-car session to the end. Returns the best lap (0 = none) or -1 on error.
float runAlone(const RaceConfig& rc, const std::vector<std::string>& botDirs,
               const std::vector<std::string>& trackDirs, std::string* err);

// Entry indices sorted by lap time, untimed cars last in their original order.
std::vector<int> gridOrder(const std::vector<float>& times);

}  // namespace rr
