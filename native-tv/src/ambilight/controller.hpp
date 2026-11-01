#pragma once

#include "ambilight/colors.hpp"
#include "core/json.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nativetv {
namespace ambilight {

// The "ledStrip" part of the config, every field checked.
struct LedConfig {
    std::string address;  // lower case aa:bb:cc:dd:ee:ff, or empty
    std::string name;
    bool follow = false;  // follow the screen while the TV is on
    int brightness = 100;
    std::string ambient = "warm";  // a room light name, "off" or "keep"
    int ambientBrightness = 60;
};

bool validAddress(const std::string &address);
LedConfig cleanLedConfig(const Json &raw);
Json ledConfigJson(const LedConfig &config);
// The room light for the TV off; false to leave the strip as it is ("keep").
bool ambientLight(const LedConfig &config, Rgb &rgb);

// One connection to one strip.
class StripLink {
  public:
    virtual ~StripLink() = default;
    virtual const std::string &address() const = 0;
    virtual bool connected() const = 0;
    virtual std::string reason() const = 0;
    virtual bool connect() = 0;
    virtual bool write(const std::vector<int> &frame) = 0;
    virtual void close() = 0;
    virtual double lastWrite() const = 0;
};

// The screen, captured in a thread of its own: with a video playing a
// capture takes about 0.45 s, so the strip fades towards the last capture
// instead of waiting for the next. The thread stops when nobody asks for two
// seconds.
class Grabber {
  public:
    Grabber(std::function<bool(Frame &)> capture, bool threaded);
    ~Grabber();
    // The number of the capture and whether there is a frame.
    unsigned long latest(Frame &frame, bool &have);
    int misses() const { return misses_; }

  private:
    bool grab();
    void run();

    std::function<bool(Frame &)> capture_;
    bool threaded_;
    std::mutex mutex_;
    Frame frame_;
    bool have_ = false;
    unsigned long seq_ = 0;
    std::atomic<int> misses_{0};
    std::atomic<double> wanted_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
};

struct ControllerDeps {
    std::function<LedConfig()> config;
    std::function<std::string()> power;  // LG's power state: "Active" is on
    std::function<double()> clock;       // seconds
    std::function<std::unique_ptr<StripLink>(const std::string &address)> makeStrip;
    std::shared_ptr<Grabber> grabber;
    std::function<bool()> resetBluetooth;
    std::function<bool(const std::string &address)> staleLink;
    // A colour asked by Prova, consumed: the colour and the strip chosen on
    // screen (maybe not saved).
    std::function<bool(Rgb &rgb, std::string &address)> takePreview;
    std::function<void(const std::string &)> log;
};

// One process holds the strip while the TV is on. With the TV on and
// "follow", the strip follows the screen; when the TV goes off (or only the
// screen) it gets the room light; when "follow" is switched off it gets the
// room light and is let go. LG's gatt/disconnect keeps the link, so letting
// go means restarting LG's Bluetooth service: the connection is kept while
// the TV is on, and settings are read again while running.
class Controller {
  public:
    static constexpr double kTick = 0.05;
    static constexpr double kPreviewSeconds = 6;

    explicit Controller(ControllerDeps deps);
    // What to show now; the seconds to wait, or a negative value to stop.
    double step();
    // LG announced it is going off: its radio goes about two seconds later.
    void prepareOff(const std::string &kind);
    // Room light, then the strip let go.
    void release();
    const std::string &mode() const { return mode_; }
    StripLink *strip() { return strip_.get(); }

  private:
    double stepLocked();
    StripLink *connectedStrip();
    bool show(const Rgb &rgb, const std::string &mode);
    bool showAmbient();
    bool tvOn();
    void log(const std::string &message) const;

    ControllerDeps deps_;
    std::recursive_mutex mutex_;
    LedConfig config_;
    std::unique_ptr<StripLink> strip_;
    Smoother smooth_;
    unsigned long seen_ = 0;
    bool haveTarget_ = false;
    Rgb target_{{0, 0, 0}};
    double lastTick_ = 0;
    bool unreadable_ = false;
    bool haveSent_ = false;
    Rgb sent_{{0, 0, 0}};
    std::string mode_;
    double previewUntil_ = 0;
    std::string previewAddress_;
    double retryAt_ = 0;
    int retries_ = 0;
    double goingOff_ = 0;
};

} // namespace ambilight
} // namespace nativetv
