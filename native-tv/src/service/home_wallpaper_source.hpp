#pragma once

#include "core/json.hpp"
#include <functional>
#include <string>
#include <vector>

namespace nativetv {
namespace service {

// Where Home's wallpaper comes from, as Cinema's backgrounds: the four pictures by time of day
// ("slots", Native Tv's uploads), one of Cinema's gradients ("gradient"), Cinema's own photos
// ("photos", downloaded once from its repository) or the pictures of a USB stick's
// lounge/backgrounds folder ("usb"). Photos and USB pictures change every `interval` minutes;
// `zoom` is Cinema's slow zoom. Settings live in home-wallpaper-source.json.
struct WallpaperPicture {
    std::string key;  // "cinema-<id>" or "usb-<n>"
    std::string path; // the file on the TV
};

// {"source", "gradient", "interval", "zoom"} with the defaults filled in.
Json readWallpaperSource(const std::string &dataRoot);
// Change any of those; false with `error` for a value out of range.
bool saveWallpaperSource(const std::string &dataRoot, const Json &change, std::string &error);
// Cinema's gradients and photos: [{"id", "title"}].
Json wallpaperGradients();
Json wallpaperPhotos();
// The pictures the source rotates through, in order (empty for slots and gradients).
std::vector<WallpaperPicture> wallpaperRotation(const std::string &dataRoot, const Json &source,
                                                const std::string &usbRoot = "/tmp/usb");
// The file behind a rotation key, or "" when the key is not one of them.
std::string wallpaperRotationFile(const std::string &dataRoot, const std::string &key,
                                  const std::string &usbRoot = "/tmp/usb");
// Downloads Cinema's photos that are not on the TV yet; true when all of them are there.
using WallpaperFetch = std::function<bool(const std::string &url, const std::string &toFile)>;
bool fetchCinemaPhotos(const std::string &dataRoot, WallpaperFetch fetch = WallpaperFetch());
// The status the catalogue carries for the TV: source, gradient, zoom, interval and rotation keys.
void addWallpaperSource(Json &status, const std::string &dataRoot, const std::string &usbRoot = "/tmp/usb");

} // namespace service
} // namespace nativetv
