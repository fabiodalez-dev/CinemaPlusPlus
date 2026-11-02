#include "daemon/ambient_music.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <random>
#include <sstream>
#include <sys/stat.h>

namespace nativetv {
namespace daemon {
namespace {
// The tracks are not in the package: as Cinema does, they stream from Cinema's
// repository through jsDelivr, pinned to a release tag (immutable, so cached).
const char *kBase = "https://cdn.jsdelivr.net/gh/fabiodalez-dev/Cinema@v0.3.9/assets/music/";
const char *kMedia = "luna://com.webos.media/";
constexpr int kFadeMs = 1500;

struct Builtin {
    const char *id, *title, *artist;
};
const Builtin kBuiltins[] = {{"midnight-lounge", "Midnight Lounge", "Ambient"},
                             {"starlight-drift", "Starlight Drift", "Ambient"},
                             {"ocean-haze", "Ocean Haze", "Ambient"},
                             {"warm-glow", "Warm Glow", "Ambient"},
                             {"backbay-lounge", "Backbay Lounge", "Kevin MacLeod"},
                             {"chill-wave", "Chill Wave", "Kevin MacLeod"}};

bool audioName(const std::string &name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const char *ext : {".mp3", ".m4a", ".aac", ".ogg", ".flac", ".wav"}) {
        const size_t n = std::string(ext).size();
        if (lower.size() > n && lower.compare(lower.size() - n, n, ext) == 0) return true;
    }
    return false;
}

std::vector<std::string> listDirectory(const std::string &path) {
    std::vector<std::string> names;
    if (DIR *dir = opendir(path.c_str())) {
        while (const dirent *entry = readdir(dir))
            if (entry->d_name[0] != '.') names.push_back(entry->d_name);
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// USB sticks are mounted at /tmp/usb/<disk>/<partition>; Cinema's folder is lounge/music.
std::vector<std::string> usbTracks() {
    std::vector<std::string> files;
    for (const std::string &disk : listDirectory("/tmp/usb"))
        for (const std::string &part : listDirectory("/tmp/usb/" + disk)) {
            const std::string folder = "/tmp/usb/" + disk + "/" + part + "/lounge/music";
            for (const std::string &name : listDirectory(folder))
                if (audioName(name) && files.size() < 500) files.push_back(folder + "/" + name);
        }
    return files;
}
} // namespace

Json AmbientMusic::builtinTracks() {
    Json list = Json::array();
    for (const Builtin &track : kBuiltins) {
        Json item = Json::object();
        item.set("id", Json::text(track.id));
        item.set("title", Json::text(track.title));
        item.set("artist", Json::text(track.artist));
        item.set("uri", Json::text(std::string(kBase) + track.id + ".mp3"));
        list.add(item);
    }
    return list;
}

AmbientMusic::AmbientMusic(std::string dataRoot, std::string runDirectory, Call call, Subscribe subscribe,
                           ListUsb listUsb, Cancel cancel)
    : path_((dataRoot.empty() ? std::string("/var/lib/tvweb") : dataRoot) + "/home-music.json"),
      runDirectory_(std::move(runDirectory)), call_(std::move(call)), subscribe_(std::move(subscribe)),
      listUsb_(listUsb ? listUsb : ListUsb(usbTracks)), cancel_(std::move(cancel)) {
    if (!call_ || !subscribe_ || !cancel_) {
        auto bus = [this]() -> ambilight::NodeBus & {
            if (!bus_) {
                (void)mkdir(runDirectory_.c_str(), 0700);
                bus_.reset(new ambilight::NodeBus(runDirectory_, "/usr/lib/nodejs/palmbus.js"));
            }
            return *bus_;
        };
        if (!call_)
            call_ = [bus](const std::string &uri, const Json &payload) { return bus().call(uri, payload, 8000); };
        if (!subscribe_)
            subscribe_ = [bus](const std::string &uri, const Json &payload, std::function<void(const Json &)> on) {
                return bus().subscribe(uri, payload, on, []() {});
            };
        if (!cancel_)
            cancel_ = [bus](int id) { bus().cancel(id); };
    }
    std::ifstream file(path_.c_str());
    std::stringstream text;
    text << file.rdbuf();
    const Json saved = Json::parse(text.str());
    settings_ = Json::object();
    settings_.set("enabled", Json::flag(saved["enabled"].boolean(false)));
    settings_.set("source", Json::text(saved["source"].string() == "usb" ? "usb" : "builtin"));
    settings_.set("volume", Json::numeric(std::max(0.0, std::min(100.0, saved["volume"].number(35)))));
    settings_.set("shuffle", Json::flag(saved["shuffle"].boolean(true)));
    settings_.set("tracks", saved["tracks"].isArray() ? saved["tracks"] : Json::array());
    worker_ = std::thread([this]() { loop(); });
}

AmbientMusic::~AmbientMusic() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AmbientMusic::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
}

void AmbientMusic::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) break;
        std::function<void()> job = std::move(jobs_.front());
        jobs_.pop_front();
        busy_ = true;
        lock.unlock();
        job();
        lock.lock();
        busy_ = false;
        if (jobs_.empty()) idle_.notify_all();
    }
    // Closing: the music stops with the service, and its events with it.
    if (subscription_ >= 0) {
        lock.unlock();
        cancel_(subscription_);
        lock.lock();
        subscription_ = -1;
    }
    if (!mediaId_.empty()) {
        Json payload = Json::object();
        payload.set("mediaId", Json::text(mediaId_));
        lock.unlock();
        (void)call_(std::string(kMedia) + "unload", payload);
    }
}

