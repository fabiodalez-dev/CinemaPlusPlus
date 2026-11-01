// The LED strip's control loop, with the cases of Cinema's ambilight_test.py.
#include "ambilight/controller.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace nativetv;
using namespace nativetv::ambilight;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Record {
    std::vector<std::vector<int>> frames;
    int closed = 0;
    int connects = 0;
};

class FakeStrip : public StripLink {
  public:
    FakeStrip(std::string address, Record *record, std::function<bool(int)> connectAs)
        : address_(std::move(address)), record_(record), connectAs_(std::move(connectAs)) {}
    const std::string &address() const override { return address_; }
    bool connected() const override { return connected_; }
    std::string reason() const override { return reason_; }
    bool connect() override {
        ++record_->connects;
        connected_ = connectAs_ ? connectAs_(record_->connects) : true;
        reason_ = connected_ ? "" : "Message status unknown.";
        return connected_;
    }
    bool write(const std::vector<int> &frame) override {
        record_->frames.push_back(frame);
        return true;
    }
    void close() override {
        ++record_->closed;
        connected_ = false;
    }
    double lastWrite() const override { return 0; }

  private:
    std::string address_;
    Record *record_;
    std::function<bool(int)> connectAs_;
    bool connected_ = false;
    std::string reason_;
};

std::vector<Rgb> colors(const Record &r) {
    std::vector<Rgb> out;
    for (const auto &f : r.frames)
        if (f[3] == 0x03) out.push_back(Rgb{{double(f[4]), double(f[5]), double(f[6])}});
    return out;
}

bool same(const Rgb &a, int r, int g, int b) { return a[0] == r && a[1] == g && a[2] == b; }

Frame solid(int r, int g, int b) {
    Frame f;
    f.width = 16;
    f.height = 8;
    for (int i = 0; i < 16 * 8; ++i) {
        f.pixels.push_back(uint8_t(b));
        f.pixels.push_back(uint8_t(g));
        f.pixels.push_back(uint8_t(r));
    }
    return f;
}

struct Rig {
    Json conf = Json::parse(R"({"address":"BE:32:03:63:BA:78","follow":true,"ambient":"red","ambientBrightness":100})");
    std::string power = "Active";
    double now = 1000;
    Record record;
    int resets = 0;
    bool stale = false;
    std::function<bool(int)> connectAs;
    Frame screen = solid(0, 0, 255);
    bool pending = false;
    Rgb previewRgb{{0, 0, 0}};
    std::string previewAddress;
    std::unique_ptr<Controller> a;

    Rig() { make(); }
    void make() {
        ControllerDeps d;
        d.config = [this]() { return cleanLedConfig(conf); };
        d.power = [this]() { return power; };
        d.clock = [this]() { return now; };
        d.makeStrip = [this](const std::string &address) {
            return std::unique_ptr<StripLink>(new FakeStrip(address, &record, connectAs));
        };
        d.grabber = std::make_shared<Grabber>([this](Frame &f) { f = screen; return true; }, false);
        d.resetBluetooth = [this]() { ++resets; return true; };
        d.staleLink = [this](const std::string &) { return stale; };
        d.takePreview = [this](Rgb &rgb, std::string &address) {
            if (!pending) return false;
            pending = false;
            rgb = previewRgb;
            address = previewAddress;
            return true;
        };
        a.reset(new Controller(d));
    }
};
} // namespace

int main() {
    {   // Follows the screen while on.
        Rig r;
        r.a->step();
        check(r.a->mode() == "follow" && colors(r.record).back()[2] > 200, "blue screen, blue strip");
        check(r.a->strip()->address() == "be:32:03:63:ba:78", "address in lower case");
    }
    {   // Room light once when the TV goes off, the film again when on.
        Rig r;
        r.a->step();
        r.power = "Suspend";
        r.a->step();
        r.a->step();
        int reds = 0;
        for (const Rgb &c : colors(r.record)) reds += same(c, 255, 0, 0);
        check(reds == 1, "the room light once");
        r.power = "Active";
        r.a->step();
        check(r.a->mode() == "follow", "following again");
    }
    {   // LG's announcement gives the room light before the state changes.
        Rig r;
        r.a->step();
        r.a->prepareOff("Suspend");
        check(same(colors(r.record).back(), 255, 0, 0), "red at the announcement");
        r.a->step();
        check(r.a->mode() == "ambient", "still Active, not back to the film");
        r.now += 30;
        r.a->step();
        check(r.a->mode() == "follow", "LG changed its mind");
    }
    {   // "off" also switches the strip off; "keep" writes nothing.
        Rig r;
        r.conf.set("ambient", Json::text("off"));
        r.a->step();
        r.a->prepareOff("PowerOff");
        check(r.record.frames.back() == powerFrame(false), "power frame off");
        Rig k;
        k.conf.set("ambient", Json::text("keep"));
        k.a->step();
        const size_t n = k.record.frames.size();
        k.a->prepareOff("Suspend");
        check(k.record.frames.size() == n, "keep leaves the last colour");
    }
    {   // Follow off: stop, room light, strip let go.
        Rig r;
        r.a->step();
        r.conf.set("follow", Json::flag(false));
        check(r.a->step() < 0, "stops");
        r.a->release();
        check(same(colors(r.record).back(), 255, 0, 0) && r.resets == 1 && !r.a->strip(), "released with the room light");
    }
    {   // Prova shows the colour, and after it the colour stays.
        Rig r;
        r.conf.set("follow", Json::flag(false));
        r.conf.set("address", Json());
        r.pending = true;
        r.previewRgb = Rgb{{10, 20, 30}};
        r.previewAddress = "be:32:03:63:ba:78";
        check(r.a->step() == 0.2, "Prova shown");
        check(same(colors(r.record).back(), 10, 20, 30), "the colour asked");
        r.now += Controller::kPreviewSeconds + 1;
        check(r.a->step() < 0, "then stops");
        r.a->release();
        check(same(colors(r.record).back(), 10, 20, 30), "the colour tried stays");
    }
    {   // Between captures the strip keeps fading.
        Rig r;
        r.screen = solid(0, 0, 0);
        r.a->step();
        r.screen = solid(0, 0, 255);
        std::vector<int> blues;
        for (int i = 0; i < 30; ++i) {
            r.now += Controller::kTick;
            r.a->step();
            blues.push_back(int(colors(r.record).back()[2]));
        }
        std::sort(blues.begin(), blues.end());
        const size_t distinct = size_t(std::unique(blues.begin(), blues.end()) - blues.begin());
        check(distinct > 8 && blues[distinct - 1] > 200, "many small writes, not one jump");
    }
    {   // A hung Bluetooth service is restarted.
        Rig r;
        r.connectAs = [](int n) { return n > 1; };
        r.make();
        r.a->step();
        check(r.resets == 1 && r.a->mode() == "follow", "restarted once, then following");
    }
    {   // A stale link is cleared by restarting Bluetooth.
        Rig r;
        r.stale = true;
        r.connectAs = [](int n) { return n > 1; };
        r.make();
        r.a->step();
        check(r.resets == 1 && r.a->mode() == "follow", "stale link cleared");
    }
    {   // The config is checked.
        const LedConfig c = cleanLedConfig(Json::parse(R"({"address":"nope","follow":true,"ambient":"mauve","brightness":900})"));
        check(c.address.empty() && !c.follow && c.ambient == "warm" && c.brightness == 100, "bad values cleaned");
    }
    std::cout << "ambilight controller tests passed\n";
    return 0;
}
