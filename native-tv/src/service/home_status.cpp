#include "service/home_status.hpp"
#include "domain/say.hpp"
#include "service/home_profiles.hpp"

namespace nativetv {
namespace service {
namespace {

Json profileColours() {
    return Json::parse(R"(["#a8c7fa","#f2b8b5","#6dd58c","#ffb74d","#7fcfff","#d0bcff"])");
}
} // namespace

Json homeStatusSupported(const std::string &version, bool allowExperimental) {
    Json result = Json::object();
    result.set("webos", Json::text(version));
    if (version.compare(0, 2, "9.") == 0) {
        result.set("ok", Json::flag(true));
        return result;
    }
    const bool on10 = version.compare(0, 3, "10.") == 0;
    const bool on11 = version.compare(0, 3, "11.") == 0;
    // The W22 family (LG 43UQ81003LB and others) reports webOS 7.6.0. On these
    // TVs the takeover is hook-based: the Home key and a boot activity bring up
    // Native Tv in place of LG's Home; the file-level manager does not touch LG
    // layouts. Behind allowExperimentalHome so a plain install never flips a TV
    // without the user's consent.
    const bool on7or8 = version.compare(0, 2, "7.") == 0 || version.compare(0, 2, "8.") == 0;
    if (on10 || (on11 && allowExperimental) || (on7or8 && allowExperimental)) {
        result.set("ok", Json::flag(true));
        result.set("experimental", Json::flag(true));
        return result;
    }

    result.set("ok", Json::flag(false));
    Json variables = Json::object();
    variables.set("version", Json::text(version.empty() ? "?" : version));
    result.set("webos", version.empty() ? Json() : Json::text(version));
    result.set("reason", Json::text(say::msg(
        "srv.home.untested",
        "Native Tv Home has been tried on webOS 9 and 10 natively, and on webOS 7, 8 and 11 as experimental (set allowExperimentalHome). This TV runs webOS {version}.",
        variables)));
    return result;
}

Json homeStatusProjection(const Json &rawDocument,
                          const std::string &webosVersion,
                          bool allowExperimental, bool active,
                          bool lgHomeOpen, bool historyEnabled,
                          const Json &wallpapers, const std::string &mainProfileName) {
    const Json document = normalizeHomeDocument(rawDocument);
    const Json supported = homeStatusSupported(webosVersion, allowExperimental);
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("available", Json::flag(supported["ok"].boolean() ||
                                         document["enabled"].boolean() ||
                                         document["restorePending"].boolean()));
    result.set("supported", supported);
    result.set("enabled", document["enabled"]);
    result.set("openOnPowerOn", document["openOnPowerOn"]);
    result.set("restorePending", document["restorePending"]);
    result.set("active", Json::flag(active));
    result.set("lgHomeOpen", Json::flag(lgHomeOpen));
    result.set("hideLgHomeAtStart",
               Json::flag(document["hideLgHomeAtStart"].isBoolean() &&
                          document["hideLgHomeAtStart"].boolean()));
    result.set("hideLgHomeAtStartAvailable",
               Json::flag(webosVersion.compare(0, 2, "9.") == 0));
    result.set("history", Json::flag(historyEnabled));
    result.set("wallpapers", wallpapers);
    result.set("profiles", homeProfileList(document, mainProfileName));
    result.set("profile", document["profile"]);
    result.set("profileColours", profileColours());
    result.set("layout", homeLayout(document));
    return result;
}

} // namespace service
} // namespace nativetv
