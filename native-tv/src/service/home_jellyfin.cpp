#include "service/home_jellyfin.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <curl/curl.h>
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace nativetv {
namespace service {
namespace {
constexpr int64_t kStaleMs = 10 * 60 * 1000;
const char *kFields = "Fields=Overview,Genres,ParentId&EnableImageTypes=Backdrop,Primary,Thumb&ImageTypeLimit=1";

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

size_t collect(char *data, size_t size, size_t count, void *user) {
    auto *target = static_cast<std::pair<std::string *, size_t> *>(user);
    const size_t n = size * count;
    if (target->first->size() + n > target->second) return 0;
    target->first->append(data, n);
    return n;
}

bool curlGet(const std::string &url, const std::string &authorization, std::string &body, long &status,
             size_t maxBytes) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    std::pair<std::string *, size_t> sink(&body, maxBytes);
    curl_slist *headers = curl_slist_append(nullptr, ("Authorization: " + authorization).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

constexpr size_t kMaxArtwork = 240;

// The library's pictures in the cache folder ("jf-*.jpg"), oldest removed past
// the cap.
void pruneArtwork(const std::string &dir) {
    std::vector<std::pair<time_t, std::string>> files;
    if (DIR *folder = opendir(dir.c_str())) {
        while (const dirent *entry = readdir(folder)) {
            const std::string name = entry->d_name;
            if (name.compare(0, 3, "jf-") != 0) continue;
            struct stat st;
            if (lstat((dir + "/" + name).c_str(), &st) == 0 && S_ISREG(st.st_mode))
                files.push_back({st.st_mtime, name});
        }
        closedir(folder);
    }
    if (files.size() < kMaxArtwork) return;
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i + kMaxArtwork <= files.size(); ++i) std::remove((dir + "/" + files[i].second).c_str());
}

std::string readText(const std::string &path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

bool hexId(const std::string &id) {
    if (id.size() < 32 || id.size() > 36) return false;
    for (char c : id)
        if (!std::isxdigit(static_cast<unsigned char>(c)) && c != '-') return false;
    return true;
}

std::string stripTags(const std::string &text) {
    std::string out;
    bool tag = false;
    for (char c : text) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) out += c;
    }
    return out.size() > 650 ? out.substr(0, 650) : out;
}

std::string urlEncode(const std::string &value) {
    std::string out;
    char buf[4];
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

Json row(const std::string &key, const Json &items) {
    Json r = Json::object();
    r.set("key", Json::text(key));
    r.set("items", items);
    return r;
}
} // namespace

HomeJellyfin::HomeJellyfin(std::string dataRoot, std::string connectionPath, Get get, Clock clock)
    : dataRoot_(std::move(dataRoot)), connectionPath_(std::move(connectionPath)),
      cachePath_(dataRoot_ + "/home-jellyfin.json"), get_(get ? std::move(get) : Get(curlGet)),
      clock_(clock ? std::move(clock) : Clock(steadyMs)) {
    worker_ = std::thread(&HomeJellyfin::workerLoop, this);
}

HomeJellyfin::~HomeJellyfin() {
    stopping_ = true;
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
}

HomeJellyfin::Connection HomeJellyfin::readConnection() const {
    Connection c;
    try {
        const Json j = Json::parse(readText(connectionPath_));
        c.server = j["server"].isString() ? j["server"].string() : "";
        while (!c.server.empty() && c.server.back() == '/') c.server.pop_back();
        if (c.server.compare(0, 7, "http://") != 0 && c.server.compare(0, 8, "https://") != 0) c.server.clear();
        c.userId = j["userId"].isString() && hexId(j["userId"].string()) ? j["userId"].string() : "";
        c.token = j["token"].isString() ? j["token"].string() : "";
        c.userName = j["userName"].isString() ? j["userName"].string() : "";
    } catch (...) {
    }
    return c;
}

bool HomeJellyfin::getJson(const Connection &c, const std::string &path, Json &out, long &status) {
    std::string body;
    status = 0;
    if (!get_(c.server + path, "MediaBrowser Token=\"" + c.token + "\"", body, status, 4 * 1024 * 1024))
        return false;
    if (status != 200) return false;
    try {
        out = Json::parse(body);
    } catch (...) {
        return false;
    }
    return true;
}

Json HomeJellyfin::normalizeItem(const Json &raw) {
    Json item = Json::object();
    const std::string type = raw["Type"].isString() ? raw["Type"].string() : "";
    const bool episode = type == "Episode";
    item.set("id", raw["Id"]);
    item.set("type", Json::text(type));
    const std::string series = raw["SeriesName"].isString() ? raw["SeriesName"].string() : "";
    const std::string name = raw["Name"].isString() ? raw["Name"].string() : "";
    item.set("title", Json::text(!series.empty() ? series : name));
    if (episode) item.set("episode", Json::text(name));
    if (raw["SeriesId"].isString()) item.set("seriesId", raw["SeriesId"]);
    const int year = raw["ProductionYear"].isNumber() ? int(raw["ProductionYear"].number()) : 0;
    if (year) item.set("year", Json::numeric(year));
    Json genres = Json::array();
    if (raw["Genres"].isArray())
        for (size_t i = 0; i < raw["Genres"].size() && i < 2; ++i) genres.add(raw["Genres"][i]);
    item.set("genres", genres);
    if (raw["Overview"].isString()) item.set("overview", Json::text(stripTags(raw["Overview"].string())));
    const double ticks = raw["RunTimeTicks"].isNumber() ? raw["RunTimeTicks"].number() : 0;
    if (ticks > 0) item.set("minutes", Json::numeric(std::round(ticks / 6e8)));
    const double position = raw["UserData"]["PlaybackPositionTicks"].isNumber()
                                ? raw["UserData"]["PlaybackPositionTicks"].number() : 0;
    if (ticks > 0 && position > 0) {
        item.set("progress", Json::numeric(std::round(position / ticks * 100)));
        item.set("remaining", Json::numeric(std::max(1.0, std::round((ticks - position) / 6e8))));
    }
    if (raw["ParentIndexNumber"].isNumber()) item.set("season", raw["ParentIndexNumber"]);
    if (raw["IndexNumber"].isNumber()) item.set("number", raw["IndexNumber"]);
    item.set("music", Json::flag(type == "MusicAlbum" || type == "Audio" || type == "MusicArtist"));
    // Artwork: its own backdrop, the series' backdrop, a thumb, the poster.
    std::string image, backdrop;
    const Json tags = raw["ImageTags"];
    if (raw["BackdropImageTags"].isArray() && raw["BackdropImageTags"].size()) backdrop = raw["Id"].string() + ":Backdrop";
    else if (raw["ParentBackdropItemId"].isString()) backdrop = raw["ParentBackdropItemId"].string() + ":Backdrop";
    if (!backdrop.empty()) image = backdrop;
    else if (tags["Thumb"].isString()) image = raw["Id"].string() + ":Thumb";
    else if (tags["Primary"].isString()) image = raw["Id"].string() + ":Primary";
    if (!image.empty()) item.set("image", Json::text(image));
    if (!backdrop.empty()) item.set("backdrop", Json::text(backdrop));
    return item;
}

Json HomeJellyfin::mergeContinue(const Json &resume, const Json &nextUp) {
    Json out = Json::array();
    std::vector<std::string> seen;
    for (const Json *list : {&resume, &nextUp}) {
        if (!list->isArray()) continue;
        for (const Json &item : list->items()) {
            const std::string id = item["id"].isString() ? item["id"].string() : "";
            const std::string series = item["seriesId"].isString() ? item["seriesId"].string() : "";
            bool dup = false;
            for (const std::string &s : seen) dup = dup || s == id || (!series.empty() && s == series);
            if (dup) continue;
            seen.push_back(id);
            if (!series.empty()) seen.push_back(series);
            out.add(item);
            if (out.size() >= 12) return out;
        }
    }
    return out;
}

Json HomeJellyfin::items(const Connection &c, const std::string &path, long &status) {
    Json doc;
    Json out = Json::array();
    if (!getJson(c, path, doc, status)) return out;
    if (doc["Items"].isArray())
        for (const Json &raw : doc["Items"].items()) out.add(normalizeItem(raw));
    return out;
}

void HomeJellyfin::refresh() {
    const Connection c = readConnection();
    Json result = Json::object();
    if (!c.valid()) {
        result.set("state", Json::text("unconfigured"));
        result.set("rows", Json::array());
    } else {
        const std::string user = "/Users/" + c.userId + "/Items";
        const std::string common = std::string("&") + kFields + "&Limit=12";
        long sResume = 0, sNext = 0, sAll = 0, sMovies = 0, sSeries = 0, sMusic = 0;
        const Json resume = items(c, user + "/Resume?MediaTypes=Video" + common, sResume);
        const Json next = items(c, "/Shows/NextUp?UserId=" + c.userId + common, sNext);
        const std::string latest = "?Recursive=true&SortBy=DateCreated&SortOrder=Descending";
        const Json all = items(c, user + latest + "&IncludeItemTypes=Movie,Series" + common, sAll);
        // The Film, Serie and Musica tabs show these: more than Home's twelve.
        const std::string many = std::string("&") + kFields + "&Limit=36";
        const Json movies = items(c, user + latest + "&IncludeItemTypes=Movie" + many, sMovies);
        const Json series = items(c, user + latest + "&IncludeItemTypes=Series" + many, sSeries);
        const Json music = items(c, user + latest + "&IncludeItemTypes=MusicAlbum" + many, sMusic);
        Json rows = Json::array();
        const Json cont = mergeContinue(resume, next);
        if (cont.size()) rows.add(row("jf-continue", cont));
        if (all.size()) rows.add(row("jf-recent", all));
        if (movies.size()) rows.add(row("jf-movies", movies));
        if (series.size()) rows.add(row("jf-series", series));
        if (music.size()) rows.add(row("jf-music", music));
        const bool unauthorized = sAll == 401 || sAll == 403;
        result.set("state", Json::text(unauthorized ? "unauthorized" : sAll == 200 ? "online" : "offline"));
        result.set("user", Json::text(c.userName));
        result.set("rows", rows);
        // Scopri as on Cinema: Seerr behind LiteFin Companion on the server, when it is set up.
        Json seerr;
        long sSeerr = 0;
        Json status = Json::object();
        if (getJson(c, "/Litefin/Seerr/Status", seerr, sSeerr) && sSeerr == 200) {
            status.set("configured", Json::flag(seerr["configured"].boolean()));
            status.set("available", Json::flag(seerr["available"].boolean()));
        } else {
            status.set("configured", Json::flag(false));
            status.set("available", Json::flag(false));
            status.set("plugin", Json::flag(false));
        }
        result.set("seerr", status);
        // Offline keeps the last rows Home had.
        if (sAll != 200 && !unauthorized) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cache_["rows"].isArray() && cache_["rows"].size()) result.set("rows", cache_["rows"]);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    cache_ = result;
    fetchedAtMs_ = clock_();
    const std::string tmp = cachePath_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << result.dump();
    }
    chmod(tmp.c_str(), 0600);
    std::rename(tmp.c_str(), cachePath_.c_str());
}

void HomeJellyfin::workerLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        changed_.wait(lock, [this]() { return stopping_ || requested_; });
        if (stopping_) break;
        requested_ = false;
        lock.unlock();
        refresh();
        lock.lock();
    }
}

