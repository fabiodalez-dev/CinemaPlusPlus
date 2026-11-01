#include "daemon/live_tv.hpp"
#include <cctype>
#include <sys/stat.h>

namespace nativetv {
namespace daemon {
namespace {
constexpr const char *kBroadcast = "luna://com.webos.service.utp.broadcast/";
constexpr const char *kDefaultApp = "com.tvweb.dashboard";
constexpr int kTimeoutMs = 8000;
} // namespace

bool validWindowId(const std::string &id) {
    if (id.size() < 12 || id.size() > 40 || id.compare(0, 11, "_Window_Id_") != 0) return false;
    for (size_t i = 11; i < id.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(id[i]))) return false;
    return true;
}

bool validChannelId(const std::string &id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id)
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') return false;
    return true;
}

LiveTv::LiveTv(std::string runDirectory, Call call)
    : runDirectory_(std::move(runDirectory)), call_(std::move(call)) {
    if (!call_)
        call_ = [this](const std::string &uri, const Json &payload) {
            if (!bus_) {
                (void)mkdir(runDirectory_.c_str(), 0700);
                bus_.reset(new ambilight::NodeBus(runDirectory_, "/usr/lib/nodejs/palmbus.js"));
            }
            return bus_->call(uri, payload, kTimeoutMs);
        };
}

Json LiveTv::tune(const std::string &channelId) {
    Json payload = Json::object();
    payload.set("broadcastId", Json::text(broadcastId_));
    payload.set("channelId", Json::text(channelId));
    const Json reply = call_(std::string(kBroadcast) + "changeChannel", payload);
    if (reply["returnValue"].boolean()) channelId_ = channelId;
    return reply;
}

Json LiveTv::closeLocked() {
    if (!broadcastId_.empty()) {
        Json payload = Json::object();
        payload.set("broadcastId", Json::text(broadcastId_));
        (void)call_(std::string(kBroadcast) + "close", payload);
    }
    broadcastId_.clear();
    windowId_.clear();
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("playing", Json::flag(false));
    return result;
}

Json LiveTv::act(const Json &request) {
    std::lock_guard<std::mutex> lock(mutex_);
    Json result = Json::object();
    const std::string action = request["action"].string();
    const auto fail = [&result](const std::string &error) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text(error));
        return result;
    };
    if (action == "close") return closeLocked();
    if (action == "open") {
        const std::string window = request["windowId"].string();
        if (!validWindowId(window)) return fail("windowId expected");
        const std::string app = request["appId"].string(kDefaultApp);
        if (!validChannelId(app)) return fail("appId expected");
        // A window already showing the broadcast keeps it; another one gets its own.
        if (!broadcastId_.empty() && window != windowId_) (void)closeLocked();
        if (broadcastId_.empty()) {
            Json options = Json::object();
            options.set("appId", Json::text(app));
            options.set("windowId", Json::text(window));
            Json payload = Json::object();
            payload.set("options", options);
            const Json opened = call_(std::string(kBroadcast) + "open", payload);
            if (!opened["broadcastId"].isString()) return fail("the tuner did not open");
            broadcastId_ = opened["broadcastId"].string();
            windowId_ = window;
            Json mute = Json::object();
            mute.set("broadcastId", Json::text(broadcastId_));
            mute.set("isMuteOn", Json::flag(false));
            mute.set("muteOwner", Json::text(app));
            (void)call_(std::string(kBroadcast) + "setAudioMute", mute);
        }
        std::string channel = request["channelId"].string();
        if (!validChannelId(channel)) {
            Json source = Json::object();
            source.set("sourceType", Json::text("INPUTSOURCE_NONE"));
            channel = call_("luna://com.webos.service.utp.channel/getLastChannelId", source)["channelId"].string();
        }
        if (validChannelId(channel)) (void)tune(channel);
    } else if (action == "tune") {
        const std::string channel = request["channelId"].string();
        if (!validChannelId(channel)) return fail("channelId expected");
        if (broadcastId_.empty()) return fail("live TV is not open");
        if (!tune(channel)["returnValue"].boolean()) return fail("the channel did not change");
    } else {
        return fail("action expected");
    }
    result.set("ok", Json::flag(true));
    result.set("playing", Json::flag(!broadcastId_.empty()));
    result.set("channelId", Json::text(channelId_));
    return result;
}

Json LiveTv::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("playing", Json::flag(!broadcastId_.empty()));
    result.set("channelId", Json::text(channelId_));
    return result;
}

} // namespace daemon
} // namespace nativetv
