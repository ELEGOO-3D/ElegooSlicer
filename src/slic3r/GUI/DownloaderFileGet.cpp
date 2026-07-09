#include "DownloaderFileGet.hpp"

#include <thread>
#include <curl/curl.h>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>

#include "format.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cstring>
using namespace std;

namespace Slic3r { namespace GUI {

const size_t DOWNLOAD_MAX_CHUNK_SIZE = 10ULL * 1024 * 1024;
const size_t DOWNLOAD_SIZE_LIMIT     = 5ULL * 1024 * 1024 * 1024;

std::string FileGet::unescape_url(const std::string& unescaped)
{
    std::string ret_val;
    CURL*       curl = curl_easy_init();
    if (curl) {
        int   decodelen;
        char* decoded = curl_easy_unescape(curl, unescaped.c_str(), unescaped.size(), &decodelen);
        if (decoded) {
            ret_val = std::string(decoded);
            curl_free(decoded);
        }
        curl_easy_cleanup(curl);
    }
    return ret_val;
}
bool FileGet::is_subdomain(const std::string& url, const std::string& domain)
{
    // domain should be f.e. printables.com (.com including)
    char*       host;
    std::string host_string;
    CURLUcode   rc;
    CURLU*      curl = curl_url();
    if (!curl) {
        BOOST_LOG_TRIVIAL(error) << "Failed to init Curl library in function is_subdomain.";
        return false;
    }
    rc = curl_url_set(curl, CURLUPART_URL, url.c_str(), 0);
    if (rc != CURLUE_OK) {
        curl_url_cleanup(curl);
        return false;
    }
    rc = curl_url_get(curl, CURLUPART_HOST, &host, 0);
    if (rc != CURLUE_OK || !host) {
        curl_url_cleanup(curl);
        return false;
    }
    host_string = std::string(host);
    curl_free(host);
    // now host should be subdomain.domain or just domain
    if (domain == host_string) {
        curl_url_cleanup(curl);
        return true;
    }
    if (boost::ends_with(host_string, "." + domain)) {
        curl_url_cleanup(curl);
        return true;
    }
    curl_url_cleanup(curl);
    return false;
}

namespace {
unsigned get_current_pid()
{
#ifdef WIN32
    return GetCurrentProcessId();
#else
    return ::getpid();
#endif
}
} // namespace

// int = DOWNLOAD ID; string = file path
wxDEFINE_EVENT(EVT_DWNLDR_FILE_COMPLETE, wxCommandEvent);
// int = DOWNLOAD ID; string = error msg
wxDEFINE_EVENT(EVT_DWNLDR_FILE_ERROR, wxCommandEvent);
// int = DOWNLOAD ID; string = progress percent
wxDEFINE_EVENT(EVT_DWNLDR_FILE_PROGRESS, wxCommandEvent);
// int = DOWNLOAD ID; string = name
wxDEFINE_EVENT(EVT_DWNLDR_FILE_NAME_CHANGE, wxCommandEvent);
// int = DOWNLOAD ID;
wxDEFINE_EVENT(EVT_DWNLDR_FILE_PAUSED, wxCommandEvent);
// int = DOWNLOAD ID;
wxDEFINE_EVENT(EVT_DWNLDR_FILE_CANCELED, wxCommandEvent);

struct FileGet::priv
{
    const int               m_id;
    std::string             m_url;
    std::string             m_filename;
    std::thread             m_io_thread;
    wxEvtHandler*           m_evt_handler;
    boost::filesystem::path m_dest_folder;
    boost::filesystem::path m_tmp_path; // path when ongoing download
    std::atomic_bool        m_cancel{false};
    std::atomic_bool        m_pause{false};
    std::atomic_bool        m_stopped{false}; // either canceled or paused - download is not running
    size_t                  m_written{0};
    size_t                  m_absolute_size{0};
    size_t                  m_last_percent{0};
    bool                    m_range_supported{true}; // default supported, set to false once proven otherwise
    priv(int ID, std::string&& url, const std::string& filename, wxEvtHandler* evt_handler, const boost::filesystem::path& dest_folder);

