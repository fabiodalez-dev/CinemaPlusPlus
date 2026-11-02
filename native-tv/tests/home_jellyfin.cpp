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
        // Any other width is served at one of the two sizes, never a new file.
        const int asked1280 = asked["http://jf.local:8096/Items/" + std::string(kUser) + "/Images/Backdrop/0"];
        check(jf.image(std::string(kUser) + ":Backdrop:1919", bytes), "an odd width");
        check(asked["http://jf.local:8096/Items/" + std::string(kUser) + "/Images/Backdrop/0"] == asked1280, "served from the 1280 file");
        // Search asks the library with the term encoded, and needs two letters.
        const Json found = jf.search("it f");
        check(found["items"].size() == 1 && found["items"][0]["title"].string() == "It Follows", "search results");
        check(jf.search("i")["items"].size() == 0, "one letter is not a search");
        // The Musica page: albums by name from a letter, or albums, songs and artists by a query.
        std::string musicUrl;
        HomeJellyfin music(dir, dir + "/jellyfin.json",
                           [&](const std::string &url, const std::string &, std::string &body, long &status, size_t) {
                               musicUrl = url;
                               status = 200;
                               body = "{\"TotalRecordCount\":250,\"Items\":[" +
                                      item("s1", "Audio", "Song", ",\"Artists\":[\"Bjork\"],\"AlbumId\":\"al\",\"AlbumPrimaryImageTag\":\"t\"") + "," +
                                      item("a1", "MusicAlbum", "Homogenic", ",\"AlbumArtist\":\"Bjork\",\"ImageTags\":{\"Primary\":\"t\"}") + "]}";
                               return true;
                           },
                           [&now]() { return now; });
        Json page = music.music("b", "", 120);
        check(musicUrl.find("IncludeItemTypes=MusicAlbum&") != std::string::npos &&
                  musicUrl.find("NameStartsWith=B") != std::string::npos &&
                  musicUrl.find("SortBy=SortName") != std::string::npos && musicUrl.find("StartIndex=120") != std::string::npos,
              "albums of a letter, by name, from a page");
        check(page["total"].number() == 250 && page["start"].number() == 120 && page["enabled"].boolean(), "page facts");
        page = music.music("#", "", 0);
        check(musicUrl.find("NameLessThan=A") != std::string::npos, "# is what comes before A");
        page = music.music("", "bj rk", 0);
        check(musicUrl.find("SearchTerm=bj%20rk") != std::string::npos && musicUrl.find("Audio") != std::string::npos &&
                  musicUrl.find("NameStartsWith") == std::string::npos,
              "a query searches albums, songs and artists");
        check(page["items"][0]["type"].string() == "MusicAlbum" && page["items"][1]["type"].string() == "Audio",
              "albums before songs");
        check(page["items"][0]["cover"].string() == "a1:Primary" && page["items"][0]["artist"].string() == "Bjork" &&
                  page["items"][1]["cover"].string() == "s1:Primary" && page["items"][1]["artist"].string() == "Bjork",
              "covers and artists, a song's from its artists");
        check(HomeJellyfin::normalizeItem(Json::parse(R"({"Id":"s2","Type":"Audio","Name":"x","AlbumId":"al","AlbumPrimaryImageTag":"t"})"))["cover"]
                      .string() == "al:Primary",
              "a song without art of its own has its album's cover");
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
