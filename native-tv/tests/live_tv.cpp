// Live TV inside Home, with a stand-in utp.broadcast.
#include "daemon/live_tv.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace nativetv;
using namespace nativetv::daemon;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
Json object(const std::string &text) {
    return Json::parse(text);
}
} // namespace

int main() {
    check(validWindowId("_Window_Id_22") && !validWindowId("_Window_Id_") && !validWindowId("x;rm"), "window ids");
    check(validChannelId("1_38_6_6_930_3006_272") && !validChannelId("1 2") && !validChannelId(""), "channel ids");

    std::vector<std::string> calls;
    int opened = 0;
    LiveTv live("/tmp/unused", [&](const std::string &uri, const Json &payload) {
        const std::string method = uri.substr(uri.rfind('/') + 1);
        calls.push_back(method + " " + payload.dump());
        if (uri.find("extinputs/open") != std::string::npos)
            return object(R"({"pipelineId":"p-1","returnValue":true})");
        if (method == "open") {
            ++opened;
            return object(R"({"broadcastId":"b-)" + std::to_string(opened) + R"(","returnValue":true})");
        }
        if (method == "getLastChannelId") return object(R"({"channelId":"1_38_6_6","returnValue":true})");
        if (method == "changeChannel") return object(R"({"returnValue":true})");
        if (uri.find("extinputs/open") != std::string::npos)
            return object(R"({"pipelineId":"p-1","returnValue":true})");
        return object(R"({"returnValue":true})");
    });

    check(!live.act(object(R"({"action":"open","windowId":"bad"})"))["ok"].boolean(), "a bad window refused");
    check(!live.act(object(R"({"action":"tune","channelId":"1_2"})"))["ok"].boolean(), "nothing to tune before open");

    Json reply = live.act(object(R"({"action":"open","windowId":"_Window_Id_22"})"));
    check(reply["ok"].boolean() && reply["playing"].boolean() && reply["channelId"].string() == "1_38_6_6",
          "opened on the last channel");
    check(calls[0].find("\"windowId\"") != std::string::npos && calls[0].find("com.tvweb.dashboard") != std::string::npos,
          "the window and the app go to open");

    // The same window again keeps its broadcast; a channel given is tuned.
    reply = live.act(object(R"({"action":"open","windowId":"_Window_Id_22","channelId":"1_25_7_7"})"));
    check(opened == 1 && reply["channelId"].string() == "1_25_7_7", "same window, one broadcast");
    reply = live.act(object(R"({"action":"tune","channelId":"1_48_8_8"})"));
    check(reply["ok"].boolean() && live.state()["channelId"].string() == "1_48_8_8", "zapped");

    // Another window: the old broadcast is closed first.
    calls.clear();
    reply = live.act(object(R"({"action":"open","windowId":"_Window_Id_23"})"));
    check(opened == 2 && calls[0].compare(0, 5, "close") == 0 && calls[0].find("b-1") != std::string::npos,
          "the old broadcast closed");

    // An HDMI input in the same window: the broadcast closes, the input opens on its port.
    calls.clear();
    reply = live.act(object(R"({"action":"open","windowId":"_Window_Id_23","source":"hdmi:2"})"));
    check(reply["ok"].boolean() && reply["source"].string() == "hdmi:2", "hdmi opened");
    check(calls[0].compare(0, 5, "close") == 0 && calls[1].find("\"HDMI\"") != std::string::npos &&
              calls[2].find("\"portId\":2") != std::string::npos,
          "broadcast closed, input opened on port 2");
    check(!live.act(object(R"({"action":"open","windowId":"_Window_Id_23","source":"hdmi:9"})"))["ok"].boolean(),
          "no port 9");
    calls.clear();
    reply = live.act(object(R"({"action":"close"})"));
    check(calls[0].find("p-1") != std::string::npos, "the input's pipeline closed");
    check(!reply["playing"].boolean() && !live.state()["playing"].boolean(), "closed");
    std::cout << "live tv tests passed\n";
    return 0;
}
