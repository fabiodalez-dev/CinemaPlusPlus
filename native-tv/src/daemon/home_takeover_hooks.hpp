#pragma once

#include "core/json.hpp"
#include <string>

namespace nativetv {
namespace daemon {
// Hook-based Home takeover for the W22 family of LG TVs (webOS 7.x and 8.x).
//
// Their Home is a native binary that loads QML modules at
// /usr/palm/applications/com.webos.app.home/qml/UserInterfaceLayer/Containers/
// MainView.qml with no writable layout XML (webOS 9) and no overlayable
// Flutter assets (webOS 10). A bind-mount strategy does not apply, so the
// takeover is driven by two coordinated hooks already present on a rooted TV:
//
//   1. LG Input Hook (`/home/root/.config/lginputhook/keybinds.json`),
//      which the lginputhook service reads to turn the physical Home key
//      (code 773) into a `launch` of Native Tv on its Home screen.
//   2. The Homebrew Channel boot init.d (`/var/lib/webosbrew/init.d/`),
//      which runs as root once SAM is up and launches Native Tv so the TV
//      boots straight into our Home rather than LG's.
//
// takeoverViaHooks writes both files, backs up any previous keybind under
// `<dataRoot>/home-webos22-keybinds.json.bak`, and reports whether anything
// changed. releaseViaHooks removes the init.d entry and restores the previous
// keybind (or an empty object) exactly.
//
// Rooted (prefixes a system root to every path) is kept so tests can run
// against an isolated tree without touching the real filesystem.
Json takeoverViaHooks(const std::string &systemRoot,
                      const std::string &dataRoot);
Json releaseViaHooks(const std::string &systemRoot,
                     const std::string &dataRoot);
// True when the files written by takeoverViaHooks are currently in place.
bool activeViaHooks(const std::string &systemRoot,
                    const std::string &dataRoot);
} // namespace daemon
} // namespace nativetv