    void get_perform();
    void parse_response_headers(const std::string& header, size_t written_previously);
    bool ensure_paths_and_open(FILE*& file, boost::filesystem::path& dest_path, size_t written_previously);
};

FileGet::priv::priv(
    int ID, std::string&& url, const std::string& filename, wxEvtHandler* evt_handler, const boost::filesystem::path& dest_folder)
    : m_id(ID), m_url(std::move(url)), m_filename(filename), m_evt_handler(evt_handler), m_dest_folder(dest_folder)
{}

string parse_content_disposition(const string& content_disp);

std::string extract_remote_filename(const std::string& str)
{
    std::istringstream stream(str);
    std::string        line;

    while (std::getline(stream, line)) {
        if (line.find("Content-Disposition:") != std::string::npos || line.find("content-disposition:") != std::string::npos) {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string value    = line.substr(colon_pos + 1);
                std::string filename = parse_content_disposition(value);
                if (!filename.empty()) {
                    return filename;
                }
            }
        }
    }

    return "";
}

namespace {
static inline std::string trim_ws_local(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return std::string();
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

static inline bool starts_with_icase(const std::string& s, const char* prefix)
{
    size_t n = std::strlen(prefix);
    if (s.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

static inline bool parse_http_status(const std::string& line, int& out_status)
{
    // HTTP/1.1 200 OK
    if (!starts_with_icase(line, "http/"))
        return false;
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos)
        return false;
    size_t sp2 = line.find(' ', sp1 + 1);
    std::string code_str = (sp2 == std::string::npos) ? line.substr(sp1 + 1) : line.substr(sp1 + 1, sp2 - (sp1 + 1));
    try {
        out_status = std::stoi(code_str);
        return true;
    } catch (...) {
        return false;
    }
}

static inline bool parse_content_range(const std::string& value, size_t& out_start, size_t& out_total)
{
    // bytes 100-200/999
    std::string v = value;
    // lower-case for searching "bytes"
    std::string lower = v;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t bytes_pos = lower.find("bytes");
    if (bytes_pos == std::string::npos)
        return false;
    size_t start_pos = lower.find_first_of("0123456789", bytes_pos);
    if (start_pos == std::string::npos)
        return false;
    size_t dash_pos = lower.find('-', start_pos);
    size_t slash_pos = lower.find('/', start_pos);
    if (dash_pos == std::string::npos || slash_pos == std::string::npos || dash_pos >= slash_pos)
        return false;
    std::string start_str = lower.substr(start_pos, dash_pos - start_pos);
    std::string total_str = lower.substr(slash_pos + 1);
    total_str = trim_ws_local(total_str);
    try {
        out_start = static_cast<size_t>(std::stoull(start_str));
        out_total = static_cast<size_t>(std::stoull(total_str));
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace

void FileGet::priv::parse_response_headers(const std::string& header, size_t written_previously)
{
    // Only fill defaults once:
    // - filename: only if empty
    // - size: only if 0
    // - range_supported: default true, set to false once proven otherwise

    std::istringstream stream(header);
    std::string        line;

    bool   content_length_seen = false;
    size_t content_length      = 0;

    bool   content_range_seen  = false;
    size_t content_range_start = 0;
    size_t content_range_total = 0;

    bool accept_ranges_seen  = false;
    bool accept_ranges_bytes = false;

    bool transfer_encoding_chunked = false;

    int  http_status      = 0;
    bool http_status_seen = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!http_status_seen && parse_http_status(line, http_status)) {
            http_status_seen = true;
            continue;
        }

        if (!accept_ranges_seen && starts_with_icase(line, "accept-ranges:")) {
            std::string value = line.substr(std::string("accept-ranges:").size());
            value             = trim_ws_local(value);
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            accept_ranges_bytes = (lower.find("bytes") != std::string::npos);
            accept_ranges_seen  = true;
            continue;
        }

        if (starts_with_icase(line, "transfer-encoding:")) {
            std::string value = line.substr(std::string("transfer-encoding:").size());
            value             = trim_ws_local(value);
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("chunked") != std::string::npos) {
                transfer_encoding_chunked = true;
            }
            continue;
        }

        if (!content_length_seen && starts_with_icase(line, "content-length:")) {
            std::string value = line.substr(std::string("content-length:").size());
            value             = trim_ws_local(value);
            try {
                content_length      = static_cast<size_t>(std::stoull(value));
                content_length_seen = true;
            } catch (...) {
            }
            continue;
        }

        if (!content_range_seen && starts_with_icase(line, "content-range:")) {
            std::string value = line.substr(std::string("content-range:").size());
            value             = trim_ws_local(value);
            if (parse_content_range(value, content_range_start, content_range_total)) {
                content_range_seen = true;
            }
            continue;
        }
    }

    // filename (only if default)
    if (m_filename.empty()) {
        std::string remote = extract_remote_filename(header);
        if (!remote.empty()) {
            m_filename = sanitize_filename(remote);
            if (!m_filename.empty()) {
                wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_NAME_CHANGE);
                evt->SetString(boost::nowide::widen(m_filename));
                evt->SetInt(m_id);
                m_evt_handler->QueueEvent(evt);
            }
        }
    }

    // size (only if default)
    if (m_absolute_size == 0) {
        if (content_range_seen && content_range_total > 0) {
            m_absolute_size = content_range_total;
        } else if (content_length_seen && content_length > 0) {
            if (written_previously > 0 && http_status_seen && http_status == 206) {
                m_absolute_size = written_previously + content_length;
            } else {
                m_absolute_size = content_length;
            }
        }
    }

    // range support detection (only if still default true)
    if (m_range_supported) {
        // For resume requests: validate response
        if (written_previously > 0) {
            if (http_status_seen && http_status == 200) {
                m_range_supported = false;  // should be 206, got 200
            }
            if (content_range_seen && content_range_start != written_previously) {
                m_range_supported = false;  // offset mismatch
            }
        }
        
        // Server capability hints (for both first download and resume)
        if (accept_ranges_seen && !accept_ranges_bytes) {
            m_range_supported = false;  // Accept-Ranges: none
        }
        
        // Conservative: chunked without explicit Accept-Ranges: bytes support
        if (transfer_encoding_chunked && !accept_ranges_bytes) {
            m_range_supported = false;  // chunked usually means dynamic content
        }
    }
}

bool FileGet::priv::ensure_paths_and_open(FILE*& file, boost::filesystem::path& dest_path, size_t written_previously)
{
    if (file != nullptr)
        return true;

    // Decide final filename value (external provided or header-filled). Fallback is URL-derived.
    if (m_filename.empty()) {
        std::string fallback = FileGet::filename_from_url(m_url);
        fallback             = sanitize_filename(fallback);
        if (fallback.empty())
            fallback = "download";
        m_filename = fallback;
        wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_NAME_CHANGE);
        evt->SetString(boost::nowide::widen(m_filename));
        evt->SetInt(m_id);
        m_evt_handler->QueueEvent(evt);
    }

