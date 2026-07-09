#ifndef slic3r_FlushVolCalc_hpp_
#define slic3r_FlushVolCalc_hpp_

#include "libslic3r.h"
#include "FlushVolPredictor.hpp"
#include "PrintConfig.hpp"

#include <string>

namespace Slic3r {

extern const int g_min_flush_volume_from_support;
extern const int g_flush_volume_to_support;
extern const int g_max_flush_volume;

class FlushVolCalculator
{
public:
    FlushVolCalculator(int min, int max, int flush_dataset, float multiplier = 1.0f);
    ~FlushVolCalculator()
    {
    }

    // When printer_settings_id is non-null, the per-printer override table in
    // resources/profiles/<Vendor>/flush/flush_volumes.json is consulted first.
    // Falls back to the HSV formula when no override matches.
    int calc_flush_vol(unsigned char src_a, unsigned char src_r, unsigned char src_g, unsigned char src_b,
        unsigned char dst_a, unsigned char dst_r, unsigned char dst_g, unsigned char dst_b,
        const std::string& printer_settings_id = "");

    int calc_flush_vol_rgb(unsigned char src_r,unsigned char src_g,unsigned char src_b,
        unsigned char dst_r, unsigned char dst_g, unsigned char dst_b);

    bool get_flush_vol_from_data(unsigned char src_r, unsigned char src_g, unsigned char src_b,
        unsigned char dst_r, unsigned char dst_g, unsigned char dst_b, float& flush);

private:
    int m_min_flush_vol;
    int m_max_flush_vol;
    float m_multiplier;
    int m_flush_dataset;
};


}

#endif
