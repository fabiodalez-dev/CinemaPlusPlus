#include "service/home_wallpaper_source.hpp"
#include <algorithm>
#include <cstdio>
#include <curl/curl.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace nativetv {
namespace service {
namespace {
// Cinema's photos, 1920 px, from its repository at the release Cinema++ follows.
const char *kPhotosBase = "https://cdn.jsdelivr.net/gh/fabiodalez-dev/Cinema@v0.3.9/assets/backgrounds/1920/";
const struct {
    const char *id, *title;
} kPhotos[] = {{"azure-cove", "Azure Cove"},
               {"palm-shore", "Palm Shore"},
               {"mountain-sunset", "Mountain Sunset"},
               {"sunny-peaks", "Sunny Peaks"},
               {"starry-night", "Starry Night"}};
// Cinema's gradient presets (background.js); the TV draws them.
const struct {
    const char *id, *title;
} kGradients[] = {{"midnight", "Midnight"},
                  {"cool-gradient", "Cool"},
                  {"warm-gradient", "Warm"},
                  {"ember", "Ember"}};
constexpr size_t kUsbMaximum = 200;
constexpr size_t kPictureBytes = 12 * 1024 * 1024;

std::string join(const std::string &root, const std::string &name) {
    const std::string base = root.empty() ? "/var/lib/tvweb" : root;
    return base + (base[base.size() - 1] == '/' ? "" : "/") + name;
}

std::string settingsPath(const std::string &dataRoot) {
    return join(dataRoot, "home-wallpaper-source.json");
}

std::string photosDirectory(const std::string &dataRoot) {
    return join(dataRoot, "home-wallpaper-cinema");
}

bool regularFile(const std::string &path) {
    struct stat info = {};
    return lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
           static_cast<unsigned long long>(info.st_size) <= kPictureBytes;
}

bool picture(const std::string &name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const char *extension : {".jpg", ".jpeg", ".png"}) {
        const std::string ending = extension;
        if (lower.size() > ending.size() && lower.compare(lower.size() - ending.size(), ending.size(), ending) == 0)
            return name[0] != '.';
    }
    return false;
}

std::vector<std::string> entries(const std::string &directory, bool directories) {
    std::vector<std::string> names;
    DIR *dir = opendir(directory.c_str());
    if (!dir) return names;
    while (const dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat info = {};
        if (lstat((directory + "/" + name).c_str(), &info) != 0) continue;
        if (directories ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

// The USB sticks' lounge/backgrounds pictures, as Cinema finds them: /tmp/usb/<device>/<drive>.
std::vector<std::string> usbPictures(const std::string &usbRoot) {
    std::vector<std::string> files;
    for (const std::string &device : entries(usbRoot, true))
        for (const std::string &drive : entries(usbRoot + "/" + device, true)) {
            const std::string folder = usbRoot + "/" + device + "/" + drive + "/lounge/backgrounds";
            for (const std::string &name : entries(folder, false))
                if (picture(name) && files.size() < kUsbMaximum && regularFile(folder + "/" + name))
                    files.push_back(folder + "/" + name);
        }
    return files;
}

size_t sink(char *data, size_t size, size_t count, void *user) {
    return std::fwrite(data, size, count, static_cast<FILE *>(user)) * size;
}

bool curlFetch(const std::string &url, const std::string &toFile) {
    FILE *file = std::fopen(toFile.c_str(), "wb");
    if (!file) return false;
    CURL *curl = curl_easy_init();
    bool ok = false;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120000L);
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, static_cast<long>(kPictureBytes));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sink);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "CinemaPlusPlus");
        long status = 0;
        ok = curl_easy_perform(curl) == CURLE_OK &&
             curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && status == 200;
        curl_easy_cleanup(curl);
    }
    std::fclose(file);
    return ok;
}

bool knownGradient(const std::string &id) {
    for (const auto &gradient : kGradients)
        if (id == gradient.id) return true;
    return false;
}
} // namespace

Json readWallpaperSource(const std::string &dataRoot) {
    std::ifstream file(settingsPath(dataRoot).c_str());
    std::stringstream text;
    text << file.rdbuf();
    const Json saved = Json::parse(text.str());
    const std::string source = saved["source"].string();
    Json result = Json::object();
    result.set("source", Json::text(source == "gradient" || source == "photos" || source == "usb" ? source : "slots"));
    const std::string gradient = saved["gradient"].string();
    result.set("gradient", Json::text(knownGradient(gradient) ? gradient : "midnight"));
    const double interval = saved["interval"].number(10);
    result.set("interval", Json::numeric(interval >= 1 && interval <= 1440 ? static_cast<int>(interval) : 10));
    result.set("zoom", Json::flag(saved["zoom"].boolean()));
    return result;
}

