#include "ambilight/colors.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace nativetv {
namespace ambilight {
namespace {
uint32_t u32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 |
           uint32_t(d[at + 3]) << 24;
}
int32_t i32(const std::vector<uint8_t> &d, size_t at) { return static_cast<int32_t>(u32(d, at)); }
uint16_t u16(const std::vector<uint8_t> &d, size_t at) { return uint16_t(d[at] | d[at + 1] << 8); }
double maxOf(const Rgb &c) { return std::max(c[0], std::max(c[1], c[2])); }
// The strip's own white is cold: its green and blue LEDs outshine the red.
const Rgb kWhiteBalance{{1.0, 0.85, 0.7}};
} // namespace

bool parseBmp(const std::vector<uint8_t> &data, Frame &frame) {
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') return false;
    const uint32_t offset = u32(data, 10);
    const int32_t w = i32(data, 18);
    int32_t h = i32(data, 22);
    if (u16(data, 28) != 24 || w <= 0 || h == 0) return false;
    const bool topDown = h < 0;
    h = std::abs(h);
    const size_t stride = (size_t(w) * 3 + 3) & ~size_t(3);
    if (data.size() < offset + stride * size_t(h)) return false;
    frame.width = w;
    frame.height = h;
    frame.pixels.resize(size_t(w) * 3 * size_t(h));
    for (int y = 0; y < h; ++y) {
        const int src = topDown ? y : h - 1 - y;
        std::copy(data.begin() + offset + size_t(src) * stride,
                  data.begin() + offset + size_t(src) * stride + size_t(w) * 3,
                  frame.pixels.begin() + size_t(y) * size_t(w) * 3);
    }
    return true;
}

int hueBin(int r, int g, int b, int bins) {
    const int mx = std::max(r, std::max(g, b));
    const int mn = std::min(r, std::min(g, b));
    const int d = mx - mn;
    if (d == 0) return 0;
    double h;
    if (mx == r) {
        h = std::fmod(double(g - b) / d, 6.0);
        if (h < 0) h += 6.0;
    } else if (mx == g) {
        h = double(b - r) / d + 2;
    } else {
        h = double(r - g) / d + 4;
    }
    return int(h * bins / 6.0) % bins;
}

Rgb screenColor(const Frame &frame, int step) {
    std::map<int, std::array<double, 4>> bins;
    double total[4] = {0, 0, 0, 0};
    long lit = 0, seen = 0;
    for (int y = step / 2; y < frame.height; y += step) {
        const uint8_t *row = frame.pixels.data() + size_t(y) * size_t(frame.width) * 3;
        for (int x = step / 2; x < frame.width; x += step) {
            const int b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
            ++seen;
            const int mx = std::max(r, std::max(g, b));
            if (mx < kDark) continue;
            ++lit;
            total[0] += r;
            total[1] += g;
            total[2] += b;
            total[3] += 1;
            const double sat = double(mx - std::min(r, std::min(g, b))) / mx;
            if (sat < 0.2) continue;
            const double weight = sat * sat * mx;
            std::array<double, 4> &acc = bins[hueBin(r, g, b)];
            acc[0] += r * weight;
            acc[1] += g * weight;
            acc[2] += b * weight;
            acc[3] += weight;
        }
    }
    if (!seen || !total[3]) return Rgb{{0, 0, 0}};
    const Rgb avg{{total[0] / total[3], total[1] / total[3], total[2] / total[3]}};
    // How much of the screen is lit: a mostly black frame gives a dim light.
    const double level = std::min(1.0, double(lit) / seen * 2.5);
    if (!bins.empty()) {
        const std::array<double, 4> *best = nullptr;
        double weights = 0;
        for (const auto &entry : bins) {
            weights += entry.second[3];
            if (!best || entry.second[3] > (*best)[3]) best = &entry.second;
        }
        const double colorful = weights / (255.0 * total[3]);
        if (colorful > 0.05) {
            Rgb col{{(*best)[0] / (*best)[3], (*best)[1] / (*best)[3], (*best)[2] / (*best)[3]}};
            // The hue of the winner at the brightness of the scene, mixed with
            // the scene itself: a face on a beige wall stays a soft warm light.
            const double bright = maxOf(avg) / (maxOf(col) ? maxOf(col) : 1);
            for (double &c : col) c *= std::max(bright, 0.6);
            const double m = std::min(0.7, colorful * 1.5);
            Rgb out;
            for (int i = 0; i < 3; ++i) out[i] = (avg[i] + (col[i] - avg[i]) * m) * level;
            return out;
        }
    }
    return Rgb{{avg[0] * level, avg[1] * level, avg[2] * level}};
}

