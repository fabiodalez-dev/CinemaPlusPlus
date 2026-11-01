#pragma once

#include "core/json.hpp"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace nativetv {
namespace service {

// Linking Cinema++ to a Jellyfin server with Quick Connect, as Cinema does:
// the TV shows a six-digit code, the viewer approves it in any Jellyfin client
// signed in to the same server, and the token lands in Cinema's root-only
// connection file {server, userId, token, userName}. The token never leaves
// the daemon; status() reports only the code, the server and the user.
class JellyfinLink {
  public:
    // One HTTP exchange: method, url, Authorization header, JSON body (empty
    // for GET); the response body and status.
    using Request = std::function<bool(const std::string &method, const std::string &url,
                                       const std::string &authorization, const std::string &body,
                                       std::string &response, long &status)>;
    using Clock = std::function<int64_t()>;

    JellyfinLink(std::string connectionPath, Request request = Request(), Clock clock = Clock());

    // Check the server and ask it for a code. False with error set when the
    // address is not a Jellyfin server or Quick Connect is off.
    bool start(const std::string &server, std::string &error);
    // Ask the server whether the code was approved; on approval sign in, write
    // the connection file and return true once.
    bool poll(std::string &error);
    void cancel();
    bool unlink(std::string &error);
    // {"state": "unlinked"|"waiting"|"linked", "code", "server", "user", "expiresInS"}.
    Json status() const;

    // Exposed for the tests: "jf.local:8096/" becomes "http://jf.local:8096".
    static std::string normalizeServer(const std::string &server);

  private:
    std::string authorization(const std::string &token = "") const;
    bool writeConnection(const std::string &server, const std::string &userId, const std::string &token,
                         const std::string &userName, std::string &error);

    std::string connectionPath_;
    Request request_;
    Clock clock_;
    mutable std::mutex mutex_;
    std::string deviceId_;
    std::string server_;
    std::string secret_;
    std::string code_;
    int64_t startedMs_ = 0;
};

} // namespace service
} // namespace nativetv
