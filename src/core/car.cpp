#include "car.hpp"

#include <algorithm>

namespace rr {

float CarParams::engineTorque(float rpm) const {
    // Piecewise-linear full-throttle torque curve.
    // A 3-litre V10 of the mid-2000s: ~350 N m, ~660 kW at 18,500 rpm.
    static const float pts[][2] = {{0, 150},     {4000, 220},  {8000, 290},  {12000, 330},
                                   {16000, 352}, {18500, 340}, {19000, 300}, {19400, 0}};
    const int n = sizeof(pts) / sizeof(pts[0]);
    if (rpm <= pts[0][0]) return pts[0][1];
    for (int i = 1; i < n; ++i) {
        if (rpm <= pts[i][0]) {
            float f = (rpm - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
            return pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * f;
        }
    }
    return 0;
}

float compoundGrip(int compound) {
    return compound == RR_TIRE_SOFT ? 1.035f : (compound == RR_TIRE_HARD ? 0.975f : 1.0f);
}

float compoundWear(int compound) {
    return compound == RR_TIRE_SOFT ? 1.7f : (compound == RR_TIRE_HARD ? 0.6f : 1.0f);
}

float wornGrip(float w) {
    w = clampf(w, 0, 1);
    return 1.0f - 0.07f * w - 0.8f * std::max(0.0f, w - 0.7f);
}

float axleGrip(const CarState& c, int axle) { return compoundGrip(c.compound) * wornGrip(c.tireWear[axle]); }

float carMass(const CarParams& p, const CarState& c) { return p.mass + std::max(0.0f, c.fuel) * p.fuelDensity; }

RRCarSpec CarParams::spec() const {
    RRCarSpec s{};
    s.mass = mass;
    s.length = length;
    s.width = width;
    s.wheelbase = wheelbase();
    s.cg_to_front = cgToFront;
    s.cg_to_rear = cgToRear;
    s.max_steer = maxSteer;
    s.tire_mu = tireMu;
    s.drag_coeff = dragCoeff;
    s.downforce_coeff = downforceCoeff;
    s.max_rpm = maxRpm;
    s.wheel_radius = wheelRadius;
    s.final_drive = finalDrive;
    s.num_gears = numGears;
    for (int i = 0; i < RR_MAX_GEARS; ++i) s.gear_ratios[i] = gearRatios[i];
    s.max_brake_force = maxBrakeForce;
    s.fuel_capacity = fuelCapacity;
    s.fuel_density = fuelDensity;
    return s;
}

namespace {

float gearRatio(const CarParams& p, int gear) {
    if (gear > 0) return p.gearRatios[gear - 1] * p.finalDrive;
    if (gear < 0) return -p.reverseRatio * p.finalDrive;
    return 0;
}

float rpmFor(const CarParams& p, float vx, int gear) {
    const float toRpm = 60.0f / (2 * kPi);
    return std::fabs(vx / p.wheelRadius * gearRatio(p, gear)) * toRpm;
}

// Lateral tyre force for slip angle alpha, load fz and friction mu.
// fz0 is the static axle load: away from it the cornering stiffness and the
// friction coefficient change less than linearly, as real tyres do.
float tyreLateral(const CarParams& p, float stiffness, float alpha, float fz, float fz0, float mu) {
    float b = p.tireB * stiffness * std::pow(fz0 / fz, p.loadSens);
    return -mu * fz * std::sin(p.tireC * std::atan(b * alpha));
}

// Saturate a force pair to the friction circle.
void frictionCircle(float& fx, float& fy, float maxF) {
    float f = std::sqrt(fx * fx + fy * fy);
    if (f > maxF && f > 0) {
        float k = maxF / f;
        fx *= k;
        fy *= k;
    }
}

}  // namespace

void stepCar(CarState& c, const CarParams& p, const RRControl& in, bool autoGear,
             const Surface& surf, const WearRates& rates, float dt) {
    const float g = 9.81f;
    const float m = carMass(p, c);
    const float yawInertia = p.yawInertia;  // the fuel sits at the CG: it adds mass, not yaw inertia
    const float a = p.cgToFront, b = p.cgToRear, L = p.wheelbase();

    // --- steering actuator (rate limited) ---
    float target = clampf(in.steer, -1, 1) * p.maxSteer;
    float maxD = p.steerRate * dt;
    c.steerAngle += clampf(target - c.steerAngle, -maxD, maxD);
    const float accel = clampf(in.accel, 0, 1);
    const float brake = clampf(in.brake, 0, 1);

    // --- gearbox ---
    if (autoGear) {
        if (in.gear == -1) {
            if (c.vx < 1.0f) c.gear = -1;
        } else {
            if (c.gear <= 0 && c.vx > -1.0f) c.gear = 1;
            if (c.gear > 0) {
                float rpm = rpmFor(p, c.vx, c.gear);
                if (c.gear < p.numGears && rpm > 0.965f * p.maxRpm) {
                    c.gear++;
                } else if (c.gear > 1 && rpm < 0.62f * p.maxRpm &&
                           rpmFor(p, c.vx, c.gear - 1) < 0.9f * p.maxRpm) {  // keep the revs up, F1 style
                    c.gear--;
                }
            }
        }
    } else {
        c.gear = std::max(-1, std::min(in.gear, p.numGears));
    }

    // --- engine ---
    const float ratio = gearRatio(p, c.gear);
    const float engRpm = rpmFor(p, c.vx, c.gear);
    c.rpm = std::max(p.idleRpm, engRpm);  // clutch slips below idle
    float torque = 0;
    const bool hasFuel = c.fuel > 0;
    if (c.gear != 0) {
        if (c.rpm < p.maxRpm && hasFuel) torque = p.engineTorque(c.rpm) * accel;
        if (engRpm > p.idleRpm) torque -= (1 - accel) * 60.0f * (engRpm / p.maxRpm);  // engine braking
    }
    // ratio carries the direction (negative in reverse)
    const float fDrive = torque * ratio * p.drivetrainEff / p.wheelRadius;
    if (torque > 0) {
        const float engOmega = c.rpm * (2 * kPi / 60.0f);
        c.fuel = std::max(0.0f, c.fuel - torque * engOmega * p.fuelPerJoule * rates.fuel * dt);
    }

    // --- aero and loads ---
    const float aeroLoss = p.maxAeroLoss * std::min(1.0f, c.damage / p.damageForMaxLoss);
    const float down = p.downforceCoeff * (1 - aeroLoss) * c.vx * c.vx;
    const float drag = p.dragCoeff * surf.dragScale * c.vx * std::fabs(c.vx);
    float fzf = m * g * b / L + down * p.downforceFront - m * c.ax * p.cgHeight / L;
    float fzr = m * g * a / L + down * (1 - p.downforceFront) + m * c.ax * p.cgHeight / L;
    fzf = std::max(fzf, 0.05f * m * g);
    fzr = std::max(fzr, 0.05f * m * g);
    const float mu = p.tireMu * surf.muScale;

    // --- tyre kinematics ---
    const float cd = std::cos(c.steerAngle), sd = std::sin(c.steerAngle);
    const float vfx = c.vx, vfy = c.vy + a * c.yawRate;  // front axle velocity, body frame
    const float wfLong = vfx * cd + vfy * sd;           // ...in the wheel frame
    const float wfLat = -vfx * sd + vfy * cd;
    const float vrLat = c.vy - b * c.yawRate;
    const float minV = 2.0f;  // keeps slip angles sane near standstill
    const float alphaF = std::atan(wfLat / std::max(std::fabs(wfLong), minV));
    const float alphaR = std::atan(vrLat / std::max(std::fabs(c.vx), minV));

    const float fz0f = m * g * b / L, fz0r = m * g * a / L;
    const float muF = mu * p.frontGrip * axleGrip(c, 0) * std::max(0.7f, 1.0f - p.muLoadDrop * (fzf / fz0f - 1.0f));
    const float muR = mu * p.rearGrip * axleGrip(c, 1) * std::max(0.7f, 1.0f - p.muLoadDrop * (fzr / fz0r - 1.0f));
    float fyf = tyreLateral(p, p.frontStiffness, alphaF, fzf, fz0f, muF);
    float fyr = tyreLateral(p, 1.0f, alphaR, fzr, fz0r, muR);

    // brakes and rolling resistance ramp to zero at standstill so they never push backwards
    const float rampF = clampf(wfLong / 0.5f, -1, 1);
    const float rampR = clampf(c.vx / 0.5f, -1, 1);
    const float fBrake = brake * p.maxBrakeForce;
    float fxf = -fBrake * p.brakeFront * rampF - p.rollingResist * fzf * rampF;
    float fxrDemand = fDrive - fBrake * (1 - p.brakeFront) * rampR - p.rollingResist * fzr * rampR;
    float fxr = fxrDemand;

    // How far the driven axle is past its grip (> 0 = spinning or sliding):
    // longitudinal demand plus the slip angle relative to the peak, combined.
    // This is what a traction control would watch.
    {
        const float peakBa = std::tan(kPi / (2 * p.tireC));  // B*alpha at peak lateral force
        const float bEff = p.tireB * std::pow(fz0r / fzr, p.loadSens);
        const float lat = bEff * std::fabs(alphaR) / peakBa;
        const float lon = fxrDemand / (muR * fzr);
        c.wheelSpin = std::max(0.0f, std::sqrt(lat * lat + lon * lon) - 1.0f);
    }
    frictionCircle(fxf, fyf, muF * fzf);
    frictionCircle(fxr, fyr, muR * fzr);

    // Tyre wear from sliding work: lateral slip speed times lateral force,
    // plus longitudinal work (more when the rear is spinning or locking).
    {
        const float v = std::fabs(c.vx);
        const float workF = std::fabs(fyf) * std::fabs(wfLat) + std::fabs(fxf) * 0.03f * v;
        const float workR = std::fabs(fyr) * std::fabs(vrLat) + std::fabs(fxr) * (0.03f + 0.3f * std::min(1.0f, c.wheelSpin)) * v;
        const float k = p.wearPerJoule * compoundWear(c.compound) * rates.tire * dt;
        c.tireWear[0] = std::min(1.0f, c.tireWear[0] + workF * k);
        c.tireWear[1] = std::min(1.0f, c.tireWear[1] + workR * k);
    }

    // --- body forces ---
    float fx = fxf * cd - fyf * sd + fxr - drag;
    float fy = fxf * sd + fyf * cd + fyr;
    float mz = a * (fxf * sd + fyf * cd) - b * fyr;
    fx -= surf.extraDrag * c.vx;
    fy -= surf.extraDrag * c.vy;

    const float axb = fx / m, ayb = fy / m;
    c.vx += (axb + c.vy * c.yawRate) * dt;
    c.vy += (ayb - c.vx * c.yawRate) * dt;
    c.yawRate += mz / yawInertia * dt;
    c.ax += (axb - c.ax) * std::min(1.0f, dt * 20.0f);  // filtered for load transfer

    c.pos += rotate({c.vx, c.vy}, c.yaw) * dt;
    c.yaw = wrapAngle(c.yaw + c.yawRate * dt);
    c.wheelRot += c.vx / p.wheelRadius * dt;
}

}  // namespace rr
