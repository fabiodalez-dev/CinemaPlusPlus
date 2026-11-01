#pragma once

#include "core/json.hpp"
#include <functional>
#include <string>
#include <vector>

namespace nativetv {
namespace daemon {

// The LED strip's process (native/nativetv-ambilight), seen from the daemon:
// started when the strip is set to follow the screen and is not running,
// asked to scan and to show a colour by the dashboard.
std::string ambilightBinary(const std::string &dataRoot);
bool ambilightRunning();
// Starts the process, detached, when config.json says follow and it is not
// running. True when it was started.
bool ensureAmbilight(const std::string &dataRoot, const std::string &configPath,
                     const std::function<void(const std::string &)> &log);
// {"conf", "running", "log": the last lines}
Json ledStripStatus(const std::string &dataRoot, const std::string &configPath);
// The arguments to run the process with, for a ChildProcessRunner.
std::vector<std::string> ambilightCommand(const std::string &dataRoot,
                                          const std::vector<std::string> &arguments);
std::vector<std::string> ambilightEnvironment(const std::string &dataRoot);

} // namespace daemon
} // namespace nativetv
