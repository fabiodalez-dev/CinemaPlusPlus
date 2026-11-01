#pragma once

#include "core/json.hpp"
#include <string>

namespace nativetv {
namespace service {

struct HomeWallpaperFile {
    bool validSlot = false;
    bool found = false;
    bool readError = false;
    std::string bytes;
    std::string etag;
};

enum class HomeWallpaperSaveResult {
    saved,
    invalidSlot,
    invalidImage,
    insufficientSpace,
    ioError
};

// Read one of the four uploaded Home wallpaper slots from the shared state
// directory. The slot is allow-listed and the final file cannot be a symlink.
HomeWallpaperFile readHomeWallpaper(const std::string &slot,
                                    const std::string &dataRoot);
Json homeWallpaperStatus(const std::string &dataRoot);
bool validHomeWallpaperJpeg(const std::string &bytes);
HomeWallpaperSaveResult saveHomeWallpaper(const std::string &slot,
                                          const std::string &dataRoot,
                                          const std::string &bytes,
                                          std::string *error = nullptr);
// On the first start, put the bundled wallpaper in all four slots so Home is
// never a flat colour. A marker file records that it ran: slots the viewer
// clears or replaces later are left as they are.
bool seedHomeWallpapers(const std::string &dataRoot, const std::string &bundledJpeg,
                        std::string *error = nullptr);
bool removeHomeWallpapers(const std::string &dataRoot,
                          std::string *error = nullptr);

} // namespace service
} // namespace nativetv