    // For fresh download, ensure a unique final name once.
    if (written_previously == 0) {
        boost::filesystem::path cand_path = m_dest_folder / m_filename;
        std::string extension             = cand_path.extension().string();
        std::string just_filename         = m_filename.substr(0, m_filename.size() - extension.size());
        std::string final_filename        = just_filename;
        try {
            size_t version = 0;
            while (boost::filesystem::exists(m_dest_folder / (final_filename + extension)) ||
                   boost::filesystem::exists(m_dest_folder /
                                             (final_filename + extension + "." + std::to_string(get_current_pid()) + ".download"))) {
                ++version;
                if (version > 999)
                    break;
                final_filename = GUI::format("%1%(%2%)", just_filename, std::to_string(version));
            }
        } catch (...) {
        }
        std::string uniq = sanitize_filename(final_filename + extension);
        if (!uniq.empty() && uniq != m_filename) {
            m_filename = uniq;
            wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_NAME_CHANGE);
            evt->SetString(boost::nowide::widen(m_filename));
            evt->SetInt(m_id);
            m_evt_handler->QueueEvent(evt);
        }
    }

    dest_path = m_dest_folder / m_filename;

    // Create tmp path if needed (resume keeps existing m_tmp_path)
    if (m_tmp_path.empty()) {
        m_tmp_path = m_dest_folder / (m_filename + "." + std::to_string(get_current_pid()) + ".download");
    }

