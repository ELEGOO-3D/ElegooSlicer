#include "PresetUpdater.hpp"

#include <algorithm>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <functional>
#include <thread>
#include <unordered_map>
#include <ostream>
#include <utility>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/UpdateDialogs.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "slic3r/GUI/MarkdownTip.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/Utils/Elegoo/PrinterNetwork.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;


// FIXME: Incompat bundle resolution doesn't deal with inherited user presets

namespace Slic3r {


static const char *INDEX_FILENAME = "index.idx";
static const char *TMP_EXTENSION = ".data";
static const char *PRESET_SUBPATH = "profiles";
 static const char *OTA_PROFILES_PREFIX = "profiles_ota_";


void copy_file_fix(const fs::path &source, const fs::path &target)
{
	BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);
	std::string error_message;
	//CopyFileResult cfr = Slic3r::GUI::copy_file_gui(source.string(), target.string(), error_message, false);
	CopyFileResult cfr = copy_file(source.string(), target.string(), error_message, false);
	if (cfr != CopyFileResult::SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
		throw Slic3r::CriticalException(GUI::format(
				_L("Copying of file %1% to %2% failed: %3%"),
				source, target, error_message));
	}
	// Permissions should be copied from the source file by copy_file(). We are not sure about the source
	// permissions, let's rewrite them with 644.
	static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
	fs::permissions(target, perms);
}

struct Update
{
	fs::path source;
	fs::path target;

	Version version;
	std::string vendor;
	//BBS: use changelog string instead of url
	std::string change_log;
	std::string descriptions;
    // ElegooSlicer: add file filter support
    std::function<bool(const std::string)> file_filter;

	bool forced_update;
	//BBS: add directory support
	bool is_directory {false};

	Update() {}
	//BBS: add directory support
	//BBS: use changelog string instead of url
	Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog, std::string description, bool forced = false, bool is_dir = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, change_log(std::move(changelog))
		, descriptions(std::move(description))
		, forced_update(forced)
		, is_directory(is_dir)
	{}

    Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog, std::string description, std::function<bool(const std::string)> file_filter,  bool forced = false, bool is_dir = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, change_log(std::move(changelog))
		, descriptions(std::move(description))
        , file_filter(file_filter)
		, forced_update(forced)
		, is_directory(is_dir)
	{}

	//BBS: add directory support
	void install() const
	{
	    if (is_directory) {
            copy_directory_recursively(source, target, file_filter);
        }
        else {
            copy_file_fix(source, target);
        }
	}

	friend std::ostream& operator<<(std::ostream& os, const Update &self)
	{
		os << "Update(" << self.source.string() << " -> " << self.target.string() << ')';
		return os;
	}
};

struct Incompat
{
	fs::path bundle;
	Version version;
	std::string vendor;
	//BBS: add directory support
	bool is_directory {false};

	Incompat(fs::path &&bundle, const Version &version, std::string vendor, bool is_dir = false)
		: bundle(std::move(bundle))
		, version(version)
		, vendor(std::move(vendor))
		, is_directory(is_dir)
	{}

	void remove() {
		// Remove the bundle file
		if (is_directory) {
			if (fs::exists(bundle))
                fs::remove_all(bundle);
		}
		else {
			if (fs::exists(bundle))
				fs::remove(bundle);
		}
	}

	friend std::ostream& operator<<(std::ostream& os , const Incompat &self) {
		os << "Incompat(" << self.bundle.string() << ')';
		return os;
	}
};

struct Updates
{
	std::vector<Incompat> incompats;
	std::vector<Update> updates;
};


wxDEFINE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);


struct PresetUpdater::priv
{
	std::vector<Index> index_db;

	bool enabled_version_check;
	bool enabled_config_update;
	std::string version_check_url;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	bool has_waiting_updates { false };
	Updates waiting_updates;
	bool has_waiting_printer_updates { false };
    Updates waiting_printer_updates;

    struct Resource
    {
        std::string              version;
        std::string              description;
        std::string              url;
        bool                     force{false};
        std::string              cache_root;
        std::vector<std::string> sub_caches;
    };

    priv();

	void set_download_prefs(AppConfig *app_config);
	bool get_file(const std::string &url, const fs::path &target_path) const;
	//BBS: refine preset update logic
    bool extract_file(const fs::path &source_path, const fs::path &dest_path = {});
	void prune_tmps() const;
	void sync_version() const;
	void parse_version_string(const std::string& body) const;
    void sync_resources(std::string http_url, std::map<std::string, Resource> &resources, bool check_patch = false,  std::string current_version="", std::string changelog_file="");
    
    bool get_valid_ota_version(const Semver& app_version, const Semver& current_version, const Semver& cached_version, int compare_count) const;
    void parse_ota_files(std::string ota_json, std::string& version, bool& force_upgrade, nlohmann::json & description) const;

    // Fetch OTA version from API, returns true if version found, false if network error or no version available
    bool fetch_ota_version_from_api(const std::string& profile_update_url, 
                                     const Semver& app_semver, 
                                     const Semver& current_preset_semver,
                                     Resource& ota_profile_info) const;
    // Find latest valid local OTA version from cached folders
    bool find_latest_valid_local_ota_version(const Semver& app_semver,
                                             const Semver& current_preset_semver,
                                             const std::string& current_language,
                                             const std::string& current_region,
                                             Resource& ota_profile_info) const;
    // Clean up old OTA cache files and folders that don't match the current valid OTA base name
    void cleanup_old_ota_cache(const fs::path& cache_path, const std::string& ota_base_name) const;
    // Check if preset files are complete for any vendor in the cache path
    bool check_preset_files_complete(const fs::path& cache_profile_path) const;
    // Check if preset files are complete for a specific vendor
    bool check_vendor_preset_files_complete(const fs::path& cache_profile_path, const std::string& vendor_name) const;
    bool check_has_valid_ota_version();
    void sync_tooltip(std::string http_url, std::string language);
    void sync_plugins(std::string http_url, std::string plugin_version);
    void sync_printer_config(std::string http_url);
    bool get_cached_plugins_version(std::string &cached_version, bool& force);

	//BBS: refine preset update logic
	bool install_bundles_rsrc(const std::vector<std::string>& bundles, bool snapshot) const;
	void check_installed_vendor_profiles() const;
    Updates get_printer_config_updates(bool update = false) const;
	Updates get_config_updates(const Semver& old_slic3r_version) const;
	bool perform_updates(Updates &&updates, bool snapshot = true) const;
	void set_waiting_updates(Updates u);
};

//BBS: change directories by design
PresetUpdater::priv::priv()
	: cache_path(fs::path(Slic3r::data_dir()) / "ota")
	, rsrc_path(fs::path(resources_dir()) / "profiles")
	, vendor_path(fs::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR)
	, cancel(false)
{
	//BBS: refine preset updater logic
	enabled_version_check = true;
	set_download_prefs(GUI::wxGetApp().app_config);
	// Install indicies from resources. Only installs those that are either missing or older than in resources.
	check_installed_vendor_profiles();
    perform_updates(get_printer_config_updates(), false);
	// Load indices from the cache directory.
	//index_db = Index::load_db();
}

// Pull relevant preferences from AppConfig
void PresetUpdater::priv::set_download_prefs(AppConfig *app_config)
{
	version_check_url = app_config->version_check_url();

	auto profile_update_url = app_config->profile_update_url();
	if (!profile_update_url.empty())
		enabled_config_update = true;
	else
		enabled_config_update = false;
}

