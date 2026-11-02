// Ambient music on Home, with a stand-in media service.
#include "daemon/ambient_music.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
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
} // namespace

int main() {
    check(AmbientMusic::builtinTracks().size() == 6, "six built-in tracks");
    char pattern[] = "/tmp/ambient-music-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    std::vector<std::string> calls;
    std::function<void(const Json &)> ended;
    int loads = 0, subscriptions = 0, cancelled = 0;
    {
        AmbientMusic music(
            dir, dir + "/run",
            [&](const std::string &uri, const Json &payload) {
                calls.push_back(uri.substr(uri.rfind('/') + 1) + " " + payload.dump());
                if (uri.find("/load") != std::string::npos)
                    return Json::parse("{\"mediaId\":\"m" + std::to_string(++loads) + "\",\"returnValue\":true}");
                return Json::parse(R"({"returnValue":true})");
            },
            [&](const std::string &, const Json &, std::function<void(const Json &)> on) {
                ended = on;
                return ++subscriptions;
            },
            []() { return std::vector<std::string>{"/tmp/usb/sda/sda1/lounge/music/rain.mp3"}; },
            [&](int) { ++cancelled; });

        // Off by default: Home shows, nothing plays.
        music.home(true);
        music.settle();
        check(calls.empty() && !music.state()["playing"].boolean(), "off by default");

        std::string error;
        Json on = Json::parse(R"({"enabled":true,"shuffle":false,"volume":40,"tracks":["warm-glow"]})");
        check(music.configure(on, error), "switched on");
        music.settle();
        check(loads == 1 && calls[0].find("warm-glow.mp3") != std::string::npos, "the chosen track loads");
        bool fadeIn = false;
        for (const std::string &call : calls)
            if (call.find("setVolume") == 0 && call.find("40") != std::string::npos &&
                call.find("ease") != std::string::npos)
                fadeIn = true;
        check(fadeIn, "fades in to the volume");
        check(music.state()["track"]["title"].string() == "Warm Glow", "what plays");

        // The end of a track starts the next.
        ended(Json::parse(R"({"endOfStream":{"mediaId":"m1"}})"));
        music.settle();
        check(loads == 2, "next track at the end");

        // Leaving Home fades out and unloads.
        calls.clear();
        music.home(false);
        music.settle();
        check(!music.state()["playing"].boolean() && calls.back().find("unload") == 0, "stopped off Home");
        check(cancelled == subscriptions, "every track's events cancelled");

        // USB source.
        check(music.configure(Json::parse(R"({"source":"usb"})"), error), "usb");
        music.home(true);
        music.settle();
        check(music.state()["track"]["title"].string() == "rain", "a USB track by its name");
        music.toggle();
        music.settle();
        check(music.state()["paused"].boolean() && !music.state()["playing"].boolean(), "paused");
    }
    std::remove((dir + "/home-music.json").c_str());
    rmdir(dir.c_str());
    std::cout << "ambient music tests passed\n";
    return 0;
}
