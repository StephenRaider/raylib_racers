#pragma once
// A car's robot, as the race sees it: either the library loaded into the
// simulator (fast, the default) or the library running in its own sandboxed
// process (rr_bothost), where a crash or a hang only loses that car and the
// robot cannot touch files.
#include <memory>
#include <string>
#include <vector>

#include "rr/robot_api.h"

namespace rr {

class RobotModule;

struct DriveResult {
    double cpu = 0;      // s of CPU the robot used for this call (wall time on Windows)
    bool failed = false; // the robot crashed or hung: the car is out
    std::string why;
};

class RobotDriver {
public:
    virtual ~RobotDriver() = default;
    const std::string& name() const { return name_; }
    int abi() const { return abi_; }
    // create(): fills `cfg` back from the robot. False when the robot refused the car or failed.
    virtual bool create(const RRTrackInfo& track, const RRCarSpec& car, int index, const std::string& params,
                        RRRobotConfig& cfg, std::string* err) = 0;
    // A drive call in two halves, so sandboxed robots of several cars run at the same time.
    virtual void beginDrive(const RRSensors& s, const RRControl& in) = 0;
    virtual DriveResult endDrive(RRControl& out) = 0;
    // The end of the session (calls the robot's session_end when it has one) and,
    // for a sandboxed robot, brings its weekend memory back into `memory`.
    virtual void sessionEnd(const RRSessionSummary& s, std::vector<unsigned char>* memory) = 0;
    virtual bool hasDebugPath() const = 0;
    virtual int debugPath(float* xy, int maxPoints) = 0;
    virtual bool sandboxed() const { return false; }
    // Sandboxed robots: how long one drive call may take before the car is out as hung.
    virtual void setHangTimeout(double) {}

    // The robot loaded into this process.
    static std::shared_ptr<RobotDriver> inProcess(const std::shared_ptr<RobotModule>& module);
    // The robot library at `libPath` run by the rr_bothost program at `hostPath`.
    static std::shared_ptr<RobotDriver> sandbox(const std::string& hostPath, const std::string& libPath,
                                                std::string* err);

protected:
    std::string name_;
    int abi_ = 0;
};

// The thread's CPU time, s (wall time on Windows, where thread times are too coarse).
double threadCpuSeconds();

}  // namespace rr
