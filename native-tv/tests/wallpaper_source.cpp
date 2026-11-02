#include "service/home_wallpaper_source.hpp"
#include "service/usb_config.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace nativetv;
using namespace nativetv::service;

namespace {
void write(const std::string &path, const std::string &bytes) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out << bytes;
}
} // namespace

int main() {
    char pattern[] = "/tmp/wallsrc.XXXXXX";
    const std::string root = mkdtemp(pattern);
    const std::string usb = root + "/usb";

    // Defaults, then changes in range and refusals out of it.
    Json source = readWallpaperSource(root);
    assert(source["source"].string() == "slots" && source["gradient"].string() == "midnight");
    assert(source["interval"].number() == 10 && !source["zoom"].boolean());
    std::string error;
    assert(saveWallpaperSource(root, Json::parse(R"({"source":"gradient","gradient":"ember","zoom":true})"), error));
    source = readWallpaperSource(root);
    assert(source["source"].string() == "gradient" && source["gradient"].string() == "ember" && source["zoom"].boolean());
    assert(!saveWallpaperSource(root, Json::parse(R"({"source":"web"})"), error));
    assert(!saveWallpaperSource(root, Json::parse(R"({"gradient":"pink"})"), error));
    assert(!saveWallpaperSource(root, Json::parse(R"({"interval":0})"), error));
    assert(readWallpaperSource(root)["gradient"].string() == "ember");

    // Cinema's photos: downloaded once, checked to be JPEG, then in the rotation.
    int fetched = 0;
    assert(!fetchCinemaPhotos(root, [&](const std::string &url, const std::string &file) {
        ++fetched;
        assert(url.find("cdn.jsdelivr.net/gh/fabiodalez-dev/Cinema@") != std::string::npos);
        write(file, url.find("palm-shore") != std::string::npos ? "not a picture" : std::string("\xff\xd8 jpeg", 7));
        return true;
    }));
    assert(fetched == 5);
    assert(saveWallpaperSource(root, Json::parse(R"({"source":"photos"})"), error));
    std::vector<WallpaperPicture> rotation = wallpaperRotation(root, readWallpaperSource(root), usb);
    assert(rotation.size() == 4 && rotation[0].key == "cinema-azure-cove");
    assert(wallpaperRotationFile(root, "cinema-azure-cove", usb) == rotation[0].path);
    assert(wallpaperRotationFile(root, "cinema-palm-shore", usb).empty());
    assert(wallpaperRotationFile(root, "cinema-../../etc/passwd", usb).empty());
    fetched = 0;
    assert(fetchCinemaPhotos(root, [&](const std::string &, const std::string &file) {
        ++fetched;
        write(file, std::string("\xff\xd8 jpeg", 7));
        return true;
    }));
    assert(fetched == 1);

    // A USB stick's lounge/backgrounds, pictures only, in name order.
    const std::string folder = usb + "/sda/sda1/lounge/backgrounds";
    assert(std::system(("mkdir -p '" + folder + "'").c_str()) == 0);
    write(folder + "/b.png", "png");
    write(folder + "/a.jpg", "jpg");
    write(folder + "/notes.txt", "text");
    write(folder + "/.hidden.jpg", "jpg");
    assert(saveWallpaperSource(root, Json::parse(R"({"source":"usb","interval":5})"), error));
    rotation = wallpaperRotation(root, readWallpaperSource(root), usb);
    assert(rotation.size() == 2 && rotation[0].path == folder + "/a.jpg" && rotation[1].key == "usb-1");
    assert(wallpaperRotationFile(root, "usb-1", usb) == folder + "/b.png");
    assert(wallpaperRotationFile(root, "usb-9", usb).empty() && wallpaperRotationFile(root, "usb-x", usb).empty());
    // A link out of the stick is not followed.
    assert(std::system(("mkdir -p '" + usb + "/sdb/sdb1' '" + root + "/outside/backgrounds' && ln -s '" + root +
                        "/outside' '" + usb + "/sdb/sdb1/lounge'").c_str()) == 0);
    write(root + "/outside/backgrounds/secret.jpg", "jpg");
    assert(wallpaperRotation(root, readWallpaperSource(root), usb).size() == 2);
    Json status = Json::object();
    addWallpaperSource(status, root, usb);
    assert(status["source"].string() == "usb" && status["rotation"].size() == 2 && status["interval"].number() == 5);

    // Cinema's lounge/config.json: what Cinema++ takes from it.
    std::string fingerprint;
    assert(!findUsbConfig(usb, fingerprint).valid() && fingerprint.empty());
    write(usb + "/sda/sda1/lounge/config.json",
          R"({"profile":"night","launcher":{"clockFormat":"12","showDate":false,"focusColour":"gold"},
              "background":{"source":"preset","preset":"ember","kenBurns":true,"slideshowIntervalSec":600},
              "music":{"enabled":true,"volume":0.2,"source":"usb","shuffle":true},
              "weather":{"enabled":true,"units":"c","location":{"name":"Milano","latitude":45.46,"longitude":9.19}},
              "unknown":{"x":1}})");
    const Json config = findUsbConfig(usb, fingerprint);
    assert(config.isObject() && !fingerprint.empty());
    const UsbConfigChanges changes = usbConfigChanges(config);
    assert(changes.layout["ambience"].string() == "night" && changes.layout["clockFormat"].string() == "12h");
    assert(!changes.layout["showDate"].boolean(true) && changes.layout["colours"]["accent"].string() == "#ffd60a");
    assert(changes.wallpaper["source"].string() == "gradient" && changes.wallpaper["gradient"].string() == "ember");
    assert(changes.wallpaper["zoom"].boolean() && changes.wallpaper["interval"].number() == 10);
    assert(changes.music["enabled"].boolean() && changes.music["volume"].number() == 20 &&
           changes.music["source"].string() == "usb");
    assert(changes.weather["place"]["name"].string() == "Milano" && changes.weather["units"].string() == "c");
    const UsbConfigChanges none = usbConfigChanges(Json::parse(R"({"profile":"party","music":{"volume":3}})"));
    assert(!none.layout.size() && !none.music.size() && !none.wallpaper.size() && !none.weather.size());

    (void)std::system(("rm -rf '" + root + "'").c_str());
    std::cout << "wallpaper source tests passed\n";
    return 0;
}
