// Musica: the Jellyfin music library as a grid of album covers or of artists, in name order, with a
// strip over it: Search (albums, songs and artists), Albums and Artists, and the letters. Home's
// Musica tab opens it in place of a row that scrolls sideways: with hundreds of albums a row cannot
// be searched by eye.
//
// The items come from the daemon a page at a time (kMusicPage); the next page is asked for when
// the focus nears the end of the ones that are here. OK on a letter shows the albums or artists
// that start with it ("#" the ones before A); OK on Albums or Artists switches to them, or, already
// there, shows them all again. CH+ and CH− in the grid step to the next and the previous letter.
// OK on an album opens it in the Jellyfin client, as Home's cards do; OK on an artist shows the
// artist's albums here (the clients' artist pages may show none), and Back goes back to the artists.
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/dashboard_screens.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include <algorithm>
#include <cmath>

namespace nativetv {
namespace {
namespace ms = kit::motion::ms;
using kit::easing;
using say::msg;

constexpr const char *kChannel = "music:library", *kMemory = "music.page";
constexpr size_t kColumns = 7;
constexpr float kTitleY = 150, kStripY = 236, kChipH = 56, kGridTop = 336, kCover = 224, kGapX = 32,
                kPitchY = kCover + 40, kGridBottom = 1080;
constexpr float kSearchW = 156, kViewW[2] = {124, 132}, kLetterW = 44, kLetterGap = 4;
constexpr size_t kFirstLetter = 3;

// The strip: Search, Albums, Artists, "#", then A to Z.
const std::vector<std::string> &letters() {
    static const std::vector<std::string> all = [] {
        std::vector<std::string> list = {"search", "albums", "artists", "#"};
        for (char c = 'A'; c <= 'Z'; ++c)
            list.push_back(std::string(1, c));
        return list;
    }();
    return all;
}

class MusicScreen final : public ScreenView {
  public:
    std::string name() const override {
        return "music";
    }
    int barTab() const override {
        if (host_)
            for (size_t i = 0; i < host_->view().home.tabs.size(); ++i)
                if (host_->view().home.tabs[i].key == "music")
                    return static_cast<int>(i);
        return -1;
    }

    void enter(ScreenHost &host) override {
        host_ = &host;
        // Back from the search, or from an album's client, finds the letter and the album it left.
        auto &memory = host.memory();
        const Json saved = memory.count(kMemory) ? memory[kMemory] : Json();
        letter_ = saved["letter"].string();
        artists_ = saved["view"].string() == "artists";
        artist_ = saved["artist"].string();
        artistName_ = saved["artistName"].string();
        artistAt_ = static_cast<size_t>(saved["artistAt"].number(0));
        const size_t at = static_cast<size_t>(saved["focus"].number(0));
        Motion &motion = host.motion();
        motion.play("music:E", Tween(1, 0, ms::base, easing(kit::motion::ease::out), kit::motion::stagger::pageBase));
        motion.set("music:gy", 0);
        load(host, 0);
        wanted_ = at;
        zone_ = saved["zone"].string() == "strip" ? Zone::Strip : Zone::Grid;
        strip_ = chosenChip();
        focusNow(host, true);
    }
    // Back on an artist's albums: the artists again, on the one that was open.
    bool back(ScreenHost &host) override {
        if (artist_.empty())
            return false;
        artist_.clear();
        artistName_.clear();
        albums_.clear();
        total_ = 0;
        load(host, 0);
        wanted_ = artistAt_;
        zone_ = Zone::Grid;
        focusNow(host, false);
        return true;
    }
    void leave(ScreenHost &host) override {
        remember(host);
        host.services().cancel(kChannel);
        host.motion().erasePrefix("music:");
    }
    void focus(ScreenHost &host) override {
        zone_ = Zone::Strip;
        focusNow(host, false);
    }

    bool key(ScreenHost &host, Key key) override {
        host.invalidate();
        if (zone_ == Zone::Strip)
            return stripKey(host, key);
        return gridKey(host, key);
    }
    void click(ScreenHost &host, const std::string &hit) override {
        if (hit.compare(0, 12, "music:strip:") == 0) {
            strip_ = static_cast<size_t>(std::atoi(hit.c_str() + 12));
            zone_ = Zone::Strip;
            choose(host);
        } else if (hit.compare(0, 12, "music:album:") == 0) {
            selected_ = static_cast<size_t>(std::atoi(hit.c_str() + 12));
            zone_ = Zone::Grid;
            focusNow(host, false);
            open(host);
        }
    }

