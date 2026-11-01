#include "ambilight/tv.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sys/time.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace nativetv {
namespace ambilight {
namespace {
const char *kBt = "luna://com.webos.service.bluetooth2";
const char *kService = "0000fff0-0000-1000-8000-00805f9b34fb";
const char *kWriteChar = "0000fff3-0000-1000-8000-00805f9b34fb";
constexpr double kMinGap = 0.06;  // never two writes closer: the strip drops them

void sleepFor(double seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(long(seconds * 1000)));
}

Json subscribePayload() {
    Json p = Json::object();
    p.set("subscribe", Json::flag(true));
    return p;
}
} // namespace

double now() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

BluetoothStrip::BluetoothStrip(NodeBus &bus, std::string address,
                               std::function<void(const std::string &)> log)
    : bus_(bus), address_(std::move(address)), log_(std::move(log)) {}

BluetoothStrip::~BluetoothStrip() { close(); }

std::string BluetoothStrip::reason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reason_;
}

bool BluetoothStrip::connect() {
    close();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reason_.clear();
    }
    const int scanner = bus_.subscribe(std::string(kBt) + "/le/startScan", subscribePayload(),
                                       [](const Json &) {}, nullptr);
    sleepFor(1.5);
    Json payload = subscribePayload();
    payload.set("address", Json::text(address_));
    ended_ = false;
    sub_ = bus_.subscribe(
        std::string(kBt) + "/gatt/connect", payload,
        [this](const Json &m) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (m["clientId"].isString() && !m["clientId"].string().empty()) {
                client_ = m["clientId"].string();
                connected_ = true;
            }
            if ((m["returnValue"].isBoolean() && !m["returnValue"].boolean()) ||
                (m["connected"].isBoolean() && !m["connected"].boolean())) {
                connected_ = false;
                reason_ = m["errorText"].isString() ? m["errorText"].string() : "disconnected";
            }
        },
        [this]() {
            connected_ = false;
            ended_ = true;
        });
    const double end = now() + 12;
    while (now() < end && !connected_ && sub_ && !ended_ && reason().empty()) sleepFor(0.1);
    if (scanner) bus_.cancel(scanner);
    if (!connected_) {
        if (log_) log_("ambilight: no connection to " + address_ + " (" +
                       (reason().empty() ? std::string("timeout") : reason()) + ")");
        if (sub_) bus_.cancel(sub_);
        sub_ = 0;
        return false;
    }
    sleepFor(1.0);  // the controller wants a moment before the first write
    failures_ = 0;
    if (log_) log_("ambilight: connected to " + address_);
    return true;
}

bool BluetoothStrip::write(const std::vector<int> &frame) {
    std::string client;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client = client_;
    }
    if (!connected_ || client.empty()) return false;
    const double gap = kMinGap - (now() - lastWrite_);
    if (gap > 0) sleepFor(gap);
    Json bytes = Json::array();
    for (int b : frame) bytes.add(Json::numeric(b));
    Json value = Json::object();
    value.set("bytes", bytes);
    Json payload = Json::object();
    payload.set("clientId", Json::text(client));
    payload.set("service", Json::text(kService));
    payload.set("characteristic", Json::text(kWriteChar));
    payload.set("value", value);
    const Json r = bus_.call(std::string(kBt) + "/gatt/writeCharacteristicValue", payload, 3000);
    lastWrite_ = now();
    if (r["returnValue"].boolean()) {
        failures_ = 0;
        return true;
    }
    if (++failures_ >= 5) {
        if (log_) log_("ambilight: writes failing (" +
                       (r["errorText"].isString() ? r["errorText"].string() : std::string("no reply")) +
                       "), reconnecting");
        connected_ = false;
    }
    return false;
}

void BluetoothStrip::close() {
    std::string client;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client = client_;
        client_.clear();
    }
    if (!client.empty() && connected_) {
        Json payload = Json::object();
        payload.set("clientId", Json::text(client));
        (void)bus_.call(std::string(kBt) + "/gatt/disconnect", payload, 4000);
    }
    if (sub_) bus_.cancel(sub_);
    sub_ = 0;
    connected_ = false;
}

Json scanStrips(NodeBus &bus, double seconds) {
    struct Found {
        std::string name;
        double rssi = -127;
    };
    std::mutex mutex;
    std::map<std::string, Found> found;
    const int id = bus.subscribe(std::string(kBt) + "/le/startScan", subscribePayload(),
        [&mutex, &found](const Json &m) {
            if (!m["devices"].isArray()) return;
            std::lock_guard<std::mutex> lock(mutex);
            for (const Json &d : m["devices"].items()) {
                if (!d["address"].isString()) continue;
                const std::string address = d["address"].string();
                std::vector<int> record;
                if (d["scanRecord"].isArray())
                    for (const Json &b : d["scanRecord"].items()) record.push_back(int(b.number()));
                std::string name = stripName(record);
                if (name.empty() && d["name"].isString()) name = d["name"].string();
                if (!isStrip(name) && !found.count(address)) continue;
                Found &f = found[address];
                if (!name.empty()) f.name = name;
                if (d["rssi"].isNumber()) f.rssi = std::max(f.rssi, d["rssi"].number());
            }
        },
        nullptr);
    sleepFor(seconds);
    if (id) bus.cancel(id);
    std::vector<std::pair<std::string, Found>> list;
    {
        std::lock_guard<std::mutex> lock(mutex);
        list.assign(found.begin(), found.end());
    }
    std::sort(list.begin(), list.end(),
              [](const std::pair<std::string, Found> &a, const std::pair<std::string, Found> &b) {
                  return a.second.rssi > b.second.rssi;
              });
    Json out = Json::array();
    for (const auto &entry : list) {
        Json d = Json::object();
        d.set("address", Json::text(entry.first));
        d.set("name", Json::text(entry.second.name));
        d.set("rssi", Json::numeric(entry.second.rssi));
        out.add(d);
    }
    return out;
}

bool staleLink(NodeBus &bus, const std::string &address) {
    Json payload = Json::object();
    payload.set("address", Json::text(address));
    const Json r = bus.call(std::string(kBt) + "/gatt/getStatus", payload, 4000);
    return r["connected"].isBoolean() && r["connected"].boolean();
}

bool resetBluetooth(const std::function<void(const std::string &)> &log) {
    static double last = 0;
    if (last && now() - last < 120) return false;
    last = now();
    const pid_t pid = fork();
    if (pid == 0) {
        for (int fd = 3; fd < 256; ++fd) ::close(fd);
        execl("/sbin/restart", "restart", "webos-bluetooth-service", static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) == 127) {
        if (log) log("ambilight: could not restart Bluetooth");
        return false;
    }
    if (log) log("ambilight: Bluetooth service restarted to clear a stale connection");
    sleepFor(8);
    return true;
}

bool captureScreen(NodeBus &bus, const std::string &framePath, Frame &frame) {
    // The capture service runs as root and follows the path it is given: the
    // frame goes in our root-only folder, never at a fixed name under /tmp.
    std::remove(framePath.c_str());
    Json payload = Json::object();
    payload.set("path", Json::text(framePath));
    payload.set("method", Json::text("SCREEN"));
    payload.set("width", Json::numeric(kWidth));
    payload.set("height", Json::numeric(kHeight));
    payload.set("format", Json::text("BMP"));
    const Json r = bus.call("luna://com.webos.service.capture/executeOneShot", payload, 3000);
    if (!r["returnValue"].boolean()) return false;
    std::ifstream in(framePath, std::ios::binary);
    if (!in) return false;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseBmp(data, frame);
}

} // namespace ambilight
} // namespace nativetv
