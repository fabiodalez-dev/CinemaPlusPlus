// The colour pipeline of the LED strip, with the cases of Cinema's
// test/py/ambilight_test.py.
#include "ambilight/colors.hpp"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using namespace nativetv::ambilight;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using Pixel = std::function<Rgb(int, int)>;

std::vector<uint8_t> bmp(int w, int h, const Pixel &pixel, bool topDown = true) {
    const int stride = (w * 3 + 3) & ~3;
    std::vector<uint8_t> data(54 + size_t(stride) * h, 0);
    auto put32 = [&data](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) data[at + i] = uint8_t(v >> (8 * i));
    };
    data[0] = 'B';
    data[1] = 'M';
    put32(2, uint32_t(data.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, uint32_t(w));
    put32(22, uint32_t(topDown ? -h : h));
    data[26] = 1;
    data[28] = 24;
    for (int y = 0; y < h; ++y) {
        const int row = topDown ? y : h - 1 - y;
        for (int x = 0; x < w; ++x) {
            const Rgb c = pixel(x, y);
            uint8_t *p = &data[54 + size_t(row) * stride + size_t(x) * 3];
            p[0] = uint8_t(c[2]);
            p[1] = uint8_t(c[1]);
            p[2] = uint8_t(c[0]);
        }
    }
    return data;
}

Rgb screen(const Pixel &pixel) {
    Frame f;
    check(parseBmp(bmp(240, 136, pixel), f), "a valid BMP");
    return screenColor(f);
}
} // namespace

int main() {
    // Frames and names.
    check((colorFrame({{255, 0, 128.4}}) == std::vector<int>{0x7e, 7, 5, 3, 255, 0, 128, 0x10, 0xef}), "colour frame");
    check((colorFrame({{300, -5, 10}}) == std::vector<int>{0x7e, 7, 5, 3, 255, 0, 10, 0x10, 0xef}), "clamped");
    check((powerFrame(true) == std::vector<int>{0x7e, 4, 4, 1, 0, 1, 0xff, 0, 0xef}), "power frame");
    check(brightnessFrame(250)[3] == 100, "brightness clamped");
    std::vector<int> rec = {11, 9};
    for (char c : std::string("ELK-BLEDOB")) rec.push_back(c);
    for (int v : {2, 1, 6, 3, 25, 128, 1, 5, 2, 18, 24, 240, 255}) rec.push_back(v);
    check(stripName(rec) == "ELK-BLEDOB", "name from the scan record");
    check(isStrip("ELK-BLEDOB") && isStrip("MELK-OA10") && !isStrip("[TV] Samsung"), "strip names");
    check(stripName({27, 255, 117, 0, 66}).empty() && stripName({}).empty(), "no name");

    // The BMP, both row orders.
    for (bool topDown : {true, false}) {
        Frame f;
        check(parseBmp(bmp(5, 4, [](int, int y) { return y < 2 ? Rgb{{200, 0, 0}} : Rgb{{0, 0, 200}}; }, topDown), f),
              "small BMP");
        check(f.width == 5 && f.height == 4 && f.pixels[2] == 200 && f.pixels[0] == 0, "red on top");
    }
    Frame bad;
    check(!parseBmp(std::vector<uint8_t>{'n', 'o'}, bad), "not a BMP");

    // Letterbox bars ignored.
    Rgb c = screen([](int, int y) { return y < 30 || y >= 106 ? Rgb{{0, 0, 0}} : Rgb{{30, 60, 200}}; });
    check(c[2] > 150 && c[0] < 60, "bars ignored");
    // A strong colour tints a grey background.
    c = screen([](int x, int y) {
        return x >= 100 && x < 160 && y >= 40 && y < 100 ? Rgb{{220, 20, 20}} : Rgb{{120, 120, 120}};
    });
    check(c[0] > c[1] * 1.2 && c[1] > 40, "red tint, not pure red");
    // Grey gives its average, black gives off.
    c = screen([](int, int) { return Rgb{{150, 150, 140}}; });
    check(c[0] > 120 && c[0] < 160 && c[2] > 120 && c[2] < 160, "grey average");
    c = screen([](int, int) { return Rgb{{0, 0, 0}}; });
    check(c[0] == 0 && c[1] == 0 && c[2] == 0, "black is off");
    // A mostly black frame is dim.
    c = screen([](int x, int) { return x < 12 ? Rgb{{255, 255, 255}} : Rgb{{0, 0, 0}}; });
    check(std::max(c[0], std::max(c[1], c[2])) < 40, "dim");
    // Skin on a beige wall is a soft warm light.
    c = boost(screen([](int x, int y) {
        if (x >= 80 && x < 160 && y >= 20 && y < 120) return Rgb{{225, 170, 140}};
        return y > 15 ? Rgb{{190, 180, 150}} : Rgb{{30, 30, 30}};
    }));
    check(c[0] > c[2] * 2 && c[1] > c[0] * 0.45 && c[2] > 20, "warm, orange, not red");

    // From screen to LEDs.
    c = boost({{255, 0, 0}});
    check(std::lround(c[0]) == 255 && std::lround(c[1]) == 0 && std::lround(c[2]) == 0, "pure red kept");
    c = boost({{90, 62, 41}});
    check(c[0] > 100 && c[0] > c[1] * 1.6 && c[1] > c[2] * 1.6, "warm brown is a visible orange");
    c = boost({{255, 50, 0}});
    check(std::lround(c[0]) == 255 && c[1] < 15, "orange stays orange");
    c = boost({{0, 0, 0}});
    check(c[0] == 0 && c[1] == 0 && c[2] == 0, "black kept");
    c = boost({{255, 255, 255}});
    // 255 x 0.85 and 255 x 0.7: Python rounds 178.5 down to even, C++ up.
    check(std::lround(c[0]) == 255 && std::lround(c[1]) == 217 && std::abs(c[2] - 178.5) < 0.01, "white warmed");

    // The fade.
    Smoother s;
    s.step({{0, 0, 0}}, 0.05);
    const double small = s.step({{40, 0, 0}}, 0.05)[0];
    Smoother s2;
    s2.step({{0, 0, 0}}, 0.05);
    const double cut = s2.step({{255, 0, 0}}, 0.05)[0];
    check(small / 40.0 < cut / 255.0, "cuts followed faster");
    Smoother s3;
    s3.step({{0, 0, 0}}, 0.05);
    double last = 0, biggest = 0;
    int under = 0;
    for (int i = 0; i < 40; ++i) {
        const double v = s3.step({{0, 0, 200}}, 0.05)[2];
        biggest = std::max(biggest, v - last);
        under += v < 150;
        last = v;
    }
    check(last > 190 && biggest < 40 && under > 4, "small steps to the colour");

    Rgb red;
    check(ambientColor("red", red) && red[0] == 255 && !ambientColor("mauve", red), "room light names");
    Rgb prev{{10, 10, 10}};
    check(!changed(&prev, {{11, 10, 10}}) && changed(&prev, {{12, 10, 10}}) && changed(nullptr, prev), "changed");
    std::cout << "ambilight colour tests passed\n";
    return 0;
}
