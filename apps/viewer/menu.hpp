#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The race setup screen shown before each race: track, race length, tyre life and
// number of cars. Tyre life is chosen in laps; the menu turns it into a wear rate
// using what a calibration lap on that track measured.
struct TrackStats {
    std::string file;          // name in tracks/ (no extension)
    std::string title;         // the track's own name
    float length = 0;          // m
    float lapTime = 60;        // s, a racingline flying lap
    float fuelPerLap = 2.3f;   // l
    float wearPerLap = 0.03f;  // medium tyres at wear rate 1, the faster-wearing axle
    bool measured = false;
};

// A driving algorithm the grid page offers: a robot library and its parameters.
struct Algorithm {
    std::string label;   // shown in the menu and used as the car's name
    std::string robot;   // name in bots/ or a path
    std::string params;
    std::string stats;   // the team stats that suit this style ("key=n,..."; "" = all neutral)
};

// The team stats the rules allow (specs/development.json).
struct StatRules {
    std::vector<std::string> keys, labels;
    int budget = 40, min = 0, max = 10, neutral = 5;
    bool empty() const { return keys.empty(); }
    std::vector<int> parse(const std::string& dev) const;   // unknown keys ignored, missing = neutral
    std::string format(const std::vector<int>& v) const;    // "" when all neutral
};

struct MenuState {
    std::vector<TrackStats> tracks;
    int track = 0;
    int laps = 10;
    int tyreLife = 6;  // index into kTyreLives
    int cars = 20, maxCars = 20;
    int teams = 10, drivers = 2;   // the grid: teams x drivers per team (cars is the product)
    int row = 0;       // selected row; the last is Start
    float tankLitres = 58;

    // Grid page: one livery slot, algorithm and starting tyre per car.
    bool gridPage = false;
    int gridRow = 0, gridCol = 0;  // car, and 0 = livery / 1 = algorithm / 2 = tyres
    static constexpr int kGridCols = 3;
    std::vector<int> carLivery, carAlgo;
    std::vector<int> carTires;  // RR_TIRE_*, 0 = the algorithm chooses
    std::vector<Algorithm> algos;
    int liveryCount = 1;

    // Teams: the livery slots of each team (a team's first driver, then its
    // second), and the team's stats. Teammates share them; picking a style for a
    // driver gives the team that style's stats.
    std::vector<std::vector<int>> teamSlots;
    std::vector<int> slotTeam;  // livery slot -> team
    StatRules statRules;
    std::vector<std::vector<int>> teamStats;
    bool teamsPage = false;
    int teamRow = 0, statCol = 0;
    int teamOfCar(int car) const {
        const int slot = car < (int)carLivery.size() ? carLivery[car] : car;
        return slot >= 0 && slot < (int)slotTeam.size() ? slotTeam[slot] : -1;
    }
    int statSum(int team) const {
        int n = 0;
        for (int v : teamStats[team]) n += v;
        return n;
    }
    std::vector<int> raceTeams() const;  // teams with a car in the race, in grid order
    // Lays the grid out for teams x drivers: each team's first drivers, then the second ones.
    void layoutGrid();
    // A driver's style changed: the team takes that style's stats.
    void styleChanged(int car);

    static constexpr int kTyreLives[] = {3, 5, 8, 10, 12, 15, 20, 25, 30, 40, 50, 75, 100, 0};  // 0 = no wear
    static constexpr int kNumTyreLives = sizeof(kTyreLives) / sizeof(kTyreLives[0]);
    bool weekend = false;  // qualifying (each car alone) sets the grid, then the race
    int tyreRule = 0;      // two-compound rule: 0 automatic (races over 20 laps), 1 on, 2 off

    static constexpr int kRows = 10;  // track, laps, tyre life, teams, drivers, session, tyre rule, grid, team stats, start
    static constexpr int kTeamsRow = 3, kDriversRow = 4, kSessionRow = 5, kRuleRow = 6, kGridRow = 7, kStatsRow = 8,
                         kStartRow = 9;

    const TrackStats& stats() const { return tracks[track]; }
    // Laps the medium tyre lasts at wear rate 1 on this track (to the 0.7 wear cliff).
    float baseTyreLife() const { return 0.7f / std::max(1e-4f, stats().wearPerLap); }
    float wearRate() const {
        int life = kTyreLives[tyreLife];
        return life == 0 ? 0.0f : baseTyreLife() / life;
    }
    float lapsPerTank() const { return tankLitres / std::max(0.1f, stats().fuelPerLap); }
    // Picks the tyre-life option nearest the given wear rate.
    void setWearRate(float rate);
    bool twoCompoundRule() const { return tyreRule == 1 || (tyreRule == 0 && laps > 20); }
    int twoCompoundsArg() const { return tyreRule == 0 ? -1 : tyreRule == 1 ? 1 : 0; }  // RaceConfig::twoCompounds
};

enum class MenuAction { None, Start, Quit, TrackChanged };

// Keyboard and mouse input for the menu. `hits` holds the clickable rectangles the
// last draw produced (row arrows and the start button).
// row: setup row, or 100 + car * kGridCols + column on the grid page, or
// 1000 + team * 16 + stat on the team stats page; 99 is a page's Done button.; dir -1 / +1 = arrow, 0 = select.
struct MenuHit { float x, y, w, h; int row; int dir; };
MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits);