    void response(ScreenHost &host, const Response &response) override {
        if (response.channel != kChannel || response.generation != generation_)
            return;
        generation_ = 0;
        Json data = response.json();
        const bool usable = response.ok() && data.valid() && data["ok"].boolean();
        if (!usable || !data["enabled"].boolean() || data.has("error")) {
            failed_ = !usable || data.has("error");
            unlinked_ = usable && !data["enabled"].boolean();
            host.invalidate();
            return;
        }
        failed_ = unlinked_ = false;
        if (static_cast<int>(data["start"].number(0)) == 0)
            albums_.clear();
        for (const auto &item : data["items"].items())
            albums_.push_back(item);
        total_ = static_cast<size_t>(data["total"].number(0));
        loaded_ = true;
        // The focus to come back to is further on: the pages up to it first.
        if (wanted_ >= albums_.size() && albums_.size() < total_ && !data["items"].items().empty()) {
            load(host, albums_.size());
            return;
        }
        if (wanted_ < albums_.size())
            selected_ = wanted_;
        wanted_ = 0;
        selected_ = std::min(selected_, albums_.empty() ? 0 : albums_.size() - 1);
        if (albums_.empty() && zone_ == Zone::Grid)
            zone_ = Zone::Strip;
        focusNow(host, true);
        host.invalidate();
    }

    // The bar took or gave back the focus: the page's own focus hides or shows again.
    bool update(ScreenHost &host) override {
        if (host.barFocused() != barWas_) {
            barWas_ = host.barFocused();
            focusNow(host, false);
        }
        return !loaded_ && !failed_ && !unlinked_; // the spinner turns
    }

    void build(ScreenHost &host, kit::Paint paint) override {
        Motion &motion = host.motion();
        Scene &scene = paint.scene;
        const float enter = static_cast<float>(motion.value("music:E"));
        scene.pushOpacity(1 - enter);
        scene.pushTransform(Transform::translate(0, 24 * enter));
        const float width = kit::canvas::width - 2 * kit::canvas::safeX;
        paint.glyphs.text(scene, {kit::canvas::safeX, kTitleY, width / 2, kit::type::headline.line},
                          artist_.empty() ? msg("tv.music.title", "Your music") : artistName_, kit::type::headline,
                          kit::color::onSurface);
        paint.glyphs.text(scene, {kit::canvas::safeX + width / 2, kTitleY + 20, width / 2, kit::type::body.line},
                          countText(), kit::type::body, kit::color::onSurface2, Node::Align::Right);
        drawStrip(host, paint);
        drawGrid(host, paint);
        scene.popTransform();
        scene.popOpacity();
    }

    Json snapshot() const override {
        Json value = Json::object();
        value.set("letter", Json::text(letter_));
        value.set("view", Json::text(artists_ ? "artists" : "albums"));
        value.set("artist", Json::text(artistName_));
        value.set("albums", Json::numeric(static_cast<double>(albums_.size())));
        value.set("total", Json::numeric(static_cast<double>(total_)));
        value.set("zone", Json::text(zone_ == Zone::Strip ? "strip" : "grid"));
        value.set("focus", Json::numeric(static_cast<double>(zone_ == Zone::Strip ? strip_ : selected_)));
        return value;
    }

  private:
    enum class Zone { Strip, Grid };

    // The chip that shows what the grid holds: the letter, or Albums or Artists for all of them.
    size_t chosenChip() const {
        const auto &list = letters();
        for (size_t i = kFirstLetter; i < list.size(); ++i)
            if (list[i] == letter_)
                return i;
        return artists_ ? 2 : 1;
    }
    static Rect chip(size_t index) {
        float x = kit::canvas::safeX;
        if (index == 0)
            return {x, kStripY, kSearchW, kChipH};
        x += kSearchW + 14;
        if (index == 1)
            return {x, kStripY, kViewW[0], kChipH};
        x += kViewW[0] + 6;
        if (index == 2)
            return {x, kStripY, kViewW[1], kChipH};
        x += kViewW[1] + 14 + static_cast<float>(index - kFirstLetter) * (kLetterW + kLetterGap);
        return {x, kStripY, kLetterW, kChipH};
    }
    static Rect cover(size_t index, float scroll) {
        return {kit::canvas::safeX + static_cast<float>(index % kColumns) * (kCover + kGapX),
                kGridTop + static_cast<float>(index / kColumns) * kPitchY + scroll, kCover, kCover};
    }

