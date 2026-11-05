#pragma once

#include "core/json.hpp"
#include <mutex>
#include <string>

namespace nativetv {
namespace daemon {
class ChildProcessRunner;

// Owns LG Home layout files, process management, and the webOS 10 overlay
// mount used by the Home takeover lifecycle.
class HomeScreenManager {
  public:
    HomeScreenManager(ChildProcessRunner &children, std::string systemRoot,
                      std::string dataRoot, std::string procRoot,
                      bool allowExperimental);

    bool isWebos9() const;
    // True on the W22 family (webOS 7.x) and its near siblings (8.x). Takeover
    // on these TVs is hook-based rather than mount-based.
    bool isWebos22Family() const;
    bool supportsTakeover(bool allowExperimentalHome) const;
    // Path to the per-TV data directory the manager was built with, used by
    // the webOS 22 hook takeover to keep backups and the init.d script.
    const std::string &dataRoot() const;
    Json state() const;
    Json setPart(const std::string &id, bool shown);
    Json startLayout(bool blank);
    Json restore();
    Json setAllowed(bool allowed);
    Json reapply();
    Json setAddedInputs(const Json &inputs);
    bool stopHome(bool &stopped, std::string *error = nullptr);
    bool restartHome(std::string *error = nullptr);

  private:
    ChildProcessRunner &children_;
    std::string systemRoot_;
    std::string dataRoot_;
    std::string procRoot_;
    bool allowExperimental_;
    mutable std::mutex mutex_;
};
} // namespace daemon
} // namespace nativetv