    // Open file when we actually start writing
    file = boost::nowide::fopen(m_tmp_path.string().c_str(), (written_previously == 0) ? "wb" : "ab");
    return file != nullptr;
}
void FileGet::priv::get_perform()
{
    assert(m_evt_handler);
    assert(!m_url.empty());
    assert(boost::filesystem::is_directory(m_dest_folder));

    m_stopped = false;
    m_last_percent = 0;

    bool need_restart = false;

    do {
        need_restart = false;

        boost::filesystem::path dest_path;
        FILE*                   file = nullptr;

        auto safe_close = [&file]() {
            if (file) {
                fclose(file);
                file = nullptr;
            }
        };

        // If we are resuming but already know range is not supported, restart from 0 without sending Range.
        if (m_written > 0 && !m_range_supported) {
            if (!m_tmp_path.empty())
                std::remove(m_tmp_path.string().c_str());
            m_tmp_path.clear();
            m_written       = 0;
            m_absolute_size = 0;
            m_last_percent  = 0;
        }

        std::string range_string;
        if (m_written > 0 && m_range_supported)
            range_string = std::to_string(m_written) + "-";

        size_t written_previously   = m_written;
        size_t written_this_session = 0;

        Http::get(m_url)
            .size_limit(DOWNLOAD_SIZE_LIMIT) // more?
            .set_range(range_string)
            .on_header_callback([&](std::string header) {
                parse_response_headers(header, written_previously);
                // If we just proved resume is not supported, restart as full download.
                if (written_previously > 0 && !m_range_supported) {
                    need_restart = true;
                }
            })
            .on_progress([&](Http::Progress progress, bool& cancel) {
            // to prevent multiple calls into following ifs (m_cancel / m_pause)
            if (m_stopped) {
                cancel = true;
                return;
            }
            if (need_restart) {
                // Cancel before any write happens to avoid corrupting the file (server may send 200 full content).
                m_stopped = true;
                safe_close();
                if (!m_tmp_path.empty())
                    std::remove(m_tmp_path.string().c_str());
                m_tmp_path.clear();
                m_written       = 0;
                m_absolute_size = 0;
                m_last_percent  = 0;
                cancel          = true;
                return;
            }
            if (m_cancel) {
                m_stopped = true;
                safe_close();
                if (!m_tmp_path.empty())
                    std::remove(m_tmp_path.string().c_str());
                m_written           = 0;
                cancel              = true;
                wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_CANCELED);
                evt->SetInt(m_id);
                m_evt_handler->QueueEvent(evt);
                return;
            }
            if (m_pause) {
                m_stopped = true;
                safe_close();
                cancel = true;
                if (m_written == 0)
                    if (!m_tmp_path.empty())
                        std::remove(m_tmp_path.string().c_str());
                wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_PAUSED);
                evt->SetInt(m_id);
                m_evt_handler->QueueEvent(evt);
                return;
            }

            if (progress.dlnow != 0) {
                size_t to_write = progress.dlnow - written_this_session;
                if (to_write > DOWNLOAD_MAX_CHUNK_SIZE || progress.dlnow == progress.dltotal) {
                    try {
                        // Initialize paths and open file right before the first actual write.
                        if (!ensure_paths_and_open(file, dest_path, written_previously)) {
                            safe_close();
                            wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_ERROR);
                            evt->SetString(_L("can't create download file"));
                            evt->SetInt(m_id);
                            m_evt_handler->QueueEvent(evt);
                            cancel = true;
                            return;
                        }

                        size_t offset = written_this_session;
                        while (offset < progress.dlnow) {
                            size_t chunk_size = std::min(DOWNLOAD_MAX_CHUNK_SIZE, progress.dlnow - offset);
                            size_t written = fwrite(progress.buffer.c_str() + offset, 1, chunk_size, file);
                            if (written != chunk_size) {
                                throw std::runtime_error("failed to write file");
                            }
                            offset += chunk_size;
                        }
                        written_this_session = progress.dlnow;
                        m_written            = written_previously + written_this_session;
                    } catch (const std::exception& e) {
                        safe_close();
                        wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_ERROR);
                        evt->SetString(e.what());
                        evt->SetInt(m_id);
                        m_evt_handler->QueueEvent(evt);
                        cancel = true;
                        return;
                    }
                }
                int percent_total = m_absolute_size == 0 
                    ? 0 
                    : static_cast<int>((static_cast<uint64_t>(written_previously) + static_cast<uint64_t>(progress.dlnow)) * 100ULL 
                                       / static_cast<uint64_t>(m_absolute_size));
                
                if (percent_total != m_last_percent) {
                    m_last_percent = percent_total;
                    wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_PROGRESS);
                    evt->SetString(std::to_string(percent_total));
                    evt->SetInt(m_id);
                    m_evt_handler->QueueEvent(evt);
                }
            }
        })
        .on_error([&](std::string body, std::string error, unsigned http_status) {
            // If we cancelled to restart, suppress error event.
            if (need_restart)
                return;
            safe_close();
            wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_ERROR);
            if (!error.empty())
                evt->SetString(GUI::from_u8(error));
            else
                evt->SetString(GUI::from_u8(body));
            evt->SetInt(m_id);
            m_evt_handler->QueueEvent(evt);
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
             try {
                 // Some servers may not call on_progress; ensure file is opened before writing any remaining data.
                 if (file == nullptr && !body.empty()) {
                     if (!ensure_paths_and_open(file, dest_path, written_previously)) {
                         safe_close();
                         wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_ERROR);
                         evt->SetString(_L("can't create download file"));
                         evt->SetInt(m_id);
                         m_evt_handler->QueueEvent(evt);
                         return;
                     }
                 }
                 // Orca: thingiverse need this
                 // Some servers may not trigger on_progress for the last chunk.
                 // 'body' is the payload of THIS request, so we must use 'written_this_session' as offset.
                 if (written_this_session < body.size()) {
                     std::string part_for_write = body.substr(written_this_session);
                     size_t      written        = fwrite(part_for_write.c_str(), 1, part_for_write.size(), file);
                     if (written != part_for_write.size()) {
                         throw std::runtime_error("failed to write final data");
                     }
                     written_this_session = body.size();
                     m_written            = written_previously + written_this_session;

                     wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_PROGRESS);
                     evt->SetString(std::to_string(100));
                     evt->SetInt(m_id);
                     m_evt_handler->QueueEvent(evt);
                 }
                 safe_close();
                 
                 // Use rename_file for safer file moving (handles existing files on macOS)
                 if (dest_path.empty())
                     dest_path = m_dest_folder / m_filename;
                 std::error_code rename_err = rename_file(m_tmp_path.string(), dest_path.string());
                 if (rename_err) {
                     throw std::runtime_error("failed to rename file: " + rename_err.message());
                 }
             } catch (const std::exception& e) {
                 safe_close();
                 wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_ERROR);
                 evt->SetString("failed to write and move: " + std::string(e.what()));
                 evt->SetInt(m_id);
                 m_evt_handler->QueueEvent(evt);
                 return;
             }

            wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_COMPLETE);
            evt->SetString(dest_path.wstring());
            evt->SetInt(m_id);
            m_evt_handler->QueueEvent(evt);
        })
        .perform_sync();

        safe_close();

    } while (need_restart);
}