    void load(ScreenHost &host, size_t start) {
        if (start == 0) {
            loaded_ = false;
            selected_ = 0;
        }
        failed_ = false;
        host.services().cancel(kChannel);
        Json arguments = Json::object();
        arguments.set("letter", Json::text(letter_));
        arguments.set("view", Json::text(artists_ ? "artists" : "albums"));
        arguments.set("artist", Json::text(artist_));
        arguments.set("start", Json::numeric(static_cast<double>(start)));
        generation_ = host.services().read(kChannel, Operation::JellyfinMusic, arguments);
        if (!generation_)
            failed_ = true;
        host.invalidate();
    }
    // The next page, once the focus is within two rows of the albums that are here.
    void more(ScreenHost &host) {
        if (generation_ || !loaded_ || albums_.size() >= total_)
            return;
        if (selected_ + 2 * kColumns >= albums_.size())
            load(host, albums_.size());
    }

    bool stripKey(ScreenHost &host, Key key) {
        const size_t count = letters().size();
        if (key == Key::Left && strip_ > 0)
            --strip_;
        else if (key == Key::Right && strip_ + 1 < count)
            ++strip_;
        else if (key == Key::Down) {
            if (albums_.empty())
                return true;
            zone_ = Zone::Grid;
            selected_ = std::min(selected_, albums_.size() - 1);
        } else if (key == Key::Ok) {
            choose(host); // the search route destroys this screen: nothing after it
            return true;
        } else if (key == Key::Up)
            return false; // the bar
        else if (key != Key::Left && key != Key::Right)
            return false;
        focusNow(host, false);
        return true;
    }
    bool gridKey(ScreenHost &host, Key key) {
        const size_t count = albums_.size();
        if (!count) {
            zone_ = Zone::Strip;
            return stripKey(host, key);
        }
        if (key == Key::Left && selected_ % kColumns)
            --selected_;
        else if (key == Key::Right && selected_ % kColumns + 1 < kColumns && selected_ + 1 < count)
            ++selected_;
        else if (key == Key::Down && selected_ + kColumns < count)
            selected_ += kColumns;
        else if (key == Key::Down && selected_ / kColumns < (count - 1) / kColumns)
            selected_ = count - 1; // the last row is short: its last album
        else if (key == Key::Up) {
            if (selected_ >= kColumns)
                selected_ -= kColumns;
            else
                zone_ = Zone::Strip;
        } else if (key == Key::Ok) {
            open(host);
            return true;
        } else if (key == Key::PageDown || key == Key::PageUp) {
            // CH+ and CH−: the next or the previous letter, without going up to the strip.
            const size_t at = std::max(chosenChip(), kFirstLetter - 1);
            if (key == Key::PageDown && at + 1 < letters().size())
                strip_ = at + 1;
            else if (key == Key::PageUp && at > kFirstLetter)
                strip_ = at - 1;
            else if (key == Key::PageUp && at == kFirstLetter)
                strip_ = artists_ ? 2 : 1; // before "#": all of them
            else
                return true;
            choose(host);
            return true;
        } else
            return key == Key::Left || key == Key::Right || key == Key::Down;
        more(host);
        focusNow(host, false);
        return true;
    }

