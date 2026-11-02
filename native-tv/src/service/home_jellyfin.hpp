#pragma once

#include "core/json.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace service {

// The Jellyfin library on Home, as Cinema shows it: Continue watching (Resume
// and Next Up, without doubles), Recently added, films, series and music. The
// connection is Cinema's root-only file {server, userId, token, userName}; the
// token never leaves the daemon, artwork included. Refreshed in a worker every
// ten minutes and on request, cached on disk so Home has rows at once.
class HomeJellyfin {
  public:
    // GET url with an Authorization header; the body and the HTTP status.
    using Get = std::function<bool(const std::string &url, const std::string &authorization,
                                   std::string &body, long &status, size_t maxBytes)>;
    using Clock = std::function<int64_t()>;

    HomeJellyfin(std::string dataRoot, std::string connectionPath, Get get = Get(), Clock clock = Clock());
    ~HomeJellyfin();
    HomeJellyfin(const HomeJellyfin &) = delete;
    HomeJellyfin &operator=(const HomeJellyfin &) = delete;

    // {"state": unconfigured|loading|online|offline|unauthorized, "user", "rows": [...]};
    // asks for a refresh when the cache is older than ten minutes.
    Json feed();
    void refreshNow();
    // Artwork of an item: spec "<itemId>:<Primary|Backdrop|Thumb>:<width>". JPEG bytes.
    bool image(const std::string &spec, std::string &bytes);
    // Search titles; the same row shape.
    Json search(const std::string &query);
    // The music library for the Musica page, by name: the albums whose name starts with `letter`
    // ("#" for the ones before A, "" for all), or, with a `query` of two characters or more, the
    // albums, songs and artists that match it. {"enabled", "items", "total", "start"}; `start`
    // pages through them, kMusicPage at a time.
    static constexpr int kMusicPage = 120;
    Json music(const std::string &letter, const std::string &query, int start);

    // Exposed for the tests.
    static Json normalizeItem(const Json &raw);
    static Json mergeContinue(const Json &resume, const Json &nextUp);

  private:
    struct Connection {
        std::string server, userId, token, userName;
        bool valid() const { return !server.empty() && !userId.empty() && token.size() > 8; }
    };
    Connection readConnection() const;
    bool getJson(const Connection &c, const std::string &path, Json &out, long &status);
    Json items(const Connection &c, const std::string &path, long &status);
    void refresh();
    void workerLoop();

    std::string dataRoot_;
    std::string connectionPath_;
    std::string cachePath_;
    Get get_;
    Clock clock_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool requested_ = false;
    bool loaded_ = false;
    Json cache_;
    int64_t fetchedAtMs_ = 0;
};

} // namespace service
} // namespace nativetv