//BBS: refine the Preset Updater logic
// Downloads a file (http get operation). Cancels if the Updater is being destroyed.
bool PresetUpdater::priv::get_file(const std::string &url, const fs::path &target_path) const
{
    bool res = false;
    fs::path tmp_path = target_path;
    tmp_path += format(".%1%%2%", get_current_pid(), TMP_EXTENSION);

    auto remove_tmp_if_exists = [&tmp_path]() {
        try {
            if (fs::exists(tmp_path))
                fs::remove(tmp_path);
        } catch (...) {
            // Keep best-effort cleanup only.
        }
    };

    BOOST_LOG_TRIVIAL(info) << format("[BBS Updater]download file `%1%`, stored to `%2%`, tmp path `%3%`",
        url,
        target_path.string(),
        tmp_path.string());

    Slic3r::Http::get(url)
        .on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned http_status) {
            (void)body;
            BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]getting: `%1%`: http status %2%, %3%",
                url,
                http_status,
                error);
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
            try {
                const fs::path parent_dir = target_path.parent_path();
                if (!parent_dir.empty() && !fs::exists(parent_dir)) {
                    fs::create_directories(parent_dir);
                }

                boost::nowide::ofstream file(tmp_path.string(), std::ios::out | std::ios::binary | std::ios::trunc);
                if (!file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]open tmp file failed: `%1%`", tmp_path.string());
                    remove_tmp_if_exists();
                    return;
                }

                file.write(body.data(), static_cast<std::streamsize>(body.size()));
                if (!file.good()) {
                    BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]write tmp file failed: `%1%`", tmp_path.string());
                    file.close();
                    remove_tmp_if_exists();
                    return;
                }
                file.close();

                // On Windows, rename may fail if target already exists, remove it first.
                if (fs::exists(target_path))
                    fs::remove(target_path);

                fs::rename(tmp_path, target_path);
                res = true;
            } catch (const std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]save file failed: `%1%`, reason: %2%", target_path.string(), e.what());
                remove_tmp_if_exists();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]save file failed: `%1%`, unknown exception", target_path.string());
                remove_tmp_if_exists();
            }
        })
        .perform_sync();

    return res;
}

//BBS: refine preset update logic
bool PresetUpdater::priv::extract_file(const fs::path &source_path, const fs::path &dest_path)
{
    bool res = true;
    std::string file_path = source_path.string();
    std::string parent_path = (!dest_path.empty() ? dest_path : source_path.parent_path()).string();
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    if (!open_zip_reader(&archive, file_path))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to open zip reader for "<<file_path;
        return false;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

    mz_zip_archive_file_stat stat;
    // we first loop the entries to read from the archive the .amf file only, in order to extract the version from it
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
            std::string dest_file = parent_path+"/"+stat.m_filename;
            if (stat.m_is_directory) {
                fs::path dest_path(dest_file);
                if (!fs::exists(dest_path))
                    fs::create_directories(dest_path);
				continue;
            }
            else if (stat.m_uncomp_size == 0) {
                BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]Unzip: invalid size for file "<<stat.m_filename;
                continue;
            }
            try
            {
                std::string dest_file_encoded = encode_path(dest_file.c_str());
                res = mz_zip_reader_extract_to_file(&archive, stat.m_file_index, dest_file_encoded.c_str(), 0);
                if (!res) {
                    BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]extract file "<<stat.m_filename<<" to dest "<<dest_file<<" failed";
                    close_zip_reader(&archive);
                    return res;
                }
                BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]successfully extract file " << stat.m_file_index << " to "<<dest_file;
            }
            catch (const std::exception& e)
            {
                // ensure the zip archive is closed and rethrow the exception
                close_zip_reader(&archive);
                BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]Archive read exception:"<<e.what();
                return false;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]Unzip: read file stat failed";
        }
    }
    close_zip_reader(&archive);

	return true;
}

// Remove leftover paritally downloaded files, if any.
void PresetUpdater::priv::prune_tmps() const
{
    for (auto &dir_entry : boost::filesystem::directory_iterator(cache_path))
		if (is_plain_file(dir_entry) && dir_entry.path().extension() == TMP_EXTENSION) {
			BOOST_LOG_TRIVIAL(debug) << "[ElegooSlicer Updater]remove old cached files: " << dir_entry.path().string();
			fs::remove(dir_entry.path());
		}
}

//BBS: refine the Preset Updater logic
// Get Slic3rPE version available online, save in AppConfig.
void PresetUpdater::priv::sync_version() const
{
	if (! enabled_version_check) { return; }

#if 0
	Http::get(version_check_url)
		.size_limit(SLIC3R_VERSION_BODY_MAX)
		.on_progress([this](Http::Progress, bool &cancel) {
			cancel = this->cancel;
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%",
				version_check_url,
				http_status,
				error);
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			boost::trim(body);
			parse_version_string(body);
		})
		.perform_sync();
#endif
}

// Parses version string obtained in sync_version() and sends events to UI thread.
// Version string must contain release version on first line. Follows non-mandatory alpha / beta releases on following lines (alpha=2.0.0-alpha1).
void PresetUpdater::priv::parse_version_string(const std::string& body) const
{
#if 0
	// release version
	std::string version;
	const auto first_nl_pos = body.find_first_of("\n\r");
	if (first_nl_pos != std::string::npos)
		version = body.substr(0, first_nl_pos);
	else
		version = body;
	boost::optional<Semver> release_version = Semver::parse(version);
	if (!release_version) {
		BOOST_LOG_TRIVIAL(error) << format("Received invalid contents from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
		return;
	}
	BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
	wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
	evt->SetString(GUI::from_u8(version));
	GUI::wxGetApp().QueueEvent(evt);

	// alpha / beta version
	std::vector<std::string> prerelease_versions;
	size_t nexn_nl_pos = first_nl_pos;
	while (nexn_nl_pos != std::string::npos && body.size() > nexn_nl_pos + 1) {
		const auto last_nl_pos = nexn_nl_pos;
		nexn_nl_pos = body.find_first_of("\n\r", last_nl_pos + 1);
		std::string line;
		if (nexn_nl_pos == std::string::npos)
			line = body.substr(last_nl_pos + 1);
		else
			line = body.substr(last_nl_pos + 1, nexn_nl_pos - last_nl_pos - 1);

		// alpha
		if (line.substr(0, 6) == "alpha=") {
			version = line.substr(6);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for alpha release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		// beta
		}
		else if (line.substr(0, 5) == "beta=") {
			version = line.substr(5);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for beta release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		}
	}
	// find recent version that is newer than last full release.
	boost::optional<Semver> recent_version;
	for (const std::string& ver_string : prerelease_versions) {
		boost::optional<Semver> ver = Semver::parse(ver_string);
		if (ver && *release_version < *ver && ((recent_version && *recent_version < *ver) || !recent_version)) {
			recent_version = ver;
			version = ver_string;
		}
	}
	if (recent_version) {
		BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE);
		evt->SetString(GUI::from_u8(version));
		GUI::wxGetApp().QueueEvent(evt);
	}
#endif
    return;
}