    // OK on the strip: the search, Albums or Artists (all of them when already there), or the
    // albums or artists of that letter.
    void choose(ScreenHost &host) {
        if (strip_ == 0) {
            remember(host);
            host.open("search:music");
            return;
        }
        const bool view = strip_ < kFirstLetter;
        const bool artists = view ? strip_ == 2 : artists_;
        const std::string letter = view ? (artists == artists_ ? std::string() : letter_) : letters()[strip_];
        if (letter == letter_ && artists == artists_ && artist_.empty() && loaded_) {
            if (!albums_.empty())
                zone_ = Zone::Grid;
            focusNow(host, false);
            return;
        }
        letter_ = letter;
        artists_ = artists;
        artist_.clear();
        artistName_.clear();
        albums_.clear();
        total_ = 0;
        load(host, 0);
        host.motion().set("music:gy", 0);
        focusNow(host, false);
    }
    void open(ScreenHost &host) {
        if (selected_ >= albums_.size())
            return;
        if (artists_ && artist_.empty()) {
            // An artist: their albums, here.
            artistAt_ = selected_;
            artist_ = albums_[selected_]["id"].string();
            artistName_ = albums_[selected_]["title"].string();
            albums_.clear();
            total_ = 0;
            load(host, 0);
            zone_ = Zone::Grid;
            host.motion().set("music:gy", 0);
            focusNow(host, false);
            return;
        }
        remember(host);
        Json body = Json::object();
        body.set("kind", Json::text("jellyfin"));
        body.set("id", albums_[selected_]["id"]);
        body.set("action", Json::text("details"));
        Write write;
        write.operation = Operation::OpenHomeItem;
        write.body = [body](const Json &) { return body; };
        write.failure = msg("tv.details.openFailed", "That did not open");
        host.optimistic().run("music.open", write);
    }
    void remember(ScreenHost &host) {
        Json saved = Json::object();
        saved.set("letter", Json::text(letter_));
        saved.set("view", Json::text(artists_ ? "artists" : "albums"));
        saved.set("artist", Json::text(artist_));
        saved.set("artistName", Json::text(artistName_));
        saved.set("artistAt", Json::numeric(static_cast<double>(artistAt_)));
        saved.set("focus", Json::numeric(static_cast<double>(selected_)));
        saved.set("zone", Json::text(zone_ == Zone::Strip ? "strip" : "grid"));
        host.memory()[kMemory] = saved;
    }

    // Moves the focus values to where the focus is now, and scrolls the grid so the focused row
    // is the first or the second on screen.
    void focusNow(ScreenHost &host, bool jump) {
        Motion &motion = host.motion();
        const bool bar = host.barFocused();
        const std::string now = bar ? std::string()
                                : zone_ == Zone::Strip ? "music:s:" + std::to_string(strip_)
                                                       : "music:a:" + std::to_string(selected_);
        if (now != focusId_) {
            if (!focusId_.empty())
                motion.tween(focusId_, 0, ms::focus, easing(kit::motion::ease::out));
            if (!now.empty()) {
                if (jump)
                    motion.set(now, 1);
                else
                    motion.tween(now, 1, ms::focus, easing(kit::motion::ease::out));
            }
            focusId_ = now;
        }
        const size_t row = selected_ / kColumns;
        const double target = zone_ == Zone::Grid && row > 0 ? -static_cast<double>(row - 1) * kPitchY : 0;
        if (jump)
            motion.set("music:gy", target);
        else
            motion.tween("music:gy", target, ms::page, easing(kit::motion::ease::out));
    }

    std::string countText() const {
        if (!loaded_ || failed_ || unlinked_)
            return std::string();
        Json vars = Json::object();
        vars.set("n", Json::text(std::to_string(total_)));
        if (artists_ && artist_.empty())
            return total_ == 1 ? msg("tv.music.oneArtist", "1 artist") : msg("tv.music.artists", "{n} artists", vars);
        return total_ == 1 ? msg("tv.music.oneAlbum", "1 album") : msg("tv.music.albums", "{n} albums", vars);
    }

    void drawStrip(ScreenHost &host, kit::Paint paint) {
        Motion &motion = host.motion();
        Scene &scene = paint.scene;
        const auto &list = letters();
        const size_t chosen = chosenChip();
        for (size_t i = 0; i < list.size(); ++i) {
            const Rect rest = chip(i);
            const float focus = static_cast<float>(motion.value("music:s:" + std::to_string(i)));
            const float scale = kit::focus::scaleAt(1.12f, focus);
            scene.pushTransform(kit::focus::transformAt(rest, scale));
            // The view's chip stays lit beside the letter it is filtered by.
            const bool on = i == chosen || (i == (artists_ ? 2u : 1u) && chosen >= kFirstLetter);
            Color fill = on ? kit::color::accent : kit::focus::withAlpha(kit::color::surface2, .9f);
            Node &base = scene.shape(rest, fill, kit::radius::pill);
            if (focus > 0)
                base.ring = {0, kit::elevation::focusRingWidth,
                             kit::focus::withAlpha(kit::focus::ringColor(), kit::focus::clamp01(focus))};
            const Color ink = on ? kit::color::ink : kit::color::onSurface;
            if (i == 0) {
                paint.glyphs.icon(scene, {rest.x + 20, rest.y + 14, 28, 28}, "search", ink);
                paint.glyphs.text(scene, {rest.x + 56, rest.y + (kChipH - kit::type::labelL.line) / 2, rest.w - 64,
                                          kit::type::labelL.line},
                                  msg("tv.music.search", "Search"), kit::type::labelL, ink);
            } else
                paint.glyphs.text(scene, {rest.x, rest.y + (kChipH - kit::type::labelL.line) / 2, rest.w,
                                          kit::type::labelL.line},
                                  i == 1   ? msg("tv.music.viewAlbums", "Albums")
                                  : i == 2 ? msg("tv.music.viewArtists", "Artists")
                                           : list[i],
                                  kit::type::labelL, ink,
                                  Node::Align::Center);
            scene.hits.push_back({rest, "music:strip:" + std::to_string(i)});
            scene.popTransform();
        }
    }

