#include "domain/models.hpp"
#include "domain/say.hpp"
#include <algorithm>
#include <map>

namespace nativetv {
Json fields(std::initializer_list<std::pair<std::string, Json>> values) {
    Json result = Json::object();
    for (const auto &pair : values)
        result.set(pair.first, pair.second);
    return result;
}
Command post(Operation operation, const Json &body, bool confirm) {
    Command c;
    c.operation = operation;
    c.body = body;
    c.confirm = confirm;
    return c;
}
Command control(const std::string &action, const Json &value, bool confirm) {
    return post(Operation::Control, fields({{"action", Json::text(action)}, {"value", value}}),
                confirm);
}
Command navigate(const std::string &route) {
    Command c;
    c.route = route;
    return c;
}
std::string artwork(const Json &item, bool hero) {
    std::string path = item[hero ? "backdrop" : "poster"].string();
    if (!hero && !item["backdrop"].string().empty())
        path = item["backdrop"].string();
    if (path.empty())
        return "";
    std::string size = hero ? "w1280" : item["backdrop"].string().empty() ? "w342" : "w500";
    return "tmdb:" + size + ":" + path;
}
// "Film  ·  2026  ·  ★ 6.4": the kit draws the star as an icon (the face has none). The details
// page names the kind on its source line, so it asks for the facts without it.
std::string titleFacts(const Json &item, bool withKind) {
    std::vector<std::string> parts;
    if (withKind && item["type"].string() == "movie")
        parts.push_back("Film");
    else if (withKind && item["type"].string() == "tv")
        parts.push_back("Series");
    if (!item["year"].string().empty())
        parts.push_back(item["year"].string());
    if (item["rating"].number() > 0)
        parts.push_back("★ " + compactNumber(item["rating"].number(), 1));
    if (item["runtime"].number() > 0)
        parts.push_back(compactNumber(item["runtime"].number()) + " min");
    if (item["seasons"].number() > 0)
        parts.push_back(compactNumber(item["seasons"].number()) + " seasons");
    for (const auto &genre : item["genres"].items())
        parts.push_back(genre.string());
    std::string text;
    for (const auto &part : parts) {
        if (!text.empty())
            text += "  ·  ";
        text += part;
    }
    return text;
}
std::string pictureLabel(const Json &picture) {
    if (!picture["ok"].boolean())
        return "";
    std::vector<std::string> parts;
    Json signal = picture["signal"];
    if (signal["width"].number() > 0 && signal["height"].number() > 0) {
        std::string dimensions = signal["width"].string() + " × " + signal["height"].string();
        if (signal["hz"].number() > 0) {
            std::string hz = compactNumber(signal["hz"].number(), 2);
            while (!hz.empty() && hz.back() == '0')
                hz.pop_back();
            if (!hz.empty() && hz.back() == '.')
                hz.pop_back();
            dimensions += " at " + hz + " Hz";
        }
        parts.push_back(dimensions);
        std::string hdr = signal["hdr"].string();
        if (hdr == "hdr10")
            parts.push_back("HDR10");
        if (hdr == "hlg")
            parts.push_back("HLG");
        if (hdr == "dolby_vision")
            parts.push_back("Dolby Vision");
    }
    if (!picture["mode"]["display"].string().empty())
        parts.push_back(picture["mode"]["display"].string());
    std::string text;
    for (const auto &part : parts) {
        if (!text.empty())
            text += "  ·  ";
        text += part;
    }
    return text;
}
// An HDMI port number as the TV sends it, a number or a string: "1", never "1.0".
std::string portText(const Json &port) {
    return port.isNumber() ? std::to_string(static_cast<int>(port.number())) : port.string("");
}

std::vector<Tab> homeTabs(const Json &catalogue, const Json &discover) {
    std::vector<Tab> tabs = {{"home", say::msg("tv.home.tab.home", "Home"), Operation::None}};
    // Cinema's tabs for the Jellyfin library: Film, Serie, Musica, when the server has them.
    bool has[3] = {false, false, false};
    for (const auto &row : discover["rows"].items()) {
        const std::string key = row["key"].string();
        has[0] = has[0] || key == "jf-movies";
        has[1] = has[1] || key == "jf-series";
        has[2] = has[2] || key == "jf-music";
    }
    if (has[0]) tabs.push_back({"movies", say::msg("tv.home.tab.movies", "Films"), Operation::None});
    if (has[1]) tabs.push_back({"series", say::msg("tv.home.tab.series", "Series"), Operation::None});
    if (has[2]) tabs.push_back({"music", say::msg("tv.home.tab.music", "Music"), Operation::None});
    if (catalogue["layout"]["showLive"].boolean() &&
        catalogue["sources"]["channels"].number(-1) != 0)
        tabs.push_back({"live", say::msg("tv.home.tab.live", "Live"), Operation::None});
    tabs.push_back({"apps", say::msg("tv.home.tab.apps", "Apps"), Operation::None});
    if (catalogue["layout"]["showInputs"].boolean(true) && catalogue["devices"].size())
        tabs.push_back({"inputs", say::msg("tv.home.tab.inputs", "Inputs"), Operation::None});
    return tabs;
}
// The ids hidden from `list`: "app", "dev" or "ch" are Home's rows, "appsTab" is the Apps tab's own
// apps. A catalogue from before the Apps tab had a list hides there what Home hides.
Json hiddenIds(const Json &catalogue, const std::string &list) {
    Json hidden = catalogue["layout"]["hidden"];
    return list == "appsTab" && !hidden["appsTab"].isArray() ? hidden["app"] : hidden[list];
}
namespace {
const char *const kClientApp = "com.tvweb.dashboard";
bool contains(const Json &values, const std::string &id) {
    for (const auto &value : values.items())
        if (value.string() == id)
            return true;
    return false;
}
// `withHidden`: the hidden apps (never LG Home) follow the others, flagged. `list`: the hidden list
// that applies, the row's own unless given.
CardRow catalogueRow(const std::string &kind, const Json &catalogue, const Json &guide,
                     bool withHidden = false, const std::string &list = std::string()) {
    const Json hiddenList = hiddenIds(catalogue, list.empty() ? kind : list);
    CardRow row;
    row.key = kind;
    row.title = kind == "app" ? say::msg("tv.home.tab.apps", "Apps")
                : kind == "dev" ? say::msg("tv.home.tab.inputs", "Inputs")
                                : say::msg("tv.home.row.channels", "Channels");
    std::map<std::string, Json> index;
    for (const auto &item : catalogue[kind == "app"   ? "apps"
                                      : kind == "dev" ? "devices"
                                                      : "channels"]
                                .items())
        index[item["id"].string()] = item;
    std::vector<Card> hiddenCards;
    for (const auto &value : catalogue["order"][kind].items()) {
        std::string id = value.string();
        const bool hidden = contains(hiddenList, id);
        if (hidden && !withHidden)
            continue;
        if (id == "lg-home" && !catalogue["layout"]["showLgHome"].boolean(true))
            continue;
        if (!index.count(id) && id != "lg-home")
            continue;
        Json item = index[id];
        Card card;
        card.id = id;
        card.kind = kind;
        card.hidden = hidden;
        card.data = item;
        card.title = id == "lg-home" ? "LG Home" : item["title"].string(item["name"].string(id));
        if (kind == "app" && id != "lg-home")
            card.image = "app:" + id;
        if (kind == "dev")
            card.subtitle =
                item["unused"].boolean() ? say::msg("tv.quick.nothingConnected", "Nothing connected")
                                         : "HDMI " + portText(item["port"]);
        if (kind == "ch") {
            card.subtitle = item["number"].string();
            Json programme = guide[id][static_cast<size_t>(0)];
            if (programme.valid())
                card.subtitle += "  " + programme["title"].string();
        }
        // The Native Tv tile is this client: it is already in front while Home shows it, so a
        // launch would change nothing. It opens the settings the tile opens from LG's launcher.
        card.command = kind == "app" && id == kClientApp
                           ? navigate("dashboard")
                           : post(Operation::OpenHomeItem,
                                  fields({{"kind", Json::text(id == "lg-home" ? "lg" : kind)},
                                          {"id", Json::text(id)}}));
        (hidden ? hiddenCards : row.cards).push_back(card);
    }
    row.cards.insert(row.cards.end(), hiddenCards.begin(), hiddenCards.end());
    return row;
}
// The apps as one row or, with the apps sorted by kind, one row per kind in the owner's order.
// `list` names the hidden apps that are left out: Home's ("app") or the Apps tab's ("appsTab").
std::vector<CardRow> appRows(const Json &catalogue, const Json &guide, const std::string &list) {
    CardRow all = catalogueRow("app", catalogue, guide, false, list);
    std::vector<CardRow> rows;
    if (catalogue["layout"]["sort"]["app"].string() == "kind")
        for (const auto &kind : catalogue["kinds"].items()) {
            CardRow row;
            row.key = "app";
            row.group = kind["key"].string();
            row.title = kind["label"].string(row.group);
            for (const Card &card : all.cards)
                if (kindOf(card) == row.group)
                    row.cards.push_back(card);
            if (!row.cards.empty())
                rows.push_back(row);
        }
    if (rows.empty())
        rows.push_back(all);
    return rows;
}
} // namespace
std::string kindOf(const Card &card) {
    return card.id == "lg-home" ? "tv" : card.data["kind"].string("other");
}
CardRow appsRow(const Json &catalogue, const Json &guide, bool withHidden, const std::string &list) {
    CardRow apps;
    apps.key = "app";
    apps.title = list == "appsTab" ? say::msg("tv.home.tab.apps", "Apps") : say::msg("tv.home.row.yourApps", "Your apps");
    for (const CardRow &row : appRows(catalogue, guide, list))
        apps.cards.insert(apps.cards.end(), row.cards.begin(), row.cards.end());
    if (withHidden)
        for (const Card &card : catalogueRow("app", catalogue, guide, true, list).cards)
            if (card.hidden)
                apps.cards.push_back(card);
    return apps;
}
std::vector<std::string> orderAfterMove(const Json &order, const std::vector<std::string> &shown) {
    std::vector<std::string> result;
    size_t next = 0;
    for (const auto &value : order.items()) {
        const std::string id = value.string();
        const bool onScreen = std::find(shown.begin(), shown.end(), id) != shown.end();
        // A card on screen takes the next place held by a card on screen; the rest keep theirs.
        if (onScreen && next < shown.size())
            result.push_back(shown[next++]);
        else
            result.push_back(id);
    }
    // Cards the order did not list (an app installed since) follow.
    for (; next < shown.size(); ++next)
        if (std::find(result.begin(), result.end(), shown[next]) == result.end())
            result.push_back(shown[next]);
    return result;
}
// A row of the Jellyfin library: cards that open the title in the Jellyfin app.
CardRow jellyfinRow(const std::string &key, const std::string &title, const Json &source) {
    CardRow row;
    row.key = key;
    row.title = title;
    for (const auto &item : source["items"].items()) {
        Card card;
        card.id = item["id"].string();
        card.kind = "jellyfin";
        card.data = item;
        card.title = item["title"].string();
        if (item["remaining"].isNumber()) {
            Json vars = Json::object();
            vars.set("n", Json::text(std::to_string(int(item["remaining"].number()))));
            card.subtitle = say::msg("tv.home.jf.remaining", "{n} min left", vars);
        } else if (item["episode"].isString()) {
            card.subtitle = (item["season"].isNumber() ? "S" + std::to_string(int(item["season"].number())) + " · " : std::string()) +
                            (item["number"].isNumber() ? "E" + std::to_string(int(item["number"].number())) + " · " : std::string()) +
                            item["episode"].string();
        } else {
            std::string facts = item["year"].isNumber() ? std::to_string(int(item["year"].number())) : "";
            if (item["genres"].size()) facts += (facts.empty() ? "" : " · ") + item["genres"][0].string();
            card.subtitle = facts;
        }
        if (item["image"].isString()) card.image = "jellyfin:" + item["image"].string() + ":640";
        if (item["backdrop"].isString()) card.backdrop = "jellyfin:" + item["backdrop"].string() + ":1280";
        card.command = post(Operation::OpenHomeItem,
                            fields({{"kind", Json::text("jellyfin")}, {"id", item["id"]},
                                    {"action", Json::text(item["progress"].number() > 0 ? "resume" : "details")}}));
        row.cards.push_back(card);
    }
    return row;
}
std::vector<CardRow> homeRows(const std::string &tab, const Json &catalogue, const Json &discover,
                              const Json &guide) {
    std::vector<CardRow> result;
    if (tab == "apps") {
        for (const CardRow &row : appRows(catalogue, guide, "appsTab"))
            if (!row.cards.empty())
                result.push_back(row);
        return result;
    }
    if (tab == "movies" || tab == "series" || tab == "music") {
        const std::string key = "jf-" + tab;
        for (const auto &source : discover["rows"].items())
            if (source["key"].string() == key) {
                CardRow row = jellyfinRow(key, tab == "movies" ? say::msg("tv.home.row.jfMovies", "Your films")
                                               : tab == "series" ? say::msg("tv.home.row.jfSeries", "Your series")
                                                                 : say::msg("tv.home.row.jfMusic", "Your music"), source);
                if (!row.cards.empty())
                    result.push_back(row);
            }
        return result;
    }
    if (tab == "inputs" || tab == "live") {
        CardRow row = catalogueRow(tab == "inputs" ? "dev" : "ch", catalogue, guide);
        if (!row.cards.empty())
            result.push_back(row);
        return result;
    }
    const std::map<std::string, std::string> names = {
        {"trending", say::msg("tv.home.row.trending", "Top picks for you")},
        {"movies", say::msg("tv.home.row.movies", "Trending films this week")},
        {"series", say::msg("tv.home.row.series", "Trending series this week")},
        {"netflix", say::msg("tv.home.row.netflix", "Available on Netflix")},
        {"youtube", say::msg("tv.home.row.youtube", "Trending on YouTube")}};
    std::map<std::string, Json> feedRows;
    for (const auto &source : discover["rows"].items())
        feedRows[source["key"].string()] = source;
    // The apps lead, as on Google TV, in their kinds' order when sorted by kind; the top picks
    // follow and the hero is drawn from them.
    CardRow apps = appsRow(catalogue, guide, false);
    // Cinema's dock ends with All apps and the TV's own settings.
    if (!apps.cards.empty() && catalogue["layout"]["appShortcuts"].boolean()) {
        Card all;
        all.id = "shortcut:apps";
        all.kind = "app";
        all.title = say::msg("tv.home.allApps", "All apps");
        all.data = Json::object();
        all.data.set("icon", Json::text("apps"));
        all.command = navigate("home:tab:apps");
        apps.cards.push_back(all);
        Card settings;
        settings.id = "shortcut:settings";
        settings.kind = "app";
        settings.title = say::msg("tv.home.tvSettings", "TV settings");
        settings.data = Json::object();
        settings.data.set("icon", Json::text("settings"));
        settings.command = post(Operation::OpenHomeItem,
                                fields({{"kind", Json::text("settings")}, {"id", Json::text("settings")}}));
        apps.cards.push_back(settings);
    }
    if (!apps.cards.empty())
        result.push_back(apps);
    // Cinema's wide dock: the HDMI inputs under the apps, one with nothing on it
    // greyed out with "No signal".
    if (catalogue["layout"]["dockInputs"].boolean()) {
        CardRow inputs = catalogueRow("dev", catalogue, guide);
        if (!inputs.cards.empty())
            result.push_back(inputs);
    }
    // The Jellyfin library first, as on Cinema: Continue watching leads.
    const std::map<std::string, std::string> library = {
        {"jf-continue", say::msg("tv.home.row.jfContinue", "Continue watching")},
        {"jf-recent", say::msg("tv.home.row.jfRecent", "Recently added")},
        {"jf-movies", say::msg("tv.home.row.jfMovies", "Your films")},
        {"jf-series", say::msg("tv.home.row.jfSeries", "Your series")},
        {"jf-music", say::msg("tv.home.row.jfMusic", "Your music")}};
    bool libraryShown = false;
    for (const auto &key : {"jf-continue", "jf-recent", "jf-movies", "jf-series", "jf-music"}) {
        if (!feedRows.count(key))
            continue;
        CardRow row = jellyfinRow(key, library.at(key), feedRows[key]);
        if (!row.cards.empty()) {
            result.push_back(row);
            libraryShown = true;
        }
    }
    // A loaded feed with nothing in it (no library, no picks): Cinema's shortcuts instead, a
    // Jellyfin app, YouTube, live TV and the inputs, all the apps.
    bool feedShown = false;
    for (const auto &key : {"trending", "movies", "series", "netflix", "youtube"})
        feedShown = feedShown || (feedRows.count(key) && feedRows[key]["items"].size() > 0);
    if (!libraryShown && !feedShown && discover["rows"].isArray()) {
        CardRow shortcuts;
        shortcuts.key = "shortcuts";
        shortcuts.title = say::msg("tv.home.row.shortcuts", "Your shortcuts");
        const CardRow installed = catalogueRow("app", catalogue, guide);
        const auto addApp = [&](std::initializer_list<const char *> ids, const std::string &subtitle,
                                const char *icon) {
            for (const char *id : ids)
                for (const Card &card : installed.cards)
                    if (card.id == id) {
                        Card copy = card;
                        copy.id = "shortcut:" + card.id;
                        copy.subtitle = subtitle;
                        copy.data.set("icon", Json::text(icon));
                        shortcuts.cards.push_back(copy);
                        return;
                    }
        };
        addApp({"org.litefin.app", "org.moonfin.webos", "org.jellyfin.webos", "com.breezyfin.app", "com.ocenfin.app"},
               say::msg("tv.home.shortcut.jellyfin", "Your own library"), "movie");
        addApp({"youtube.leanback.v4"}, say::msg("tv.home.shortcut.youtube", "Videos and your channels"), "play");
        Card live;
        live.id = "shortcut:live";
        live.kind = "app";
        live.title = say::msg("tv.home.shortcut.live", "TV and HDMI");
        live.subtitle = say::msg("tv.home.shortcut.liveSub", "Channels and connected devices");
        live.data = Json::object();
        live.data.set("icon", Json::text("tv"));
        live.command = navigate("home:tab:live");
        shortcuts.cards.push_back(live);
        Card all;
        all.id = "shortcut:all";
        all.kind = "app";
        all.title = say::msg("tv.home.allApps", "All apps");
        all.subtitle = say::msg("tv.home.shortcut.allSub", "Every app installed");
        all.data = Json::object();
        all.data.set("icon", Json::text("apps"));
        all.command = navigate("home:tab:apps");
        shortcuts.cards.push_back(all);
        result.push_back(shortcuts);
    }
    for (const auto &key : {"trending", "movies", "series", "netflix", "youtube"}) {
        if (!feedRows.count(key))
            continue;
        Json source = feedRows[key];
        CardRow row;
        row.key = source["key"].string();
        row.posters = row.key != "trending" && row.key != "youtube";
        auto name = names.find(row.key);
        row.title = name == names.end() ? source["title"].string(row.key) : name->second;
        for (const auto &item : source["items"].items()) {
            Card card;
            card.id = item["id"].string();
            card.kind = row.key == "youtube" ? "youtube" : row.key == "netflix" ? "netflix" : "title";
            card.data = item;
            card.title = item["title"].string();
            card.subtitle = titleFacts(item);
            // Every card is landscape, so the backdrop is preferred over the portrait poster.
            card.image = artwork(item);
            card.backdrop = artwork(item, true);
            if (card.kind == "netflix") {
                // OK opens the title in Netflix itself, as a YouTube card opens YouTube.
                card.command = post(Operation::OpenHomeItem,
                                    fields({{"kind", Json::text("netflix")}, {"id", item["id"]},
                                            {"type", item["type"]}}));
            } else if (card.kind == "youtube") {
                card.image = "youtube:" + card.id;
                card.command = post(Operation::OpenHomeItem,
                                    fields({{"kind", Json::text("youtube")}, {"id", item["id"]}}));
            } else
                card.command = navigate("details");
            row.cards.push_back(card);
        }
        if (!row.cards.empty())
            result.push_back(row);
    }
    return result;
}
} // namespace nativetv
