// racingline: a planning robot. At create() it computes a minimum-curvature
// racing line inside the track limits and a grip-limited speed profile, then
// drives it with pure pursuit and a speed controller.
//
// On top of that it races:
//   - the speed profile is re-planned every lap for the current fuel load and
//     tyre grip;
//   - pit strategy: it measures fuel use and tyre wear per metre, stops when the
//     fuel will not last or the tyres would pass the wear limit, and picks the
//     fuel load and the softest compound that reaches the flag;
//   - overtaking: picks the side with more room around the car ahead and does
//     not run into the back of it;
//   - defending: when a car close behind is closing, covers the inside of the
//     next corner, one move, then holds it;
//   - awareness: never moves across a car alongside or one closing from
//     behind, and brakes in time for the car ahead;
//   - blue flags: moves aside and lifts for a car that is lapping it;
//   - the limit: learns, per 20 m of track, how much grip there really is.
//     Sliding (front or rear past its grip) lowers that stretch's speed and the
//     braking zone before it; clean laps well inside the limit raise it. On top
//     of that a driver-aid layer (rr_awareness.h) catches oversteer and manages
//     wheelspin.
//
// params:
//   grip=<0.8>     share of the tyre friction the speed profile may use
//   brake=<0.7>    share of the braking capacity the profile assumes
//   margin=<1.3>   distance kept from the tarmac edge, m
//   look=<1.0>     pure-pursuit lookahead scale
//   pass=<1>       0 disables overtaking moves
//   defend=<1>     0 disables defending
//   wear=<0.7>     tyre wear at which to stop for new tyres
//   fuel=<full>    litres at the start
//   tires=<2>      starting compound: 1 soft, 2 medium, 3 hard
//   pit=<1>        0 never stops
//   push=<1.2>     how far above `grip` the learnt limit may go (1 = never)
//   learn=<1>      0 disables learning the limit
//   attack=<1>     racecraft aggression: > 1 follows closer and looks for gaps sooner
//   heat=<5>       how far (C) past the top of the tyres' window it keeps pushing before
//                  backing off to cool them
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../common/rr_awareness.h"
#include "../common/rr_params.h"
#include "../common/rr_recovery.h"
#include "rr/robot_api.h"

namespace {

struct P2 { float x, y; };

enum Mode { RACE, PIT_IN, PIT_OUT };

float compoundWear(int c) { return c == RR_TIRE_SOFT ? 2.0f : (c == RR_TIRE_HARD ? 0.55f : 1.0f); }
float smooth01(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3 - 2 * u); }

struct RacingLine {
    // settings
    float grip, brakeScale, margin, look, wearLimit;
    bool pass, defend, usePit;
    RRCarSpec car;
    RRPitInfo pit;
    float ds, L;
    // track copy and plan (indexed like the track samples)
    std::vector<RRTrackPoint> tp;
    std::vector<float> offset;   // lateral offset of the line, + = left
    std::vector<P2> line;
    std::vector<float> kappa;    // curvature of the line, 1/m
    std::vector<float> kappaSigned;  // the same, + = turning left
    float yawGain = 0.06f;
    std::vector<float> speed;    // target speed
    float plannedMass = 0, plannedGrip = 0;
    float damage = 0;            // 0..1, as the speed profile last assumed
    float attack = 1.0f;         // racecraft: > 1 follows closer and goes for gaps sooner
    float heat = 5.0f;           // C past the tyre window tolerated before backing off
    float tyreNow = 1.0f;        // speed factor for the tyres' temperature right now (smoothed)
    float startLat = 0, startDist = 0;  // grid slot: hold that lane off the line, then ease onto the racing line
    float sideLo = -1e9f, sideHi = 1e9f;  // lateral room left by cars alongside (absolute, m)
    int plannedLap = -1;

    // learning the limit: grip multiplier per 20 m bin
    static constexpr float kBin = 20.0f;
    std::vector<float> adj;
    float push = 1.2f;
    bool learn = true, adjDirty = false;
    int bin = -1;
    float binUse = 0;          // worst grip use seen in the current bin
    bool binClean = true;      // no traffic or racecraft moves in this bin
    bool binLost = false;      // spun, ran off or needed recovery
    float lastAccel = 0;       // throttle we asked for last time
    int binOf(int i) const { return std::min((int)adj.size() - 1, (int)(tp[wrap(i)].s / kBin)); }

    // racecraft
    float passOffset = 0;        // current shift from the line, m
    int passCar = -1;            // car we are passing
    float passSide = 0;          // side we pass on
    float defendSide = 0;        // side we are covering, 0 = not defending
    double defendUntil = 0, defendCooldown = 0;
    float tc = 1;                // traction-control throttle limit
    RRRecovery recovery{};

