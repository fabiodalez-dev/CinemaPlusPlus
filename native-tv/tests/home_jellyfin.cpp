// The Jellyfin rows on Home, with a stand-in server.
#include "service/home_jellyfin.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unistd.h>

using namespace nativetv;
using namespace nativetv::service;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

const char *kUser = "0123456789abcdef0123456789abcdef";

std::string item(const std::string &id, const std::string &type, const std::string &name,
                 const std::string &extra = "") {
    return "{\"Id\":\"" + id + "\",\"Type\":\"" + type + "\",\"Name\":\"" + name +
           "\",\"RunTimeTicks\":36000000000,\"ImageTags\":{\"Primary\":\"x\"}" + extra + "}";
}
} // namespace

int main() {
    // Items as Cinema shows them.
    const Json ep = HomeJellyfin::normalizeItem(Json::parse(
        R"({"Id":"aaaa","Type":"Episode","Name":"The Pilot","SeriesName":"Slow Horses","SeriesId":"ssss",
            "ParentIndexNumber":2,"IndexNumber":3,"RunTimeTicks":36000000000,
            "UserData":{"PlaybackPositionTicks":12000000000},"ParentBackdropItemId":"pppp",
            "Overview":"<p>A spy</p> story","Genres":["Drama","Thriller","Spy"]})"));
    check(ep["title"].string() == "Slow Horses" && ep["episode"].string() == "The Pilot", "series name first");
    check(ep["minutes"].number() == 60 && ep["progress"].number() == 33 && ep["remaining"].number() == 40, "progress");
    check(ep["backdrop"].string() == "pppp:Backdrop" && ep["image"].string() == "pppp:Backdrop", "series backdrop");
    check(ep["overview"].string() == "A spy story" && ep["genres"].size() == 2, "overview and two genres");

    // Continue watching: Resume then Next Up, one per series.
    const Json resume = Json::parse(R"([{"id":"a","seriesId":"s1"},{"id":"b"}])");
    const Json next = Json::parse(R"([{"id":"c","seriesId":"s1"},{"id":"b"},{"id":"d","seriesId":"s2"}])");
    const Json merged = HomeJellyfin::mergeContinue(resume, next);
    check(merged.size() == 3 && merged[2]["id"].string() == "d", "no doubles by id or series");

    // A whole refresh against a stand-in server.
    char pattern[] = "/tmp/home-jellyfin-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    {
        std::ofstream c(dir + "/jellyfin.json");
        c << "{\"server\":\"http://jf.local:8096/\",\"userId\":\"" << kUser
          << "\",\"token\":\"0123456789secret\",\"userName\":\"Fabio\"}";
    }
    std::map<std::string, int> asked;
    std::string authSeen;
    const auto get = [&](const std::string &url, const std::string &auth, std::string &body, long &status,
                         size_t) {
        authSeen = auth;
        ++asked[url.substr(0, url.find('?'))];
        status = 200;
        if (url.find("/Items/Resume") != std::string::npos)
            body = "{\"Items\":[" + item("r1", "Movie", "Sully", ",\"UserData\":{\"PlaybackPositionTicks\":6000000000}") + "]}";
        else if (url.find("/Shows/NextUp") != std::string::npos)
            body = "{\"Items\":[" + item("n1", "Episode", "Ep", ",\"SeriesName\":\"Show\",\"SeriesId\":\"s9\"") + "]}";
        else if (url.find("/Images/") != std::string::npos)
            body = "JPEG";
        else
            body = "{\"Items\":[" + item("m1", "Movie", "It Follows") + "]}";
        return true;
    };
    int64_t now = 1000;
    {
        HomeJellyfin jf(dir, dir + "/jellyfin.json", get, [&now]() { return now; });
        check(jf.feed()["state"].string() == "loading", "loading at first");
        for (int i = 0; i < 100 && jf.feed()["state"].string() == "loading"; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const Json feed = jf.feed();
        check(feed["state"].string() == "online" && feed["user"].string() == "Fabio", "online");
        check(feed["rows"][0]["key"].string() == "jf-continue" && feed["rows"][0]["items"].size() == 2, "continue row");
        check(feed["rows"][0]["items"][0]["title"].string() == "Sully", "resume first");
        check(authSeen == "MediaBrowser Token=\"0123456789secret\"", "token in the header only");
        check(asked.count("http://jf.local:8096/Users/0123456789abcdef0123456789abcdef/Items/Resume") == 1, "server without its slash");
        std::string bytes;
        check(jf.image(std::string(kUser) + ":Backdrop:1280", bytes) && bytes == "JPEG", "artwork");
        const int before = asked["http://jf.local:8096/Items/" + std::string(kUser) + "/Images/Backdrop/0"];
        check(jf.image(std::string(kUser) + ":Backdrop:1280", bytes) && bytes == "JPEG", "artwork again");
        check(asked["http://jf.local:8096/Items/" + std::string(kUser) + "/Images/Backdrop/0"] == before, "from the disk cache");
        check(!jf.image("../etc:Backdrop:1280", bytes) && !jf.image(std::string(kUser) + ":Logo:100", bytes), "bad specs refused");
    }
    // Without a connection: unconfigured.
    {
        HomeJellyfin jf(dir, dir + "/missing.json", get, [&now]() { return now; });
        for (int i = 0; i < 100 && jf.feed()["state"].string() == "loading"; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // The cache on disk from the run before is shown first, then replaced.
        for (int i = 0; i < 100 && jf.feed()["state"].string() != "unconfigured"; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(jf.feed()["state"].string() == "unconfigured", "no connection");
    }
    std::cout << "home jellyfin tests passed\n";
    return 0;
}
