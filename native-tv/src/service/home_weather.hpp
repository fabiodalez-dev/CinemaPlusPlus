#pragma once

#include "core/json.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace service {

// The weather on Home, as Cinema shows it: now and the next days from
// Open-Meteo (no key), for a place found with its geocoding search. Settings
// and the last forecast live in home-weather.json; a worker refreshes the
// forecast every fifteen minutes and when the place changes.
class HomeWeather {
  public:
    using Get = std::function<bool(const std::string &url, std::string &body, long &status)>;
    using Clock = std::function<int64_t()>; // wall clock, milliseconds

    HomeWeather(std::string dataRoot, Get get = Get(), Clock clock = Clock());
    ~HomeWeather();
    HomeWeather(const HomeWeather &) = delete;
    HomeWeather &operator=(const HomeWeather &) = delete;

    // {"enabled", "units": "c"|"f", "place", "state": off|loading|online|offline,
    //  "now": {"temp", "code", "day"}, "days": [{"date", "code", "high", "low", "rain"}]}.
    Json feed();
    // Change any of {"enabled", "units", "place": {"name", "latitude", "longitude"}}.
    bool configure(const Json &change, std::string &error);
    // Places for a name: [{"name", "latitude", "longitude"}], at most eight.
    Json search(const std::string &query, const std::string &language = "en");

    // Exposed for the tests.
    static std::string condition(int code);
    static Json parseForecast(const Json &raw, int days);

  private:
    void refresh();
    void workerLoop();
    void save();

    std::string path_;
    Get get_;
    Clock clock_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool requested_ = true;
    Json settings_;
    Json forecast_;
    std::string state_ = "loading";
    int64_t fetchedAtMs_ = 0;
};

} // namespace service
} // namespace nativetv
