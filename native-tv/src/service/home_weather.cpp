#include "service/home_weather.hpp"
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <curl/curl.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unistd.h>

namespace nativetv {
namespace service {
namespace {

constexpr int64_t kRefreshMs = 15 * 60 * 1000;
constexpr int64_t kRetryMs = 2 * 60 * 1000;
constexpr int kDaysShown = 5;

size_t collect(char *data, size_t size, size_t count, void *user) {
    auto *body = static_cast<std::string *>(user);
    const size_t n = size * count;
    if (body->size() + n > 512 * 1024) return 0;
    body->append(data, n);
    return n;
}

// Stops a transfer when the daemon is closing, so shutting down never waits
// for Open-Meteo's timeout.
int abortWhenStopping(void *stopping, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return stopping && static_cast<std::atomic<bool> *>(stopping)->load() ? 1 : 0;
}

bool curlGet(const std::string &url, std::string &body, long &status, std::atomic<bool> *stopping) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortWhenStopping);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, stopping);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 12000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

int64_t wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string urlEncode(const std::string &value) {
    std::ostringstream out;
    static const char digits[] = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << digits[c >> 4] << digits[c & 15];
    }
    return out.str();
}

std::string coordinate(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

// Cinema's default place, until one is chosen.
Json defaultPlace() {
    Json place = Json::object();
    place.set("name", Json::text("City of London, UK"));
    place.set("latitude", Json::numeric(51.5123));
    place.set("longitude", Json::numeric(-0.0907));
    return place;
}

bool validPlace(const Json &place) {
    return place.isObject() && place["latitude"].isNumber() && place["longitude"].isNumber() &&
           std::fabs(place["latitude"].number()) <= 90 && std::fabs(place["longitude"].number()) <= 180;
}
} // namespace

HomeWeather::HomeWeather(std::string dataRoot, Get get, Clock clock)
    : path_((dataRoot.empty() ? std::string("/var/lib/tvweb") : dataRoot) + "/home-weather.json"),
      get_(get), clock_(clock ? clock : Clock(wallMs)) {
    if (!get_)
        get_ = [this](const std::string &url, std::string &body, long &status) {
            return curlGet(url, body, status, &stopping_);
        };
    std::ifstream file(path_.c_str());
    std::stringstream text;
    text << file.rdbuf();
    const Json saved = Json::parse(text.str());
    settings_ = Json::object();
    settings_.set("enabled", Json::flag(saved["enabled"].boolean(true)));
    settings_.set("units", Json::text(saved["units"].string() == "f" ? "f" : "c"));
    settings_.set("place", validPlace(saved["place"]) ? saved["place"] : defaultPlace());
    if (saved["forecast"].isObject()) {
        forecast_ = saved["forecast"];
        fetchedAtMs_ = static_cast<int64_t>(saved["fetchedAt"].number());
        state_ = "online";
    }
    worker_ = std::thread([this]() { workerLoop(); });
}

HomeWeather::~HomeWeather() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::string HomeWeather::condition(int code) {
    switch (code) {
    case 0: case 1: return "clear";
    case 2: return "partly";
    case 3: return "cloudy";
    case 45: case 48: return "fog";
    case 51: case 53: case 55: case 56: case 57: case 61: case 63: case 66: case 80: return "rain";
    case 65: case 67: case 81: case 82: return "pouring";
    case 71: case 73: case 75: case 77: case 85: case 86: return "snow";
    case 95: case 96: case 99: return "storm";
    default: return "cloudy";
    }
}

Json HomeWeather::parseForecast(const Json &raw, int days) {
    Json result = Json::object();
    const Json current = raw["current"];
    if (current["temperature_2m"].isNumber()) {
        Json now = Json::object();
        now.set("temp", Json::numeric(std::round(current["temperature_2m"].number())));
        now.set("code", Json::numeric(current["weather_code"].number()));
        now.set("day", Json::flag(current["is_day"].number(1) != 0));
        result.set("now", now);
    }
    Json list = Json::array();
    const Json daily = raw["daily"];
    for (size_t i = 0; i < daily["time"].size() && static_cast<int>(i) < days; ++i) {
        Json day = Json::object();
        day.set("date", daily["time"][i]);
        day.set("code", Json::numeric(daily["weather_code"][i].number()));
        day.set("high", Json::numeric(std::round(daily["temperature_2m_max"][i].number())));
        day.set("low", Json::numeric(std::round(daily["temperature_2m_min"][i].number())));
        day.set("rain", Json::numeric(daily["precipitation_probability_max"][i].number()));
        list.add(day);
    }
    result.set("days", list);
    return result;
}

