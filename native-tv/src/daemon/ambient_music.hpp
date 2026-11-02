#pragma once

#include "ambilight/bus.hpp"
#include "core/json.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nativetv {
namespace daemon {

// Cinema's ambient music on Home: its six built-in tracks (streamed, pinned to
// a Cinema release) or the tracks in a USB stick's lounge/music folder, played
// by LG's media service while Cinema++ Home is in front, fading in and out.
// Settings in home-music.json. All bus calls go through one long-lived client
// on a worker thread: a track's end arrives as an event on that client.
class AmbientMusic {
  public:
    // One bus call: the first reply, or an invalid Json. Subscribe: replies until the end.
    using Call = std::function<Json(const std::string &uri, const Json &payload)>;
    // Returns the subscription's id, which Cancel ends.
    using Subscribe = std::function<int(const std::string &uri, const Json &payload,
                                        std::function<void(const Json &)> onMessage)>;
    using Cancel = std::function<void(int id)>;
    using ListUsb = std::function<std::vector<std::string>()>;

    AmbientMusic(std::string dataRoot, std::string runDirectory, Call call = Call(),
                 Subscribe subscribe = Subscribe(), ListUsb listUsb = ListUsb(), Cancel cancel = Cancel());
    ~AmbientMusic();
    AmbientMusic(const AmbientMusic &) = delete;
    AmbientMusic &operator=(const AmbientMusic &) = delete;

    // {"enabled", "source": "builtin"|"usb", "volume" 0-100, "shuffle", "tracks": [ids]}.
    bool configure(const Json &change, std::string &error);
    // Home in front (true) or not: music starts or fades out. `quiet`: Cinema's Night, at a
    // quarter of the volume.
    void home(bool shown, bool quiet = false);
    void next();
    void toggle();
    // Settings, the tracks there are, and what plays.
    Json state();
    // Waits for queued work (tests).
    void settle();

    static Json builtinTracks();

  private:
    void post(std::function<void()> job);
    void loop();
    void start();
    void stop(bool fade);
    void playIndex(size_t index);
    std::vector<Json> playlist();
    void save();

    std::string path_;
    std::string runDirectory_;
    Call call_;
    Subscribe subscribe_;
    ListUsb listUsb_;
    Cancel cancel_;
    int subscription_ = -1; // the playing track's events
    std::unique_ptr<ambilight::NodeBus> bus_;
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<std::function<void()>> jobs_;
    bool busy_ = false, stopping_ = false;
    std::thread worker_;
    Json settings_;
    bool shown_ = false, paused_ = false, quiet_ = false;
    std::string mediaId_;
    Json current_;
    size_t position_ = 0;
    unsigned generation_ = 0;
};

} // namespace daemon
} // namespace nativetv
