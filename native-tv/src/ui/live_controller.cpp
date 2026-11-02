#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/kit/tuning.hpp"
#include <algorithm>
#include <cstdlib>

// Live TV inside Home, as on Cinema: the Live tab plays the broadcast behind its page, the
// controls fade after ten seconds, CH+/CH- step through the channels (presses within 450 ms
// are gathered) and numbers type a channel (taken 1.8 s after the last digit, or at once when
// no longer number starts with it).
namespace nativetv {
namespace {
constexpr uint32_t kControlsMs = 10000;
constexpr uint32_t kBannerMs = 4000;
constexpr uint32_t kZapGatherMs = 450;
constexpr uint32_t kTypeMs = 1800;

bool due(uint32_t now, uint32_t at) {
    return at && static_cast<int32_t>(now - at) >= 0;
}
double numberOf(const Json &channel) {
    const std::string text = channel["number"].string();
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? 1e9 : value;
}
} // namespace

bool Application::liveTab() const {
    const auto &home = state_.home;
    return state_.screen == Screen::Home && !screen_ && home.tab < home.tabs.size() &&
           (home.tabs[home.tab].key == "live" || home.tabs[home.tab].key == "inputs") &&
           flow_.pendingTab < 0;
}

// What should play now: the tuner on the Live tab, the input picked on the Inputs tab.
std::string Application::liveSource() const {
    if (!liveTab())
        return "";
    return state_.home.tabs[state_.home.tab].key == "live" ? "tv" : state_.live.pick;
}

// Watchable channels in number order, as LG's own CH+/CH- go.
std::vector<Json> Application::liveChannels() const {
    std::vector<Json> list;
    for (const Json &channel : state_.catalogue["channels"].items())
        if (channel["id"].isString() && !channel["radio"].boolean() && !channel["hidden"].boolean())
            list.push_back(channel);
    std::stable_sort(list.begin(), list.end(),
                     [](const Json &a, const Json &b) { return numberOf(a) < numberOf(b); });
    return list;
}

// Opens the picture when the Live tab shows and the client is in front, closes it otherwise.
void Application::liveSync() {
    auto &live = state_.live;
    // Without a video plane (a desktop run) the Live tab stays a row of channels.
    if (!liveTab())
        live.pick.clear();
    const std::string source = active_ && videoPlane_ ? liveSource() : std::string();
    const bool want = !source.empty();
    if (want && live.open && source != live.source) {
        // Another input on the same window: the service closes the old one first.
        live.source = source;
        live.channelId.clear();
        live.targetId.clear();
        Json body = Json::object();
        body.set("action", Json::text("open"));
        body.set("windowId", Json::text(live.windowId));
        body.set("source", Json::text(source));
        (void)services_.write("live", Operation::Live, body);
        liveShowControls();
    }
    if (want && !live.open) {
        live.windowId = videoPlane_(true);
        if (live.windowId.empty())
            return;
        live.open = true;
        live.source = source;
        live.channelId.clear();
        if (source == "tv")
            live.status = say::msg("tv.live.opening", "Opening the tuner…");
        Json body = Json::object();
        body.set("action", Json::text("open"));
        body.set("windowId", Json::text(live.windowId));
        body.set("source", Json::text(source));
        (void)services_.write("live", Operation::Live, body);
        live.controls = true;
        liveControlsUntil_ = now_ + kControlsMs;
        motion_.tween("livectl", 1, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
        motion_.tween("live", 1, kit::motion::ms::page, kit::easing(kit::motion::ease::out));
        dirty_ = true;
    } else if (!want && live.open) {
        liveClose();
    }
    if (!live.open)
        return;
    if (live.controls && due(now_, liveControlsUntil_)) {
        live.controls = false;
        motion_.tween("livectl", 0, kit::motion::ms::page, kit::easing(kit::motion::ease::inOut));
        dirty_ = true;
    }
    if (live.banner && due(now_, liveBannerUntil_)) {
        live.banner = false;
        motion_.tween("liveban", 0, kit::motion::ms::fast, kit::easing(kit::motion::ease::inOut));
        dirty_ = true;
    }
    if (due(now_, liveTuneAt_)) {
        liveTuneAt_ = 0;
        liveTune(live.targetId, true);
    }
    if (!live.typed.empty() && due(now_, liveTypedAt_ + kTypeMs)) {
        const std::string typed = live.typed;
        live.typed.clear();
        for (const Json &channel : liveChannels())
            if (channel["number"].string() == typed) {
                liveTune(channel["id"].string(), true);
                return;
            }
        live.status = say::msg("tv.live.noChannel", "No channel {n}", fields({{"n", Json::text(typed)}}));
        liveBannerUntil_ = now_ + kBannerMs;
        dirty_ = true;
    }
}

void Application::liveClose() {
    auto &live = state_.live;
    if (!live.open) {
        live.pick.clear();
        return;
    }
    Json body = Json::object();
    body.set("action", Json::text("close"));
    (void)services_.write("live", Operation::Live, body);
    if (videoPlane_ && !live.windowId.empty())
        (void)videoPlane_(false);
    live = LiveState();
    liveTuneAt_ = liveBannerUntil_ = 0;
    motion_.tween("live", 0, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
    motion_.tween("livectl", 1, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
    motion_.tween("liveban", 0, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
    dirty_ = true;
}

void Application::liveShowControls() {
    auto &live = state_.live;
    liveControlsUntil_ = now_ + kControlsMs;
    if (!live.controls) {
        live.controls = true;
        motion_.tween("livectl", 1, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
    }
}

void Application::liveTune(const std::string &channelId, bool now) {
    auto &live = state_.live;
    if (channelId.empty())
        return;
    live.targetId = channelId;
    live.banner = true;
    liveBannerUntil_ = now_ + kBannerMs;
    motion_.tween("liveban", 1, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
    dirty_ = true;
    if (!now) {
        liveTuneAt_ = now_ + kZapGatherMs;
        return;
    }
    if (channelId == live.channelId || live.windowId.empty())
        return;
    Json body = Json::object();
    body.set("action", Json::text("tune"));
    body.set("channelId", Json::text(channelId));
    (void)services_.write("live", Operation::Live, body);
}

void Application::liveZap(int step) {
    const std::vector<Json> list = liveChannels();
    if (list.empty())
        return;
    const std::string from = state_.live.targetId.empty() ? state_.live.channelId : state_.live.targetId;
    size_t at = list.size();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]["id"].string() == from)
            at = i;
    size_t next;
    if (at == list.size())
        next = step > 0 ? 0 : list.size() - 1;
    else
        next = (at + list.size() + (step > 0 ? 1 : list.size() - 1)) % list.size();
    liveTune(list[next]["id"].string(), false);
}

void Application::liveDigit(int digit) {
    auto &live = state_.live;
    if (live.typed.size() >= 4)
        live.typed.clear();
    live.typed += static_cast<char>('0' + digit);
    liveTypedAt_ = now_;
    live.banner = true;
    liveBannerUntil_ = now_ + kTypeMs + kBannerMs;
    motion_.tween("liveban", 1, kit::motion::ms::fast, kit::easing(kit::motion::ease::out));
    // Nothing longer starts with what was typed: no need to wait.
    bool longer = false;
    for (const Json &channel : liveChannels()) {
        const std::string number = channel["number"].string();
        if (number.size() > live.typed.size() && number.compare(0, live.typed.size(), live.typed) == 0)
            longer = true;
    }
    if (!longer)
        liveTypedAt_ = now_ - kTypeMs;
    dirty_ = true;
}

// Keys while the picture plays: the first key with the controls hidden only brings them back;
// CH+/CH- always change channel.
bool Application::liveKey(Key key) {
    auto &live = state_.live;
    if (!live.open || !liveTab())
        return false;
    if ((key == Key::PageUp || key == Key::PageDown) && live.source == "tv") {
        liveZap(key == Key::PageUp ? 1 : -1);
        return true;
    }
    const bool hidden = !live.controls;
    liveShowControls();
    dirty_ = true;
    return hidden && key != Key::Back;
}
// Ambient music plays while Home shows (not over live TV or an input, not behind another
// screen or app); the service fades it in and out.
void Application::musicSync() {
    // Not before the server has said whether first-run setup is done: setup comes first.
    const bool shown = active_ && capsAnswered_ && state_.screen == Screen::Home && !screen_ &&
                       !state_.live.open && !coveredForCaps();
    if (shown == musicShown_)
        return;
    musicShown_ = shown;
    Json body = Json::object();
    body.set("action", Json::text("home"));
    body.set("shown", Json::flag(shown));
    (void)services_.write("music", Operation::Music, body);
}

void Application::musicKey(bool next) {
    if (state_.screen != Screen::Home || screen_)
        return;
    Json body = Json::object();
    body.set("action", Json::text(next ? "next" : "toggle"));
    (void)services_.write("music", Operation::Music, body);
}
} // namespace nativetv
