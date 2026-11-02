#pragma once

#include "core/json.hpp"
#include <string>

namespace nativetv {
namespace service {

// Cinema's lounge/config.json on a USB stick: the settings it carries that Cinema++ has too,
// applied once per content of the file (Cinema merged it into its settings at every start).
struct UsbConfigChanges {
    Json layout;    // for updateHomeLayout: ambience, clockFormat, showDate, dockInputs, colours
    Json wallpaper; // for saveWallpaperSource: source, gradient, interval, zoom
    Json music;     // for AmbientMusic::configure: enabled, source, volume, shuffle
    Json weather;   // for HomeWeather::configure: enabled, units, place
};

// The first lounge/config.json on a stick under usbRoot (/tmp/usb/<device>/<drive>), parsed,
// with a fingerprint of its bytes; an invalid Json when there is none.
Json findUsbConfig(const std::string &usbRoot, std::string &fingerprint);
// What Cinema's settings mean for Cinema++; keys it does not know are left out.
UsbConfigChanges usbConfigChanges(const Json &config);

} // namespace service
} // namespace nativetv