    void drawGrid(ScreenHost &host, kit::Paint paint) {
        Motion &motion = host.motion();
        Scene &scene = paint.scene;
        const Rect area = {kit::canvas::safeX, kGridTop, kit::canvas::width - 2 * kit::canvas::safeX, 560};
        if (albums_.empty()) {
            kit::StatusPanel box;
            box.rest = {kit::canvas::safeX, kGridTop, 900, 300};
            box.icon = "music";
            if (unlinked_) {
                box.title = msg("tv.music.unlinked", "Jellyfin is not linked");
                box.message = msg("tv.music.unlinkedBody", "Link it in Settings → Jellyfin to see your music here.");
            } else if (failed_) {
                box.error = true;
                box.title = msg("tv.music.failed", "The library did not answer");
                box.message = msg("tv.music.failedBody", "Choose a letter to try again.");
            } else if (!loaded_) {
                kit::Loading spinner;
                spinner.area = area;
                spinner.label = msg("tv.music.loading", "Reading your music…");
                spinner.phase = kit::spinnerPhase(motion, "music:spin");
                kit::loadingIndicator(paint, spinner);
                return;
            } else if (artists_ && artist_.empty()) {
                box.title = msg("tv.music.noArtists", "No artists here");
                box.message = msg("tv.music.noneBody", "Choose another letter, or search.");
            } else {
                box.title = msg("tv.music.none", "No albums here");
                box.message = msg("tv.music.noneBody", "Choose another letter, or search.");
            }
            kit::statusPanel(paint, box);
            return;
        }
        // The covers pass under the strip: cut just below it.
        scene.pushClip({0, kStripY + kChipH + 14, kit::canvas::width, kGridBottom});
        const float scroll = static_cast<float>(motion.value("music:gy"));
        size_t lifted = albums_.size();
        for (size_t pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < albums_.size(); ++i) {
                // The focused album grows over its neighbours, so it is drawn last.
                const float focus = static_cast<float>(motion.value("music:a:" + std::to_string(i)));
                if (pass == 0 && focus > .001f) {
                    lifted = i;
                    continue;
                }
                if (pass == 1 && i != lifted)
                    continue;
                const Rect rest = cover(i, scroll);
                if (rest.y + rest.h < kStripY || rest.y > kGridBottom + 40)
                    continue;
                const Json &album = albums_[i];
                kit::PosterCard card;
                card.rest = rest;
                card.id = "music:album:" + std::to_string(i);
                const std::string art = album["cover"].isString() ? album["cover"].string() : album["image"].string();
                card.image = art.empty() ? "" : "jellyfin:" + art + ":640";
                card.title = album["title"].string();
                if (!artists_ || !artist_.empty())
                    card.subtitle = artist_.empty() ? album["artist"].string()
                                    : album["year"].isNumber() ? std::to_string(static_cast<int>(album["year"].number()))
                                                               : std::string();
                card.focus = focus;
                kit::card(paint, card);
            }
        scene.popClip();
    }

    ScreenHost *host_ = nullptr;
    Zone zone_ = Zone::Grid;
    size_t strip_ = 1, selected_ = 0, wanted_ = 0, total_ = 0;
    std::string letter_, focusId_, artist_, artistName_;
    size_t artistAt_ = 0;
    bool artists_ = false;
    std::vector<Json> albums_;
    unsigned generation_ = 0;
    bool loaded_ = false, failed_ = false, unlinked_ = false, barWas_ = false;
};
} // namespace

std::unique_ptr<ScreenView> makeMusicScreen(const std::string &route) {
    return route == "tab:music" || route == "music" ? std::unique_ptr<ScreenView>(new MusicScreen()) : nullptr;
}
} // namespace nativetv