    // strategy
    Mode mode = RACE;
    bool decidedThisLap = false;
    float fuelRef = 0, fuelDistRef = 0;   // since the last refuel
    float wearRef = 0, wearDistRef = 0;   // since the last tyre change
    int stopsSeen = 0;
    RRControl order{};                    // what we ask the crew for
    char plan[48] = "";

    int n() const { return (int)tp.size(); }
    int wrap(int i) const { int m = n(); i %= m; return i < 0 ? i + m : i; }
    P2 pointAt(int i, float off) const {
        const auto& p = tp[wrap(i)];
        return {p.x - p.dir_y * off, p.y + p.dir_x * off};
    }
    float fwd(float from, float to) const { float d = std::fmod(to - from, L); return d < 0 ? d + L : d; }
    float signedDs(float from, float to) const { float d = fwd(from, to); return d > L * 0.5f ? d - L : d; }
    bool inSpan(float s, float a, float b) const { return fwd(a, s) <= fwd(a, b); }
};

// Minimise the summed squared curvature of the line (squared second
// differences of the points) by projected gradient descent on the lateral
// offsets. Coarse-to-fine so long corners converge quickly.
void planLine(RacingLine& r) {
    const int n = r.n();
    std::vector<float> full(n, 0.0f);
    for (float spacing : {24.0f, 12.0f, 6.0f, 3.0f}) {
        const int stride = std::max(1, (int)std::lround(spacing / r.ds));
        const int m = n / stride;
        if (m < 8) continue;
        std::vector<float> e(m), lim(m);
        std::vector<P2> c(m), nr(m), pos(m), d2(m);
        for (int k = 0; k < m; ++k) {
            const auto& p = r.tp[k * stride];
            c[k] = {p.x, p.y};
            nr[k] = {-p.dir_y, p.dir_x};
            lim[k] = std::max(0.0f, p.half_width - r.margin);
            e[k] = full[k * stride];
        }
        for (int it = 0; it < 3000; ++it) {
            for (int k = 0; k < m; ++k) pos[k] = {c[k].x + nr[k].x * e[k], c[k].y + nr[k].y * e[k]};
            for (int k = 0; k < m; ++k) {
                const P2& a = pos[(k - 1 + m) % m];
                const P2& b = pos[k];
                const P2& d = pos[(k + 1) % m];
                d2[k] = {a.x - 2 * b.x + d.x, a.y - 2 * b.y + d.y};
            }
            for (int k = 0; k < m; ++k) {
                const P2& a = d2[(k - 1 + m) % m];
                const P2& b = d2[k];
                const P2& d = d2[(k + 1) % m];
                float gx = a.x - 2 * b.x + d.x, gy = a.y - 2 * b.y + d.y;
                e[k] = std::clamp(e[k] - 0.05f * (gx * nr[k].x + gy * nr[k].y), -lim[k], lim[k]);
            }
        }
        // Back to full resolution (the last coarse interval may be longer than stride).
        for (int i = 0; i < n; ++i) {
            int k0 = std::min(i / stride, m - 1), k1 = (k0 + 1) % m;
            int i0 = k0 * stride, i1 = k1 == 0 ? n : k1 * stride;
            float f = (float)(i - i0) / (float)(i1 - i0);
            full[i] = e[k0] * (1 - f) + e[k1] * f;
        }
    }
    r.offset.resize(n);
    for (int i = 0; i < n; ++i) {
        float lim = std::max(0.0f, r.tp[i].half_width - r.margin);
        r.offset[i] = std::clamp(full[i], -lim, lim);
    }
    r.line.resize(n);
    for (int i = 0; i < n; ++i) r.line[i] = r.pointAt(i, r.offset[i]);

    r.kappa.resize(n);
    r.kappaSigned.resize(n);
    const int h = 4;  // circle through points i-h, i, i+h
    for (int i = 0; i < n; ++i) {
        P2 a = r.line[r.wrap(i - h)], b = r.line[i], c = r.line[r.wrap(i + h)];
        float abx = b.x - a.x, aby = b.y - a.y, bcx = c.x - b.x, bcy = c.y - b.y, acx = c.x - a.x, acy = c.y - a.y;
        float cr = abx * bcy - aby * bcx;
        float la = std::sqrt(abx * abx + aby * aby), lb = std::sqrt(bcx * bcx + bcy * bcy),
              lc = std::sqrt(acx * acx + acy * acy);
        r.kappaSigned[i] = (la * lb * lc > 1e-6f) ? 2 * cr / (la * lb * lc) : 0.0f;
        r.kappa[i] = std::fabs(r.kappaSigned[i]);
    }
}

