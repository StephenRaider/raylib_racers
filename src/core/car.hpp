#pragma once
#include "rr/robot_api.h"
#include "vec2.hpp"

namespace rr {

struct CarParams {
    float mass = 800;
    float length = 4.6f, width = 1.9f;
    float cgToFront = 1.35f, cgToRear = 1.45f;
    float cgHeight = 0.28f;
    float yawInertia = 1300;
    float maxSteer = 0.32f;       // rad
    float steerRate = 2.5f;       // rad/s at the road wheels
    float tireMu = 1.5f;
    float tireB = 18.0f, tireC = 1.3f;  // simplified magic formula
    // Rear tyres a little stiffer and grippier than the fronts: a stable,
    // mildly understeering car, like most racing setups.
    float frontGrip = 0.96f, rearGrip = 1.04f;
    float frontStiffness = 0.8f;   // scales tireB at the front
    // Load sensitivity: cornering stiffness grows like Fz^(1 - loadSens) and
    // friction drops by muLoadDrop per extra static load.
    float loadSens = 0.5f;
    float muLoadDrop = 0.08f;
    float dragCoeff = 0.48f;      // 0.5 rho Cd A
    float downforceCoeff = 1.4f;  // 0.5 rho Cl A
    float downforceFront = 0.42f; // share on the front axle
    float rollingResist = 0.015f;
    float wheelRadius = 0.33f;
    float finalDrive = 3.0f;
    int numGears = 6;
    float gearRatios[RR_MAX_GEARS] = {3.2f, 2.35f, 1.85f, 1.52f, 1.3f, 1.15f, 0, 0};
    float reverseRatio = 3.0f;
    float idleRpm = 2000, maxRpm = 9000;
    float maxBrakeForce = 17000;  // N
    float brakeFront = 0.66f;     // brake bias
    float drivetrainEff = 0.9f;

    // Fuel: mass is the dry car with driver; fuel adds to it.
    float fuelCapacity = 60.0f;   // litres
    float fuelDensity = 0.75f;    // kg/l
    float fuelPerJoule = 2.3e-7f; // litres per joule of engine work (~3 l per lap of the circuit)
    // Tyre wear: wear per joule of sliding work, before compound and race multipliers.
    float wearPerJoule = 2.0e-8f;
    // Damage costs downforce: up to maxAeroLoss at damageForMaxLoss.
    float maxAeroLoss = 0.35f, damageForMaxLoss = 8000.0f;

    float wheelbase() const { return cgToFront + cgToRear; }
    float engineTorque(float rpm) const;  // N m at full throttle
    RRCarSpec spec() const;
};

struct CarState {
    Vec2 pos;
    float yaw = 0;
    float vx = 0, vy = 0;   // body frame
    float yawRate = 0;
    float steerAngle = 0;   // actual road-wheel angle
    int gear = 1;
    float rpm = 0;
    float ax = 0;           // last longitudinal accel (load transfer)
    float wheelSpin = 0;
    float wheelRot = 0;     // for rendering
    float damage = 0;
    float fuel = 60;
    float tireWear[2] = {0, 0};  // front, rear
    int compound = RR_TIRE_MEDIUM;

    Vec2 velWorld() const { return rotate({vx, vy}, yaw); }
    void setVelWorld(Vec2 v) { Vec2 b = rotate(v, -yaw); vx = b.x; vy = b.y; }
};

struct Surface {
    float muScale = 1.0f;   // grip multiplier
    float extraDrag = 0.0f; // N per m/s (grass)
    float dragScale = 1.0f; // < 1 in another car's slipstream
};

// Race-wide multipliers (command line) for consumables.
struct WearRates {
    float fuel = 1.0f;
    float tire = 1.0f;
};

float compoundGrip(int compound);  // grip multiplier of a new tyre
float compoundWear(int compound);  // wear-rate multiplier
float wornGrip(float wear);        // grip multiplier from wear (1 when new, cliff past 0.7)
float axleGrip(const CarState& c, int axle);  // compound x wear, 0 front / 1 rear
float carMass(const CarParams& p, const CarState& c);  // including fuel

// One fixed physics step of a planar dynamic bicycle model with load
// transfer, aero, a simple engine/gearbox and friction-circle tyres.
void stepCar(CarState& c, const CarParams& p, const RRControl& in, bool autoGear,
             const Surface& surf, const WearRates& rates, float dt);

}  // namespace rr
