#include "FlushVolumeRules.hpp"

#include "StandardColorMatcher.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <nlohmann/json.hpp>

namespace Slic3r {

namespace {

static constexpr const char* RELATIVE_FLUSH_PATH = "flush/flush_volumes.json";

// Read from the shipped resource tree only: the CLI does not populate the
// runtime data directory, so per-user overrides would silently disappear.
std::vector<boost::filesystem::path> collect_flush_files()
{
    std::vector<boost::filesystem::path> out;
    const boost::filesystem::path root = boost::filesystem::path(resources_dir()) / "profiles";
    if (!boost::filesystem::exists(root) || !boost::filesystem::is_directory(root))
        return out;
    for (const auto& entry : boost::filesystem::directory_iterator(root)) {
        if (!entry.is_directory())
            continue;
        boost::filesystem::path candidate = entry.path() / RELATIVE_FLUSH_PATH;
        if (boost::filesystem::exists(candidate))
            out.push_back(std::move(candidate));
    }
    return out;
}

} // namespace

FlushVolumeRules& FlushVolumeRules::instance()
{
    static FlushVolumeRules s;
    return s;
}

void FlushVolumeRules::ensure_loaded()
{
    static std::once_flag once;
    std::call_once(once, [this]() { this->load_default(); });
}

void FlushVolumeRules::load_default()
{
    const auto files = collect_flush_files();

    // Two-phase load: palette first so rule entries can be canonicalized
    // through the matcher and end up byte-identical to snapped inputs at
    // lookup time.
    std::vector<StandardColorMatcher::Color> palette;
    for (const auto& p : files)
        this->load_one_file(p, palette);

    if (!palette.empty())
        m_matcher = StandardColorMatcher(std::move(palette));

    std::size_t kept = 0;
    for (auto& rule : m_rules) {
        std::vector<Entry> good;
        good.reserve(rule.entries.size());
        for (auto& e : rule.entries) {
            auto mf = m_matcher.match(e.from);
            auto mt = m_matcher.match(e.to);
            if (!mf || !mt)
                continue;
            good.push_back({mf->hex, mt->hex, e.volume_mm3});
        }
        kept += good.size();
        rule.entries = std::move(good);
    }

    m_loaded = true;
    BOOST_LOG_TRIVIAL(info)
        << __FUNCTION__
        << boost::format(": loaded %1% rules (%2% entries) from %3% vendor file(s)")
               % m_rules.size() % kept % files.size();
}

void FlushVolumeRules::load_one_file(const boost::filesystem::path& path,
                                     std::vector<StandardColorMatcher::Color>& palette_acc)
{
    boost::nowide::ifstream ifs(path.string());
    if (!ifs.is_open()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot open " << path.string();
        return;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error)
            << __FUNCTION__ << ": parse error in " << path.string() << ": " << e.what();
        return;
    }

    auto colors_it = root.find("standard_colors");
    if (colors_it != root.end() && colors_it->is_array()) {
        for (const auto& color_json : *colors_it) {
            if (!color_json.is_object())
                continue;
            StandardColorMatcher::Color c;
            c.name = color_json.value("name", "");
            c.hex  = color_json.value("hex", "");
            if (c.hex.empty())
                continue;
            palette_acc.push_back(std::move(c));
        }
    }

    auto rules_it = root.find("rules");
    if (rules_it == root.end() || !rules_it->is_array())
        return;

    auto pull_strings = [](const nlohmann::json&     j,
                           const char*               key,
                           std::vector<std::string>& out) {
        auto key_it = j.find(key);
        if (key_it == j.end() || !key_it->is_array())
            return;
        for (const auto& s : *key_it) {
            if (s.is_string())
                out.push_back(s.get<std::string>());
        }
    };

    for (const auto& rule_json : *rules_it) {
        if (!rule_json.is_object())
            continue;

        Rule rule;
        // Running index across all files; later entries win on equal priority.
        rule.insertion_index = m_rules.size() + 1;
        rule.priority        = rule_json.value("priority", 0);

        pull_strings(rule_json, "printer_name", rule.printer_names);

        auto entries_it = rule_json.find("flush_volume_entries");
        if (entries_it != rule_json.end() && entries_it->is_array()) {
            for (const auto& entry_json : *entries_it) {
                if (!entry_json.is_array() || entry_json.size() < 3)
                    continue;
                if (!entry_json[0].is_string() || !entry_json[1].is_string() || !entry_json[2].is_number())
                    continue;

                Entry entry;
                entry.from       = entry_json[0].get<std::string>();
                entry.to         = entry_json[1].get<std::string>();
                entry.volume_mm3 = static_cast<int>(entry_json[2].get<double>());

                if (entry.from.empty() || entry.to.empty())
                    continue;
                rule.entries.push_back(std::move(entry));
            }
        }

        m_rules.push_back(std::move(rule));
    }
}

std::optional<int> FlushVolumeRules::lookup(const std::string& printer_name,
                                            const std::string& from_hex,
                                            const std::string& to_hex)
{
    this->ensure_loaded();

    if (printer_name.empty())
        return std::nullopt;

    auto mf = m_matcher.match(from_hex);
    auto mt = m_matcher.match(to_hex);
    if (!mf || !mt)
        return std::nullopt;
    const std::string& from = mf->hex;
    const std::string& to   = mt->hex;

    auto contains = [](const std::vector<std::string>& arr, const std::string& value) {
        return std::find(arr.begin(), arr.end(), value) != arr.end();
    };

    const Rule* best        = nullptr;
    int         best_volume = 0;

    for (const auto& rule : m_rules) {
        if (!contains(rule.printer_names, printer_name))
            continue;

        const Entry* hit = nullptr;
        for (const auto& entry : rule.entries) {
            if (entry.from == from && entry.to == to) {
                hit = &entry;
                break;
            }
        }
        if (hit == nullptr)
            continue;

        const bool take = (best == nullptr)
            || (rule.priority > best->priority)
            || (rule.priority == best->priority && rule.insertion_index > best->insertion_index);
        if (take) {
            best        = &rule;
            best_volume = hit->volume_mm3;
        }
    }

    if (best == nullptr)
        return std::nullopt;
    return best_volume;
}

} // namespace Slic3r
