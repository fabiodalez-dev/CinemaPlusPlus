#include "daemon/memory_keeper.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
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

std::string read(const std::string &path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }
} // namespace

int main() {
    char pattern[] = "/tmp/memory-keeper-XXXXXX";
    const std::string root = mkdtemp(pattern);
    mkdir((root + "/etc").c_str(), 0755);
    mkdir((root + "/etc/palm").c_str(), 0755);
    mkdir((root + "/home").c_str(), 0755);
    mkdir((root + "/home/root").c_str(), 0755);
    { std::ofstream(root + "/etc/palm/memorymanager-conf.json") << "{\"KeepOnLaunchEx\":[\"com.webos.app.home\"]}"; }
    const std::string override = root + "/home/root/memorymanager-conf.json";
    const std::string mark = root + "/home/root/.cinema-memorymanager";

    Json keep = Json::array();
    keep.add(Json::text("org.bloomee.webos"));
    std::string logged;
    applyMemoryKeep(root, keep, [&logged](const std::string &m) { logged = m; });
    check(exists(override) && exists(mark), "override and mark written");
    const Json written = Json::parse(read(override));
    check(written["KeepOnLaunchEx"].size() == 2 &&
          written["KeepOnLaunchEx"][1].string() == "org.bloomee.webos", "Bloomee added after LG's app");
    check(logged.find("org.bloomee.webos") != std::string::npos, "the change is logged");

    logged.clear();
    applyMemoryKeep(root, keep, [&logged](const std::string &m) { logged = m; });
    check(logged.empty(), "nothing done when already as wanted");

    applyMemoryKeep(root, Json::array(), nullptr);
    check(!exists(override) && !exists(mark), "an empty list removes ours");

    { std::ofstream(override) << "{}"; }
    applyMemoryKeep(root, keep, nullptr);
    check(read(override).find("bloomee") == std::string::npos, "someone else's override left alone");

    std::cout << "memory keeper tests passed\n";
    return 0;
}