// Grip-limited speed for a given mass and tyre grip: lateral limit from
// curvature, then a backward pass for braking.
void planSpeed(RacingLine& r, float mass, float tyreGrip) {
    const int n = r.n();
    const float g = 9.81f, m = mass;
    // Damage costs downforce and mechanical grip (see RRSensors.damage).
    const float mu0 = r.car.tire_mu * r.grip * tyreGrip * (1 - 0.08f * r.damage);
    const float D = r.car.downforce_coeff * (1 - 0.35f * r.damage), drag = r.car.drag_coeff * (1 + 0.1f * r.damage);
    const float vCap = 95.0f;
    const auto& kappa = r.kappa;
    r.speed.assign(n, vCap);
    for (int i = 0; i < n; ++i) {
        const float mu = mu0 * r.adj[r.binOf(i)];
        // m v^2 k = mu (m g + D v^2)  ->  v^2 = mu g / (k - mu D / m)
        float den = kappa[i] - mu * D / m;
        if (den > 1e-6f) r.speed[i] = std::min(vCap, std::sqrt(mu * g / den));
    }
    // Backward pass: v_i^2 <= v_{i+1}^2 + 2 a ds, a from the friction left over after cornering.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = n - 1; i >= 0; --i) {
            int j = r.wrap(i + 1);
            const float mu = mu0 * r.adj[r.binOf(j)];
            float v = r.speed[j];
            float normal = m * g + D * v * v;
            float lat = m * v * v * kappa[j];
            float fLong = std::sqrt(std::max(0.0f, mu * mu * normal * normal - lat * lat));
            fLong = std::min(fLong, r.car.max_brake_force);
            float a = (fLong * r.brakeScale + drag * v * v) / m;
            float vMax = std::sqrt(v * v + 2 * a * r.ds);
            r.speed[i] = std::min(r.speed[i], vMax);
        }
    }
    r.plannedMass = mass;
    r.plannedGrip = tyreGrip;
}

void* create(const RRTrackInfo* track, const RRCarSpec* car, int, const char* params, RRRobotConfig* cfg) {
    auto* r = new RacingLine();
    r->grip = rr_param(params, "grip", 0.8f);
    r->brakeScale = rr_param(params, "brake", 0.7f);
    r->margin = rr_param(params, "margin", 1.3f);
    r->look = rr_param(params, "look", 1.0f);
    r->yawGain = rr_param(params, "yawgain", 0.06f);
    r->pass = rr_param(params, "pass", 1.0f) != 0.0f;
    r->defend = rr_param(params, "defend", 1.0f) != 0.0f;
    r->wearLimit = rr_param(params, "wear", 0.7f);
    r->usePit = rr_param(params, "pit", 1.0f) != 0.0f && track->pit.has_pit;
    r->push = std::max(1.0f, rr_param(params, "push", 1.2f));
    r->learn = rr_param(params, "learn", 1.0f) != 0.0f;
    r->attack = std::clamp(rr_param(params, "attack", 1.0f), 0.5f, 2.0f);
    r->heat = rr_param(params, "heat", 5.0f);
    cfg->initial_fuel = rr_param(params, "fuel", car->fuel_capacity);
    cfg->tire_compound = (int)rr_param(params, "tires", (float)RR_TIRE_MEDIUM);
    r->car = *car;
    r->pit = track->pit;
    r->tp.assign(track->points, track->points + track->num_points);
    r->L = track->length;
    r->ds = track->length / track->num_points;
    r->adj.assign((size_t)std::ceil(r->L / RacingLine::kBin), 1.0f);
    planLine(*r);
    planSpeed(*r, car->mass + cfg->initial_fuel * car->fuel_density, 1.0f);
    return r;
}

// ---------------------------------------------------------------- strategy

