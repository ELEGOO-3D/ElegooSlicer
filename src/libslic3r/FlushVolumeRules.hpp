#ifndef slic3r_FlushVolumeRules_hpp_
#define slic3r_FlushVolumeRules_hpp_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "StandardColorMatcher.hpp"

namespace Slic3r {

/**
 * @brief Per-printer flush volume override table.
 *
 * Merges every `<resources_dir>/profiles/<Vendor>/flush/flush_volumes.json`
 * into a single rule set keyed by printer name. Input colors are snapped to
 * the nearest standard color before lookup so visually identical inputs
 * (e.g. several blues) all hit the same rule.
 */
class FlushVolumeRules
{
public:
    static FlushVolumeRules& instance();

    /**
     * @brief Look up an override flush volume for a color transition.
     * @return Override in mm^3, or std::nullopt when no rule matches.
     */
    std::optional<int> lookup(const std::string& printer_name,
                              const std::string& from_hex,
                              const std::string& to_hex);

private:
    FlushVolumeRules() = default;
    FlushVolumeRules(const FlushVolumeRules&) = delete;
    FlushVolumeRules& operator=(const FlushVolumeRules&) = delete;

    struct Entry
    {
        std::string from;
        std::string to;
        int         volume_mm3 = 0;
    };

    struct Rule
    {
        std::vector<std::string> printer_names;
        int                      priority        = 0;
        std::vector<Entry>       entries;
        std::size_t              insertion_index = 0;
    };

    void ensure_loaded();
    void load_default();
    void load_one_file(const boost::filesystem::path&            path,
                       std::vector<StandardColorMatcher::Color>& palette_acc);

    bool                 m_loaded = false;
    std::vector<Rule>    m_rules;
    StandardColorMatcher m_matcher;
};

} // namespace Slic3r

#endif // slic3r_FlushVolumeRules_hpp_
