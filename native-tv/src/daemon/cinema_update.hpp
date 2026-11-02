#pragma once

#include "core/json.hpp"
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace daemon {

// Cinema++'s own updates from its GitHub releases, as Cinema updates itself:
// the latest release's cinemapp.manifest.json names the version, the package
// and its SHA-256; the package (the install bundle and its install script) is
// downloaded beside the install, checked against that hash, unpacked and
// installed by its script in a process of its own, which restarts the service.
class CinemaUpdate {
  public:
    // GET url into body (at most maxBytes) or into the file `toFile` when set.
    using Fetch = std::function<bool(const std::string &url, std::string &body, const std::string &toFile,
                                     size_t maxBytes, std::string &error)>;
    // Runs a command line in the background, detached from the service.
    using Launch = std::function<bool(const std::string &command)>;

    CinemaUpdate(std::string installRoot, Fetch fetch = Fetch(), Launch launch = Launch());
    ~CinemaUpdate();

    // {"current", "latest", "available", "state": idle|checking|downloading|installing|failed, "error"}.
    Json status();
    void check();   // in the background
    bool install(std::string &error); // in the background; false when there is nothing to install
    void wait();    // tests

    static bool newer(const std::string &candidate, const std::string &current);
    static bool parseManifest(const std::string &body, Json &manifest);

  private:
    void run(std::function<void()> job);

    std::string root_;
    Fetch fetch_;
    Launch launch_;
    std::mutex mutex_;
    std::thread worker_;
    std::string state_ = "idle", error_;
    Json manifest_;
};

} // namespace daemon
} // namespace nativetv