//BBS: refine the Preset Updater logic
// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
void PresetUpdater::priv::sync_resources(std::string http_url, std::map<std::string, Resource> &resources, bool check_patch, std::string current_version_str, std::string changelog_file)
{
    std::map<std::string, Resource>    resource_list;

    BOOST_LOG_TRIVIAL(info) << boost::format("[ElegooSlicer Updater]: sync_resources get preferred setting version for app version %1%, url: %2%, current_version_str %3%, check_patch %4%")%SLIC3R_APP_NAME%http_url%current_version_str%check_patch;

    std::string query_params = "?";
    bool        first        = true;
    for (auto resource_it : resources) {
        if (cancel) { return; }
        auto resource_name = resource_it.first;
        boost::to_lower(resource_name);
        std::string query_resource = (boost::format("%1%=%2%")
            % resource_name % resource_it.second.version).str();
        if (!first) query_params += "&";
        query_params += query_resource;
        first = false;
    }

    std::string url = http_url;
    url += query_params;
    Slic3r::Http http = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << boost::format("[ElegooSlicer Updater]: sync_resources request_url: %1%")%url;
    http.on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_complete([this, &resource_list, resources](std::string body, unsigned) {
            try {
                BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: request_resources, body=" << body;

                json        j       = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            std::string version;
                            std::string url;
                            std::string resource;
                            std::string description;
                            bool force_upgrade = false;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    resource = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(trace) << "[ElegooSlicer Updater]: get version of settings's type, " << sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "version")) {
                                    version = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "force_update")) {
                                    force_upgrade = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: get type " << resource << ", version " << version << ", url " << url<<", force_update "<<force_upgrade;

                            resource_list.emplace(resource, Resource{version, description, url, force_upgrade});
                        }
                    }
                } else {
                    BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]: get version of settings failed, body=" << body;
                }
            } catch (std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << (boost::format("[ElegooSlicer Updater]: get version of settings failed, exception=%1% body=%2%") % e.what() % body).str();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]: get version of settings failed, body=" << body;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("[ElegooSlicer Updater]: status=%1%, error=%2%, body=%3%") % status % error % body;
        })
        .perform_sync();

    for (auto & resource_it : resources) {
        if (cancel) { return; }

        auto resource = resource_it.second;
        std::string resource_name = resource_it.first;
        boost::to_lower(resource_name);
        auto        resource_update = resource_list.find(resource_name);
        if (resource_update == resource_list.end()) {
            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]Vendor " << resource_name << " can not get setting versions online";
            continue;
        }
        Semver online_version = resource_update->second.version;
        // Semver current_version = get_version_from_json(vendor_root_config.string());
        Semver current_version = current_version_str.empty()?resource.version:current_version_str;
        bool version_match = ((online_version.maj() == current_version.maj()) && (online_version.min() == current_version.min()));
        if (version_match && check_patch) {
            int online_cc_patch = online_version.patch()/100;
            int current_cc_patch = current_version.patch()/100;
            if (online_cc_patch != current_cc_patch) {
                version_match = false;
                BOOST_LOG_TRIVIAL(warning) << boost::format("[ElegooSlicer Updater]: online patch CC not match: online_cc_patch=%1%, current_cc_patch=%2%") % online_cc_patch % current_cc_patch;
            }
        }
        if (version_match && (current_version < online_version)) {
            if (cancel) { return; }

            // need to download the online files
            fs::path cache_path(resource.cache_root);
            std::string online_url      = resource_update->second.url;
            std::string cache_file_path = (fs::temp_directory_path() / (fs::unique_path().string() + TMP_EXTENSION)).string();
            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]Downloading resource: " << resource_name << ", version " << online_version.to_string();
            if (!get_file(online_url, cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]download resource " << resource_name << " failed, url: " << online_url;
                continue;
            }
            if (cancel) { return; }

            // remove previous files before
            if (resource.sub_caches.empty()) {
                if (fs::exists(cache_path)) {
                    fs::remove_all(cache_path);
                    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]remove cache path " << cache_path.string();
                }
            } else {
                for (auto sub : resource.sub_caches) {
                    if (fs::exists(cache_path / sub)) {
                        fs::remove_all(cache_path / sub);
                        BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]remove cache path " << (cache_path / sub).string();
                    }
                }
            }
            // extract the file downloaded
            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]start to unzip the downloaded file " << cache_file_path << " to "<<cache_path;
            fs::create_directories(cache_path);
            if (!extract_file(cache_file_path, cache_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]extract resource " << resource_it.first << " failed, path: " << cache_file_path;
                continue;
            }
            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]finished unzip the downloaded file " << cache_file_path;

            // save the description to disk
            if (changelog_file.empty())
                changelog_file = (cache_path / "changelog.json").string();
            else
                changelog_file = (cache_path / changelog_file).string();

            try {
                json j;
                //record the headers
                j["version"] = resource_update->second.version;
                j["description"] = resource_update->second.description;
                j["force"] = resource_update->second.force;

                boost::nowide::ofstream c;
                c.open(changelog_file, std::ios::out | std::ios::trunc);
                c << std::setw(4) << j << std::endl;
                c.close();
            }
            catch(std::exception &err) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": save to "<<changelog_file<<" got a generic exception, reason = " << err.what();
            }

            resource_it.second = resource_update->second;
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("[ElegooSlicer Updater]: online version=%1%, current_version=%2%, no need to download") % online_version.to_string() % current_version.to_string();
        }
    }
}
bool PresetUpdater::priv::get_valid_ota_version(const Semver& app_version, const Semver& current_version, const Semver& cached_version, int compare_count) const
{
    bool valid           = false;
    int  app_patch_cc    = app_version.patch() / 100;
    int  cached_patch_cc = cached_version.patch() / 100;
    int  curent_patch_cc = current_version.patch() / 100;

    int app_patch_dd    = app_version.patch() % 100;
    int cached_patch_dd = cached_version.patch() % 100;
    int curent_patch_dd = current_version.patch() % 100;

    if (compare_count <= 1) {
        if ((cached_version.maj() == app_version.maj()) &&
            ((cached_version.min() > current_version.min()) ||
             ((cached_version.min() == current_version.min()) && (cached_patch_cc > curent_patch_cc)) ||
             ((cached_version.min() == current_version.min()) && (cached_patch_cc == curent_patch_cc) &&
              (cached_patch_dd > curent_patch_dd))))
            valid = true;
    } else if (compare_count == 2) {
        if ((cached_version.maj() == app_version.maj()) && (cached_version.min() == app_version.min()) &&
            ((cached_patch_cc > curent_patch_cc) || ((cached_patch_cc == curent_patch_cc) && (cached_patch_dd > curent_patch_dd))))
            valid = true;
    } else if (compare_count == 3) {
        if ((cached_version.maj() == app_version.maj()) && (cached_version.min() == app_version.min()) &&
            (cached_patch_cc == app_patch_cc) && (cached_patch_dd > curent_patch_dd))
            valid = true;
    }
    return valid;
}

void PresetUpdater::priv::parse_ota_files(std::string ota_json, std::string& version, bool& force_upgrade, nlohmann::json & description) const
{
    version.clear();
    if (fs::exists(ota_json)) {
        try {
            boost::nowide::ifstream ifs(ota_json);
            json                    j;
            ifs >> j;

            if (j.contains("version"))
                version = j["version"];
            if (j.contains("force"))
                force_upgrade = j["force"];
            if (j.contains("description"))
                description = j["description"];

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                    << boost::format(": ota_json %1%, version %2%, force %3%, description %4%") % ota_json % version %
                                           force_upgrade % description;
        } catch (nlohmann::detail::parse_error& err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << ota_json
                                     << " got a nlohmann::detail::parse_error, reason = " << err.what();
        }
    }
    return;
}

// Fetch OTA version from API
// Returns true if a compatible version is found, false if network error or no version available
bool PresetUpdater::priv::fetch_ota_version_from_api(const std::string& profile_update_url, 
                                                       const Semver& app_semver, 
                                                       const Semver& current_preset_semver,
                                                       Resource& ota_profile_info) const
{
    ota_profile_info.version.clear();
    Http::get(profile_update_url)
        .on_error([](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]: network error when checking OTA version: HTTP " << http_status 
                                       << ", error: " << error;
        })
        .timeout_connect(5)
        .on_complete([this, &ota_profile_info, &app_semver, &current_preset_semver](std::string body, unsigned http_status) {
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]: HTTP error when checking OTA version: HTTP " << http_status;
                return;
            }
            
            try {
                json j = json::parse(body);
                if (!j.contains("code") || j["code"].get<int>() != 200 || 
                    !j.contains("message") || j["message"].get<std::string>() != "success" ||
                    !j.contains("data") || !j["data"].is_object()) {
                    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: API response indicates no OTA version available (code or message mismatch)";
                    return;
                }
                
                auto data = j["data"];
                if (!data.contains("items") || !data["items"].is_array()) {
                    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: API response does not contain items array, no OTA version available";
                    return;
                }
                
                // Multiple versions in items array
                std::vector<Resource> valid_versions;
                for (const auto& item : data["items"]) {
                    if (!item.contains("version") || !item.contains("downloadUrl"))
                        continue;
                    
                    std::string version_str = item["version"].get<std::string>();
                    Semver item_semver(version_str);
                    
                    if (!get_valid_ota_version(app_semver, current_preset_semver, item_semver, 2))
                        continue;
                    
                    Resource res;
                    res.version = version_str;
                    res.url = item["downloadUrl"].get<std::string>();
                    if (item.contains("info"))
                        res.description = item["info"].get<std::string>();
                    res.force = false;
                    
                    valid_versions.push_back(res);
                }
                
                if (valid_versions.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: no compatible OTA version found in API response";
                    return;
                }
                
                // Find the latest version by comparing version numbers
                std::sort(valid_versions.begin(), valid_versions.end(), 
                    [](const Resource& a, const Resource& b) {
                        Semver ver_a(a.version);
                        Semver ver_b(b.version);
                        return ver_b < ver_a; // Higher version first
                    });
                
                ota_profile_info = valid_versions[0];
                BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: found " << valid_versions.size() 
                                        << " compatible versions, selected latest: version=" << ota_profile_info.version
                                        << ", url=" << ota_profile_info.url;
            } catch (std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]: failed to parse API response, exception=" << e.what() << ", body=" << body;               
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]: failed to parse API response, body=" << body;               
            }
        })
        .perform_sync();
    
    // Return true only if a valid version was found
    return !ota_profile_info.version.empty() && !ota_profile_info.url.empty();
}