// Decide, once per lap shortly before the pit entry, whether to stop and what for.
void strategy(RacingLine& r, const RRSensors* in) {
    if (!r.usePit || r.mode != RACE) return;
    const float s = in->dist_from_start;
    const float toEntry = r.fwd(s, r.pit.entry_s);
    if (toEntry > 400.0f || toEntry < 30.0f) {
        if (toEntry > 400.0f) r.decidedThisLap = false;
        return;
    }
    if (r.decidedThisLap) return;
    r.decidedThisLap = true;

    // Rates measured since the last refuel / tyre change; until there is most
    // of a lap's worth of data there is nothing to decide on.
    const float fuelDist = in->dist_raced - r.fuelDistRef, wearDist = in->dist_raced - r.wearDistRef;
    if (fuelDist < r.L * 0.8f || wearDist < r.L * 0.8f) return;
    const float fuelPerM = std::max(0.0f, r.fuelRef - in->fuel) / fuelDist;
    const float wearNow = std::max(in->tire_wear[0], in->tire_wear[1]);
    const float wearPerM = std::max(0.0f, wearNow - r.wearRef) / wearDist;

    // Distance from the pit entry to the flag.
    const float atEntry = in->dist_raced + toEntry;
    const float toGo = in->race_laps * r.L - atEntry;
    if (toGo < 0.3f * r.L) return;
    const float fuelAtEntry = in->fuel - fuelPerM * toEntry;
    const float wearAtEntry = wearNow + wearPerM * toEntry;

    const float reserve = 0.5f;  // litres
    const bool fuelShort = fuelAtEntry - fuelPerM * toGo < reserve &&                    // won't make the flag
                           fuelAtEntry - fuelPerM * r.L < reserve + fuelPerM * 400.0f;  // nor the next pit entry
    const bool tyresGone = wearAtEntry + wearPerM * r.L > r.wearLimit && wearPerM * toGo > 0.05f;
    // Heavy damage (a quarter of the downforce gone) is worth a stop to repair.
    const bool broken = in->damage > 5000.0f && toGo > 3.0f * r.L;
    if (!fuelShort && !tyresGone && !broken) return;

    RRControl o{};
    o.pit_request = 1;
    // Fuel to the flag plus a little; the host clamps it to the tank (then another stop follows).
    o.pit_fuel = std::max(0.0f, fuelPerM * toGo * 1.08f + reserve + 1.0f - fuelAtEntry);
    // Tyres: change if they are past a third of their life or will not reach the
    // flag; take the softest compound that lasts to the flag.
    const float baseWear = wearPerM / compoundWear(in->tire_compound);  // per metre on mediums
    if (tyresGone || wearAtEntry > 0.3f || wearAtEntry + wearPerM * toGo > r.wearLimit) {
        o.pit_tires = RR_TIRE_HARD;
        for (int c : {RR_TIRE_SOFT, RR_TIRE_MEDIUM})
            if (baseWear * compoundWear(c) * toGo < r.wearLimit) { o.pit_tires = c; break; }
    }
    o.pit_repair = in->damage > 1500.0f;
    r.order = o;
    r.mode = PIT_IN;
    std::snprintf(r.plan, sizeof r.plan, "box: %.0f l%s%s", o.pit_fuel,
                  o.pit_tires == RR_TIRE_SOFT     ? " +soft"
                  : o.pit_tires == RR_TIRE_MEDIUM ? " +medium"
                  : o.pit_tires == RR_TIRE_HARD   ? " +hard"
                                                  : "",
                  o.pit_repair ? " +repair" : "");
}

// Lateral offset (from the centreline) of the pit path at track sample i.
float pitOffset(const RacingLine& r, int i, float boxS, bool serviced) {
    const RRPitInfo& p = r.pit;
    const float s = r.tp[r.wrap(i)].s;
    const float line = r.offset[r.wrap(i)];
    const float lane = p.lane_offset, box = p.box_offset;
    const float blend = 12.0f;  // metres to move between the fast lane and the box
    if (r.inSpan(s, p.entry_s, p.lane_start_s)) {
        float len = std::max(10.0f, r.fwd(p.entry_s, p.lane_start_s) - 10.0f);
        float u = smooth01(r.fwd(p.entry_s, s) / len);
        return line + (lane - line) * u;
    }
    if (r.inSpan(s, p.lane_start_s, p.lane_end_s)) {
        float d = r.signedDs(boxS, s);  // + = past the box
        if (!serviced) {
            if (d >= 0) return box;
            if (d > -blend) return lane + (box - lane) * smooth01((d + blend) / blend);
            return lane;
        }
        if (d >= 0 && d < blend) return box + (lane - box) * smooth01(d / blend);
        if (d < 0 && d > -blend) return box;
        return lane;
    }
    if (r.inSpan(s, p.lane_end_s, p.exit_s)) {
        float start = 5.0f;  // stay clear of the end of the pit wall
        float len = std::max(10.0f, r.fwd(p.lane_end_s, p.exit_s) - start);
        float u = smooth01((r.fwd(p.lane_end_s, s) - start) / len);
        return lane + (line - lane) * u;
    }
    return line;
}

// ---------------------------------------------------------------- racecraft

// Speed limit for holding a lateral position other than the racing line
// through the next corners: path curvature from the centreline curvature at
// that offset, then what can still be braked for.
float offLineSpeed(const RacingLine& r, int idx, float lat, float mass, float tyreGrip) {
    const float g = 9.81f, mu = r.car.tire_mu * r.grip * tyreGrip, D = r.car.downforce_coeff;
    const float decel = 0.6f * mu * g;
    float best = 1e9f;
    const int steps = (int)(160.0f / r.ds);
    for (int k = 0; k < steps; k += 2) {
        const RRTrackPoint& p = r.tp[r.wrap(idx + k)];
        float c = p.curvature;
        float k2 = std::fabs(c / std::max(0.2f, 1.0f - c * lat));
        float den = k2 - mu * D / mass;
        if (den <= 1e-6f) continue;
        float vc = std::sqrt(mu * g / den);
        best = std::min(best, std::sqrt(vc * vc + 2 * decel * k * r.ds));
    }
    return best;
}