FileGet::FileGet(int ID, std::string url, const std::string& filename, wxEvtHandler* evt_handler, const boost::filesystem::path& dest_folder)
    : p(new priv(ID, std::move(url), filename, evt_handler, dest_folder))
{}

FileGet::FileGet(FileGet&& other) : p(std::move(other.p)) {}

FileGet::~FileGet()
{
    if (p && p->m_io_thread.joinable()) {
        p->m_cancel = true;
        p->m_io_thread.join();
    }
}

void FileGet::get()
{
    assert(p);
    if (p->m_io_thread.joinable()) {
        // This will stop transfers being done by the thread, if any.
        // Cancelling takes some time, but should complete soon enough.
        p->m_cancel = true;
        p->m_io_thread.join();
    }
    p->m_cancel    = false;
    p->m_pause     = false;
    p->m_io_thread = std::thread([this]() { p->get_perform(); });
}

void FileGet::cancel()
{
    if (p && p->m_stopped) {
        if (p->m_io_thread.joinable()) {
            p->m_cancel = true;
            p->m_io_thread.join();
            wxCommandEvent* evt = new wxCommandEvent(EVT_DWNLDR_FILE_CANCELED);
            evt->SetInt(p->m_id);
            p->m_evt_handler->QueueEvent(evt);
        }
    }

    if (p)
        p->m_cancel = true;
}

