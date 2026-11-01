// nativetv-ambilight: the LED strip behind the TV follows the colours on
// screen. A process of its own, started by the daemon when the strip is set
// to follow, and by the dashboard's Prova.
//
//   nativetv-ambilight daemon             hold the strip and follow the screen
//   nativetv-ambilight scan [SECONDS]     strips in range, as JSON
//   nativetv-ambilight preview ADDR R G B show a colour on that strip
//   nativetv-ambilight status             running or not, and the settings
//
// Settings live in config.json under "ledStrip": {"address", "name",
// "follow", "brightness", "ambient", "ambientBrightness"}.
#include "ambilight/tv.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace nativetv;
using namespace nativetv::ambilight;

namespace {
std::string env(const char *name, const std::string &fallback) {
    const char *v = std::getenv(name);
    return v && *v ? v : fallback;
}

const std::string kData = env("TVWEB_DATA_DIR", "/var/lib/tvweb");
const std::string kConfig = env("TVWEB_CONFIG", kData + "/config.json");
const std::string kRun = env("NATIVETV_AMBILIGHT_RUN", "/run/nativetv-ambilight");
const std::string kPid = kRun + "/ambilight.pid";
const std::string kPreview = kRun + "/preview.json";
const std::string kFrame = kRun + "/frame.bmp";
// On flash, beside the config: /run is emptied when the TV restarts, and what
// went wrong before a restart is what is needed. Few lines a day.
const std::string kLog = env("NATIVETV_AMBILIGHT_LOG", kData + "/ambilight.log");

std::atomic<bool> stopping{false};

void runDirectory() {
    mkdir(kRun.c_str(), 0700);
    chmod(kRun.c_str(), 0700);
}

std::string readText(const std::string &path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

void fileLog(const std::string &message) {
    struct stat st;
    if (stat(kLog.c_str(), &st) == 0 && st.st_size > 32768) {
        std::vector<std::string> lines;
        std::ifstream in(kLog);
        for (std::string line; std::getline(in, line);) lines.push_back(line);
        std::ofstream out(kLog, std::ios::trunc);
        for (size_t i = lines.size() > 100 ? lines.size() - 100 : 0; i < lines.size(); ++i) out << lines[i] << '\n';
    }
    const int fd = open(kLog.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    char stamp[32];
    const time_t t = time(nullptr);
    strftime(stamp, sizeof(stamp), "%d/%m %H:%M:%S ", localtime(&t));
    const std::string line = stamp + message + "\n";
    (void)!::write(fd, line.data(), line.size());
    close(fd);
}

LedConfig loadConfig() {
    try {
        return cleanLedConfig(Json::parse(readText(kConfig))["ledStrip"]);
    } catch (...) {
        return LedConfig();
    }
}

std::string palmbus() {
    for (const char *p : {"/usr/lib/nodejs/palmbus.js", "/usr/palm/nodejs/palmbus.js"})
        if (access(p, R_OK) == 0) return p;
    return "/usr/lib/nodejs/palmbus.js";
}

int running() {
    const int pid = std::atoi(readText(kPid).c_str());
    if (pid <= 0) return 0;
    return readText("/proc/" + std::to_string(pid) + "/cmdline").find("ambilight") != std::string::npos ? pid : 0;
}

bool takePreview(Rgb &rgb, std::string &address) {
    std::string text = readText(kPreview);
    if (text.empty()) return false;
    std::remove(kPreview.c_str());
    try {
        const Json p = Json::parse(text);
        if (!p["rgb"].isArray() || p["rgb"].size() < 3) return false;
        for (int i = 0; i < 3; ++i) rgb[i] = std::max(0.0, std::min(255.0, p["rgb"][size_t(i)].number()));
        address = p["address"].isString() ? p["address"].string() : "";
        for (char &c : address) c = char(std::tolower(static_cast<unsigned char>(c)));
        return true;
    } catch (...) {
        return false;
    }
}

int daemon() {
    if (running()) return 0;
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 0);
    runDirectory();
    // Two starts close together: the lock lets one through, held to the end.
    const int lock = open((kPid + ".lock").c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) return 0;
    {
        std::ofstream pid(kPid, std::ios::trunc);
        pid << getpid();
    }
    std::signal(SIGTERM, [](int) { stopping = true; });
    std::signal(SIGPIPE, SIG_IGN);

    NodeBus bus(kRun, palmbus());
    std::atomic<int> power{1};  // 1 Active, 0 anything else
    std::mutex announcedMutex;
    std::string announced;
    Json powerPayload = Json::object();
    powerPayload.set("subscribe", Json::flag(true));
    const auto onPower = [&](const Json &m) {
        if (m["state"].isString()) power = m["state"].string() == "Active" ? 1 : 0;
        // "Request Power Off" comes with the state still Active, about 2.5 s
        // before standby. (Registering for LG's prepare steps instead turns
        // every standby into a full restart.)
        if (m["onOff"].isString() && m["onOff"].string() == "off" && m["processing"].isString()) {
            std::lock_guard<std::mutex> l(announcedMutex);
            announced = m["processing"].string();
        }
    };
    std::atomic<int> powerSub{0};
    std::function<void()> subscribePower;
    subscribePower = [&]() {
        powerSub = bus.subscribe("luna://com.webos.service.tvpower/power/getPowerState", powerPayload, onPower,
                                 [&]() { if (!stopping) fileLog("ambilight: power subscription ended"); powerSub = 0; });
    };
    subscribePower();

    ControllerDeps deps;
    deps.config = loadConfig;
    deps.power = [&power]() { return std::string(power ? "Active" : "Off"); };
    deps.clock = now;
    deps.makeStrip = [&bus](const std::string &address) {
        return std::unique_ptr<StripLink>(new BluetoothStrip(bus, address, fileLog));
    };
    deps.grabber = std::make_shared<Grabber>([&bus](Frame &f) { return captureScreen(bus, kFrame, f); }, true);
    deps.resetBluetooth = []() { return resetBluetooth(fileLog); };
    deps.staleLink = [&bus](const std::string &address) { return staleLink(bus, address); };
    deps.takePreview = takePreview;
    deps.log = fileLog;
    Controller a(deps);

    // LG's announcements are handled here, never on the bus's own thread: the
    // loop may be waiting there for a reply.
    std::thread watch([&]() {
        while (!stopping) {
            std::string kind;
            {
                std::lock_guard<std::mutex> l(announcedMutex);
                kind.swap(announced);
            }
            if (!kind.empty()) {
                fileLog("ambilight: LG: " + kind);
                a.prepareOff(kind);
            }
            if (!powerSub && !stopping) subscribePower();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    fileLog("ambilight: start, strip " + (loadConfig().address.empty() ? std::string("none") : loadConfig().address));
    while (!stopping) {
        const double wait = a.step();
        if (wait < 0) break;
        if (wait > 0) std::this_thread::sleep_for(std::chrono::milliseconds(long(wait * 1000)));
    }
    stopping = true;
    a.release();
    watch.join();
    std::remove(kPid.c_str());
    fileLog("ambilight: stop");
    return 0;
}

int preview(const std::string &address, int r, int g, int b, const char *self) {
    LedConfig config = loadConfig();
    std::string chosen = address;
    for (char &c : chosen) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (!validAddress(chosen)) chosen = config.address;
    if (chosen.empty()) {
        std::cout << "{\"ok\":false,\"error\":\"no strip chosen\"}\n";
        return 0;
    }
    runDirectory();
    Json p = Json::object();
    Json rgb = Json::array();
    for (int v : {r, g, b}) rgb.add(Json::numeric(std::max(0, std::min(255, v))));
    p.set("rgb", rgb);
    p.set("address", Json::text(chosen));
    {
        std::ofstream out(kPreview + ".tmp", std::ios::trunc);
        out << p.dump(false);
    }
    std::rename((kPreview + ".tmp").c_str(), kPreview.c_str());
    // Started for the purpose when "follow" is off: it shows the colour for a
    // few seconds, leaves it on and lets the strip go.
    if (!running()) {
        const pid_t pid = fork();
        if (pid == 0) {
            setsid();
            if (fork() == 0) {
                const int devnull = open("/dev/null", O_RDWR);
                dup2(devnull, 0);
                dup2(devnull, 1);
                dup2(devnull, 2);
                for (int fd = 3; fd < 256; ++fd) close(fd);
                execl(self, self, "daemon", static_cast<char *>(nullptr));
            }
            _exit(0);
        }
        int status;
        waitpid(pid, &status, 0);
    }
    std::cout << "{\"ok\":true}\n";
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    self[n > 0 ? n : 0] = 0;
    if (cmd == "daemon") return daemon();
    if (cmd == "scan") {
        runDirectory();
        NodeBus bus(kRun, palmbus());
        std::cout << scanStrips(bus, argc > 2 ? std::atof(argv[2]) : 10).dump(false) << "\n";
        return 0;
    }
    if (cmd == "preview" && argc >= 6)
        return preview(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), self);
    if (cmd == "ambient-preview") {
        Rgb rgb;
        if (!ambientLight(loadConfig(), rgb)) {
            std::cout << "{\"ok\":true,\"via\":\"none\"}\n";
            return 0;
        }
        return preview("", int(rgb[0]), int(rgb[1]), int(rgb[2]), self);
    }
    if (cmd == "status") {
        Json out = Json::object();
        out.set("running", Json::flag(running() != 0));
        out.set("conf", ledConfigJson(loadConfig()));
        std::cout << out.dump(false) << "\n";
        return 0;
    }
    std::cerr << "usage: nativetv-ambilight daemon|scan [s]|preview ADDR R G B|ambient-preview|status\n";
    return 2;
}
