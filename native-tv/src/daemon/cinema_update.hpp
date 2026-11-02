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

    // The other apps Cinema updated, Bloomee and Cinema web, from their own GitHub releases:
    // installed and latest version, the release's IPK and its SHA-256 from SHA256SUMS.txt, for
    // the service's own install from a URL. Checked in the background.
    void setAppsDir(std::string appsDir) { appsDir_ = std::move(appsDir); }
    // {"state": idle|checking, "apps": [{"id", "name", "installed", "latest", "available", "ipkUrl",
    // "sha256", "notes", "error"}]}.
    Json appsStatus();
    void checkApps();
    // The newest published release of `releases` (GitHub's list) whose tag is tagPrefix and a
    // version and that ships <appId>_<version>_all.ipk and SHA256SUMS.txt.
    static bool pickRelease(const Json &releases, const std::string &appId, const std::string &tagPrefix,
                            Json &release);
    static std::string sumFor(const std::string &sums, const std::string &name);

  private:
    void run(std::function<void()> job);

    std::string root_;
    Fetch fetch_;
    Launch launch_;
    std::mutex mutex_;
    std::thread worker_;
    std::string state_ = "idle", error_;
    Json manifest_;
    std::string appsDir_ = "/media/developer/apps/usr/palm/applications";
    std::thread appsWorker_;
    std::string appsState_ = "idle";
    Json apps_;
};

} // namespace daemon
} // namespace nativetv
