#ifndef slic3r_StandardColorMatcher_hpp_
#define slic3r_StandardColorMatcher_hpp_

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

/**
 * @brief Maps arbitrary colors onto a small "standard" color palette.
 *
 * Shared by features that need a fixed color vocabulary (filament cloud-sync
 * mapping and flush volume overrides). Matching uses CIEDE2000 in CIE Lab
 * space; the instance is immutable after construction.
 */
class StandardColorMatcher
{
public:
    struct Color
    {
        std::string name;
        std::string hex; ///< canonical `#RRGGBB`
    };

    StandardColorMatcher();
    explicit StandardColorMatcher(std::vector<Color> palette);

    /**
     * @brief Find the palette entry closest to @p hex.
     * @return std::nullopt when @p hex is not a valid `#RRGGBB` string or
     *         the palette is empty.
     */
    std::optional<Color> match(const std::string& hex) const;

private:
    std::vector<Color> m_palette;
};

} // namespace Slic3r

#endif // slic3r_StandardColorMatcher_hpp_
