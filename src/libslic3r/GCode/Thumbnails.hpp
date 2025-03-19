#ifndef slic3r_GCodeThumbnails_hpp_
#define slic3r_GCodeThumbnails_hpp_

#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "../enum_bitmask.hpp"
#include "ThumbnailData.hpp"
#include "../enum_bitmask.hpp"
#include <tuple>
#include <vector>
#include <memory>
#include <string_view>

#include <boost/beast/core/detail/base64.hpp>

namespace Slic3r {
    enum class ThumbnailError : int { InvalidVal, OutOfRange, InvalidExt };
    using ThumbnailErrors = enum_bitmask<ThumbnailError>;
    ENABLE_ENUM_BITMASK_OPERATORS(ThumbnailError);
}

namespace Slic3r::GCodeThumbnails {

struct CompressedImageBuffer
{
    void       *data { nullptr };
    size_t      size { 0 };
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::string get_hex(const unsigned int input);
std::string rjust(std::string input, unsigned int width, char fill_char);
std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData& data, GCodeThumbnailsFormat format);
std::string get_error_string(const ThumbnailErrors& errors);


typedef std::vector<std::tuple<GCodeThumbnailsFormat, Vec2d, Vec4d>> GCodeThumbnailDefinitionsList;
using namespace std::literals;
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string& thumbnails_string, const std::string_view def_ext = "PNG"sv);
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase &config);


template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_thumbnails_to_file(ThumbnailsGeneratorCallback&                                thumbnail_cb,
                                      int                                                         plate_id,
                                      const std::vector<std::tuple<GCodeThumbnailsFormat, Vec2d, Vec4d>>& thumbnails_list,
                                      WriteToOutput                                               output,
                                      ThrowIfCanceledCallback                                     throw_if_canceled)
{
    // Write thumbnails using base64 encoding
    if (thumbnail_cb == nullptr)
        return;
    short i = 0;
    bool first_ColPic = true;
    for (const auto& [format, size, background_color] : thumbnails_list) {
        static constexpr const size_t max_row_length = 78;
        ThumbnailsList                thumbnails     = thumbnail_cb(ThumbnailsParams{{size}, true, true, true, true, plate_id});
        for (ThumbnailData &data : thumbnails) {
            if (data.is_valid()) {
                //set background color
                if (int(background_color[3] * 255) != 0) {
                    for (unsigned int y = 0; y < data.height; ++y) {
                        for (unsigned int x = 0; x < data.width; ++x) {
                            unsigned int index     = (y * data.width + x) * 4;
                            float        alpha     = data.pixels[index + 3] / 255.0f;
                            data.pixels[index + 0] = static_cast<unsigned char>((data.pixels[index + 0] * alpha) +
                                                                                (background_color[0] * 255 * (1 - alpha)));
                            data.pixels[index + 1] = static_cast<unsigned char>((data.pixels[index + 1] * alpha) +
                                                                                (background_color[1] * 255 * (1 - alpha)));
                            data.pixels[index + 2] = static_cast<unsigned char>((data.pixels[index + 2] * alpha) +
                                                                                (background_color[2] * 255 * (1 - alpha)));
                            data.pixels[index + 3] = static_cast<unsigned char>((data.pixels[index + 3] * alpha) +
                                                                                (background_color[3] * 255 * (1 - alpha)));
                        }
                    }
                }

                auto compressed = compress_thumbnail(data, format);
                if (compressed->data && compressed->size) {
                    if (format == GCodeThumbnailsFormat::BTT_TFT) {
                        // write BTT_TFT header
                        output((";" + rjust(get_hex(data.width), 4, '0') + rjust(get_hex(data.height), 4, '0') + "\r\n").c_str());
                        output((char *) compressed->data);
                        if (i == (thumbnails_list.size() - 1))
                            output("; bigtree thumbnail end\r\n\r\n");
                    }
                    else if (format == GCodeThumbnailsFormat::ColPic) {
                        std::string prefix;
                        if (first_ColPic) {
                            prefix = ";gimage:";
                        } else {
                            prefix = ";simage:";
                        }
                        int prefix_len = prefix.length();
                        int max_line_len = 1024;
                        int max_line_data_len = max_line_len - prefix_len - 1;
                    
                        auto encoded_data = reinterpret_cast<char*>(compressed->data);
                        std::vector<uint8_t> data(encoded_data, encoded_data + strlen(encoded_data));
                        int data_len = data.size();
                        int lines_count = data_len / max_line_data_len;
                        int append_len = max_line_data_len - 3 - (data_len % max_line_data_len);
                    
                        std::string result;
                        for (int i = 0; i < data_len; ++i) {
                            if (i % max_line_data_len == 0) {
                                if (i > 0) {
                                    result += '\r';
                                    if (i == lines_count * max_line_data_len) {
                                        result += ';';
                                    }
                                }
                                result += prefix;
                            }
                            result += static_cast<char>(data[i]);
                        }            
                        result += "\r;" + std::string(append_len, '0');
                        result += '\r';
                        //if (first_ColPic) {
                        //    output((boost::format("\n\n;gimage:%s\n\n") % reinterpret_cast<char*>(compressed->data)).str().c_str());
                        //} else {
                        //    output((boost::format("\n\n;simage:%s\n\n") % reinterpret_cast<char*>(compressed->data)).str().c_str());
                        //}
                        output(result.c_str());
                        first_ColPic = false;
                    } 
                    else {
                        output("\n\n");
                        output("; THUMBNAIL_BLOCK_START\n");
                        std::string encoded;
                        encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
                        encoded.resize(boost::beast::detail::base64::encode((void *) encoded.data(), (const void *) compressed->data,
                                                                            compressed->size));                        
                        output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % data.width % data.height % encoded.size())
                                   .str()
                                   .c_str());                        
                        while (encoded.size() > max_row_length) {
                            output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
                            encoded = encoded.substr(max_row_length);
                        }

                        // Orca write remaining ecoded data
                        if (encoded.size() > 0)
                            output((boost::format("; %s\n") % encoded).str().c_str());

                        output((boost::format("; %s end\n") % compressed->tag()).str().c_str());
                        output("; THUMBNAIL_BLOCK_END\n\n");
                    }
                    throw_if_canceled();
                }
            }
        }
        i++;
    }
}

} // namespace Slic3r::GCodeThumbnails

#endif // slic3r_GCodeThumbnails_hpp_