Json HomeJellyfin::feed() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_) {
        loaded_ = true;
        try {
            cache_ = Json::parse(readText(cachePath_));
        } catch (...) {
        }
    }
    const bool stale = !fetchedAtMs_ || clock_() - fetchedAtMs_ > kStaleMs;
    if (stale && !requested_) {
        requested_ = true;
        changed_.notify_all();
    }
    if (!cache_.isObject()) {
        Json loading = Json::object();
        loading.set("state", Json::text("loading"));
        loading.set("rows", Json::array());
        return loading;
    }
    return cache_;
}

void HomeJellyfin::refreshNow() {
    std::lock_guard<std::mutex> lock(mutex_);
    requested_ = true;
    changed_.notify_all();
}

bool HomeJellyfin::image(const std::string &spec, std::string &bytes) {
    // <itemId>:<Type>:<width>
    const size_t a = spec.find(':'), b = spec.find(':', a == std::string::npos ? 0 : a + 1);
    if (a == std::string::npos || b == std::string::npos) return false;
    const std::string id = spec.substr(0, a), type = spec.substr(a + 1, b - a - 1);
    int width = std::atoi(spec.substr(b + 1).c_str());
    if (!hexId(id) || (type != "Primary" && type != "Backdrop" && type != "Thumb")) return false;
    // Only the two sizes Home draws: any other width would be one more file on
    // the TV's flash for anyone who can reach the dashboard.
    width = width > 640 ? 1280 : 640;
    const std::string dir = dataRoot_ + "/home-images";
    mkdir(dir.c_str(), 0700);
    const std::string file = dir + "/jf-" + id + "-" + type + "-" + std::to_string(width) + ".jpg";
    bytes = readText(file);
    if (!bytes.empty()) return true;
    const Connection c = readConnection();
    if (!c.valid()) return false;
    long status = 0;
    const std::string path = "/Items/" + id + "/Images/" + type + (type == "Backdrop" ? "/0" : "") +
                             "?MaxWidth=" + std::to_string(width) + "&Quality=70&Format=jpg";
    if (!get_(c.server + path, "MediaBrowser Token=\"" + c.token + "\"", bytes, status, 3 * 1024 * 1024) ||
        status != 200 || bytes.empty()) {
        bytes.clear();
        return false;
    }
    // At most kMaxArtwork pictures of the library: the oldest go first.
    pruneArtwork(dir);
    const std::string tmp = file + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        const bool written = write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size());
        close(fd);
        if (!written || std::rename(tmp.c_str(), file.c_str()) != 0) std::remove(tmp.c_str());
    }
    return true;
}

Json HomeJellyfin::search(const std::string &query) {
    const Connection c = readConnection();
    Json result = Json::object();
    if (!c.valid() || query.size() < 2) {
        result.set("items", Json::array());
        return result;
    }
    long status = 0;
    const Json found = items(c, "/Users/" + c.userId + "/Items?Recursive=true&SearchTerm=" + urlEncode(query) +
                                    "&IncludeItemTypes=Movie,Series,MusicAlbum,Audio,MusicArtist&" + kFields +
                                    "&Limit=24", status);
    // Films and series before music, each in the server's order.
    Json ordered = Json::array();
    for (int pass = 0; pass < 2; ++pass)
        for (const Json &item : found.items()) {
            const std::string type = item["type"].string();
            if ((type == "Movie" || type == "Series") == (pass == 0))
                ordered.add(item);
        }
    result.set("items", ordered);
    return result;
}

} // namespace service
} // namespace nativetv