void AmbientMusic::settle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait_for(lock, std::chrono::seconds(10), [this]() { return jobs_.empty() && !busy_; });
}

std::vector<Json> AmbientMusic::playlist() {
    std::vector<Json> list;
    if (settings_["source"].string() == "usb") {
        for (const std::string &file : listUsb_()) {
            Json item = Json::object();
            const size_t slash = file.rfind('/');
            std::string title = file.substr(slash + 1);
            const size_t dot = title.rfind('.');
            if (dot != std::string::npos) title = title.substr(0, dot);
            item.set("id", Json::text(file));
            item.set("title", Json::text(title));
            item.set("uri", Json::text("file://" + file));
            list.push_back(item);
        }
        return list;
    }
    const Json chosen = settings_["tracks"];
    for (const Json &track : builtinTracks().items()) {
        bool wanted = chosen.size() == 0;
        for (const Json &id : chosen.items())
            if (id.string() == track["id"].string()) wanted = true;
        if (wanted) list.push_back(track);
    }
    return list;
}

// Runs on the worker.
void AmbientMusic::playIndex(size_t index) {
    std::vector<Json> list;
    int volume;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        list = playlist();
        volume = static_cast<int>(settings_["volume"].number(35));
        if (quiet_) volume = (volume + 3) / 4;
    }
    if (list.empty()) return;
    if (!mediaId_.empty()) stop(false);
    const Json track = list[index % list.size()];
    Json option = Json::object();
    option.set("appId", Json::text("com.tvweb.dashboard"));
    Json payloadOption = Json::object();
    payloadOption.set("option", option);
    Json load = Json::object();
    load.set("uri", track["uri"]);
    load.set("type", Json::text("media"));
    load.set("payload", payloadOption);
    const Json loaded = call_(std::string(kMedia) + "load", load);
    if (!loaded["mediaId"].isString()) return;
    const std::string id = loaded["mediaId"].string();
    unsigned generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mediaId_ = id;
        current_ = track;
        position_ = index % list.size();
        generation = ++generation_;
    }
    Json media = Json::object();
    media.set("mediaId", Json::text(id));
    // The end of the track, from the media service's own events, starts the next one.
    subscription_ = subscribe_(std::string(kMedia) + "subscribe", media, [this, generation](const Json &event) {
        if (event.has("endOfStream") || event.has("error"))
            post([this, generation]() {
                if (generation != generation_ || !shown_ || paused_) return;
                playIndex(position_ + 1);
            });
    });
    Json quiet = media;
    quiet.set("volume", Json::numeric(0));
    (void)call_(std::string(kMedia) + "setVolume", quiet);
    (void)call_(std::string(kMedia) + "play", media);
    Json fade = media;
    fade.set("volume", Json::numeric(volume));
    fade.set("ease", Json::numeric(kFadeMs));
    (void)call_(std::string(kMedia) + "setVolume", fade);
}

