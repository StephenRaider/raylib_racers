#include "test_figures.hpp"

#include <algorithm>
#include <cmath>

#include "rr/robot_api.h"

namespace {

constexpr float kG = 9.81f;

struct Straight {
    float to100 = 0, to200 = 0, top = 0;  // s, s, km/h
    float brake300 = 0;                   // m from 300 (or near the top speed) to 100 km/h
    float brakeFrom = 300;
};

// Full throttle from rest on a flat, grippy road with warm tyres and half a
// tank, then full braking from 300 km/h (or just under the top speed).
Straight straightLine(const rr::CarParams& p) {
    Straight out;
    const float dt = 0.002f;
    rr::Surface surf;
    rr::WearRates rates;
    rr::CarState s;
    s.fuel = 0.5f * p.fuelCapacity;
    s.tireTemp[0] = s.tireTemp[1] = 100;
    RRControl c{};
    c.accel = 1;
    c.gear = 1;
    float t = 0, vmax = 0;
    for (int i = 0; i < 40000; ++i) {  // 80 s
        rr::stepCar(s, p, c, true, surf, rates, dt);
        s.tireTemp[0] = s.tireTemp[1] = 100;
        t += dt;
        const float v = s.vx * 3.6f;
        if (out.to100 == 0 && v >= 100) out.to100 = t;
        if (out.to200 == 0 && v >= 200) out.to200 = t;
        vmax = std::max(vmax, v);
    }
    out.top = vmax;
    out.brakeFrom = std::min(300.0f, std::floor((vmax - 5) / 10) * 10);
    // brake from brakeFrom: set the speed and keep the gear the car is in
    s.vx = out.brakeFrom / 3.6f;
    s.vy = 0;
    s.yawRate = 0;
    c.accel = 0;
    c.brake = 1;
    float dist = 0;
    for (int i = 0; i < 10000 && s.vx * 3.6f > 100; ++i) {
        rr::stepCar(s, p, c, true, surf, rates, dt);
        s.tireTemp[0] = s.tireTemp[1] = 100;
        dist += s.vx * dt;
    }
    out.brake300 = dist;
    return out;
}

unsigned maskFor(const rr::DevRules& rules, std::initializer_list<const char*> fields) {
    unsigned m = 0;
    for (size_t k = 0; k < rules.categories.size(); ++k)
        for (const rr::DevEffect& e : rules.categories[k].effects)
            for (const char* f : fields)
                if (e.field == f) m |= 1u << k;
    return m;
}

}  // namespace

rr::CarParams carWithStats(const rr::DevRules& rules, const std::vector<int>& stats) {
    rr::CarParams p;
    if (!rules.empty() && stats.size() == rules.categories.size()) rr::applyDevelopment(rules, stats, p);
    return p;
}

std::vector<FigureLine> carFigures(const rr::DevRules& rules, const std::vector<int>& stats, const FigureInputs& in) {
    const rr::CarParams p0, p1 = carWithStats(rules, stats);
    const Straight s0 = straightLine(p0), s1 = straightLine(p1);
    std::vector<FigureLine> out;
    auto add = [&](const char* name, const char* unit, float a, float b, int dec, bool higher,
                   std::initializer_list<const char*> fields, bool approx = false) {
        FigureLine f;
        f.name = name;
        f.unit = unit;
        f.stock = a;
        f.now = b;
        f.decimals = dec;
        f.higherBetter = higher;
        f.statMask = maskFor(rules, fields);
        f.approx = approx;
        out.push_back(f);
    };
    const float m0 = p0.mass + 0.5f * p0.fuelCapacity * p0.fuelDensity;
    auto latG = [&](const rr::CarParams& p, float kmh) {
        const float v = kmh / 3.6f, df = p.downforceCoeff * v * v;
        return p.tireMu * 0.5f * (p.frontGrip + p.rearGrip) * (m0 * kG + df) / (m0 * kG);
    };
    auto service = [](const rr::CarParams& p, float litres, bool tyres) {
        return p.pitServiceScale *
               (RR_PIT_SERVICE_BASE + std::max(litres / RR_PIT_FUEL_RATE, tyres ? RR_PIT_TIRE_CHANGE : 0.0f));
    };
    add("Engine power", "kW", p0.maxPower() * p0.torqueScale / 1000, p1.maxPower() * p1.torqueScale / 1000, 0, true,
        {"torqueScale"});
    add("Top speed", "km/h", s0.top, s1.top, 1, true, {"dragCoeff", "torqueScale"});
    add("0-200 km/h", "s", s0.to200, s1.to200, 2, false, {"torqueScale", "dragCoeff", "tireMu", "downforceCoeff"});
    char buf[48];
    std::snprintf(buf, sizeof buf, "Braking %.0f-100 km/h", s1.brakeFrom);
    add(buf, "m", s0.brake300, s1.brake300, 1, false, {"maxBrakeForce", "tireMu", "downforceCoeff", "dragCoeff"});
    add("Downforce at 250 km/h", "kg", p0.downforceCoeff * 69.44f * 69.44f / kG, p1.downforceCoeff * 69.44f * 69.44f / kG,
        0, true, {"downforceCoeff"});
    add("Drag at 300 km/h", "N", p0.dragCoeff * 83.33f * 83.33f, p1.dragCoeff * 83.33f * 83.33f, 0, false, {"dragCoeff"});
    add("Cornering grip at 100 km/h", "g", latG(p0, 100), latG(p1, 100), 2, true, {"tireMu", "downforceCoeff"}, true);
    add("Cornering grip at 250 km/h", "g", latG(p0, 250), latG(p1, 250), 2, true, {"tireMu", "downforceCoeff"}, true);
    add("Turn-in agility", "%", 100.0f, 100.0f * p0.yawInertia / p1.yawInertia, 1, true, {"yawInertia"});
    add("Brake force", "kN", p0.maxBrakeForce / 1000, p1.maxBrakeForce / 1000, 1, true, {"maxBrakeForce"});
    add("Pit service: tyres only", "s", service(p0, 0, true), service(p1, 0, true), 2, false, {"pitServiceScale"});
    add("Pit service: tyres + 20 L", "s", service(p0, 20, true), service(p1, 20, true), 2, false, {"pitServiceScale"});
    const float fuel1 = in.fuelPerLap * p1.fuelPerJoule / p0.fuelPerJoule;
    add("Fuel use on this track", "L/lap", in.fuelPerLap, fuel1, 2, false, {"fuelPerJoule"}, true);
    add("Laps on a full tank", "laps", p0.fuelCapacity / std::max(0.1f, in.fuelPerLap),
        p1.fuelCapacity / std::max(0.1f, fuel1), 1, true, {"fuelPerJoule"}, true);
    if (in.tyreLifeLaps > 0) {
        // sliding heat also wears: a cooler tyre lasts a little longer
        const float heat = 1 + 0.5f * (p0.tireSlideHeat - p1.tireSlideHeat) / p0.tireSlideHeat;
        add("Medium tyre life", "laps", in.tyreLifeLaps, in.tyreLifeLaps * p0.wearPerJoule / p1.wearPerJoule * heat, 1,
            true, {"wearPerJoule", "tireSlideHeat"}, true);
    }
    add("Tyre heat from sliding", "%", 100.0f, 100.0f * p1.tireSlideHeat / p0.tireSlideHeat, 1, false, {"tireSlideHeat"});
    return out;
}
