#include "service/usb_config.hpp"
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace nativetv {
namespace service {
namespace {
constexpr size_t kConfigBytes = 256 * 1024;

std::vector<std::string> folders(const std::string &directory) {
    std::vector<std::string> names;
    DIR *dir = opendir(directory.c_str());
    if (!dir) return names;
    while (const dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        struct stat info = {};
        if (name != "." && name != ".." && lstat((directory + "/" + name).c_str(), &info) == 0 &&
            S_ISDIR(info.st_mode))
            names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

// FNV-1a, enough to tell one file's content from another.
std::string fingerprintOf(const std::string &bytes) {
    unsigned long long hash = 1469598103934665603ULL;
    for (unsigned char c : bytes) hash = (hash ^ c) * 1099511628211ULL;
    std::ostringstream text;
    text << std::hex << hash << "-" << bytes.size();
    return text.str();
}

// Cinema's focus colours (launcher.focusColour) as Cinema++'s accent.
std::string focusAccent(const std::string &name) {
    static const struct {
        const char *name, *accent;
    } colours[] = {{"blue", "#a8c7fa"}, {"purple", "#d0bcff"}, {"green", "#6dd58c"},
                   {"gold", "#ffd60a"}, {"warm", "#ffb74d"}};
    for (const auto &colour : colours)
        if (name == colour.name) return colour.accent;
    return "";
}
} // namespace

Json findUsbConfig(const std::string &usbRoot, std::string &fingerprint) {
    for (const std::string &device : folders(usbRoot))
        for (const std::string &drive : folders(usbRoot + "/" + device)) {
            const std::string path = usbRoot + "/" + device + "/" + drive + "/lounge/config.json";
            struct stat info = {};
            if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
                static_cast<unsigned long long>(info.st_size) > kConfigBytes)
                continue;
            std::ifstream file(path.c_str(), std::ios::binary);
            std::stringstream bytes;
            bytes << file.rdbuf();
            const Json config = Json::parse(bytes.str());
            if (!config.isObject()) continue;
            fingerprint = fingerprintOf(bytes.str());
            return config;
        }
    fingerprint.clear();
    return Json();
}

UsbConfigChanges usbConfigChanges(const Json &config) {
    UsbConfigChanges changes;
    changes.layout = Json::object();
    changes.wallpaper = Json::object();
    changes.music = Json::object();
    changes.weather = Json::object();

    const std::string profile = config["profile"].string();
    if (profile == "default" || profile == "night" || profile == "cinema")
        changes.layout.set("ambience", Json::text(profile));
    const Json launcher = config["launcher"];
    const std::string clock = launcher["clockFormat"].string();
    if (clock == "12" || clock == "24")
        changes.layout.set("clockFormat", Json::text(clock + "h"));
    if (launcher["showDate"].isBoolean()) changes.layout.set("showDate", launcher["showDate"]);
    if (launcher["dockInputs"].isBoolean()) changes.layout.set("dockInputs", launcher["dockInputs"]);
    if (launcher["focusColour"].isString()) {
        const std::string accent = focusAccent(launcher["focusColour"].string());
        if (accent.empty()) {
            changes.layout.set("colours", Json::parse("null"));
        } else {
            Json colours = Json::object();
            colours.set("text", Json::text("#ffffff"));
            colours.set("accent", Json::text(accent));
            changes.layout.set("colours", colours);
        }
    }

    const Json background = config["background"];
    const std::string source = background["source"].string();
    if (source == "usb") changes.wallpaper.set("source", Json::text("usb"));
    else if (source == "builtin") changes.wallpaper.set("source", Json::text("photos"));
    else if (source == "preset") {
        changes.wallpaper.set("source", Json::text("gradient"));
        const std::string preset = background["preset"].string();
        if (preset == "midnight" || preset == "cool-gradient" || preset == "warm-gradient" || preset == "ember")
            changes.wallpaper.set("gradient", Json::text(preset));
    }
    if (background["kenBurns"].isBoolean()) changes.wallpaper.set("zoom", background["kenBurns"]);
    const double seconds = background["slideshowIntervalSec"].number(-1);
    if (seconds >= 60) changes.wallpaper.set("interval", Json::numeric(std::min(1440.0, std::max(1.0, seconds / 60))));

    const Json music = config["music"];
    if (music["enabled"].isBoolean()) changes.music.set("enabled", music["enabled"]);
    if (music["shuffle"].isBoolean()) changes.music.set("shuffle", music["shuffle"]);
    const std::string musicSource = music["source"].string();
    if (musicSource == "builtin" || musicSource == "usb") changes.music.set("source", Json::text(musicSource));
    const double volume = music["volume"].number(-1);
    if (volume >= 0 && volume <= 1) changes.music.set("volume", Json::numeric(static_cast<int>(volume * 100 + .5)));

    const Json weather = config["weather"];
    if (weather["enabled"].isBoolean()) changes.weather.set("enabled", weather["enabled"]);
    const std::string units = weather["units"].string();
    if (units == "c" || units == "f") changes.weather.set("units", Json::text(units));
    const Json location = weather["location"];
    if (location["name"].isString() && location["latitude"].isNumber() && location["longitude"].isNumber()) {
        Json place = Json::object();
        place.set("name", location["name"]);
        place.set("latitude", location["latitude"]);
        place.set("longitude", location["longitude"]);
        changes.weather.set("place", place);
    }
    return changes;
}

} // namespace service
} // namespace nativetv
