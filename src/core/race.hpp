#pragma once
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "car.hpp"
#include "config.hpp"
#include "robot_loader.hpp"
#include "track.hpp"

namespace rr {

struct Car {
    // identity
    std::string name;
    std::string robotName;
    std::string params;
    std::shared_ptr<RobotModule> module;
    void* robot = nullptr;
    RRRobotConfig robotCfg{};

    // physics
    CarParams phys{};
    CarState state{};
    RRControl control{};
    RRSensors sensors{};

    // track position
    int trackIdx = 0;
    float trackS = 0;
    float lateral = 0;
    float halfWidth = 7;
    bool onTrack = true;

    // race progress
    double distRaced = 0;
    int lapsDone = 0;
    double lapStart = 0;
    std::vector<float> lapTimes;
    float bestLap = 0;
    bool finished = false;
    double finishTime = 0;
    bool dnf = false;
    std::string dnfReason;
    int position = 0;
    double gap = 0;        // seconds behind the leader at the same point of the track, -1 if not timed yet
    int lapsBehind = 0;
    int collisions = 0;
    float stuckTime = 0;
    std::vector<double> checkpoints;  // race time at every 10 m of distance

    // consumables and pit
    int lapsOnTires = 0;
    int pitState = RR_PIT_NONE;
    int pitStops = 0;
    float pitBoxS = 0;
    float serviceLeft = 0;
    RRControl pitOrder{};       // the request as it was when service started
    double pitLaneTime = 0;     // total time spent in the pit lane
    std::vector<int> pitLaps;   // lap on which each stop happened
    float noFuelTime = 0;

    // blue flags and penalties
    int blueCar = -1;           // car lapping us, close behind (-1 = no blue flag)
    float blueDs = 0;
    float blueHeld = 0;         // s spent holding that car up
    int blueFlags = 0;          // times a blue flag was shown
    int penalties = 0;
    float penaltyTime = 0;      // s added to the race time
    double raceTime() const { return finishTime + penaltyTime; }

    FILE* telemetry = nullptr;

    int currentLap(int raceLaps) const { return std::min(raceLaps, std::max(1, lapsDone + 1)); }
};

class Race {
public:
    Race() = default;
    ~Race();
    Race(const Race&) = delete;
    Race& operator=(const Race&) = delete;

    // botDirs/trackDirs are searched for robot libraries and track files.
    bool setup(const RaceConfig& cfg, const std::vector<std::string>& botDirs,
               const std::vector<std::string>& trackDirs, std::string* err);

    void step();                // one physics step of cfg.dt
    void advance(double seconds);  // as many steps as fit
    bool isOver() const { return over_; }

    double time() const { return time_; }
    float dt() const { return cfg_.dt; }
    int laps() const { return cfg_.laps; }
    const Track& track() const { return track_; }
    const std::vector<Car>& cars() const { return cars_; }
    const std::vector<int>& order() const { return order_; }  // car indices by position
    const RaceConfig& config() const { return cfg_; }

    void printResults(FILE* out) const;
    bool writeJson(const std::string& path, double wallSeconds) const;

private:
    void placeOnGrid();
    void callRobots();
    void computeSensors(Car& c);
    void resolveWalls(Car& c);
    void resolveCarPair(Car& a, Car& b);
    void updateProgress(Car& c);
    void updateOrder();
    void writeTelemetry(const Car& c);
    void updatePit(Car& c);
    void updateBlueFlags();
    float slipstream(const Car& c) const;
    void finishService(Car& c);
    float wrapDs(float ds) const;

    RaceConfig cfg_;
    Track track_;
    std::vector<Car> cars_;
    std::vector<int> order_;
    double time_ = 0;
    long long steps_ = 0;
    int robotPeriod_ = 10;
    bool over_ = false;
    double leaderFinish_ = -1;
    double maxTime_ = 0;
    std::mt19937_64 rng_;
};

}  // namespace rr
