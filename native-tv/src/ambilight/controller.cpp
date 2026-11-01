#include "ambilight/controller.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sys/time.h>

namespace nativetv {
namespace ambilight {
namespace {
double wallClock() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int percent(const Json &value, int fallback) {
    if (!value.isNumber()) return fallback;
    return std::max(5, std::min(100, int(value.number())));
}

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string text(const Rgb &rgb) {
    return "[" + std::to_string(int(std::lround(rgb[0]))) + ", " + std::to_string(int(std::lround(rgb[1]))) +
           ", " + std::to_string(int(std::lround(rgb[2]))) + "]";
}
} // namespace

bool validAddress(const std::string &address) {
    if (address.size() != 17) return false;
    for (size_t i = 0; i < 17; ++i) {
        if (i % 3 == 2) {
            if (address[i] != ':') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(address[i]))) {
            return false;
        }
    }
    return true;
}

LedConfig cleanLedConfig(const Json &raw) {
    LedConfig c;
    if (!raw.isObject()) return c;
    const std::string address = raw["address"].isString() ? lower(raw["address"].string()) : "";
    if (validAddress(address)) c.address = address;
    if (raw["name"].isString()) c.name = raw["name"].string().substr(0, 40);
    c.follow = raw["follow"].isBoolean() && raw["follow"].boolean() && !c.address.empty();
    c.brightness = percent(raw["brightness"], 100);
    Rgb probe;
    const std::string ambient = raw["ambient"].isString() ? raw["ambient"].string() : "";
    c.ambient = ambient == "keep" || ambientColor(ambient, probe) ? ambient : "warm";
    c.ambientBrightness = percent(raw["ambientBrightness"], 60);
    return c;
}

Json ledConfigJson(const LedConfig &c) {
    Json out = Json::object();
    out.set("address", c.address.empty() ? Json() : Json::text(c.address));
    out.set("name", Json::text(c.name));
    out.set("follow", Json::flag(c.follow));
    out.set("brightness", Json::numeric(c.brightness));
    out.set("ambient", Json::text(c.ambient));
    out.set("ambientBrightness", Json::numeric(c.ambientBrightness));
    return out;
}

bool ambientLight(const LedConfig &config, Rgb &rgb) {
    if (config.ambient == "keep") return false;
    Rgb base;
    if (!ambientColor(config.ambient, base)) return false;
    rgb = scaled(base, config.ambientBrightness);
    return true;
}

Grabber::Grabber(std::function<bool(Frame &)> capture, bool threaded)
    : capture_(std::move(capture)), threaded_(threaded) {}

Grabber::~Grabber() {
    wanted_ = 0;
    if (thread_.joinable()) thread_.join();
}

bool Grabber::grab() {
    Frame f;
    if (!capture_(f)) {
        ++misses_;
        return false;
    }
    misses_ = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = std::move(f);
    have_ = true;
    ++seq_;
    return true;
}

void Grabber::run() {
    while (wallClock() - wanted_ < 2) {
        if (!grab()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    have_ = false;  // old by the time it is asked again
    running_ = false;
}

unsigned long Grabber::latest(Frame &frame, bool &have) {
    if (!threaded_) {
        grab();
    } else {
        wanted_ = wallClock();
        if (!running_) {
            if (thread_.joinable()) thread_.join();
            running_ = true;
            thread_ = std::thread(&Grabber::run, this);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    have = have_;
    if (have_) frame = frame_;
    return seq_;
}

Controller::Controller(ControllerDeps deps) : deps_(std::move(deps)) { config_ = deps_.config(); }

void Controller::log(const std::string &message) const {
    if (deps_.log) deps_.log(message);
}

StripLink *Controller::connectedStrip() {
    const std::string &address = config_.address;
    if (address.empty()) return nullptr;
    if (strip_ && strip_->address() != address) release();
    if (strip_ && strip_->connected()) return strip_.get();
    if (deps_.clock() < retryAt_) return nullptr;
    if (!strip_) strip_ = deps_.makeStrip(address);
    bool ok = strip_->connect();
    // A link left behind ("Already connected"), or LG's service hung (every
    // call answers "Message status unknown"): only a restart clears either.
    if (!ok) {
        std::string reason = lower(strip_->reason());
        if (reason.find("status unknown") != std::string::npos ||
            (deps_.staleLink && deps_.staleLink(address))) {
            if (deps_.resetBluetooth) deps_.resetBluetooth();
            ok = strip_->connect();
        }
    }
    if (!ok) {
        retries_ = std::min(retries_ + 1, 6);
        retryAt_ = deps_.clock() + 5 * retries_;
        return nullptr;
    }
    retries_ = 0;
    strip_->write(powerFrame(true));
    strip_->write(brightnessFrame(100));
    haveSent_ = false;
    mode_.clear();
    return strip_.get();
}

bool Controller::show(const Rgb &rgb, const std::string &mode) {
    StripLink *s = connectedStrip();
    if (!s) return false;
    const bool ok = s->write(colorFrame(rgb));
    if (ok) {
        sent_ = rgb;
        haveSent_ = true;
        mode_ = mode;
    }
    return ok;
}

bool Controller::showAmbient() {
    if (mode_ == "ambient") return true;
    Rgb rgb;
    if (!ambientLight(config_, rgb)) {
        mode_ = "ambient";
        log("ambilight: TV off, strip left as it is");
        return true;
    }
    if (show(rgb, "ambient")) {
        if (std::max(rgb[0], std::max(rgb[1], rgb[2])) < 1) strip_->write(powerFrame(false));
        log("ambilight: room light " + config_.ambient);
        return true;
    }
    return false;
}

void Controller::release() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (strip_ && strip_->connected()) {
        // After Prova the colour tried stays: it is the one chosen.
        if (mode_ != "preview") showAmbient();
        strip_->close();
        if (deps_.resetBluetooth) deps_.resetBluetooth();
        log("ambilight: strip released");
    } else if (strip_) {
        strip_->close();
    }
    strip_.reset();
    mode_.clear();
    haveSent_ = false;
}

void Controller::prepareOff(const std::string &kind) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    goingOff_ = deps_.clock();
    if (strip_ && strip_->connected()) {
        log("ambilight: " + kind + " announced");
        showAmbient();
    }
}

bool Controller::tvOn() {
    if (deps_.power() != "Active") {
        goingOff_ = 0;
        return false;
    }
    // Announced but still Active: off, unless LG changed its mind.
    return !(goingOff_ && deps_.clock() - goingOff_ < 20);
}

double Controller::step() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return stepLocked();
}

double Controller::stepLocked() {
    const double now = deps_.clock();
    LedConfig config = deps_.config();
    Rgb previewRgb;
    std::string previewAddress;
    const bool preview = deps_.takePreview && deps_.takePreview(previewRgb, previewAddress);
    if (preview) {
        previewAddress_ = validAddress(previewAddress) ? previewAddress : config.address;
        previewUntil_ = now + kPreviewSeconds;
    }
    if (!previewAddress_.empty() && now < previewUntil_) config.address = previewAddress_;
    config_ = config;
    if (preview) {
        const bool ok = show(previewRgb, "preview");
        log("ambilight: Prova " + text(previewRgb) + (ok ? " shown" : " not shown"));
        return 0.2;
    }
    if (mode_ == "preview" && now < previewUntil_) return 0.2;
    if (!config_.follow) return -1;  // the caller lets the strip go and stops
    if (!tvOn()) {
        if (strip_ && strip_->connected()) showAmbient();
        smooth_.reset();
        haveTarget_ = false;
        seen_ = 0;
        return 1.0;
    }
    StripLink *s = connectedStrip();
    if (!s) return 1.0;
    const double t0 = deps_.clock();
    Frame frame;
    bool have = false;
    const unsigned long seq = deps_.grabber->latest(frame, have);
    if (deps_.grabber->misses() >= 20 && !unreadable_) {
        unreadable_ = true;
        log("ambilight: the screen cannot be read (protected video?)");
    }
    if (have) {
        unreadable_ = false;
        if (seq != seen_) {
            seen_ = seq;
            target_ = scaled(boost(screenColor(frame)), config_.brightness);
            haveTarget_ = true;
        }
    }
    if (!haveTarget_) return 0.1;
    // Between two captures the strip keeps fading towards the last one.
    const double dt = lastTick_ ? std::min(0.5, t0 - lastTick_) : kTick;
    lastTick_ = t0;
    const Rgb color = smooth_.step(target_, dt);
    if (mode_ != "follow" || changed(haveSent_ ? &sent_ : nullptr, color) ||
        deps_.clock() - s->lastWrite() > 2)
        show(color, "follow");
    return std::max(0.0, kTick - (deps_.clock() - t0));
}

} // namespace ambilight
} // namespace nativetv
