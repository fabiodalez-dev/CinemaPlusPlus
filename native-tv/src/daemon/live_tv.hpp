#pragma once

#include "ambilight/bus.hpp"
#include "core/json.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace nativetv {
namespace daemon {

// Live TV inside Home, as Cinema plays it: the TV client hands over its video
// window (an exported window of the SDL app) and the broadcast is opened into
// it with LG's utp.broadcast service, the way the browser plays tv://. The
// broadcast lives as long as the bus client that opened it, so the calls go
// through one long-lived Node.js client; a luna-send per call would close it
// at once.
class LiveTv {
  public:
    // One bus call: the first reply, or an invalid Json. Tests pass a stand-in.
    using Call = std::function<Json(const std::string &uri, const Json &payload)>;

    explicit LiveTv(std::string runDirectory, Call call = Call());

    // {"action": "open", "windowId", "appId"?, "source"?, "channelId"?} opens a source in the
    // window: "tv" (the default) on the given channel or the last one, or "hdmi:1".."hdmi:4"
    // (utp.extinputs, as the browser plays ext://hdmi:N); {"action": "tune", "channelId"}
    // changes channel; {"action": "close"} closes it. The reply carries "playing", "source"
    // and "channelId".
    Json act(const Json &request);
    Json state() const;

  private:
    Json tune(const std::string &channelId);
    Json closeLocked();

    std::string runDirectory_;
    Call call_;
    std::unique_ptr<ambilight::NodeBus> bus_;
    mutable std::mutex mutex_;
    std::string broadcastId_, windowId_, channelId_;
    std::string source_;      // "tv" or "hdmi:N"
    std::string pipelineId_;  // an HDMI input's pipeline
};

// The window id an SDL app gets for an exported window ("_Window_Id_12").
bool validWindowId(const std::string &id);
// A channel id from the channel list ("1_38_6_6_930_3006_272").
bool validChannelId(const std::string &id);

} // namespace daemon
} // namespace nativetv