// Helper function to replace underscores with hyphens in language/region codes
// This prevents parsing issues when language/region codes contain underscores
static std::string sanitize_for_ota_name(const std::string& str) {
    std::string result = str;
    std::replace(result.begin(), result.end(), '_', '-');
    return result;
}

// Find latest valid local OTA version from cached folders
// Returns true if a valid local version is found, false otherwise
bool PresetUpdater::priv::find_latest_valid_local_ota_version(const Semver& app_semver,
                                                                const Semver& current_preset_semver,
                                                                const std::string& current_language,
                                                                const std::string& current_region,
                                                                Resource& ota_profile_info) const
{
    auto cache_profile_path = cache_path / PRESET_SUBPATH;
    if (!fs::exists(cache_profile_path))
        return false;
    
    std::vector<Resource> valid_local_versions;
    
    // Iterate through all directories in cache_profile_path
    for (const auto& entry : fs::directory_iterator(cache_profile_path)) {
        if (!entry.is_directory())
            continue;
        
        std::string dirname = entry.path().filename().string();
        if (dirname.find(OTA_PROFILES_PREFIX) != 0)
            continue;
        
        // Parse folder name: profiles_ota_version_language_region
        // Split by underscore since version, language and region are sanitized (no underscores in them)
        std::string prefix = OTA_PROFILES_PREFIX;
        if (dirname.length() <= prefix.length())
            continue;
        
        std::string rest = dirname.substr(prefix.length());
        std::vector<std::string> parts;
        boost::split(parts, rest, boost::is_any_of("_"), boost::token_compress_off);
        
        // Should have exactly 3 parts: version, language, region
        if (parts.size() != 3)
            continue;
        
        std::string cached_language = parts[1];
        std::string cached_region = parts[2];

        // Check if language and region match
        if (cached_language != current_language || cached_region != current_region)
            continue;
        
        // Check changelog file inside the directory
        std::string changelog_file = (entry.path() / (dirname + ".changelog")).string();
        if (!fs::exists(changelog_file))
            continue;
        
        // Read changelog file (only version, force and description, not language/region)
        bool force = false;
        nlohmann::json description;
        std::string cached_version;
        try {
            parse_ota_files(changelog_file, cached_version, force, description);
        } catch (...) {
            BOOST_LOG_TRIVIAL(debug) << "[ElegooSlicer Updater]: failed to parse changelog: " << changelog_file;
            continue;
        }
        
        // Check if version is valid
        if (cached_version.empty())
            continue;
        
        Semver cached_semver(cached_version);
        if (!get_valid_ota_version(app_semver, current_preset_semver, cached_semver, 2))
            continue;
        
        // Found a valid local version
        Resource res;
        res.version = cached_version;
        res.url = ""; // Local version doesn't need URL
        res.description = description.dump();
        res.force = force;
        
        valid_local_versions.push_back(res);
    }
    
    if (valid_local_versions.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "[ElegooSlicer Updater]: no valid local OTA version found";
        return false;
    }
    
    // Find the latest version by comparing version numbers
    std::sort(valid_local_versions.begin(), valid_local_versions.end(),
        [](const Resource& a, const Resource& b) {
            Semver ver_a(a.version);
            Semver ver_b(b.version);
            return ver_b < ver_a; // Higher version first
        });
    
    ota_profile_info = valid_local_versions[0];
    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: found " << valid_local_versions.size()
                            << " valid local versions, selected latest: version=" << ota_profile_info.version;
    
    return true;
}

// Clean up old OTA cache files and folders that don't match the current valid OTA base name
// Only keeps the folder with ota_base_name and its corresponding changelog file
void PresetUpdater::priv::cleanup_old_ota_cache(const fs::path& cache_path, const std::string& ota_base_name) const
{
    if (!fs::exists(cache_path))
        return;

    for (const auto& entry : fs::directory_iterator(cache_path)) {
        std::string name = entry.path().filename().string();
        try {
            if (entry.is_directory()) {
                if (name != ota_base_name) { 
                    fs::remove_all(entry.path());     
                    BOOST_LOG_TRIVIAL(debug) << "[ElegooSlicer Updater]: removed old OTA cache folder: " << name;
                }
            } else if (entry.is_regular_file()) {
                if (name != ota_base_name + ".changelog") {            
                    fs::remove(entry.path());      
                    BOOST_LOG_TRIVIAL(debug) << "[ElegooSlicer Updater]: removed old OTA cache file: " << name;
                }
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]: failed to remove old OTA cache: " << name << ", error: " << e.what();
        }
    }
}

// Check if preset files are complete for a specific vendor
bool PresetUpdater::priv::check_vendor_preset_files_complete(const fs::path& cache_profile_path, const std::string& vendor_name) const
{
    auto vendor_cache_path = cache_profile_path / vendor_name;
    auto print_in_cache    = vendor_cache_path / PRESET_PRINT_NAME;
    auto filament_in_cache = vendor_cache_path / PRESET_FILAMENT_NAME;
    auto machine_in_cache  = vendor_cache_path / PRESET_PRINTER_NAME;
    
    return fs::exists(print_in_cache) && fs::exists(filament_in_cache) && fs::exists(machine_in_cache);
}

// Check if preset files are complete for any vendor in the cache path
bool PresetUpdater::priv::check_preset_files_complete(const fs::path& cache_profile_path) const
{
    if (!fs::exists(cache_profile_path))
        return false;
    
    // Iterate through vendor folders in cache_profile_path
    for (const auto& entry : fs::directory_iterator(cache_profile_path)) {
        if (!entry.is_directory())
            continue;
        
        std::string vendor_name = entry.path().filename().string();
        // If all preset files exist for this vendor, return true
        if (check_vendor_preset_files_complete(cache_profile_path, vendor_name)) {
            return true;
        }
    }
    
    return false;
}