Rgb boost(const Rgb &rgb, double saturation, double gamma, double mix) {
    Rgb c;
    for (int i = 0; i < 3; ++i) c[i] = std::max(0.0, std::min(255.0, rgb[i]));
    const double mx = maxOf(c);
    if (mx <= 0) return Rgb{{0, 0, 0}};
    const double grey = (c[0] + c[1] + c[2]) / 3.0;
    Rgb col;
    for (int i = 0; i < 3; ++i) col[i] = std::max(0.0, grey + (c[i] - grey) * saturation);
    const double top = maxOf(col) ? maxOf(col) : 1.0;
    for (int i = 0; i < 3; ++i) col[i] = std::pow(col[i] / top, mix) * kWhiteBalance[i];
    const double scale = 255.0 * std::pow(mx / 255.0, gamma) / (maxOf(col) ? maxOf(col) : 1);
    for (double &v : col) v = std::min(255.0, v * scale);
    return col;
}

Rgb scaled(const Rgb &rgb, double percent) {
    return Rgb{{rgb[0] * percent / 100.0, rgb[1] * percent / 100.0, rgb[2] * percent / 100.0}};
}

bool changed(const Rgb *previous, const Rgb &current, int threshold) {
    if (!previous) return true;
    int most = 0;
    for (int i = 0; i < 3; ++i)
        most = std::max(most, std::abs(int(std::lround((*previous)[i])) - int(std::lround(current[i]))));
    return most >= threshold;
}

Rgb Smoother::step(const Rgb &target, double dt) {
    if (!has_) {
        has_ = true;
        value_ = target;
        return value_;
    }
    double dist = 0;
    for (int i = 0; i < 3; ++i) dist = std::max(dist, std::abs(value_[i] - target[i]));
    const double k = 1.0 - std::exp(-std::max(0.0, dt) / (dist > 90 ? fast_ : slow_));
    for (int i = 0; i < 3; ++i) value_[i] += (target[i] - value_[i]) * k;
    return value_;
}

std::vector<int> powerFrame(bool on) {
    return {0x7e, 0x04, 0x04, on ? 0x01 : 0x00, 0x00, on ? 0x01 : 0x00, 0xff, 0x00, 0xef};
}

std::vector<int> colorFrame(const Rgb &rgb) {
    int c[3];
    for (int i = 0; i < 3; ++i) c[i] = std::max(0, std::min(255, int(std::lround(rgb[i]))));
    return {0x7e, 0x07, 0x05, 0x03, c[0], c[1], c[2], 0x10, 0xef};
}

std::vector<int> brightnessFrame(int percent) {
    return {0x7e, 0x04, 0x01, std::max(0, std::min(100, percent)), 0x00, 0xff, 0xff, 0x00, 0xef};
}

std::string stripName(const std::vector<int> &record) {
    size_t i = 0;
    while (i < record.size()) {
        const int n = record[i];
        if (n == 0 || i + size_t(n) >= record.size() + 1) break;
        const int kind = i + 1 < record.size() ? record[i + 1] : -1;
        if (kind == 0x08 || kind == 0x09) {
            std::string name;
            for (size_t k = i + 2; k < i + 1 + size_t(n) && k < record.size(); ++k)
                name += char(record[k] & 0xff);
            return name;
        }
        i += size_t(n) + 1;
    }
    return "";
}

bool isStrip(const std::string &name) {
    std::string upper;
    for (char c : name) upper += char(std::toupper(static_cast<unsigned char>(c)));
    for (const char *prefix : {"ELK-", "MELK", "ELK~", "LEDBLE", "LED LIGHT STRIP", "XSL-"})
        if (upper.compare(0, std::string(prefix).size(), prefix) == 0) return true;
    return false;
}

bool ambientColor(const std::string &name, Rgb &rgb) {
    static const std::map<std::string, Rgb> kAmbient = {
        {"warm", {{255, 120, 30}}}, {"white", {{255, 255, 255}}}, {"orange", {{255, 70, 0}}},
        {"red", {{255, 0, 0}}},     {"pink", {{255, 30, 110}}},   {"purple", {{140, 0, 255}}},
        {"blue", {{0, 40, 255}}},   {"sky", {{0, 170, 255}}},     {"green", {{0, 255, 40}}},
        {"off", {{0, 0, 0}}}};
    const auto it = kAmbient.find(name);
    if (it == kAmbient.end()) return false;
    rgb = it->second;
    return true;
}

} // namespace ambilight
} // namespace nativetv
