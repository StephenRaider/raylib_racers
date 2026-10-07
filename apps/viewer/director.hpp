#pragma once
// The TV director (camera F9): watches the race and cuts to whatever is worth
// seeing: incidents, imminent overtakes, battles and pit stops, with the leader
// when nothing is happening. It picks a car and a shot; the renderer films it.
#include <string>
#include <vector>

#include "race.hpp"
#include "renderer.hpp"

class Director {
public:
    void reset();
    // Call every frame with the frame time (real seconds).
    void update(const rr::Race& race, float dt);
    int focus() const { return focus_; }
    CamMode shot() const { return shot_; }
    const std::string& caption() const { return caption_; }

private:
    struct Story {
        int kind = 0;       // see the enum in director.cpp
        int car = -1;       // the car to film
        int other = -1;     // the car it is fighting with, if any
        float score = 0;
        std::string caption;
    };
    std::vector<Story> stories(const rr::Race& race);
    void cut(const Story& s);

    int focus_ = -1;
    CamMode shot_ = CAM_CINEMATIC;
    std::string caption_;
    Story current_;
    float held_ = 0;        // s on the current story
    float shotHeld_ = 0;    // s on the current shot
    float scan_ = 0;        // s until the next look round
    int shots_ = 0;         // shots taken, to vary the angles
    // what the cars were doing at the last look
    std::vector<int> collisions_, pit_;
    std::vector<bool> dnf_;
    std::vector<float> interval_;      // s to the car ahead
    std::vector<float> incidentUntil_; // race time an incident stays news
    std::vector<std::string> incident_;
    double lastTime_ = -1;
};
