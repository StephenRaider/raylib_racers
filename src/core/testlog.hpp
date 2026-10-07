#pragma once
// Testing sessions: one car alone on track, recorded for analysis.
//
//  - TestRecorder samples the car at kRate (its full physical state plus the
//    robot's commands), closes a TestLap summary at every line crossing and
//    flags moments worth a look (off track, contact, slides, spins...).
//  - TestStore keeps every run's setup and times in <dir>/runs.json, and each
//    run's summary.json and telemetry.csv in <dir>/run_NNNN/. Telemetry is kept
//    for the newest kKeepTelemetry runs of each algorithm only.
#include <string>
#include <vector>

#include "race.hpp"

namespace rr {

struct TestSample {
    float t = 0;        // race time, s
    float lapTime = 0;  // time since this lap started, s
    float lapDist = 0;  // m from the start line on this lap (negative on the grid)
    int lap = 1;        // 1-based
    float steer = 0, accel = 0, brake = 0;  // the robot's commands
    float lateral = 0;  // m from the centreline, + = left
    float angle = 0;    // heading minus track direction, rad
    bool onTrack = true;
    CarState s;         // pose, speeds, loads, tyres, fuel, damage
};

struct TestLap {
    int lap = 0;
    float time = 0;
    float sectors[3] = {0, 0, 0};  // thirds of the lap by distance
    float fuelUsed = 0, fuelEnd = 0;
    float wear[2] = {0, 0}, wearEnd[2] = {0, 0};  // added this lap, and at the line; front, rear
    float tempAvg[2] = {0, 0}, tempMax[2] = {0, 0};
    float topSpeed = 0, minSpeed = 0;  // km/h
    float fullThrottle = 0, braking = 0;  // share of the lap's time
    float maxLatG = 0;
    float damage = 0;  // added this lap
    int offTracks = 0, slides = 0;
    bool clean = true;  // no off-track excursion and no contact
    int firstSample = 0, endSample = 0;  // samples [first, end)
};

struct TestEvent {
    float t = 0;         // when it started
    int lap = 1;
    float lapDist = 0;
    float duration = 0;  // s
    float peak = 0;      // worst value seen (see detail)
    std::string kind;    // off_track, contact, oversteer, understeer, wheelspin, spin, stopped, out_of_fuel
    std::string detail;
};

class TestRecorder {
public:
    static constexpr float kRate = 25;  // samples per second

    void begin(const Race& race, int car);
    // Call after every Race::step(): samples, laps and events.
    void update(const Race& race);
    // Closes events still open (end of the run).
    void finish();

    int car = 0;
    float trackLength = 0;
    std::vector<TestSample> samples;
    std::vector<TestLap> laps;  // completed laps
    std::vector<TestEvent> events;

    bool empty() const { return samples.empty(); }
    double endTime() const { return samples.empty() ? 0.0 : samples.back().t; }
    // Index of the last sample at or before t.
    int indexAt(double t) const;
    // The car between the samples around t (pose interpolated).
    TestSample at(double t) const;
    int bestLap() const;  // index into laps, -1 if none
    float bestTime() const { int b = bestLap(); return b < 0 ? 0.0f : laps[b].time; }
    float averageLap() const;  // median of the laps after the first (the standing start)
    float fuelPerLap() const;  // average over completed laps
    void wearPerLap(float out[2]) const;
    // First sample of lap `lap` (1-based) and one past its last.
    void lapRange(int lap, int& first, int& end) const;
    // Rebuilds the lap summaries and events from the samples (after loading).
    void rebuild(float trackLength);

private:
    void sample(const Race& race);
    void push(const TestSample& x, const std::string& dnf);
    void closeLap(int endSample, float lapTime, const TestSample& now);
    void countEvents(TestLap& l) const;
    void addEvent(const TestEvent& e);  // in time order
    struct Flag {
        bool on = false;
        float t0 = 0, peak = 0, lapDist = 0;
        int lap = 1;
    };
    void flag(Flag& f, bool cond, float value, float minDuration, const char* kind, const std::string& detail,
              const TestSample& now);
    double next_ = 0;
    bool done_ = false;
    float lapStartT_ = 0, lapFuel_ = 0, lapWear_[2] = {0, 0}, lapDamage_ = 0;
    int lapStartSample_ = 0;
    float sectorStart_ = 0;
    int sectorDone_ = 0;
    float lapSectors_[3] = {0, 0, 0};
    float lastDamage_ = 0, contactAmount_ = 0, contactT_ = -1;
    Flag off_, over_, under_, spin_, wheelspin_, stopped_;
    bool outOfFuel_ = false;
};

// What a test run was set up with.
struct TestSetup {
    std::string track, trackTitle;
    std::string robot, label, params;  // the algorithm
    std::string dev;                   // stats, "key=n,..."
    int livery = 0;                    // livery slot (viewer)
    int laps = 0;
    int compound = 0;                  // RR_TIRE_*
    float fuel = 0;                    // litres at the start
    float wearRate = 1, fuelRate = 1, ambient = 25;
};

struct TestRun {
    int id = 0;
    std::string date;       // local time, "2026-10-07 14:03:12"
    TestSetup setup;
    int lapsDone = 0;
    bool completed = false;
    std::string end;        // finished, stopped, out of fuel, stuck, crashed
    float best = 0, average = 0, fuelPerLap = 0;
    float wearPerLap[2] = {0, 0};
    std::vector<float> lapTimes;
    std::string folder;     // run_NNNN, under the store
    bool telemetry = false; // telemetry.csv is still kept
};

class TestStore {
public:
    static constexpr int kKeepTelemetry = 10;  // per algorithm (label)

    explicit TestStore(std::string dir = "");
    const std::string& dir() const { return dir_; }
    bool load(std::string* err = nullptr);  // a missing runs.json is an empty store
    const std::vector<TestRun>& runs() const { return runs_; }
    const TestRun* find(int id) const;
    // Saves a run and returns its id (0 on failure).
    int save(const TestSetup& setup, const TestRecorder& rec, const std::string& end, bool completed,
             std::string* err = nullptr);
    bool loadTelemetry(const TestRun& run, TestRecorder& rec, std::string* err = nullptr) const;

private:
    bool writeIndex(std::string* err) const;
    void writeReadme() const;
    std::string dir_;
    std::vector<TestRun> runs_;
};

const char* compoundKey(int compound);  // "soft", "medium", "hard"

}  // namespace rr
