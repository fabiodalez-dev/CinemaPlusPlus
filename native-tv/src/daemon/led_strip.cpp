#include "daemon/led_strip.hpp"

#include "ambilight/controller.hpp"
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace nativetv {
namespace daemon {
namespace {
std::string readText(const std::string &path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

Json configLedStrip(const std::string &configPath) {
    try {
        return Json::parse(readText(configPath))["ledStrip"];
    } catch (...) {
        return Json();
    }
}
} // namespace

std::string ambilightBinary(const std::string &dataRoot) { return dataRoot + "/native/nativetv-ambilight"; }

bool ambilightRunning() {
    const int pid = std::atoi(readText("/run/nativetv-ambilight/ambilight.pid").c_str());
    if (pid <= 0) return false;
    return readText("/proc/" + std::to_string(pid) + "/cmdline").find("ambilight") != std::string::npos;
}

std::vector<std::string> ambilightEnvironment(const std::string &dataRoot) {
    return {"LD_LIBRARY_PATH=" + dataRoot + "/native", "PATH=/usr/sbin:/usr/bin:/sbin:/bin",
            "TVWEB_DATA_DIR=" + dataRoot};
}

std::vector<std::string> ambilightCommand(const std::string &dataRoot,
                                          const std::vector<std::string> &arguments) {
    std::vector<std::string> command = {ambilightBinary(dataRoot)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}

bool ensureAmbilight(const std::string &dataRoot, const std::string &configPath,
                     const std::function<void(const std::string &)> &log) {
    const ambilight::LedConfig config = ambilight::cleanLedConfig(configLedStrip(configPath));
    if (!config.follow || ambilightRunning()) return false;
    const std::string binary = ambilightBinary(dataRoot);
    if (access(binary.c_str(), X_OK) != 0) return false;
    const std::vector<std::string> environment = ambilightEnvironment(dataRoot);
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            const int devnull = open("/dev/null", O_RDWR);
            dup2(devnull, 0);
            dup2(devnull, 1);
            dup2(devnull, 2);
            for (int fd = 3; fd < 1024; ++fd) close(fd);
            std::vector<char *> envp;
            for (const std::string &e : environment) envp.push_back(const_cast<char *>(e.c_str()));
            envp.push_back(nullptr);
            execle(binary.c_str(), binary.c_str(), "daemon", static_cast<char *>(nullptr), envp.data());
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (log) log("ledstrip: controller started for " + config.address);
    return true;
}

Json ledStripStatus(const std::string &dataRoot, const std::string &configPath) {
    Json out = Json::object();
    out.set("conf", ambilight::ledConfigJson(ambilight::cleanLedConfig(configLedStrip(configPath))));
    out.set("running", Json::flag(ambilightRunning()));
    out.set("available", Json::flag(access(ambilightBinary(dataRoot).c_str(), X_OK) == 0));
    std::vector<std::string> lines;
    std::istringstream in(readText(dataRoot + "/ambilight.log"));
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    Json log = Json::array();
    for (size_t i = lines.size() > 12 ? lines.size() - 12 : 0; i < lines.size(); ++i) log.add(Json::text(lines[i]));
    out.set("log", log);
    return out;
}

} // namespace daemon
} // namespace nativetv