bool PresetUpdater::priv::check_has_valid_ota_version()
{ 
    fs::path cache_profile_path = cache_path / PRESET_SUBPATH;
    std::shared_ptr<INetworkHelper> network_helper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
    if(!network_helper) {
        BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]: failed to create network helper";
        return false;
    }
    std::string profile_update_url = network_helper->getProfileUpdateUrl();
    if(profile_update_url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: profile_update_url is empty, skip check_has_valid_ota_version";
        return false;
    }
      
    Resource ota_profile_info;
    Semver app_semver(SLIC3R_VERSION);
    Semver current_preset_semver = get_version_from_json((vendor_path / "Elegoo.json").string());
    if(!current_preset_semver.valid()) {
        BOOST_LOG_TRIVIAL(error) << "[ElegooSlicer Updater]: failed to get current preset semver";
        return false;
    }  
    AppConfig* app_config = GUI::wxGetApp().app_config;
    std::string current_language = sanitize_for_ota_name(app_config->get_language_code());
    std::string current_region = sanitize_for_ota_name(app_config->get_region());
    
    fetch_ota_version_from_api(profile_update_url, app_semver, current_preset_semver, ota_profile_info);
    // If online version is empty (not a network error), try to find local cached version
    if(ota_profile_info.version.empty() || ota_profile_info.url.empty()) {
        find_latest_valid_local_ota_version(app_semver, current_preset_semver, current_language, current_region, ota_profile_info);
    }
    
    // If no valid OTA version found (neither online nor local), clean up all cache and return
    if(ota_profile_info.version.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: no compatible OTA version available (neither online nor local)";
        cleanup_old_ota_cache(cache_profile_path, "");
        return false;
    }
    
    std::string ota_base_name = OTA_PROFILES_PREFIX + sanitize_for_ota_name(ota_profile_info.version) + "_" + current_language + "_" + current_region;
    cleanup_old_ota_cache(cache_profile_path, ota_base_name);
    
    fs::path ota_profile_path = cache_profile_path / ota_base_name;
    std::string ota_changelog = (ota_profile_path / (ota_base_name + ".changelog")).string();

    // Check if cached version is valid (changelog exists, folder exists, and preset files are complete)
    if(fs::exists(ota_changelog) && fs::exists(ota_profile_path)) {
        if (check_preset_files_complete(ota_profile_path)) {
            return true;
        }
    }
    
    // Clean up all old OTA cache and download the new OTA version
    cleanup_old_ota_cache(cache_profile_path, "");

    if(ota_profile_info.url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]: ota_profile_info.url is empty, skip download";
        return false;
    }
    std::string download_file = (cache_profile_path / (ota_base_name + TMP_EXTENSION)).string();
    if (!get_file(ota_profile_info.url, download_file)) {
        return false;
    }
    
    // Ensure the target directory exists before extracting
    fs::create_directories(ota_profile_path);
    
    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]start to unzip the downloaded file " << download_file;
    if (!extract_file(download_file, ota_profile_path)) {
        BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]extract downloaded file"
                                   << " failed, path: " << download_file;
        fs::remove_all(ota_profile_path);
        return false;
    }
    
    // Save changelog file
    json j;
    j["version"]     = ota_profile_info.version;
    j["description"] = ota_profile_info.description;
    j["force"]       = ota_profile_info.force;
    boost::nowide::ofstream c;
    c.open(ota_changelog, std::ios::out | std::ios::trunc);
    c << std::setw(4) << j << std::endl;
    c.close();

    return true;
}

void PresetUpdater::priv::sync_tooltip(std::string http_url, std::string language)
{
    try {
        std::string common_version = "00.00.00.00";
        std::string language_version = "00.00.00.00";
        fs::path cache_root = fs::path(data_dir()) / "resources/tooltip";
        try {
            auto vf = cache_root / "common" / "version";
            if (fs::exists(vf)) Slic3r::load_string_file(vf, common_version);
            vf = cache_root / language / "version";
            if (fs::exists(vf)) Slic3r::load_string_file(vf, language_version);
        } catch (...) {}
        std::map<std::string, Resource> resources
        {
            {"slicer/tooltip/common", { common_version, "", "", false, (cache_root / "common").string() }},
            {"slicer/tooltip/" + language, { language_version, "", "", false, (cache_root / language).string() }}
        };
        sync_resources(http_url, resources);
        for (auto &r : resources) {
            if (!r.second.url.empty()) {
                GUI::MarkdownTip::Reload();
                break;
            }
        }
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << format("[ElegooSlicer Updater] sync_tooltip: %1%", e.what());
    }
}

// return true means there are plugins files
bool PresetUpdater::priv::get_cached_plugins_version(std::string& cached_version, bool &force)
{
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto cache_folder = data_dir_path / "ota";
    std::string network_library, player_library, live555_library;
    bool has_plugins = false;

#if defined(_MSC_VER) || defined(_WIN32)
    network_library = cache_folder.string() + "/bambu_networking.dll";
    player_library  = cache_folder.string() + "/BambuSource.dll";
    live555_library = cache_folder.string() + "/live555.dll";
#elif defined(__WXMAC__)
    network_library = cache_folder.string() + "/libbambu_networking.dylib";
    player_library  = cache_folder.string() + "/libBambuSource.dylib";
    live555_library = cache_folder.string() + "/liblive555.dylib";
#else
    network_library = cache_folder.string() + "/libbambu_networking.so";
    player_library  = cache_folder.string() + "/libBambuSource.so";
    live555_library = cache_folder.string() + "/liblive555.so";
#endif

    std::string changelog_file = cache_folder.string() + "/network_plugins.json";
    if (boost::filesystem::exists(network_library)
        && boost::filesystem::exists(player_library)
        && boost::filesystem::exists(live555_library)
        && boost::filesystem::exists(changelog_file))
    {
        has_plugins = true;
        try {
            boost::nowide::ifstream ifs(changelog_file);
            json j;
            ifs >> j;

            if (j.contains("version"))
                cached_version = j["version"];
            if (j.contains("force"))
                force = j["force"];

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": cached_version = "<<cached_version<<", force = " << force;
        }
        catch(nlohmann::detail::parse_error &err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse "<<changelog_file<<" got a nlohmann::detail::parse_error, reason = " << err.what();
            //throw ConfigurationError(format("Failed loading json file \"%1%\": %2%", file_path, err.what()));
        }
    }

    return has_plugins;
}

void PresetUpdater::priv::sync_plugins(std::string http_url, std::string plugin_version)
{
    if (plugin_version == "00.00.00.00") {
        BOOST_LOG_TRIVIAL(info) << "non need to sync plugins for there is no plugins currently.";
        return;
    }
    std::string curr_version = NetworkAgent::use_legacy_network ? BAMBU_NETWORK_AGENT_VERSION_LEGACY : BAMBU_NETWORK_AGENT_VERSION;
    std::string using_version = curr_version.substr(0, 9) + "00";

    std::string cached_version;
    bool force_upgrade = false;
    get_cached_plugins_version(cached_version, force_upgrade);
    if (!cached_version.empty()) {
        bool need_delete_cache = false;
        Semver current_semver = curr_version;
        Semver cached_semver = cached_version;

        int curent_patch_cc = current_semver.patch()/100;
        int cached_patch_cc = cached_semver.patch()/100;
        int curent_patch_dd = current_semver.patch()%100;
        int cached_patch_dd = cached_semver.patch()%100;
        if ((cached_semver.maj() != current_semver.maj())
            || (cached_semver.min() != current_semver.min())
            || (curent_patch_cc != cached_patch_cc))
        {
            need_delete_cache = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("cached plugins version %1% not match with current %2%")%cached_version%curr_version;
        }
        else if (cached_patch_dd <= curent_patch_dd) {
            need_delete_cache = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("cached plugins version %1% not newer than current %2%")%cached_version%curr_version;
        }
        else {
            BOOST_LOG_TRIVIAL(info) << boost::format("cached plugins version %1% newer than current %2%")%cached_version%curr_version;
            plugin_version = cached_version;
        }

        if (need_delete_cache) {
            std::string data_dir_str = data_dir();
            boost::filesystem::path data_dir_path(data_dir_str);
            auto cache_folder = data_dir_path / "ota";

#if defined(_MSC_VER) || defined(_WIN32)
            auto network_library = cache_folder / "bambu_networking.dll";
            auto player_library  = cache_folder / "BambuSource.dll";
            auto live555_library  = cache_folder / "live555.dll";
#elif defined(__WXMAC__)
            auto network_library = cache_folder / "libbambu_networking.dylib";
            auto player_library = cache_folder / "libBambuSource.dylib";
            auto live555_library = cache_folder / "liblive555.dylib";
#else
            auto network_library = cache_folder / "libbambu_networking.so";
            auto player_library = cache_folder / "libBambuSource.so";
            auto live555_library = cache_folder / "liblive555.so";
#endif
            auto changelog_file = cache_folder / "network_plugins.json";

            if (boost::filesystem::exists(network_library))
            {

                BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the file "<<network_library.string();
                try {
                    fs::remove(network_library);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins file " << network_library.string();
                }
            }
            if (boost::filesystem::exists(player_library))
            {

                BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the file "<<player_library.string();
                try {
                    fs::remove(player_library);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins file " << player_library.string();
                }
            }
            if (boost::filesystem::exists(live555_library))
            {

                BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the file " << live555_library.string();
                try {
                    fs::remove(live555_library);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins file " << live555_library.string();
                }
            }
            if (boost::filesystem::exists(changelog_file))
            {

                BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the file "<<changelog_file.string();
                try {
                    fs::remove(changelog_file);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins file " << changelog_file.string();
                }
            }
        }
    }

#if defined(__WINDOWS__)
    if (GUI::wxGetApp().is_running_on_arm64() && !NetworkAgent::use_legacy_network) {
        //set to arm64 for plugins
        std::map<std::string, std::string> current_headers = Slic3r::Http::get_extra_headers();
        current_headers["X-OS-Type"] = "windows_arm";

        Slic3r::Http::set_extra_headers(current_headers);
        BOOST_LOG_TRIVIAL(info) << boost::format("set X-OS-Type to windows_arm");
    }
#endif
    try {
        std::map<std::string, Resource> resources
        {
            {"slicer/plugins/cloud", { using_version, "", "", false, cache_path.string(), {"plugins"}}}
        };
        sync_resources(http_url, resources, true, plugin_version, "network_plugins.json");
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << format("[ElegooSlicer Updater] sync_plugins: %1%", e.what());
    }
#if defined(__WINDOWS__)
    if (GUI::wxGetApp().is_running_on_arm64() && !NetworkAgent::use_legacy_network) {
        //set back
        std::map<std::string, std::string> current_headers = Slic3r::Http::get_extra_headers();
        current_headers["X-OS-Type"] = "windows";

        Slic3r::Http::set_extra_headers(current_headers);
        BOOST_LOG_TRIVIAL(info) << boost::format("set X-OS-Type back to windows");
    }
#endif

    bool result = get_cached_plugins_version(cached_version, force_upgrade);
    if (result) {
        BOOST_LOG_TRIVIAL(info) << format("[ElegooSlicer Updater] found new plugins: %1%, prompt to update, force_upgrade %2%", cached_version, force_upgrade);
        if (force_upgrade) {
            auto app_config = GUI::wxGetApp().app_config;
            if (!app_config)
                GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::BBLPluginUpdateAvailable);
            else
                app_config->set("update_network_plugin", "true");
        }
        else
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::BBLPluginUpdateAvailable);
    }
}

