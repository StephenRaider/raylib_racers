// Weekend and temperature helpers for C++ robots (ABI 8), header only.
//
//   RRWeekend  notes that practice and qualifying pass on to the race, kept in
//              the weekend memory (RRRobotConfig.memory): the grip learnt per
//              track bin plus the strategy model's pace, fuel use, tyre wear
//              and pit loss.
//   RRBrakeCare  disc grip from brake_temp (cold discs bite less, hot ones
//              fade), smoothed, with a flag for when to re-plan braking and
//              one for when to lift and coast to cool the discs.
//   rr_hottest_tyre()  the hottest single tyre, for overheat checks.
#ifndef RR_WEEKEND_H
#define RR_WEEKEND_H
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "rr/robot_api.h"

// ---------------------------------------------------------------- weekend notes

struct RRNotesModel {
    float lapRef = 0, fuelPerLap = 0, wearPerLapMed = 0, pitLoss = 0, spanNormal = 0;
};

struct RRWeekend {
    int session = RR_SESSION_RACE;
    unsigned char* memory = nullptr;
    int memorySize = 0;
    bool haveNotes = false;
    RRNotesModel notes;

    bool practice() const { return session == RR_SESSION_PRACTICE; }
    // Practice is for finding the limit: bigger learning steps, a little past the race push.
    float learnScale() const { return practice() ? 2.0f : 1.0f; }
    float pushExtra() const { return practice() ? 0.05f : 0.0f; }
};

namespace rr_weekend_detail {
constexpr unsigned kMagic = 0x52574b31;  // "RWK1"
struct Header {
    unsigned magic;
    int bins;
    float trackLen;
    int sessions;
    RRNotesModel model;
};
inline size_t bytes(int bins) { return sizeof(Header) + sizeof(float) * (size_t)std::max(0, bins); }
}  // namespace rr_weekend_detail

// Call in create(): remembers the session and memory, and loads the notes into
// adj (bins entries, clamped to [lo, hi]) when they are for this track.
inline void rr_weekend_begin(RRWeekend* w, const RRRobotConfig* cfg, float trackLen, float* adj, int bins, float lo,
                             float hi) {
    using namespace rr_weekend_detail;
    w->session = cfg->session;
    w->memory = cfg->memory;
    w->memorySize = cfg->memory_size;
    w->haveNotes = false;
    if (!w->memory || (size_t)w->memorySize < bytes(bins)) return;
    Header h;
    std::memcpy(&h, w->memory, sizeof h);
    if (h.magic != kMagic || h.bins != bins || std::fabs(h.trackLen - trackLen) > 1.0f) return;
    for (int b = 0; b < bins; ++b) {
        float a;
        std::memcpy(&a, w->memory + sizeof h + sizeof(float) * (size_t)b, sizeof a);
        if (std::isfinite(a)) adj[b] = std::clamp(a, lo, hi);
    }
    w->notes = h.model;
    w->haveNotes = true;
}

// Call in session_end(): writes the notes for the next session.
inline void rr_weekend_save(const RRWeekend* w, float trackLen, const float* adj, int bins, const RRNotesModel& m) {
    using namespace rr_weekend_detail;
    if (!w->memory || (size_t)w->memorySize < bytes(bins)) return;
    Header h;
    std::memcpy(&h, w->memory, sizeof h);
    const int sessions = h.magic == kMagic ? h.sessions + 1 : 1;
    h = Header{kMagic, bins, trackLen, sessions, m};
    std::memcpy(w->memory, &h, sizeof h);
    std::memcpy(w->memory + sizeof h, adj, sizeof(float) * (size_t)bins);
}

// Call in initStrategy() after the priors are set: measured values replace them.
inline void rr_weekend_apply(const RRWeekend* w, float* lapRef, float* fuelPerLap, float* wearPerLapMed, float* pitLoss,
                             float* spanNormal, float* lapRefN, bool usePit) {
    if (!w->haveNotes) return;
    const RRNotesModel& n = w->notes;
    if (n.lapRef > 10) { *lapRef = n.lapRef; *lapRefN = 2; }
    if (n.fuelPerLap > 0.05f) *fuelPerLap = n.fuelPerLap;
    if (n.wearPerLapMed > 0.001f) *wearPerLapMed = n.wearPerLapMed;
    if (usePit && n.pitLoss > 3) *pitLoss = n.pitLoss;
    if (usePit && n.spanNormal > 1) *spanNormal = n.spanNormal;
}

// ---------------------------------------------------------------- brakes and tyres

struct RRBrakeCare {
    float now = 1.0f;      // disc grip now, smoothed (1 inside the window)
    float planned = 1.0f;  // disc grip the speed profile assumes: multiply max_brake_force by it
    double replanAt = 0;
    bool hot = false;      // close to fading: lift and coast to cool them
};

// Call every drive(); returns true when the speed profile should be re-planned
// for the new disc grip (at most every 2 s).
inline bool rr_brake_care(RRBrakeCare* b, const RRSensors* in) {
    if (in->brake_temp_window[1] <= 0) return false;  // older host
    const float lo = in->brake_temp_window[0], hi = in->brake_temp_window[1];
    float cold = 1e9f, hot = -1e9f;
    for (int w = 0; w < 4; ++w) {
        cold = std::min(cold, in->brake_temp[w]);
        hot = std::max(hot, in->brake_temp[w]);
    }
    float f = 1.0f;
    if (cold < lo) f = 0.75f + 0.25f * std::clamp((cold - 50.0f) / (lo - 50.0f), 0.0f, 1.0f);
    if (hot > hi) f = std::min(f, std::max(0.6f, 1.0f - 0.002f * (hot - hi)));
    const float dt = in->dt > 0 ? in->dt : 0.02f;
    b->now += (f - b->now) * std::min(1.0f, dt);
    b->hot = hot > hi - (b->hot ? 60.0f : 30.0f);
    if (std::fabs(b->now - b->planned) > 0.03f && in->time >= b->replanAt) {
        b->planned = b->now;
        b->replanAt = in->time + 2.0;
        return true;
    }
    return false;
}

// The hottest single tyre (C): one cooking tyre goes off before its axle's mean shows it.
inline float rr_hottest_tyre(const RRSensors* in) {
    float t = std::max(in->tire_temp[0], in->tire_temp[1]);
    if (in->tire_temp_wheel[0] > 0)
        for (int w = 0; w < 4; ++w) t = std::max(t, in->tire_temp_wheel[w] - 3.0f);
    return t;
}

#endif