const RROpponent* carAhead(const RRSensors* in, float maxDs) {
    for (int k = 0; k < in->num_nearby; ++k) {
        const RROpponent& o = in->nearby[k];
        if (o.ds > 0 && o.ds < maxDs && o.pit_state == RR_PIT_NONE) return &o;
    }
    return nullptr;
}

// Side (+1 left, -1 right) of the inside of the next real corner within range, 0 if none.
float nextCornerInside(const RacingLine& r, int idx, float range) {
    int steps = (int)(range / r.ds);
    for (int k = 10; k < steps; ++k) {
        float c = r.tp[r.wrap(idx + k)].curvature;
        if (std::fabs(c) > 0.012f) return c > 0 ? 1.0f : -1.0f;
    }
    return 0;
}

// Target lateral shift from the racing line (m) for passing or defending;
// may also lower the target speed.
float racecraft(RacingLine& r, const RRSensors* in, int idx, float v, float* speedCap) {
    const float hw = r.tp[idx].half_width - r.margin;
    const float line = r.offset[idx];
    const float myLat = in->track_pos * r.tp[idx].half_width;
    auto pathLat = [&](float ds) { return r.offset[r.wrap(idx + (int)(ds / r.ds))] + r.passOffset; };

    // Follow: never drive into the back of a car in our path, keep a gap that
    // grows with speed.
    for (int k = 0; k < in->num_nearby; ++k) {
        const RROpponent& o = in->nearby[k];
        if (o.ds <= 0 || o.ds > 15.0f + v || o.pit_state != RR_PIT_NONE) continue;
        bool inPath = std::fabs(o.lateral - pathLat(o.ds)) < 2.3f || (o.ds < 12.0f && std::fabs(o.lateral - myLat) < 2.3f);
        if (!inPath) continue;
        // Gap that grows with speed; close it gently, and if they are
        // braking hard (or stopped) brake for their speed in time.
        float gap = o.ds - 4.8f, want = (1.5f + 0.07f * v) / r.attack;
        float cap = o.speed + 0.6f * (gap - want);
        if (gap > want) cap = std::min(cap, std::sqrt(o.speed * std::max(0.0f, o.speed) + 2.0f * 9.0f * (gap - want)) + 3.0f);
        *speedCap = std::min(*speedCap, std::max(0.0f, cap));
    }

    float target = line;  // absolute lateral we want
    bool busy = false;

    // Overtaking: go round the nearest car ahead on the side with more room.
    // The side is held once alongside, and re-chosen while still behind (the
    // car ahead may move to defend).
    if (r.pass) {
        const RROpponent* a = carAhead(in, (8.0f + 0.15f * v) * r.attack);
        if (a && (v > a->speed + 0.5f || a->ds < 12.0f)) {
            float roomLeft = hw - a->lateral, roomRight = hw + a->lateral;
            bool alongside = a->ds < 7.0f && r.passCar == a->car_index;
            if (!alongside || r.passSide == 0) {
                // Pick the side that costs least through the next corners
                // (usually the inside), among those with room for a car.
                const float mass = r.car.mass + in->fuel * r.car.fuel_density;
                float best = 0, bestV = -1;
                for (float side : {1.0f, -1.0f}) {
                    float room = side > 0 ? roomLeft : roomRight;
                    if (room < 2.8f) continue;
                    float lat = std::clamp(a->lateral + side * 3.4f, -hw, hw);
                    float vs = offLineSpeed(r, idx, lat, mass, in->tire_grip);
                    if (side == r.passSide && r.passCar == a->car_index) vs *= 1.03f;  // hysteresis
                    if (vs > bestV) { bestV = vs; best = side; }
                }
                r.passSide = best;
            }
            r.passCar = a->car_index;
            float t = std::clamp(a->lateral + r.passSide * 3.4f, -hw, hw);
            if (r.passSide != 0 && std::fabs(t - a->lateral) >= 2.8f) {
                target = t;
                busy = true;
            }
        } else {
            r.passCar = -1;
            r.passSide = 0;
        }
    }

    // Defending: a car within 25 m behind, on the same lap, and closing.
    if (!busy && r.defend && in->time >= r.defendCooldown) {
        const RROpponent* threat = nullptr;
        for (int k = 0; k < in->num_nearby; ++k) {
            const RROpponent& o = in->nearby[k];
            if (o.ds < 0 && o.ds > -25.0f && o.laps_ahead == 0 && o.pit_state == RR_PIT_NONE &&
                (o.speed > v + 0.5f || o.ds > -10.0f)) {
                threat = &o;
                break;
            }
        }
        if (r.defendSide == 0 && threat && threat->ds < -7.0f) {
            float inside = nextCornerInside(r, idx, 150.0f);
            // One move, and not across a car that is already on that side.
            if (inside != 0 && (threat->lateral - myLat) * inside < 1.0f) {
                r.defendSide = inside;
                r.defendUntil = in->time + 6.0;
            }
        }
        if (r.defendSide != 0) {
            if (!threat || in->time > r.defendUntil) {
                r.defendSide = 0;
                r.defendCooldown = in->time + 3.0;
            } else {
                target = r.defendSide * (hw - 1.0f);
            }
        }
    }

    // Blue flag: a car is lapping us. Move aside and lift a little.
    if (in->blue_flag) {
        float aside = target;
        *speedCap = std::min(*speedCap, r.speed[idx] * rr_blue_flag(in, myLat, hw, &aside));
        target = aside;
        r.defendSide = 0;
    }

    // Never squeeze a car that is alongside, or move across one closing from behind.
    float lo = -(hw + r.margin - 1.0f), hi = hw + r.margin - 1.0f;
    rr_side_limits(in, myLat, v, &lo, &hi);
    r.sideLo = lo;
    r.sideHi = hi;
    const float wanted = target;
    target = std::clamp(target, lo, hi);
    // Squeezed out of a pass (another car is where we wanted to go): back out
    // and tuck in behind rather than force it.
    if (busy && std::fabs(target - wanted) > 0.8f && r.passCar >= 0) {
        for (int k = 0; k < in->num_nearby; ++k) {
            const RROpponent& o = in->nearby[k];
            if (o.car_index == r.passCar && o.ds > -2.0f && o.ds < 10.0f)
                *speedCap = std::min(*speedCap, std::max(0.0f, o.speed - 1.5f));
        }
        r.passSide = 0;
    }
    // Off the line the corners are tighter (or wider): slow for them, with a
    // margin, since there is a car next to us.
    const float myPath = std::clamp(line + r.passOffset, lo, hi);
    if (std::fabs(target - line) > 0.5f || std::fabs(myPath - line) > 0.5f) {
        const float mass = r.car.mass + in->fuel * r.car.fuel_density;
        float lat = std::fabs(target - line) > std::fabs(myPath - line) ? target : myPath;
        *speedCap = std::min(*speedCap, 0.96f * offLineSpeed(r, idx, lat, mass, in->tire_grip));
    }
    return target - line;
}