void PresetUpdater::priv::sync_printer_config(std::string http_url)
{
    std::string curr_version  = SLIC3R_VERSION;
    std::string using_version = curr_version.substr(0, 6) + "00.00";

    std::string cached_version;
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto                    config_folder = data_dir_path / "printers";
    auto                    cache_folder = data_dir_path / "ota" / "printers";

    try {
        auto version_file = config_folder / "version.txt";
        if (fs::exists(version_file)) {
            Slic3r::load_string_file(version_file, curr_version);
            boost::algorithm::trim(curr_version);
        }
    } catch (...) {}
    try {
        auto version_file = cache_folder / "version.txt";
        if (fs::exists(version_file)) {
            Slic3r::load_string_file(version_file, cached_version);
            boost::algorithm::trim(cached_version);
        }
    } catch (...) {}
    if (!cached_version.empty()) {
        bool   need_delete_cache = false;
        Semver current_semver    = curr_version;
        Semver cached_semver     = cached_version;

        if ((cached_semver.maj() != current_semver.maj()) || (cached_semver.min() != current_semver.min())) {
            need_delete_cache = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("cached printer config version %1% not match with current %2%") % cached_version % curr_version;
        } else if (cached_semver.patch() <= current_semver.patch()) {
            need_delete_cache = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("cached printer config version %1% not newer than current %2%") % cached_version % curr_version;
        } else {
            using_version = cached_version;
        }

        if (need_delete_cache) {
            boost::system::error_code ec;
            boost::filesystem::remove_all(cache_folder, ec);
            cached_version           = curr_version;
        }
    }

    try {
        std::map<std::string, Resource> resources{{"slicer/printer/bbl", {using_version, "", "", false, cache_folder.string()}}};
        sync_resources(http_url, resources, false, cached_version, "printer.json");
    } catch (std::exception &e) {
        BOOST_LOG_TRIVIAL(warning) << format("[ElegooSlicer Updater] sync_printer_config: %1%", e.what());
    }

    bool result = false;
    try {
        auto version_file = cache_folder / "version.txt";
        if (fs::exists(version_file)) {
            Slic3r::load_string_file(version_file, cached_version);
            boost::algorithm::trim(cached_version);
            result = true;
        }
    } catch (...) {}
    if (result) {
        BOOST_LOG_TRIVIAL(info) << format("[ElegooSlicer Updater] found new printer config: %1%, prompt to update", cached_version);
        waiting_printer_updates = get_printer_config_updates(true);
        if (waiting_printer_updates.updates.size() > 0) {
            has_waiting_printer_updates = true;
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::BBLPrinterConfigUpdateAvailable);
        }
    }
}

bool PresetUpdater::priv::install_bundles_rsrc(const std::vector<std::string>& bundles, bool snapshot) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << format("Installing %1% bundles from resources ...", bundles.size());

	for (const auto &bundle : bundles) {
		auto path_in_rsrc = (this->rsrc_path / bundle).replace_extension(".json");
		auto path_in_vendors = (this->vendor_path / bundle).replace_extension(".json");
		updates.updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors), Version(), bundle, "", "");

        //BBS: add directory support
        auto print_in_rsrc = this->rsrc_path / bundle;
		auto print_in_vendors = this->vendor_path / bundle;
        fs::path print_folder(print_in_vendors);
        if (fs::exists(print_folder))
            fs::remove_all(print_folder);
        fs::create_directories(print_folder);
		updates.updates.emplace_back(std::move(print_in_rsrc), std::move(print_in_vendors), Version(), bundle, "", "",[](const std::string name){
        // return false if name is end with .stl, case insensitive
        return boost::iends_with(name, ".stl") || boost::iends_with(name, ".png") || boost::iends_with(name, ".svg") ||
               boost::iends_with(name, ".jpeg") || boost::iends_with(name, ".jpg") || boost::iends_with(name, ".3mf");
        }, false, true);
	}

	return perform_updates(std::move(updates), snapshot);
}


// Orca: copy/update the vendor profiles from resource to system folder
void PresetUpdater::priv::check_installed_vendor_profiles() const
{
    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]:Checking whether the profile from resource is newer";

    AppConfig *app_config = GUI::wxGetApp().app_config;
    const auto enabled_vendors = app_config->vendors();

    std::set<std::string> bundles;
    // Orca: always install filament library
    bundles.insert(PresetBundle::ORCA_FILAMENT_LIBRARY);
    for (auto &dir_entry : boost::filesystem::directory_iterator(rsrc_path)) {
        const auto &path = dir_entry.path();
        std::string file_path = path.string();
        if (is_json_file(file_path)) {
            const auto path_in_vendor = vendor_path / path.filename();
            std::string vendor_name = path.filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            if (bundles.find(vendor_name) != bundles.end())continue;

            const auto is_vendor_enabled = (vendor_name == PresetBundle::ORCA_DEFAULT_BUNDLE) // always update configs from resource to vendor for ORCA_DEFAULT_BUNDLE
                                           || (enabled_vendors.find(vendor_name) != enabled_vendors.end());
            if (enabled_config_update) {
                if ( fs::exists(path_in_vendor)) {
                    if (is_vendor_enabled) {
                        Semver resource_ver = get_version_from_json(file_path);
                        Semver vendor_ver = get_version_from_json(path_in_vendor.string());

                        bool version_match = ((resource_ver.maj() == vendor_ver.maj()) && (resource_ver.min() == vendor_ver.min()));

                        if (!version_match || (vendor_ver < resource_ver)) {
                            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]:found vendor "<<vendor_name<<" newer version "<<resource_ver.to_string() <<" from resource, old version "<<vendor_ver.to_string();
                            bundles.insert(vendor_name);
                        }
                    }
                    else {
                        //need to be removed because not installed
                        fs::remove(path_in_vendor);
                        const auto path_of_vendor = vendor_path / vendor_name;
                        if (fs::exists(path_of_vendor))
                            fs::remove_all(path_of_vendor);
                    }
                }
                else if (is_vendor_enabled) {
                    bundles.insert(vendor_name);
                }
            }
            else if (is_vendor_enabled) {
                bundles.insert(vendor_name);
            }
        }
    }

    if (bundles.size() > 0) {
        install_bundles_rsrc(std::vector(bundles.begin(), bundles.end()), false);
    }
}

