#include "director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

enum { LEADER, BATTLE, PIT, OVERTAKE, INCIDENT };

constexpr float kMinHold = 5.0f;   // s on a story before cutting away (an incident can cut in)
constexpr float kMaxHold = 14.0f;  // s before looking for something else
constexpr float kShotLen = 6.0f;   // s per shot of a long story

}  // namespace

void Director::reset() {
    focus_ = -1;
    held_ = shotHeld_ = scan_ = 0;
    current_ = Story{};
    collisions_.clear();
    lastTime_ = -1;
}

std::vector<Director::Story> Director::stories(const rr::Race& race) {
    const auto& cars = race.cars();
    const auto& order = race.order();
    const size_t n = cars.size();
    const double now = race.time();
    if (collisions_.size() != n || now < lastTime_) {
        collisions_.assign(n, 0);
        pit_.assign(n, RR_PIT_NONE);
        dnf_.assign(n, false);
        interval_.assign(n, 99.0f);
        incidentUntil_.assign(n, -1.0f);
        incident_.assign(n, "");
        for (size_t i = 0; i < n; ++i) {
            collisions_[i] = cars[i].collisions;
            pit_[i] = cars[i].pitState;
            dnf_[i] = cars[i].dnf;
        }
    }
    lastTime_ = now;
    std::vector<Story> out;
    char buf[160];
    auto pos = [&](int car) { return cars[car].position; };

    // incidents: contact, a spin, off the road, retirement
    for (size_t i = 0; i < n; ++i) {
        const rr::Car& c = cars[i];
        if (c.finished) continue;
        std::string what;
        if (c.dnf && !dnf_[i]) what = "OUT: " + c.name + (c.dnfReason.empty() ? "" : ", " + c.dnfReason);
        else if (!c.dnf) {
            const float v = std::hypot(c.state.vx, c.state.vy);
            const float slip = std::fabs(std::atan2(c.state.vy, std::max(0.1f, std::fabs(c.state.vx))));
            if (c.collisions > collisions_[i]) what = "CONTACT: " + c.name;
            else if (v > 8 && slip > 0.7f) what = "SPIN: " + c.name;
            else if (!c.onTrack && v > 15 && c.pitState == RR_PIT_NONE) what = "OFF: " + c.name;
        }
        collisions_[i] = c.collisions;
        dnf_[i] = c.dnf;
        if (!what.empty() && incidentUntil_[i] < now) {
            incidentUntil_[i] = (float)now + 6.0f;
            incident_[i] = what;
        }
        if (incidentUntil_[i] >= now) out.push_back({INCIDENT, (int)i, -1, 100.0f - 0.5f * pos((int)i), incident_[i]});
    }
    // pit stops: from the lane entry until the car is back out
    for (size_t i = 0; i < n; ++i) {
        const rr::Car& c = cars[i];
        if (c.pitState != RR_PIT_NONE && !c.finished && !c.dnf && !race.isOver()) {
            std::snprintf(buf, sizeof buf, "PIT STOP: %s (P%d)", c.name.c_str(), c.position);
            out.push_back({PIT, (int)i, -1, 55.0f - 1.0f * pos((int)i), buf});
        }
        pit_[i] = c.pitState;
    }
    // fights for position: the interval to the car ahead, and whether it is closing
    for (size_t p = 1; p < order.size(); ++p) {
        const int me = order[p], ahead = order[p - 1];
        const rr::Car& c = cars[me];
        const rr::Car& a = cars[ahead];
        if (c.finished || c.dnf || a.dnf || c.gap < 0 || a.gap < 0 || c.pitState != RR_PIT_NONE ||
            a.pitState != RR_PIT_NONE || c.lapsBehind != a.lapsBehind)
            continue;
        const float gapNow = (float)(c.gap - a.gap);
        const float before = interval_[me];
        interval_[me] = gapNow;
        if (gapNow < 0.35f && gapNow <= before + 0.01f) {
            std::snprintf(buf, sizeof buf, "OVERTAKE? %s ON %s FOR P%d", c.name.c_str(), a.name.c_str(), (int)p);
            out.push_back({OVERTAKE, me, ahead, 75.0f - 1.0f * (float)p, buf});
        } else if (gapNow < 1.0f) {
            std::snprintf(buf, sizeof buf, "BATTLE FOR P%d: %s AND %s", (int)p, a.name.c_str(), c.name.c_str());
            out.push_back({BATTLE, me, ahead, 40.0f - 0.8f * (float)p + 10.0f * (1.0f - gapNow), buf});
        }
    }
    if (!order.empty()) {
        const rr::Car& l = cars[order[0]];
        std::snprintf(buf, sizeof buf, race.isOver() ? "WINNER: %s" : "LEADER: %s", l.name.c_str());
        out.push_back({LEADER, order[0], -1, 10.0f, buf});
    }
    return out;
}

void Director::cut(const Story& s) {
    const bool sameStory = s.kind == current_.kind && s.car == current_.car;
    if (!sameStory) held_ = 0;
    current_ = s;
    focus_ = s.car;
    caption_ = s.caption;
    shotHeld_ = 0;
    ++shots_;
    // the angle that suits the story, varied from shot to shot
    static const CamMode battle[] = {CAM_CINEMATIC, CAM_TV, CAM_CHASE, CAM_HELI};
    static const CamMode calm[] = {CAM_CINEMATIC, CAM_HELI, CAM_TV, CAM_CINEMATIC, CAM_CHASE};
    switch (s.kind) {
        case INCIDENT: shot_ = shots_ % 2 ? CAM_TV : CAM_CINEMATIC; break;
        case OVERTAKE: shot_ = shots_ % 2 ? CAM_CHASE : CAM_CINEMATIC; break;
        case PIT: shot_ = shots_ % 2 ? CAM_HELI : CAM_CINEMATIC; break;
        case BATTLE: shot_ = battle[shots_ % 4]; break;
        default: shot_ = calm[shots_ % 5]; break;
    }
}

void Director::update(const rr::Race& race, float dt) {
    if (race.cars().empty()) return;
    held_ += dt;
    shotHeld_ += dt;
    scan_ -= dt;
    if (focus_ >= (int)race.cars().size()) focus_ = -1;
    if (scan_ > 0 && focus_ >= 0) return;
    scan_ = 0.25f;
    std::vector<Story> all = stories(race);
    const Story* best = nullptr;
    const Story* same = nullptr;
    for (const Story& s : all) {
        if (!best || s.score > best->score) best = &s;
        if (s.car == current_.car && s.kind == current_.kind) same = &s;
    }
    if (!best) return;
    if (focus_ < 0) {
        cut(*best);
        return;
    }
    // The story we are on is over (the cars split up, the stop is done): move on after a short hold.
    if (!same) {
        if (held_ > 2.0f || best->kind == INCIDENT) cut(*best);
        return;
    }
    // Something much bigger: cut in now. Otherwise hold, then look for the next story.
    if (best->score > same->score + 30.0f || (held_ > kMinHold && best->score > same->score + 10.0f)) {
        cut(*best);
        return;
    }
    if (held_ > kMaxHold) {
        // the best story that is not this one
        const Story* next = nullptr;
        for (const Story& s : all)
            if (!(s.car == current_.car && s.kind == current_.kind) && (!next || s.score > next->score)) next = &s;
        cut(next ? *next : *same);
        return;
    }
    current_ = *same;
    caption_ = same->caption;
    if (shotHeld_ > kShotLen) cut(*same);  // a new angle on the same story
}
