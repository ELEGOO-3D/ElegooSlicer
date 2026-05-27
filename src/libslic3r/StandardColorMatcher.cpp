#include "StandardColorMatcher.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r {

namespace {

struct Rgb { double r = 0.0, g = 0.0, b = 0.0; };
struct Xyz { double x = 0.0, y = 0.0, z = 0.0; };
struct Lab { double l = 0.0, a = 0.0, b = 0.0; };

bool parse_hex(const std::string& hex, Rgb& out)
{
    const std::size_t off = (!hex.empty() && hex[0] == '#') ? 1u : 0u;
    if (hex.size() != off + 6)
        return false;
    for (std::size_t i = 0; i < 6; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(hex[off + i])))
            return false;
    }
    unsigned r = 0, g = 0, b = 0;
    if (std::sscanf(hex.c_str() + off, "%2x%2x%2x", &r, &g, &b) != 3)
        return false;
    out.r = r;
    out.g = g;
    out.b = b;
    return true;
}

Xyz rgb_to_xyz(const Rgb& rgb)
{
    double rn = rgb.r / 255.0;
    double gn = rgb.g / 255.0;
    double bn = rgb.b / 255.0;

    // sRGB gamma
    rn = (rn > 0.04045) ? std::pow((rn + 0.055) / 1.055, 2.4) : rn / 12.92;
    gn = (gn > 0.04045) ? std::pow((gn + 0.055) / 1.055, 2.4) : gn / 12.92;
    bn = (bn > 0.04045) ? std::pow((bn + 0.055) / 1.055, 2.4) : bn / 12.92;

    Xyz xyz;
    xyz.x = (rn * 0.4124564 + gn * 0.3575761 + bn * 0.1804375) * 100.0;
    xyz.y = (rn * 0.2126729 + gn * 0.7151522 + bn * 0.0721750) * 100.0;
    xyz.z = (rn * 0.0193339 + gn * 0.1191920 + bn * 0.9503041) * 100.0;
    return xyz;
}

Lab xyz_to_lab(const Xyz& xyz)
{
    // D65 illuminant reference white
    constexpr double xn = 95.047;
    constexpr double yn = 100.000;
    constexpr double zn = 108.883;

    auto pivot = [](double v) {
        return (v > 0.008856) ? std::pow(v, 1.0 / 3.0) : (7.787 * v + 16.0 / 116.0);
    };

    const double fx = pivot(xyz.x / xn);
    const double fy = pivot(xyz.y / yn);
    const double fz = pivot(xyz.z / zn);

    Lab lab;
    lab.l = 116.0 * fy - 16.0;
    lab.a = 500.0 * (fx - fy);
    lab.b = 200.0 * (fy - fz);
    return lab;
}

// CIEDE2000 color difference. Handles the chroma==0 and hue wrap-around
// edge cases that the generic ColorSpaceConvert::DeltaE00 implementation
// in slic3r/Utils gets wrong.
double delta_e2000(const Lab& lab1, const Lab& lab2)
{
    const double c1    = std::sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
    const double c2    = std::sqrt(lab2.a * lab2.a + lab2.b * lab2.b);
    const double c_bar = (c1 + c2) / 2.0;

    const double c_bar7 = std::pow(c_bar, 7);
    const double g      = 0.5 * (1.0 - std::sqrt(c_bar7 / (c_bar7 + std::pow(25.0, 7))));

    const double a1p = (1.0 + g) * lab1.a;
    const double a2p = (1.0 + g) * lab2.a;

    const double c1p = std::sqrt(a1p * a1p + lab1.b * lab1.b);
    const double c2p = std::sqrt(a2p * a2p + lab2.b * lab2.b);

    auto hue_prime = [](double a, double b) {
        const double h = std::atan2(b, a) * 180.0 / M_PI;
        return (h < 0.0) ? (h + 360.0) : h;
    };

    const double h1p = hue_prime(a1p, lab1.b);
    const double h2p = hue_prime(a2p, lab2.b);

    const double dLp    = lab2.l - lab1.l;
    const double dCp    = c2p - c1p;
    const double h_diff = h2p - h1p;

    double dhp = 0.0;
    if (c1p * c2p != 0.0) {
        if (std::fabs(h_diff) <= 180.0)
            dhp = h_diff;
        else if (h_diff > 180.0)
            dhp = h_diff - 360.0;
        else
            dhp = h_diff + 360.0;
    }
    const double dHp = 2.0 * std::sqrt(c1p * c2p) * std::sin((dhp * M_PI) / 360.0);

    const double l_bar_p = (lab1.l + lab2.l) / 2.0;
    const double c_bar_p = (c1p + c2p) / 2.0;
    double       h_bar_p = 0.0;
    if (c1p * c2p == 0.0) {
        h_bar_p = h1p + h2p;
    } else {
        if (std::fabs(h_diff) <= 180.0)
            h_bar_p = (h1p + h2p) / 2.0;
        else
            h_bar_p = (h1p + h2p + 360.0) / 2.0;
    }
    if (h_bar_p >= 360.0)
        h_bar_p -= 360.0;

    const double T = 1.0
        - 0.17 * std::cos((h_bar_p - 30.0) * M_PI / 180.0)
        + 0.24 * std::cos((2.0 * h_bar_p) * M_PI / 180.0)
        + 0.32 * std::cos((3.0 * h_bar_p + 6.0) * M_PI / 180.0)
        - 0.20 * std::cos((4.0 * h_bar_p - 63.0) * M_PI / 180.0);

    const double dTheta   = 30.0 * std::exp(-std::pow((h_bar_p - 275.0) / 25.0, 2.0));
    const double c_bar_p7 = std::pow(c_bar_p, 7);
    const double rC       = 2.0 * std::sqrt(c_bar_p7 / (c_bar_p7 + std::pow(25.0, 7)));

    const double sL = 1.0 + (0.015 * std::pow(l_bar_p - 50.0, 2.0))
                            / std::sqrt(20.0 + std::pow(l_bar_p - 50.0, 2.0));
    const double sC = 1.0 + 0.045 * c_bar_p;
    const double sH = 1.0 + 0.015 * c_bar_p * T;

    const double rT = -std::sin((2.0 * dTheta) * M_PI / 180.0) * rC;

    return std::sqrt(std::pow(dLp / sL, 2.0)
                     + std::pow(dCp / sC, 2.0)
                     + std::pow(dHp / sH, 2.0)
                     + rT * (dCp / sC) * (dHp / sH));
}