// Runs on the worker.
void AmbientMusic::stop(bool fade) {
    if (subscription_ >= 0) {
        cancel_(subscription_);
        subscription_ = -1;
    }
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = mediaId_;
        mediaId_.clear();
        current_ = Json();
        ++generation_;
    }
    if (id.empty()) return;
    Json media = Json::object();
    media.set("mediaId", Json::text(id));
    if (fade) {
        Json quiet = media;
        quiet.set("volume", Json::numeric(0));
        quiet.set("ease", Json::numeric(kFadeMs));
        (void)call_(std::string(kMedia) + "setVolume", quiet);
        std::this_thread::sleep_for(std::chrono::milliseconds(kFadeMs));
    }
    (void)call_(std::string(kMedia) + "unload", media);
}

void AmbientMusic::start() {
    size_t first = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!settings_["enabled"].boolean() || !shown_ || paused_ || !mediaId_.empty()) return;
        const size_t count = playlist().size();
        if (!count) return;
        if (settings_["shuffle"].boolean()) {
            std::random_device device;
            first = device() % count;
        } else {
            first = position_;
        }
    }
    playIndex(first);
}

void AmbientMusic::home(bool shown, bool quiet) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quiet_ = quiet;
        if (shown == shown_) return;
        shown_ = shown;
    }
    post([this, shown]() {
        if (shown) start();
        else stop(true);
    });
}

void AmbientMusic::next() {
    post([this]() {
        bool shuffle;
        size_t count;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!shown_ || !settings_["enabled"].boolean()) return;
            paused_ = false;
            shuffle = settings_["shuffle"].boolean();
            count = playlist().size();
        }
        if (!count) return;
        size_t index = position_ + 1;
        if (shuffle && count > 1) {
            std::random_device device;
            index = position_ + 1 + device() % (count - 1);
        }
        playIndex(index);
    });
}

void AmbientMusic::toggle() {
    post([this]() {
        bool pause;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            paused_ = !paused_;
            pause = paused_;
        }
        if (pause) stop(true);
        else start();
    });
}

bool AmbientMusic::configure(const Json &change, std::string &error) {
    if (!change.isObject()) {
        error = "settings expected";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (change["enabled"].isBoolean()) settings_.set("enabled", change["enabled"]);
        if (change["shuffle"].isBoolean()) settings_.set("shuffle", change["shuffle"]);
        if (change["source"].isString())
            settings_.set("source", Json::text(change["source"].string() == "usb" ? "usb" : "builtin"));
        if (change["volume"].isNumber())
            settings_.set("volume", Json::numeric(std::max(0.0, std::min(100.0, change["volume"].number()))));
        if (change["tracks"].isArray()) {
            Json tracks = Json::array();
            for (const Json &id : change["tracks"].items())
                if (id.isString() && tracks.size() < 50) tracks.add(id);
            settings_.set("tracks", tracks);
        }
        paused_ = false;
        save();
    }
    // Whatever changed, the music starts again from the new settings, or stops.
    post([this]() {
        stop(false);
        start();
    });
    return true;
}

void AmbientMusic::save() {
    const std::string staging = path_ + ".part";
    std::ofstream out(staging.c_str(), std::ios::trunc);
    out << settings_.dump();
    out.close();
    if (!out || std::rename(staging.c_str(), path_.c_str()) != 0) std::remove(staging.c_str());
}

Json AmbientMusic::state() {
    std::lock_guard<std::mutex> lock(mutex_);
    Json result = settings_;
    result.set("builtin", builtinTracks());
    result.set("usbTracks", Json::numeric(static_cast<double>(listUsb_().size())));
    result.set("playing", Json::flag(!mediaId_.empty()));
    result.set("paused", Json::flag(paused_));
    if (current_.isObject()) {
        Json now = Json::object();
        now.set("title", current_["title"]);
        now.set("artist", current_["artist"]);
        result.set("track", now);
    }
    return result;
}

} // namespace daemon
} // namespace nativetv