// ---------------------------------------------------------------- the limit

// Learns how much grip each 20 m of track really has. Only laps in clean air
// count: traffic and racecraft moves put the car off its line.
void learnLimit(RacingLine& r, const RRSensors* in, int idx, bool busy) {
    if (!r.learn) return;
    const int b = r.binOf(idx), nb = (int)r.adj.size();
    if (b != r.bin) {
        if (r.bin >= 0 && r.binClean) {
            if (r.binLost || r.binUse > 1.12f) {
                // Over the limit: slower here, and brake earlier for it.
                const float cut = r.binLost ? 0.05f : 0.025f;
                for (int k = 0; k < 3; ++k) {
                    float& a = r.adj[(r.bin - k + nb) % nb];
                    a = std::max(0.8f, a - cut * (1.0f - 0.3f * k));
                }
                r.adjDirty = true;
            } else if (r.binUse > 0.4f && r.binUse < 0.9f) {
                // Working the tyres but well inside their grip: a little faster next time.
                float& a = r.adj[r.bin];
                const float na = std::min(r.push, a + 0.01f);
                if (na != a) { a = na; r.adjDirty = true; }
            }
        }
        r.bin = b;
        r.binUse = 0;
        r.binClean = true;
        r.binLost = false;
    }
    // The rear under power is traction control's business, not the corner speed's.
    float use = in->grip_use[0];
    if (r.lastAccel < 0.15f) use = std::max(use, in->grip_use[1]);
    r.binUse = std::max(r.binUse, use);
    if (!in->on_track || std::fabs(in->angle) > 0.5f) r.binLost = true;
    bool traffic = false;
    for (int k = 0; k < in->num_nearby; ++k)
        if (in->nearby[k].ds > -10.0f && in->nearby[k].ds < 40.0f) traffic = true;
    if (busy || traffic || in->pit_state != RR_PIT_NONE || in->speed_x < 15.0f || in->dist_raced < 0) r.binClean = false;
}

// ---------------------------------------------------------------- driving