Updates PresetUpdater::priv::get_printer_config_updates(bool update) const
{
    std::string             data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    boost::filesystem::path resc_dir_path(resources_dir());
    auto                    config_folder = data_dir_path / "printers";
    auto                    resc_folder   = (update ? cache_path : resc_dir_path) / "printers";
    std::string             curr_version;
    std::string             resc_version;
    try {
        Slic3r::load_string_file(resc_folder / "version.txt", resc_version);
        boost::algorithm::trim(resc_version);
    } catch (...) {}
    try {
        Slic3r::load_string_file(config_folder / "version.txt", curr_version);
        boost::algorithm::trim(curr_version);
    } catch (...) {}

    if (!curr_version.empty()) {
        Semver curr_ver = curr_version;
        Semver resc_ver   = resc_version;

        bool version_match = ((resc_ver.maj() == curr_ver.maj()) && (resc_ver.min() == curr_ver.min()));

        if (!version_match || (curr_ver < resc_ver)) {
            BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]:found newer version " << resc_version << " from resource, old version " << curr_version;
        } else {
            return {};
        }
    }
    Updates updates;
    Version version;
    version.config_version = resc_version;
    std::string change_log;
    if (update) {
        std::string changelog_file = (resc_folder / "printer.json").string();
        try {
            boost::nowide::ifstream ifs(changelog_file);
            json                    j;
            ifs >> j;
            version.comment = j["description"];
        } catch (...) {}
    }
    updates.updates.emplace_back(std::move(resc_folder), std::move(config_folder), version, "bbl", change_log, version.comment, false, true);
    return updates;
}

// Generates a list of bundle updates that are to be performed.
// Uses find_latest_valid_local_ota_version to find the latest valid OTA version from cached folders,
// then checks each vendor to see if they need to be updated.
// ElegooSlicer: OTA profile updates should be located in ota/profiles folder
Updates PresetUpdater::priv::get_config_updates(const Semver& old_slic3r_version) const
{
    Updates updates;
    BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]:Checking for cached configuration updates...";
    auto cache_profile_path = cache_path / PRESET_SUBPATH;
    
    if (!fs::exists(cache_profile_path))
        return updates;
    
    // Get current language and region
    AppConfig* app_config = GUI::wxGetApp().app_config;
    std::string current_language = sanitize_for_ota_name(app_config->get_language_code());
    std::string current_region = sanitize_for_ota_name(app_config->get_region());
    
    std::string app_version = SLIC3R_VERSION;
    Semver app_semver = app_version;
    
    // Use find_latest_valid_local_ota_version to find the latest valid version
    // We use Elegoo's current preset version as reference
    Semver current_preset_semver = get_version_from_json((vendor_path / "Elegoo.json").string());
    if (!current_preset_semver.valid()) {
        return updates;
    }
    
    Resource ota_profile_info;
    if (!find_latest_valid_local_ota_version(app_semver, current_preset_semver, current_language, current_region, ota_profile_info)) {
        return updates;
    }
    
    // Build folder path from version, language and region
    std::string ota_base_name = OTA_PROFILES_PREFIX + sanitize_for_ota_name(ota_profile_info.version) + "_" + current_language + "_" + current_region;
    fs::path latest_version_folder = cache_profile_path / ota_base_name;
    
    if (!fs::exists(latest_version_folder)) {
        return updates;
    }
    
    BOOST_LOG_TRIVIAL(info) << boost::format("[ElegooSlicer Updater] found latest valid OTA version: version=%1%, force_upgrade=%2%") %
                               ota_profile_info.version % ota_profile_info.force;
    
    // Find JSON files in the selected versioned folder
    std::vector<boost::filesystem::path> paths;
    for (const auto& dir_entry : boost::filesystem::directory_iterator(latest_version_folder)) {
        const auto& path = dir_entry.path();
        std::string file_path = path.string();
        if (!is_json_file(file_path))
            continue;
        paths.push_back(path);
    }
    
    std::stable_partition(paths.begin(), paths.end(), [](const boost::filesystem::path& path) {
        return path.filename().string().find("Elegoo") != std::string::npos;
    });
    
    // Parse description from Resource
    nlohmann::json description;
    try {
        if (!ota_profile_info.description.empty()) {
            description = nlohmann::json::parse(ota_profile_info.description);
        }
    } catch (...) {
        // Ignore parse errors
    }
    
    bool description_added = false;
    
    for (auto& path : paths) {
        const auto path_in_vendor = vendor_path / path.filename();
        std::string vendor_name = path.filename().string();
        
        // Remove the .json suffix
        vendor_name.erase(vendor_name.size() - 5);
        
        // Check if preset files are complete for this vendor
        if (!fs::exists(path_in_vendor) || !check_vendor_preset_files_complete(latest_version_folder, vendor_name))
            continue;
        
        Semver cached_semver = ota_profile_info.version;
        Semver current_preset_semver = get_version_from_json(path_in_vendor.string());
        
        // Create non-const copies for get_valid_ota_version
        Semver app_semver_copy = app_semver;
        Semver current_preset_semver_copy = current_preset_semver;
        Semver cached_semver_copy = cached_semver;
        
        bool valid_version = get_valid_ota_version(app_semver_copy, current_preset_semver_copy, cached_semver_copy, 2);
        
        if (!valid_version)
            continue;
        
        BOOST_LOG_TRIVIAL(info) << boost::format("[ElegooSlicer Updater] need to update vendor: %1%'s settings from version %2% to "
                                                 "newer version %3%, force_upgrade %4%") %
                                       vendor_name % current_preset_semver.to_string() % cached_semver.to_string() % ota_profile_info.force;
        
        Version version;
        version.config_version = cached_semver;
        if (!description_added) {
            updates.updates.emplace_back(std::move(path.string()), std::move(path_in_vendor.string()), std::move(version), vendor_name,
                                         description, "", ota_profile_info.force, false);
            description_added = true;
        }
        updates.updates.emplace_back(latest_version_folder / vendor_name, vendor_path / vendor_name, Version(), vendor_name, "", "",
                                     ota_profile_info.force, true);
    }
    
    return updates;
}

//BBS: switch to new BBL.json configs
bool PresetUpdater::priv::perform_updates(Updates &&updates, bool snapshot) const
{
    //std::string vendor_path;
    //std::string vendor_name;
    if (updates.incompats.size() > 0) {
        //if (snapshot) {
        //	BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
        //	if (! GUI::Config::take_config_snapshot_cancel_on_error(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_DOWNGRADE, "",
        //		_u8L("Continue and install configuration updates?")))
        //		return false;
        //}
        BOOST_LOG_TRIVIAL(info) << format("[ElegooSlicer Updater]:Deleting %1% incompatible bundles", updates.incompats.size());

        for (auto &incompat : updates.incompats) {
            BOOST_LOG_TRIVIAL(info) << '\t' << incompat;
            incompat.remove();
        }
    } else if (updates.updates.size() > 0) {
        //if (snapshot) {
        //	BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
        //	if (! GUI::Config::take_config_snapshot_cancel_on_error(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_UPGRADE, "",
        //		_u8L("Continue and install configuration updates?")))
        //		return false;
        //}

        BOOST_LOG_TRIVIAL(info) << format("[ElegooSlicer Updater]:Performing %1% updates", updates.updates.size());

        for (const auto &update : updates.updates) {
            BOOST_LOG_TRIVIAL(info) << '\t' << update;

            update.install();
            //if (!update.is_directory) {
            //    vendor_path = update.source.parent_path().string();
            //    vendor_name = update.vendor;
            //}
        }

        //if (!vendor_path.empty()) {
        //    PresetBundle bundle;
        //    // Throw when parsing invalid configuration. Only valid configuration is supposed to be provided over the air.
        //    bundle.load_vendor_configs_from_json(vendor_path, vendor_name, PresetBundle::LoadConfigBundleAttribute::LoadSystem, ForwardCompatibilitySubstitutionRule::Disable);

        //    BOOST_LOG_TRIVIAL(info) << format("Deleting %1% conflicting presets", bundle.prints.size() + bundle.filaments.size() + bundle.printers.size());

        //    auto preset_remover = [](const Preset& preset) {
        //        BOOST_LOG_TRIVIAL(info) << '\t' << preset.file;
        //        fs::remove(preset.file);
        //    };

        //    for (const auto &preset : bundle.prints)    { preset_remover(preset); }
        //    for (const auto &preset : bundle.filaments) { preset_remover(preset); }
        //    for (const auto &preset : bundle.printers)  { preset_remover(preset); }
        //}
    }

    return true;
}

