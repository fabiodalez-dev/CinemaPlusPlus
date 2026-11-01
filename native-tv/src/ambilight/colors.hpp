#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nativetv {
namespace ambilight {

// The LED strip behind the TV, ported from Cinema's ambilight.py (0.3.9).
// The colour pipeline: a 240x136 capture of the screen, sampled every 8th
// pixel, black left out, the strongest hue mixed into the average as much as
// the screen is colourful; then from screen values to LED light (a curve
// between the channels, the strip's cold white corrected, mid scenes lifted);
// then a time-based fade between captures.

using Rgb = std::array<double, 3>;

constexpr int kWidth = 240;
constexpr int kHeight = 136;
constexpr int kStep = 8;
constexpr int kDark = 24;

struct Frame {
    int width = 0;
    int height = 0;
    // BGR bytes, line by line from the top, width * 3 per line.
    std::vector<uint8_t> pixels;
};

// A 24-bit BMP, bottom-up or top-down; false when it is not that.
bool parseBmp(const std::vector<uint8_t> &data, Frame &frame);

int hueBin(int r, int g, int b, int bins = 12);
Rgb screenColor(const Frame &frame, int step = kStep);
Rgb boost(const Rgb &rgb, double saturation = 1.0, double gamma = 0.8, double mix = 2.0);
Rgb scaled(const Rgb &rgb, double percent);
bool changed(const Rgb *previous, const Rgb &current, int threshold = 2);

// Fades towards the target over time: about `slow` seconds on a small change,
// `fast` on a big one (a cut).
class Smoother {
  public:
    explicit Smoother(double slow = 0.7, double fast = 0.25) : slow_(slow), fast_(fast) {}
    Rgb step(const Rgb &target, double dt);
    void reset() { has_ = false; }

  private:
    double slow_;
    double fast_;
    bool has_ = false;
    Rgb value_{{0, 0, 0}};
};

// ELK-BLEDOM frames: service FFF0, characteristic FFF3.
std::vector<int> powerFrame(bool on);
std::vector<int> colorFrame(const Rgb &rgb);
std::vector<int> brightnessFrame(int percent);

// The advertised name in a BLE scan record, or "".
std::string stripName(const std::vector<int> &record);
bool isStrip(const std::string &name);

// The room light names Settings offers, and "off"; false for an unknown name.
bool ambientColor(const std::string &name, Rgb &rgb);

} // namespace ambilight
} // namespace nativetv