void drive(void* self, const RRSensors* in, RRControl* out) {
    auto* r = static_cast<RacingLine*>(self);
    const float v = std::max(0.0f, in->speed_x);
    const int idx = in->track_index;
    const float s = in->dist_from_start;

    if (in->time < 0.05) {
        r->startLat = in->track_pos * r->tp[idx].half_width;
        r->startDist = in->dist_raced;
        r->fuelRef = in->fuel;
        r->fuelDistRef = r->wearDistRef = std::max(0.0f, in->dist_raced);
    }
    // A finished service resets the strategy's references.
    if (in->pit_stops != r->stopsSeen) {
        r->stopsSeen = in->pit_stops;
        r->fuelRef = in->fuel;
        r->fuelDistRef = in->dist_raced;
        if (r->order.pit_tires) {
            r->wearRef = 0;
            r->wearDistRef = in->dist_raced;
        }
        r->mode = PIT_OUT;
    }

    if (in->pit_state == RR_PIT_SERVICE) {
        out->brake = 1;
        std::snprintf(out->status, sizeof out->status, "in the box  %.1f s", in->service_time_left);
        return;
    }
    learnLimit(*r, in, idx, r->mode != RACE || r->passCar >= 0 || r->defendSide != 0 || std::fabs(r->passOffset) > 0.5f);
    if (in->pit_state == RR_PIT_NONE && r->mode == RACE && rr_recover(&r->recovery, in, out, r->car.max_steer)) {
        r->binLost = true;
        std::snprintf(out->status, sizeof out->status, "recovering");
        return;
    }

    // Re-plan the speed profile for the fuel load and tyres once a lap, and at
    // once after a hit that cost downforce.
    const float mass = r->car.mass + in->fuel * r->car.fuel_density;
    const float dmg = std::min(1.0f, in->damage / 8000.0f);
    if (std::fabs(dmg - r->damage) > 0.03f) {
        r->damage = dmg;
        planSpeed(*r, mass, in->tire_grip);
    }
    if (in->lap != r->plannedLap) {
        r->plannedLap = in->lap;
        if (r->adjDirty || std::fabs(mass - r->plannedMass) > 3.0f || std::fabs(in->tire_grip - r->plannedGrip) > 0.003f) {
            planSpeed(*r, mass, in->tire_grip);
            r->adjDirty = false;
        }
    }

    strategy(*r, in);
    if (r->mode == PIT_OUT && in->pit_state == RR_PIT_NONE && !r->inSpan(s, r->pit.entry_s, r->pit.exit_s)) {
        r->mode = RACE;
        r->plan[0] = 0;
    }
    // Overshot the box: give up on this stop.
    if (r->mode == PIT_IN && in->pit_state == RR_PIT_LANE && r->signedDs(in->pit_box_s, s) > 2.5f) {
        r->mode = PIT_OUT;
        std::snprintf(r->plan, sizeof r->plan, "missed the box");
    }
    const bool pitting = r->mode != RACE;
    const bool serviced = r->mode == PIT_OUT;

    float speedCap = 1e9f;
    float want = 0;
    if (!pitting) {
        want = racecraft(*r, in, idx, v, &speedCap);
    } else {
        r->passCar = -1;
        r->defendSide = 0;
    }
    {
        // Smooth, rate-limited lateral moves (at most 2 m/s sideways).
        const float dt = in->dt > 0 ? in->dt : 0.02f;
        float step = (want - r->passOffset) * 0.08f;
        r->passOffset += std::clamp(step, -2.0f * dt, 2.0f * dt);
    }

    // The path itself (not just the racecraft target) keeps clear of cars
    // alongside: the racing line moves across the track into corners.
    if (pitting) {
        r->sideLo = -1e9f;
        r->sideHi = 1e9f;
    }
    // Off the grid: keep to our side of the track and ease onto the line over
    // the first few hundred metres instead of diving across the field.
    const float launch = smooth01((in->dist_raced - r->startDist) / 400.0f);
    auto targetOffset = [&](int i) {
        if (pitting) return pitOffset(*r, i, in->pit_box_s, serviced);
        float line = r->offset[r->wrap(i)];
        if (launch < 1.0f) line = r->startLat + (line - r->startLat) * launch;
        return std::clamp(line + r->passOffset, r->sideLo, r->sideHi);
    };

    // Pure pursuit on the path, plus a cross-track term (pure pursuit alone
    // drifts wide at speed because the car understeers).
    float ld = r->look * (6.0f + 0.22f * v);
    int ahead = (int)(ld / r->ds);
    int ti = r->wrap(idx + ahead);
    P2 tgt = r->pointAt(ti, targetOffset(ti));
    float dx = tgt.x - in->x, dy = tgt.y - in->y;
    float cy = std::cos(in->yaw), sy = std::sin(in->yaw);
    float lx = cy * dx + sy * dy, ly = -sy * dx + cy * dy;
    float alpha = std::atan2(ly, lx);
    float dist = std::sqrt(lx * lx + ly * ly);
    float delta = std::atan(2.0f * r->car.wheelbase * std::sin(alpha) / std::max(dist, 1.0f));
    float lateral = in->track_pos * r->tp[idx].half_width;
    float crossErr = targetOffset(idx) - lateral;  // + = path is to our left
    delta += std::atan(1.5f * crossErr / (v + 5.0f));
    // Yaw-rate feedback against the rate the line asks for: damps the weave
    // that pure pursuit plus the cross-track term can build up at speed.
    delta -= r->yawGain * (in->yaw_rate - v * r->kappaSigned[r->wrap(idx + 2)]);
    out->steer = std::clamp(delta / r->car.max_steer, -1.0f, 1.0f);

    // Tyre temperature: the profile assumes tyres in their window. Cold or
    // overheated ones have less grip (axle_grip against tire_grip), and past
    // our heat tolerance we back off on purpose to cool them.
    {
        float f = 1.0f;
        if (in->tire_temp_window[1] > 0) {  // ABI 4 host
            const float grip = std::max(0.5f, in->tire_grip);
            f = std::clamp(std::min(in->axle_grip[0], in->axle_grip[1]) / grip, 0.8f, 1.05f);
            const float over = std::max(in->tire_temp[0], in->tire_temp[1]) - (in->tire_temp_window[1] + r->heat);
            if (over > 0) f *= std::max(0.9f, 1.0f - 0.006f * over);
        }
        const float dt = in->dt > 0 ? in->dt : 0.02f;
        r->tyreNow += (f - r->tyreNow) * std::min(1.0f, dt / 1.5f);
    }
    // Speed control against the profile, looking a little ahead for actuator lag.
    int si = r->wrap(idx + (int)(v * 0.15f / r->ds) + 1);
    float vTarget = std::min(r->speed[si] * std::sqrt(r->tyreNow), speedCap);

    if (pitting) {
        const RRPitInfo& p = r->pit;
        const float vLim = p.speed_limit * 0.93f;
        if (r->mode == PIT_IN) {
            out->pit_request = 1;
            out->pit_fuel = r->order.pit_fuel;
            out->pit_tires = r->order.pit_tires;
            out->pit_repair = r->order.pit_repair;
            // Brake for the limit by the lane start, then for the box.
            float toLane = r->inSpan(s, p.lane_start_s, p.lane_end_s) ? 0.0f : r->fwd(s, p.lane_start_s);
            vTarget = std::min(vTarget, std::sqrt(vLim * vLim + 2 * 7.0f * toLane));
            float toBox = r->signedDs(s, in->pit_box_s);
            if (toBox > -3.0f) vTarget = std::min(vTarget, std::sqrt(2 * 3.0f * std::max(0.0f, toBox - 0.3f)));
            if (toBox < 0.6f && v < 3.0f) vTarget = 0;
        } else if (r->inSpan(s, p.lane_start_s, p.lane_end_s)) {
            vTarget = std::min(vTarget, vLim);
            // Pulling out of the box: let cars already in the lane go by.
            if (v < 3.0f)
                for (int k = 0; k < in->num_nearby; ++k) {
                    const RROpponent& o = in->nearby[k];
                    if (o.ds < 2.0f && o.ds > -30.0f && o.speed > 3.0f && o.pit_state != RR_PIT_NONE) vTarget = 0;
                }
        }
    }

    float err = vTarget - v;
    if (vTarget <= 0.01f) out->brake = 1;
    else if (err > 0) out->accel = std::clamp(0.5f + 0.5f * err, 0.0f, 1.0f);
    else out->brake = std::clamp(-0.25f * err, 0.0f, 1.0f);
    // Traction control: cut quickly when the rear is past its grip, restore slowly.
    // Traction, oversteer catches and understeer (see rr_awareness.h).
    rr_grip_guard(in, out, &r->tc, r->car.max_steer);
    r->lastAccel = out->accel;
    if (!in->on_track && !pitting) out->accel = std::min(out->accel, 0.5f);

    const char* what = pitting              ? r->plan
                       : r->defendSide != 0 ? "defending"
                       : r->passCar >= 0    ? "passing"
                                            : "line";
    std::snprintf(out->status, sizeof out->status, "%s  %.0f km/h", what, vTarget * 3.6f);
    out->debug[0] = vTarget;
    out->debug[1] = r->offset[idx];
    out->debug[2] = r->passOffset;
    out->debug[3] = (float)r->mode;
    out->debug[4] = r->defendSide;
    out->debug[5] = std::max(-99.0f, r->sideLo);
    out->debug[6] = std::min(99.0f, r->sideHi);
}

void destroy(void* self) { delete static_cast<RacingLine*>(self); }

int debugPath(void* self, float* xy, int maxPoints) {
    auto* r = static_cast<RacingLine*>(self);
    int step = std::max(1, (int)std::lround(2.0f / r->ds));
    int count = 0;
    for (int i = 0; i <= r->n() && count < maxPoints; i += step, ++count) {
        const P2& p = r->line[r->wrap(i)];
        xy[2 * count] = p.x;
        xy[2 * count + 1] = p.y;
    }
    return count;
}

const RRRobotApi kApi = {RR_ABI_VERSION, "racingline", "Raylib Racers examples", create, drive, destroy, debugPath};

}  // namespace

extern "C" RR_EXPORT const RRRobotApi* rr_robot_entry(void) { return &kApi; }