std::string canonicalize_hex(const std::string& hex)
{
    if (hex.empty())
        return {};
    const std::size_t off = (hex[0] == '#') ? 1u : 0u;
    if (hex.size() != off + 6)
        return {};
    std::string body = hex.substr(off);
    for (char& c : body) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return {};
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return "#" + body;
}

const std::vector<StandardColorMatcher::Color>& default_palette()
{
    static const std::vector<StandardColorMatcher::Color> palette = {
        {"white",             "#FFFFFF"},
        {"yellow",            "#FFF242"},
        {"light_yellow_green", "#DBF47A"},
        {"green",             "#09CC3A"},
        {"dark_green",        "#077747"},
        {"blue_green",        "#0B6283"},
        {"cyan_green",        "#0BE2A0"},
        {"sky_blue",          "#74D9F3"},
        {"light_blue",        "#48A7FA"},
        {"dark_blue",         "#2850DF"},
        {"purple",            "#433089"},
        {"light_purple",      "#A03BF7"},
        {"pink_purple",       "#F32FF8"},
        {"light_pink_purple", "#D4B1DD"},
        {"pink",              "#F95D77"},
        {"red",               "#F72221"},
        {"brown",             "#7C4C00"},
        {"orange",            "#F88D36"},
        {"beige",             "#FCEBD7"},
        {"light_brown",       "#D2C5A3"},
        {"dark_brown",        "#AF7832"},
        {"dark_gray",         "#898989"},
        {"light_gray",        "#BCBCBC"},
        {"black",             "#000000"},
    };
    return palette;
}

} // namespace

StandardColorMatcher::StandardColorMatcher()
    : StandardColorMatcher(default_palette())
{}

StandardColorMatcher::StandardColorMatcher(std::vector<Color> palette)
{
    m_palette.reserve(palette.size());
    for (auto& c : palette) {
        c.hex = canonicalize_hex(c.hex);
        if (c.hex.empty())
            continue;
        m_palette.push_back(std::move(c));
    }
}

std::optional<StandardColorMatcher::Color>
StandardColorMatcher::match(const std::string& hex) const
{
    if (m_palette.empty() || hex.empty())
        return std::nullopt;

    Rgb rgb;
    if (!parse_hex(hex, rgb))
        return std::nullopt;

    const Lab input_lab = xyz_to_lab(rgb_to_xyz(rgb));

    double      best_delta = std::numeric_limits<double>::max();
    std::size_t best_index = 0;
    for (std::size_t i = 0; i < m_palette.size(); ++i) {
        Rgb entry_rgb;
        if (!parse_hex(m_palette[i].hex, entry_rgb))
            continue;
        const Lab    entry_lab = xyz_to_lab(rgb_to_xyz(entry_rgb));
        const double delta     = delta_e2000(input_lab, entry_lab);
        if (delta < best_delta) {
            best_delta = delta;
            best_index = i;
        }
    }
    return m_palette[best_index];
}

} // namespace Slic3r
