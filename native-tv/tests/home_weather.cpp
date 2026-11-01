// The weather on Home, with a stand-in Open-Meteo.
#include "service/home_weather.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
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
const char *kForecast =
    R"({"current":{"temperature_2m":14.6,"weather_code":61,"is_day":0},
        "daily":{"time":["2026-11-01","2026-11-02","2026-11-03","2026-11-04","2026-11-05","2026-11-06","2026-11-07"],
                 "weather_code":[61,0,3,45,71,95,2],
                 "temperature_2m_max":[16.4,18,17,15,9,12,13],
                 "temperature_2m_min":[9.5,10,11,8,1,6,7],
                 "precipitation_probability_max":[80,0,10,5,60,90,20]}})";
} // namespace

int main() {
    check(HomeWeather::condition(0) == "clear" && HomeWeather::condition(65) == "pouring" &&
              HomeWeather::condition(95) == "storm" && HomeWeather::condition(48) == "fog",
          "WMO codes");
    const Json parsed = HomeWeather::parseForecast(Json::parse(kForecast), 5);
    check(parsed["now"]["temp"].number() == 15 && !parsed["now"]["day"].boolean(), "now, rounded, at night");
    check(parsed["days"].size() == 5 && parsed["days"][0]["high"].number() == 16 &&
              parsed["days"][0]["low"].number() == 10 && parsed["days"][0]["rain"].number() == 80,
          "five days");

    char pattern[] = "/tmp/home-weather-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    std::string lastUrl;
    int forecasts = 0;
    const auto get = [&](const std::string &url, std::string &body, long &status) {
        lastUrl = url;
        status = 200;
        if (url.find("geocoding-api") != std::string::npos)
            body = R"({"results":[{"name":"Bologna","admin1":"Emilia-Romagna","country":"Italia","latitude":44.49,"longitude":11.34},{"name":"Nowhere"}]})";
        else {
            ++forecasts;
            body = kForecast;
        }
        return true;
    };
    {
        HomeWeather weather(dir, get);
        for (int i = 0; i < 100 && weather.feed()["state"].string() != "online"; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Json feed = weather.feed();
        check(feed["state"].string() == "online" && feed["now"]["icon"].string() == "rain", "online with an icon");
        check(feed["place"]["name"].string() == "City of London, UK", "Cinema's default place");
        check(lastUrl.find("latitude=51.5123") != std::string::npos, "the place's coordinates");

        const Json places = weather.search("bolo", "it");
        check(places.size() == 1 && places[0]["name"].string() == "Bologna, Emilia-Romagna, Italia", "places");
        check(lastUrl.find("language=it&name=bolo") != std::string::npos, "the language and the name");

        std::string error;
        Json change = Json::object();
        Json place = places[0];
        change.set("place", place);
        change.set("units", Json::text("f"));
        const int before = forecasts;
        check(weather.configure(change, error), "a new place");
        for (int i = 0; i < 100 && forecasts == before; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(lastUrl.find("latitude=44.4900") != std::string::npos &&
                  lastUrl.find("temperature_unit=fahrenheit") != std::string::npos,
              "asked again for the new place in Fahrenheit");
        Json bad = Json::object();
        bad.set("place", Json::object());
        check(!weather.configure(bad, error) && !error.empty(), "a place without coordinates");
    }
    {
        // The place and the forecast are kept for the next start.
        HomeWeather again(dir, [](const std::string &, std::string &, long &) { return false; });
        Json feed = again.feed();
        check(feed["place"]["name"].string() == "Bologna, Emilia-Romagna, Italia" && feed["units"].string() == "f",
              "settings kept");
        check(feed["days"].size() == 5, "last forecast kept");
        std::string error;
        Json off = Json::object();
        off.set("enabled", Json::flag(false));
        check(again.configure(off, error) && again.feed()["state"].string() == "off" && !again.feed().has("days"),
              "switched off");
    }
    std::remove((dir + "/home-weather.json").c_str());
    rmdir(dir.c_str());
    std::cout << "home weather tests passed\n";
    return 0;
}