bool saveWallpaperSource(const std::string &dataRoot, const Json &change, std::string &error) {
    Json next = readWallpaperSource(dataRoot);
    if (change.has("source")) {
        const std::string source = change["source"].string();
        if (source != "slots" && source != "gradient" && source != "photos" && source != "usb") {
            error = "unknown wallpaper source";
            return false;
        }
        next.set("source", Json::text(source));
    }
    if (change.has("gradient")) {
        if (!knownGradient(change["gradient"].string())) {
            error = "unknown gradient";
            return false;
        }
        next.set("gradient", change["gradient"]);
    }
    if (change.has("interval")) {
        const double interval = change["interval"].number(-1);
        if (!(interval >= 1 && interval <= 1440)) {
            error = "interval out of range";
            return false;
        }
        next.set("interval", Json::numeric(static_cast<int>(interval)));
    }
    if (change.has("zoom")) {
        if (!change["zoom"].isBoolean()) {
            error = "zoom expected";
            return false;
        }
        next.set("zoom", change["zoom"]);
    }
    const std::string path = settingsPath(dataRoot);
    {
        std::ofstream out((path + ".tmp").c_str(), std::ios::trunc);
        out << next.dump() << "\n";
        if (!out) {
            error = "could not save";
            return false;
        }
    }
    if (std::rename((path + ".tmp").c_str(), path.c_str()) != 0) {
        error = "could not save";
        return false;
    }
    return true;
}

Json wallpaperGradients() {
    Json list = Json::array();
    for (const auto &gradient : kGradients) {
        Json item = Json::object();
        item.set("id", Json::text(gradient.id));
        item.set("title", Json::text(gradient.title));
        list.add(item);
    }
    return list;
}

Json wallpaperPhotos() {
    Json list = Json::array();
    for (const auto &photo : kPhotos) {
        Json item = Json::object();
        item.set("id", Json::text(photo.id));
        item.set("title", Json::text(photo.title));
        list.add(item);
    }
    return list;
}

std::vector<WallpaperPicture> wallpaperRotation(const std::string &dataRoot, const Json &source,
                                                const std::string &usbRoot) {
    std::vector<WallpaperPicture> pictures;
    const std::string kind = source["source"].string();
    if (kind == "photos") {
        for (const auto &photo : kPhotos) {
            const std::string path = photosDirectory(dataRoot) + "/" + photo.id + ".jpg";
            if (regularFile(path)) pictures.push_back({std::string("cinema-") + photo.id, path});
        }
    } else if (kind == "usb") {
        const std::vector<std::string> files = usbPictures(usbRoot);
        for (size_t i = 0; i < files.size(); ++i) pictures.push_back({"usb-" + std::to_string(i), files[i]});
    }
    return pictures;
}

std::string wallpaperRotationFile(const std::string &dataRoot, const std::string &key, const std::string &usbRoot) {
    if (key.compare(0, 7, "cinema-") == 0) {
        for (const auto &photo : kPhotos)
            if (key.substr(7) == photo.id) {
                const std::string path = photosDirectory(dataRoot) + "/" + photo.id + ".jpg";
                return regularFile(path) ? path : "";
            }
        return "";
    }
    if (key.compare(0, 4, "usb-") == 0 && key.size() > 4 && key.size() < 8 &&
        key.find_first_not_of("0123456789", 4) == std::string::npos) {
        const std::vector<std::string> files = usbPictures(usbRoot);
        const size_t index = static_cast<size_t>(std::stoul(key.substr(4)));
        return index < files.size() ? files[index] : "";
    }
    return "";
}

bool fetchCinemaPhotos(const std::string &dataRoot, WallpaperFetch fetch) {
    if (!fetch) fetch = curlFetch;
    const std::string directory = photosDirectory(dataRoot);
    (void)mkdir(directory.c_str(), 0755);
    bool all = true;
    for (const auto &photo : kPhotos) {
        const std::string path = directory + "/" + photo.id + ".jpg";
        if (regularFile(path)) continue;
        const std::string part = path + ".part";
        if (fetch(std::string(kPhotosBase) + photo.id + ".jpg", part) && regularFile(part)) {
            std::ifstream check(part.c_str(), std::ios::binary);
            unsigned char head[2] = {0, 0};
            check.read(reinterpret_cast<char *>(head), 2);
            if (head[0] == 0xff && head[1] == 0xd8 && std::rename(part.c_str(), path.c_str()) == 0) continue;
        }
        (void)unlink(part.c_str());
        all = false;
    }
    return all;
}

void addWallpaperSource(Json &status, const std::string &dataRoot, const std::string &usbRoot) {
    const Json source = readWallpaperSource(dataRoot);
    status.set("source", source["source"]);
    status.set("gradient", source["gradient"]);
    status.set("interval", source["interval"]);
    status.set("zoom", source["zoom"]);
    Json rotation = Json::array();
    std::string revision;
    for (const WallpaperPicture &item : wallpaperRotation(dataRoot, source, usbRoot)) {
        rotation.add(Json::text(item.key));
        struct stat info = {};
        if (stat(item.path.c_str(), &info) == 0)
            revision += std::to_string(static_cast<long long>(info.st_size)) + ".";
    }
    status.set("rotation", rotation);
    if (rotation.size()) status.set("rotationRevision", Json::text(std::to_string(rotation.size()) + "-" + revision));
}

} // namespace service
} // namespace nativetv
