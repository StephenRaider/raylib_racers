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

struct MenuState {
    std::vector<TrackStats> tracks;
    int track = 0;
    int laps = 10;
    int tyreLife = 6;  // index into kTyreLives
    int cars = 7, maxCars = 7;
    int row = 0;       // selected row; the last is Start
    float tankLitres = 58;

    static constexpr int kTyreLives[] = {3, 5, 8, 10, 12, 15, 20, 25, 30, 40, 50, 75, 100, 0};  // 0 = no wear
    static constexpr int kNumTyreLives = sizeof(kTyreLives) / sizeof(kTyreLives[0]);
    static constexpr int kRows = 5;  // track, laps, tyre life, cars, start

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
};

enum class MenuAction { None, Start, Quit, TrackChanged };

// Keyboard and mouse input for the menu. `hits` holds the clickable rectangles the
// last draw produced (row arrows and the start button).
struct MenuHit { float x, y, w, h; int row; int dir; };  // dir -1 / +1 = arrow, 0 = select
MenuAction updateMenu(MenuState& m, const std::vector<MenuHit>& hits);