void HomeWeather::refresh() {
    Json place, units;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        place = settings_["place"];
        units = settings_["units"];
    }
    const std::string url = "https://api.open-meteo.com/v1/forecast?latitude=" +
                            coordinate(place["latitude"].number()) +
                            "&longitude=" + coordinate(place["longitude"].number()) +
                            "&current=temperature_2m,weather_code,is_day"
                            "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                            "precipitation_probability_max&timezone=auto&forecast_days=7" +
                            std::string(units.string() == "f" ? "&temperature_unit=fahrenheit" : "");
    std::string body;
    long status = 0;
    const bool ok = get_(url, body, status) && status == 200;
    const Json parsed = ok ? parseForecast(Json::parse(body), kDaysShown) : Json();
    std::lock_guard<std::mutex> lock(mutex_);
    if (ok && parsed["days"].size()) {
        forecast_ = parsed;
        fetchedAtMs_ = clock_();
        state_ = "online";
        save();
    } else {
        state_ = forecast_.isObject() ? "online" : "offline";
        fetchedAtMs_ = clock_() - kRefreshMs + kRetryMs;
    }
}

void HomeWeather::workerLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        const bool enabled = settings_["enabled"].boolean(true);
        const bool stale = clock_() - fetchedAtMs_ >= kRefreshMs;
        if (enabled && (requested_ || stale)) {
            requested_ = false;
            lock.unlock();
            refresh();
            lock.lock();
            continue;
        }
        changed_.wait_for(lock, std::chrono::seconds(60));
    }
}

void HomeWeather::save() {
    Json document = settings_;
    if (forecast_.isObject()) document.set("forecast", forecast_);
    document.set("fetchedAt", Json::numeric(static_cast<double>(fetchedAtMs_)));
    const std::string staging = path_ + ".part";
    std::ofstream out(staging.c_str(), std::ios::trunc);
    out << document.dump();
    out.close();
    if (!out || std::rename(staging.c_str(), path_.c_str()) != 0) std::remove(staging.c_str());
}

Json HomeWeather::feed() {
    std::lock_guard<std::mutex> lock(mutex_);
    Json result = Json::object();
    const bool enabled = settings_["enabled"].boolean(true);
    result.set("enabled", Json::flag(enabled));
    result.set("units", settings_["units"]);
    result.set("place", settings_["place"]);
    result.set("state", Json::text(enabled ? state_ : "off"));
    if (enabled && forecast_.isObject()) {
        if (forecast_.has("now")) {
            Json now = forecast_["now"];
            now.set("icon", Json::text(condition(static_cast<int>(now["code"].number()))));
            result.set("now", now);
        }
        Json days = Json::array();
        for (const Json &day : forecast_["days"].items()) {
            Json entry = day;
            entry.set("icon", Json::text(condition(static_cast<int>(day["code"].number()))));
            days.add(entry);
        }
        result.set("days", days);
    }
    return result;
}

bool HomeWeather::configure(const Json &change, std::string &error) {
    if (!change.isObject()) {
        error = "settings expected";
        return false;
    }
    if (change.has("place") && !validPlace(change["place"])) {
        error = "place needs a latitude and a longitude";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool refetch = false;
        if (change["enabled"].isBoolean()) settings_.set("enabled", change["enabled"]);
        if (change["units"].isString()) {
            const std::string units = change["units"].string() == "f" ? "f" : "c";
            refetch = units != settings_["units"].string();
            settings_.set("units", Json::text(units));
        }
        if (change.has("place")) {
            Json place = Json::object();
            place.set("name", Json::text(change["place"]["name"].string("").substr(0, 120)));
            place.set("latitude", Json::numeric(change["place"]["latitude"].number()));
            place.set("longitude", Json::numeric(change["place"]["longitude"].number()));
            settings_.set("place", place);
            refetch = true;
        }
        if (refetch) {
            forecast_ = Json();
            state_ = "loading";
            requested_ = true;
        }
        save();
    }
    changed_.notify_all();
    return true;
}

Json HomeWeather::search(const std::string &query, const std::string &language) {
    Json places = Json::array();
    if (query.size() < 2 || query.size() > 80) return places;
    std::string body;
    long status = 0;
    if (!get_("https://geocoding-api.open-meteo.com/v1/search?count=8&format=json&language=" +
                  (language.size() == 2 ? language : std::string("en")) + "&name=" + urlEncode(query),
              body, status) ||
        status != 200)
        return places;
    for (const Json &hit : Json::parse(body)["results"].items()) {
        if (!hit["latitude"].isNumber() || !hit["longitude"].isNumber()) continue;
        // As Cinema labels them: the town, its region, its country.
        std::string name = hit["name"].string();
        for (const char *key : {"admin1", "country"}) {
            const std::string part = hit[key].string();
            if (!part.empty() && part != name) name += ", " + part;
        }
        Json place = Json::object();
        place.set("name", Json::text(name));
        place.set("latitude", hit["latitude"]);
        place.set("longitude", hit["longitude"]);
        places.add(place);
    }
    return places;
}

} // namespace service
} // namespace nativetv
