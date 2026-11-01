#pragma once

#include "core/json.hpp"
#include <functional>
#include <string>

namespace nativetv {
namespace daemon {

// Writes or removes /home/root/memorymanager-conf.json for the apps listed in
// the config's keepInMemory (see service/memory_keep.hpp). Never restarts
// memchute: that closes the apps it tracks, so the list counts from the next
// restart of the TV. Returns what was done, for the log.
std::string applyMemoryKeep(const std::string &systemRoot, const Json &keepInMemory,
                            const std::function<void(const std::string &)> &log);

} // namespace daemon
} // namespace nativetv
