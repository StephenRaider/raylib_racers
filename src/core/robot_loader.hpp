#pragma once
#include <memory>
#include <string>
#include <vector>

#include "rr/robot_api.h"

namespace rr {

// A loaded robot shared library. Libraries are cached, so several cars can
// share one module.
class RobotModule {
public:
    ~RobotModule();
    const RRRobotApi* api() const { return api_; }
    const std::string& path() const { return path_; }

    // Resolves "name" to <dir>/<name>.so (or .dll/.dylib) for each search dir,
    // or takes a path as is.
    // The library file for a robot name (or path), "" if none.
    static std::string find(const std::string& nameOrPath, const std::vector<std::string>& searchDirs);
    static std::shared_ptr<RobotModule> load(const std::string& nameOrPath,
                                             const std::vector<std::string>& searchDirs,
                                             std::string* err);

private:
    void* handle_ = nullptr;
    const RRRobotApi* api_ = nullptr;
    std::string path_;
};

}  // namespace rr
