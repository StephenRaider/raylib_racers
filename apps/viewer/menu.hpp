#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "test_figures.hpp"

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
    // Session: 0 race only, 1 weekend (qualifying, each car alone, sets the
    // grid), 2 testing (one car alone, recorded for analysis).
    int session = 0;
    bool weekend() const { return session == 1; }
    bool testing() const { return session == 2; }
    int tyreRule = 0;      // two-compound rule: 0 automatic (races over 20 laps), 1 on, 2 off

    // The rows of the setup page depend on the session.
    enum class Row { Track, Laps, TyreLife, Teams, Drivers, Session, TyreRule, Grid, Stats, Start,
                     TestCar, TestLivery, TestTyres, TestFuel, TestStats, TestRuns };
    std::vector<Row> rows() const;
    int rowOf(Row r) const;  // index in rows(), -1 if not shown

    // ---- testing: one car, its stats, tyres and fuel (0 = chosen for the run length)
    int testAlgo = 0, testLivery = 0, testTires = 0;
    float testFuel = 0;
    std::vector<int> testStats;
    bool testStatsPage = false;
    int testStatRow = 0;
    // Filled in by the viewer: the automatic choices and what the stats do.
    int autoTires = 2;           // RR_TIRE_MEDIUM
    float autoFuel = 0;          // litres
    float fuelPerLapEst = 2.3f;  // with this car's stats
    float compoundLife[4] = {0, 0, 0, 0};  // laps per compound (index RR_TIRE_*), 0 = no wear
    std::vector<FigureLine> figures;
    std::vector<std::string> statAbout;
    int testTiresUsed() const { return testTires ? testTires : autoTires; }
    float testFuelUsed() const { return testFuel > 0 ? testFuel : autoFuel; }
    // Saved runs page.
    struct RunLine {
        int id = 0;
        std::string date, track, algo, stats, end;
        int compound = 0, laps = 0, lapsDone = 0;
        float fuel = 0, best = 0, average = 0, fuelPerLap = 0, wearPerLap = 0;
        bool telemetry = false, completed = false;
    };
    bool runsPage = false;
    int runsSort = 0;            // 0 newest first, 1 best lap first
    bool runsAllTracks = false;  // false: this track only
    int runsRow = 0, runsTop = 0;
    int runsTotal = 0;           // runs saved, all tracks
    std::vector<RunLine> runLines;  // filled by the viewer: filtered and sorted
    int runPick = 0;             // id of the run a LoadRun / ViewRun action is for

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

enum class MenuAction { None, Start, Quit, TrackChanged, LoadRun, ViewRun };

// Keyboard and mouse input for the menu. `hits` holds the clickable rectangles the
// last draw produced (row arrows and the start button).
// row: setup row, or 100 + car * kGridCols + column on the grid page, or
// 1000 + team * 16 + stat on the team stats page, 2000 + stat on the testing
// stats page, 3000 + line on the saved runs page; 99 is a page's Done button,
// 98 / 97 load / replay a saved run, 96 / 95 its sort / track filter.
// dir -1 / +1 = arrow, 0 = select.
struct MenuHit { float x, y, w, h; int row; int dir; };
MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits);