void PresetUpdater::priv::set_waiting_updates(Updates u)
{
	waiting_updates = u;
	has_waiting_updates = true;
}

PresetUpdater::PresetUpdater() :
	p(new priv())
{}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->cancel = true;
		p->thread.join();
	}
}

//BBS: change directories by design
//BBS: refine the preset updater logic
void PresetUpdater::sync(std::string http_url, std::string language, std::string plugin_version, PresetBundle *preset_bundle)
{
	//p->set_download_prefs(GUI::wxGetApp().app_config);
	if (!p->enabled_version_check && !p->enabled_config_update) { return; }

	// Copy the whole vendors data for use in the background thread
	// Unfortunatelly as of C++11, it needs to be copied again
	// into the closure (but perhaps the compiler can elide this).
    VendorMap vendors = preset_bundle ? preset_bundle->vendors : VendorMap{};

    if(p->thread.joinable()) {
        BOOST_LOG_TRIVIAL(warning) << "[ElegooSlicer Updater]:Updater is already running, skip this sync request.";
        return;
    }
	p->thread = std::thread([this, vendors, http_url, language, plugin_version]() {
		this->p->prune_tmps();
		if (p->cancel)
			return;
		this->p->sync_version();
		if (p->cancel)
			return;
        if (!vendors.empty()) {
		    bool has_valid_ota_version = this->p->check_has_valid_ota_version();
		    if (p->cancel)
			    return;
            if(has_valid_ota_version)    
            {
                GUI::wxGetApp().CallAfter([] 
                {
                    GUI::wxGetApp().check_config_updates_from_updater();
                });
            }
        }
		if (p->cancel)
			return;
        // this->p->sync_plugins(http_url, plugin_version);
        this->p->sync_printer_config(http_url);
		//if (p->cancel)
		//	return;
		//remove the tooltip currently
		//this->p->sync_tooltip(http_url, language);
	});
}

void PresetUpdater::slic3r_update_notify()
{
	if (! p->enabled_version_check)
		return;
}

static bool reload_configs_update_gui()
{
	wxString header = _L("Need to check the unsaved changes before configuration updates.");
	if (!GUI::wxGetApp().check_and_save_current_preset_changes(_L("Configuration updates"), header, false ))
		return false;

	// Reload global configuration
	auto* app_config = GUI::wxGetApp().app_config;
	// System profiles should not trigger any substitutions, user profiles may trigger substitutions, but these substitutions
	// were already presented to the user on application start up. Just do substitutions now and keep quiet about it.
	// However throw on substitutions in system profiles, those shall never happen with system profiles installed over the air.
	GUI::wxGetApp().preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem);
	GUI::wxGetApp().load_current_presets();
	GUI::wxGetApp().plater()->set_bed_shape();

	return true;
}


PresetUpdater::UpdateResult PresetUpdater::config_update(const Semver& old_slic3r_version, UpdateParams params) const
{
    if (! p->enabled_config_update) { return R_NOOP; }

    auto updates = p->get_config_updates(old_slic3r_version);

    if (updates.updates.size() > 0) {

        bool force_update = false;
        for (const auto& update : updates.updates) {
            force_update = (update.forced_update ? true : force_update);
        }

        //forced update
        if (force_update)
        {
            BOOST_LOG_TRIVIAL(info) << format("[ElegooSlicer Updater]:Force updating will start, size %1% ", updates.updates.size());
            std::vector<std::string> bundles;
            for (const auto& update : updates.updates) {
                if (update.is_directory)
                    continue;
                bundles.push_back(update.vendor);
            }
            bool ret = p->perform_updates(std::move(updates));
            if (!ret) {
                BOOST_LOG_TRIVIAL(warning) << format("[ElegooSlicer Updater]:perform_updates failed");
                return R_INCOMPAT_EXIT;
            }

            ret = reload_configs_update_gui();
            if (!ret) {
                BOOST_LOG_TRIVIAL(warning) << format("[ElegooSlicer Updater]:reload_configs_update_gui failed");
                return R_INCOMPAT_EXIT;
            }
            for(auto b : bundles){
            Semver cur_ver = GUI::wxGetApp().preset_bundle->get_vendor_profile_version(b);
            GUI::wxGetApp()
                .plater()
                ->get_notification_manager()
                ->push_notification(GUI::NotificationType::PresetUpdateFinished,
                                    GUI::NotificationManager::NotificationLevel::ImportantNotificationLevel,
                                    _u8L("Configuration package: ") + b + _u8L(" updated to ") + cur_ver.to_string());
            }
            return R_UPDATE_INSTALLED;
        }

        // regular update
        if (params == UpdateParams::SHOW_NOTIFICATION) {
            p->set_waiting_updates(updates);
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::PresetUpdateAvailable);
        }
        else {
            BOOST_LOG_TRIVIAL(info) << format("[ElegooSlicer Updater]:Configuration package available. size %1%, need to confirm...", p->waiting_updates.updates.size());

            std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
            for (const auto& update : updates.updates) {
                if (update.is_directory)
                    continue;
                std::string changelog = update.change_log;
                updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
            }

            GUI::MsgUpdateConfig dlg(updates_msg, params == UpdateParams::FORCED_BEFORE_WIZARD);

            const auto res = dlg.ShowModal();
            if (res == wxID_OK) {
                BOOST_LOG_TRIVIAL(debug) << "[ElegooSlicer Updater]:selected yes to update";
                if (! p->perform_updates(std::move(updates)) ||
                    ! reload_configs_update_gui())
                    return R_ALL_CANCELED;
                return R_UPDATE_INSTALLED;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]:selected no for updating";
                if (params == UpdateParams::FORCED_BEFORE_WIZARD && res == wxID_CANCEL)
                    return R_ALL_CANCELED;
                return R_UPDATE_REJECT;
            }
        }

        // MsgUpdateConfig will show after the notificaation is clicked
    } else {
        BOOST_LOG_TRIVIAL(info) << "[ElegooSlicer Updater]:No configuration updates available.";
    }

	return R_NOOP;
}

//BBS: add json related logic
bool PresetUpdater::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	return p->install_bundles_rsrc(bundles, snapshot);
}

void PresetUpdater::on_update_notification_confirm()
{
	if (!p->has_waiting_updates)
		return;
	BOOST_LOG_TRIVIAL(info) << format("Update of %1% bundles available. Asking for confirmation ...", p->waiting_updates.updates.size());

	std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
	for (const auto& update : p->waiting_updates.updates) {
		//BBS: skip directory
		if (update.is_directory)
			continue;
		std::string changelog = update.change_log;
		updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
	}

	GUI::MsgUpdateConfig dlg(updates_msg);

	const auto res = dlg.ShowModal();
	if (res == wxID_OK) {
		BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
		if (p->perform_updates(std::move(p->waiting_updates)) &&
			reload_configs_update_gui()) {
			p->has_waiting_updates = false;
		}
	}
	else {
		BOOST_LOG_TRIVIAL(info) << "User refused the update";
	}
}

void PresetUpdater::do_printer_config_update()
{
    if (!p->has_waiting_printer_updates)
        return;
    BOOST_LOG_TRIVIAL(info) << "Update of printer configs available. Asking for confirmation ...";

    std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
    for (const auto &update : p->waiting_printer_updates.updates) {
        std::string changelog = update.change_log;
        updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
    }

    GUI::MsgUpdateConfig dlg(updates_msg);

    const auto res = dlg.ShowModal();
    if (res == wxID_OK) {
        BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
        if (p->perform_updates(std::move(p->waiting_printer_updates)))
            p->has_waiting_printer_updates = false;
    } else {
        BOOST_LOG_TRIVIAL(info) << "User refused the update";
    }
}

bool PresetUpdater::version_check_enabled() const
{
	return p->enabled_version_check;
}

}