void FileGet::pause()
{
    if (p) {
        p->m_pause = true;
    }
}
void FileGet::resume()
{
    assert(p);
    if (p->m_io_thread.joinable()) {
        // This will stop transfers being done by the thread, if any.
        // Cancelling takes some time, but should complete soon enough.
        p->m_cancel = true;
        p->m_io_thread.join();
    }
    p->m_cancel    = false;
    p->m_pause     = false;
    p->m_io_thread = std::thread([this]() { p->get_perform(); });
}

int hex2i(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    
    if (isdigit(uc))
        return uc - '0';

    if (isxdigit(uc)) {
        if (isupper(uc))
            return 10 + uc - 'A';
        else
            return 10 + uc - 'a';
    }

    return -1;
}

string url_decode(const string& str, bool replace_plus)
{
    string result;

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            char c1 = str[i + 1];
            char c2 = str[i + 2];
            int  v1 = hex2i(c1);
            int  v2 = hex2i(c2);
            if (v1 != -1 && v2 != -1) {
                char decoded = (v1 << 4) | v2;
                result += decoded;
                i += 2;
            } else {
                result += str[i];
            }
        } else if (replace_plus && str[i] == '+') {
            result += ' ';

        } else {
            result += str[i];
        }
    }

    return result;
}

void split(const string& s, char delimiter, vector<string>& segments)
{
    stringstream ss(s);
    string       segment;
    while (getline(ss, segment, delimiter)) {
        segments.push_back(segment);
    }
}

void split_query(const string& query, vector<pair<string, string>>& params)
{
    stringstream ss(query);
    string       pair;
    while (getline(ss, pair, '&')) {
        size_t pos = pair.find('=');

        if (pos != string::npos) {
            string key = pair.substr(0, pos);

            string value = pair.substr(pos + 1);

            params.emplace_back(key, value);

        } else {
            params.emplace_back(pair, "");
        }
    }
}

string to_lower_copy(const string& value)
{
    string lowered;
    lowered.reserve(value.size());
    for (unsigned char c : value) {
        lowered += static_cast<char>(std::tolower(c));
    }
    return lowered;
}

string replace_directory_separators(string filename)
{
    for (char& c : filename) {
        if (c == '/' || c == '\\') {
            c = '_';
        }
    }
    return filename;
}

string filename_with_supported_extension_from_text(const string& text, const vector<string>& normalized_extensions)
{
    if (text.empty() || normalized_extensions.empty()) {
        return "";
    }

    const string lowered = to_lower_copy(text);
    auto is_filename_boundary = [](char c) {
        return c == '/' || c == '\\' || c == '?' || c == '&' || c == '#' || c == '=' || c == ':' ||
               c == '"' || c == '\'' || c == '<' || c == '>' || c == '|' || c == ';' || c == ',';
    };

    for (const string& extension : normalized_extensions) {
        size_t pos = 0;
        while ((pos = lowered.find(extension, pos)) != string::npos) {
            const size_t end_pos = pos + extension.size();
            if (end_pos < text.size() && !is_filename_boundary(text[end_pos])) {
                pos = end_pos;
                continue;
            }

            size_t start_pos = pos;
            while (start_pos > 0 && !is_filename_boundary(text[start_pos - 1])) {
                --start_pos;
            }

            string filename = text.substr(start_pos, end_pos - start_pos);
            if (filename.size() > extension.size()) {
                return replace_directory_separators(filename);
            }
            pos = end_pos;
        }
    }
    return "";
}

string trim(const string& s)
{
    size_t first = s.find_first_not_of(" \t");
    if (first == string::npos)
        return "";
    size_t last = s.find_last_not_of(" \t");
    return s.substr(first, (last - first + 1));
}

