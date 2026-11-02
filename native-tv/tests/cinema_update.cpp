// Cinema++'s own updates, with a stand-in GitHub.
#include "daemon/cinema_update.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace nativetv;
using namespace nativetv::daemon;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    check(CinemaUpdate::newer("0.2.0", "0.1.9") && CinemaUpdate::newer("1.0.0", "0.9.9") &&
              !CinemaUpdate::newer("0.1.0", "0.1.0") && !CinemaUpdate::newer("0.0.9", "0.1.0") &&
              CinemaUpdate::newer("0.1.0", ""),
          "versions compared");
    Json manifest;
    const std::string sha(64, 'a');
    check(CinemaUpdate::parseManifest(R"({"id":"cinemapp","version":"0.2.0","bundle":"cinemapp-0.2.0.tar","sha256":")" +
                                          sha + "\"}",
                                      manifest),
          "a good manifest");
    check(!CinemaUpdate::parseManifest(R"({"id":"cinemapp","version":"0.2.0","bundle":"../x.tar","sha256":")" + sha +
                                           "\"}",
                                       manifest),
          "a bundle name that is not its own");
    check(!CinemaUpdate::parseManifest(R"({"id":"other","version":"0.2.0","bundle":"cinemapp-0.2.0.tar","sha256":"zz"})",
                                       manifest),
          "another app or a bad hash");

    char pattern[] = "/tmp/cinema-update-XXXXXX";
    const std::string root = mkdtemp(pattern);
    std::ofstream(root + "/CINEMAPP_VERSION") << "0.1.0\n";
    // The package: a tar holding bundle.tar and install.sh; its real SHA-256 goes in the manifest.
    const std::string make = "cd '" + root + "' && mkdir -p pkg && echo b > pkg/bundle.tar && echo 'echo ok' > pkg/install.sh"
                             " && tar -cf package.tar -C pkg bundle.tar install.sh";
    check(std::system(make.c_str()) == 0, "package made");
    std::string hash;
    {
        FILE *pipe = popen(("shasum -a 256 '" + root + "/package.tar' | cut -d' ' -f1").c_str(), "r");
        char line[100] = {0};
        if (pipe && fgets(line, sizeof line, pipe)) hash = std::string(line).substr(0, 64);
        if (pipe) pclose(pipe);
    }
    std::string launched;
    bool tamper = false;
    CinemaUpdate update(
        root,
        [&](const std::string &url, std::string &body, const std::string &toFile, size_t, std::string &) {
            if (url.find("manifest.json") != std::string::npos) {
                body = R"({"id":"cinemapp","version":"0.2.0","bundle":"cinemapp-0.2.0.tar","sha256":")" + hash + "\"}";
                return true;
            }
            check(url.find("download/v0.2.0/cinemapp-0.2.0.tar") != std::string::npos, "the tagged package");
            const std::string copy = "cp '" + root + "/package.tar' '" + toFile + "'" +
                                     (tamper ? " && echo x >> '" + toFile + "'" : std::string());
            return std::system(copy.c_str()) == 0;
        },
        [&](const std::string &command) {
            launched = command;
            return true;
        });
    std::string error;
    check(!update.install(error), "nothing to install before a check");
    update.check();
    update.wait();
    Json status = update.status();
    check(status["latest"].string() == "0.2.0" && status["available"].boolean(), "0.2.0 offered");

    tamper = true;
    check(update.install(error), "install started");
    update.wait();
    check(update.status()["state"].string() == "failed" && launched.empty(), "a damaged download refused");

    tamper = false;
    check(update.install(error), "install again");
    update.wait();
    check(update.status()["state"].string() == "installing" && launched.find("install.sh") != std::string::npos,
          "checked, unpacked and started");
    (void)std::system(("rm -rf '" + root + "'").c_str());
    std::cout << "cinema update tests passed\n";
    return 0;
}
