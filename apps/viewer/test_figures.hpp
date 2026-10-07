#pragma once
// What the stats do to the car, as numbers a person can compare: the testing
// setup shows these next to the stats while they are being changed.
#include <string>
#include <vector>

#include "spec.hpp"

struct FigureLine {
    std::string name, unit;
    float stock = 0, now = 0;
    int decimals = 0;
    bool higherBetter = true;
    unsigned statMask = 0;  // bit k: stat k changes this figure
    bool approx = false;    // an estimate rather than a measurement
};

struct FigureInputs {
    float fuelPerLap = 2.3f;    // l, stock car on this track (calibration lap)
    float tyreLifeLaps = 20;    // medium tyre, stock car, at this session's wear rate (0 = no wear)
};

// Straight-line runs with the car physics (acceleration, top speed, braking)
// plus the stats' direct effects (grip, downforce, pit crew, fuel, tyres).
std::vector<FigureLine> carFigures(const rr::DevRules& rules, const std::vector<int>& stats, const FigureInputs& in);

// The car with these stats.
rr::CarParams carWithStats(const rr::DevRules& rules, const std::vector<int>& stats);
