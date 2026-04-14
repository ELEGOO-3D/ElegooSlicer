#include "ElegooNetworkHelper.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"

namespace Slic3r {

namespace {

std::string getTestEnvUrl(const nlohmann::json& testEnvJson, const char* key, const std::string& fallback)
{
    if (!testEnvJson.is_object()) {
        return fallback;
    }
    auto it = testEnvJson.find(key);
    if (it == testEnvJson.end() || !it->is_string()) {
        return fallback;
    }
    const std::string value = it->get<std::string>();
    return value.empty() ? fallback : value;
}

std::string buildUrl(const std::string& base, const std::string& language, const std::string& region)
{
    std::string parameters;
    if(!language.empty()) {
        parameters += "language=" + language;
    }
    if(!region.empty()) {
        if(!parameters.empty()) {
            parameters += "&";
        }
        parameters += "region=" + region;
    }
    std::string theme = wxGetApp().dark_mode() ? "dark" : "light";
    if(!theme.empty()) {
        if(!parameters.empty()) {
            parameters += "&";
        }
        parameters += "theme=" + theme;
    }

    return base + (parameters.empty() ? "" : "?" + parameters);
}

} // namespace

std::string ElegooNetworkHelper::getOnlineModelsUrl()
{
    std::string    region          = getRegion();
    std::string    language        = getLanguage();
    nlohmann::json testEnvJson     = INetworkHelper::testEnvJson;
    const bool      isChina        = region == "CN";
    std::string     onlineModelsUrl =
        getTestEnvUrl(testEnvJson,
                      isChina ? "elegoo_china_online_model_url" : "elegoo_global_online_model_url",
                      isChina ? ELEGOO_CHINA_ONLINE_MODEL_URL : ELEGOO_GLOBAL_ONLINE_MODEL_URL);
    return buildUrl(onlineModelsUrl, language, region);
}

std::string ElegooNetworkHelper::getLoginUrl() {
    std::string region = getRegion();
    std::string language = getLanguage();
    nlohmann::json testEnvJson = INetworkHelper::testEnvJson;
    const bool     isChina     = region == "CN";
    std::string    loginUrl =
        getTestEnvUrl(testEnvJson,
                      isChina ? "elegoo_china_login_url" : "elegoo_global_login_url",
                      isChina ? ELEGOO_CHINA_LOGIN_URL : ELEGOO_GLOBAL_LOGIN_URL);
    return buildUrl(loginUrl, language, region);
}

std::string ElegooNetworkHelper::getProfileUpdateUrl() {
    std::string region = getRegion();
    std::string language = getLanguage();
    nlohmann::json testEnvJson = INetworkHelper::testEnvJson;
    const bool     isChina     = region == "CN";
    std::string    profileUpdateUrl =
        getTestEnvUrl(testEnvJson,
                      isChina ? "elegoo_china_profile_update_url" : "elegoo_global_profile_update_url",
                      isChina ? ELEGOO_CHINA_PROFILE_UPDATE_URL : ELEGOO_GLOBAL_PROFILE_UPDATE_URL);
    if(isChina) {
        profileUpdateUrl += "?country=china";
    } else {
        profileUpdateUrl += "?country=other";
    }
    if(language.find("zh-CN") != std::string::npos) {
        profileUpdateUrl += "&language=zh";
    } else {
        profileUpdateUrl += "&language=en";
    }
    return profileUpdateUrl;
}

std::string ElegooNetworkHelper::getAppUpdateUrl() {
    std::string region = getRegion();
    std::string language = getLanguage();
    nlohmann::json testEnvJson = INetworkHelper::testEnvJson;
    const bool     isChina     = region == "CN";
    std::string    appUpdateUrl =
        getTestEnvUrl(testEnvJson,
                      isChina ? "elegoo_china_app_update_url" : "elegoo_global_app_update_url",
                      isChina ? ELEGOO_CHINA_UPDATE_URL : ELEGOO_GLOBAL_UPDATE_URL);

    std::string query_params = std::string("?country=") + (isChina? "china" : "other");
    query_params += std::string("&language=") + (language.find("zh") != std::string::npos ? "zh" : "en");
    
    #ifdef WIN32
    query_params += "&platform=win64";
#elif __APPLE__
#ifdef __x86_64__
    query_params += "&platform=mac64";
#elif __aarch64__
    query_params += "&platform=mac_arm64";
#endif // __x86_64__
#endif //  WIN32

    return appUpdateUrl + query_params;
}

std::string ElegooNetworkHelper::getPluginUpdateUrl() { 
    std::string region = getRegion();
    std::string language = getLanguage();
    nlohmann::json testEnvJson = INetworkHelper::testEnvJson;
    const bool     isChina     = region == "CN";
    std::string    pluginUpdateUrl =
        getTestEnvUrl(testEnvJson,
                      isChina ? "elegoo_china_plugin_update_url" : "elegoo_global_plugin_update_url",
                      isChina ? ELEGOO_CHINA_PLUGIN_UPDATE_URL : ELEGOO_GLOBAL_PLUGIN_UPDATE_URL);
    return pluginUpdateUrl;
}

std::string ElegooNetworkHelper::getIotUrl() {
    std::string region = getRegion();
    nlohmann::json testEnvJson = INetworkHelper::testEnvJson;
    const bool     isChina     = region == "CN";
    std::string    iotUrl =
        getTestEnvUrl(testEnvJson,
                      isChina ? "elegoo_china_iot_url" : "elegoo_global_iot_url",
                      isChina ? ELEGOO_CHINA_IOT_URL : ELEGOO_GLOBAL_IOT_URL);
    return iotUrl;
}

std::string ElegooNetworkHelper::getUserAgent() {
    std::string userAgent = "";
    std::string theme = wxGetApp().dark_mode() ? "dark" : "light";
    std::string version = std::string(SLIC3R_VERSION);
    
    #ifdef __WIN32__
        userAgent = "ElegooSlicer/" + version + " (" + theme + ") Mozilla/5.0 (Windows NT 10.0; Win64; x64)";
    #elif defined(__WXMAC__)
        userAgent = "ElegooSlicer/" + version + " (" + theme + ") Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)";
    #elif defined(__linux__)
        userAgent = "ElegooSlicer/" + version + " (" + theme + ") Mozilla/5.0 (X11; Linux x86_64)";
    #else
        userAgent = "ElegooSlicer/" + version + " (" + theme + ") Mozilla/5.0 (compatible; ElegooSlicer)";
    #endif 
    return userAgent;
}

} // namespace Slic3r