string parse_content_disposition(const string& content_disp)
{
    vector<string> parts;
    split(content_disp, ';', parts);
    string filename_value;
    string filename_star_value;

    for (const string& part : parts) {
        string trimmed = trim(part);

        if (trimmed.find("filename*=") == 0) {
            string value_part = trimmed.substr(10);
            value_part        = trim(value_part);

            size_t quote_pos = value_part.find("''");
            if (quote_pos != string::npos) {
                string encoded_filename = value_part.substr(quote_pos + 2);
                filename_star_value     = url_decode(encoded_filename, true);
            } else {
                filename_star_value = value_part;
            }
        } else if (trimmed.find("filename=") == 0) {
            string value_part = trimmed.substr(9);
            value_part        = trim(value_part);
            char quote        = '\0';

            if (!value_part.empty() && (value_part[0] == '"' || value_part[0] == '\'')) {
                quote      = value_part[0];
                value_part = value_part.substr(1);
            }

            if (quote != '\0') {
                size_t end_quote = value_part.find(quote);
                if (end_quote != string::npos) {
                    value_part = value_part.substr(0, end_quote);
                }
            } else {
                size_t space_pos = value_part.find(' ');
                if (space_pos != string::npos) {
                    value_part = value_part.substr(0, space_pos);
                }
            }
            filename_value = value_part;
        }
    }

    if (!filename_star_value.empty()) {
        return filename_star_value;
    }
    return filename_value;
}

string FileGet::filename_from_url(const string& url, const vector<string>& supported_extensions)
{
    size_t         query_start = url.find('?');
    string         path_part   = url.substr(0, query_start);
    string         query_part  = (query_start != string::npos) ? url.substr(query_start + 1) : "";
    vector<string> path_segments;
    split(path_part, '/', path_segments);
    string filename_from_path;
    for (auto it = path_segments.rbegin(); it != path_segments.rend(); ++it) {
        if (!it->empty()) {
            filename_from_path = *it;

            break;
        }
    }

    filename_from_path = url_decode(filename_from_path, false);
    vector<pair<string, string>> query_params;
    split_query(query_part, query_params);
    vector<string>       normalized_extensions;
    normalized_extensions.reserve(supported_extensions.size());
    for (const string& extension : supported_extensions) {
        if (extension.empty()) {
            continue;
        }

        string normalized = to_lower_copy(extension);
        if (normalized.front() != '.') {
            normalized.insert(normalized.begin(), '.');
        }
        normalized_extensions.emplace_back(std::move(normalized));
    }

    string filename_from_query;
    string filename_from_query_fallback;
    string content_disp_value;

    for (const auto& param : query_params) {
        string normalized_key;
        const string decoded_key = url_decode(param.first, true);
        normalized_key.reserve(decoded_key.size());
        for (unsigned char c : decoded_key) {
            if (c == '_' || c == '-') {
                continue;
            }
            normalized_key += static_cast<char>(std::tolower(c));
        }

        if (normalized_key == "filename" || normalized_key == "name" || normalized_key == "file") {
            string decoded_value = url_decode(param.second, true);
            filename_from_query = replace_directory_separators(decoded_value);
        } else if (normalized_key == "responsecontentdisposition") {
            content_disp_value = url_decode(param.second, true);
        } else if (filename_from_query_fallback.empty()) {
            const string decoded_value = url_decode(param.second, true);
            filename_from_query_fallback = filename_with_supported_extension_from_text(decoded_value, normalized_extensions);
        }
    }

    string filename_from_cd;

    if (!content_disp_value.empty()) {
        string cd_filename = parse_content_disposition(content_disp_value);

        filename_from_cd = url_decode(cd_filename, false);
    }

    if (!filename_from_query.empty()) {
        return filename_from_query;

    } else if (!filename_from_cd.empty()) {
        return filename_from_cd;
    } else if (!normalized_extensions.empty()) {
        if (!filename_from_path.empty()) {
            const string lowered_filename_from_path = to_lower_copy(filename_from_path);
            for (const string& extension : normalized_extensions) {
                if (boost::algorithm::iends_with(lowered_filename_from_path, extension)) {
                    return filename_from_path;
                }
            }
        }

        if (!filename_from_query_fallback.empty()) {
            return filename_from_query_fallback;
        }

        string filename_from_url_fallback = filename_with_supported_extension_from_text(url_decode(path_part, false), normalized_extensions);
        if (!filename_from_url_fallback.empty()) {
            return filename_from_url_fallback;
        }

        filename_from_url_fallback = filename_with_supported_extension_from_text(url_decode(url, false), normalized_extensions);
        if (!filename_from_url_fallback.empty()) {
            return filename_from_url_fallback;
        }
    }
    return filename_from_path;
}
}} // namespace Slic3r::GUI
