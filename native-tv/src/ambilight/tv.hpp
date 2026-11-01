#pragma once

#include "ambilight/bus.hpp"
#include "ambilight/controller.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace nativetv {
namespace ambilight {

// The strip through LG's Bluetooth service (com.webos.service.bluetooth2):
// the GATT client lives as long as the gatt/connect subscription, and writes
// name that client. LG's service connects only to a device it has seen in a
// scan, so a scan runs until then. A strip takes one connection at a time.
// Protocol from Rxflex/LotusLantern and dave-code-ruiz/elkbledom (MIT).
class BluetoothStrip : public StripLink {
  public:
    BluetoothStrip(NodeBus &bus, std::string address, std::function<void(const std::string &)> log);
    ~BluetoothStrip() override;
    const std::string &address() const override { return address_; }
    bool connected() const override { return connected_; }
    std::string reason() const override;
    bool connect() override;
    bool write(const std::vector<int> &frame) override;
    void close() override;
    double lastWrite() const override { return lastWrite_; }

  private:
    NodeBus &bus_;
    std::string address_;
    std::function<void(const std::string &)> log_;
    mutable std::mutex mutex_;
    std::string client_;
    std::string reason_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> ended_{false};
    int sub_ = 0;
    double lastWrite_ = 0;
    int failures_ = 0;
};

// Strips in range for `seconds`, strongest first, as [{address, name, rssi}].
Json scanStrips(NodeBus &bus, double seconds);
// LG's service keeps the link of a client whose subscription died:
// connect then answers "Already connected" for good.
bool staleLink(NodeBus &bus, const std::string &address);
// At most once every two minutes; /sbin/restart by full path.
bool resetBluetooth(const std::function<void(const std::string &)> &log);
// The screen at 240x136 through LG's capture service, video included.
bool captureScreen(NodeBus &bus, const std::string &framePath, Frame &frame);
double now();

} // namespace ambilight
} // namespace nativetv
