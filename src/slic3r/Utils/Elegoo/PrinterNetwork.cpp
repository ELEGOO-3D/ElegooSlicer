#include "PrinterNetwork.hpp"
#include "ElegooPrinterNetwork.hpp"
#include "ElegooUserNetwork.hpp"
#include "ElegooPluginNetwork.hpp"
#include "ElegooNetworkHelper.hpp"

#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <fstream>
#include <boost/filesystem.hpp>
#include "PrintHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/Utils.hpp"
#include <mutex>
#include <boost/nowide/fstream.hpp>


namespace Slic3r {

nlohmann::json INetworkHelper::testEnvJson;

std::shared_ptr<IPrinterNetwork> NetworkFactory::createPrinterNetwork(const PrinterNetworkInfo& printerNetworkInfo)
{
    switch (PrintHost::get_print_host_type(printerNetworkInfo.hostType)) {
    case PrintHostType::htElegooLink: {
        return std::make_shared<ElegooPrinterNetwork>(printerNetworkInfo);
    }
    default: {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": unsupported network type: %s") % printerNetworkInfo.hostType;
        return nullptr;
    }
    }
    return nullptr;
}

std::shared_ptr<IUserNetwork> NetworkFactory::createUserNetwork(const UserNetworkInfo& userNetworkInfo)
{
    switch (PrintHost::get_print_host_type(userNetworkInfo.hostType)) {
    case PrintHostType::htElegooLink: {
        return std::make_shared<ElegooUserNetwork>(userNetworkInfo);
    }
    default: {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": unsupported network type: %s") % userNetworkInfo.hostType;
        return nullptr;
    }
    }
    return nullptr;
}

std::shared_ptr<IPluginNetwork> NetworkFactory::createPluginNetwork(const PluginNetworkInfo& pluginNetworkInfo)
{
    switch (PrintHost::get_print_host_type(pluginNetworkInfo.hostType)) {
    case PrintHostType::htElegooLink: {
        return std::make_shared<ElegooPluginNetwork>(pluginNetworkInfo);
    }
    default: {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": unsupported network type: %s") % pluginNetworkInfo.hostType;
        return nullptr;
    }
    }
    return nullptr;
}

std::shared_ptr<INetworkHelper> NetworkFactory::createNetworkHelper(PrintHostType hostType)
{
    static std::once_flag sOnceFlag;
    std::call_once(sOnceFlag, []() {
        boost::filesystem::path testEnvJsonPath = boost::filesystem::path(Slic3r::data_dir()) / "user" / "test_env.json";
        if (boost::filesystem::exists(testEnvJsonPath)) {
            try {
                boost::nowide::ifstream ifs(testEnvJsonPath.string());
                if (ifs.is_open()) {
                    nlohmann::json jsonData;
                    ifs >> jsonData;
                    INetworkHelper::setTestEnvJson(jsonData);
                }
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to load test env json from JSON: %s") % e.what();
            }
        }
    });

    switch (hostType) {
    case PrintHostType::htElegooLink: {
        return std::make_shared<ElegooNetworkHelper>();
    }
    default: {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": unsupported network type: %s")
                                          % PrintHost::get_print_host_type_str(hostType);
        return nullptr;
    }
    }
    return nullptr;
}


std::string INetworkHelper::getLanguage() {
    std::string language = wxGetApp().app_config->get_language_code();
    language = boost::to_upper_copy(language);
    if (language == "ZH-CN") {
        language = "zh-CN";
    } else if (language == "ZH-TW") {
        language = "zh-TW";
    } else if (language == "EN") {
        language = "en";
    } else if (language == "ES") {
        language = "es";
    } else if (language == "FR") {
        language = "fr";
    } else if (language == "DE") {
        language = "de";
    } else if (language == "JA") {
        language = "ja";
    } else if (language == "KO") {
        language = "ko";
    } else if (language == "RU") {
        language = "ru";
    } else if (language == "PT") {
        language = "pt";
    } else if (language == "IT") {
        language = "it";
    } else if (language == "NL") {
        language = "nl";
    } else if (language == "TR") {
        language = "tr";
    } else if (language == "CS") {
        language = "cs";
    } else {
        language = "en";
    }

   return language;
}

std::string INetworkHelper::getRegion() {
    std::string region = wxGetApp().app_config->get_region();
    region = boost::to_upper_copy(region);
    if (region == "CHN" || region == "CHINA") {
        region = "CN";
    } else if (region == "USA" || region == "NORTH AMERICA") {
        region = "US";
    } else if (region == "EUROPE") {
        region = "GB";
    } else if (region == "ASIA-PACIFIC") {
        region = "JP";
    } else {
        region = "other";
    }

    return region;
}

void NetworkInitializer::init(const std::string& logLevel) {
    std::shared_ptr<INetworkHelper> elegooNetworkHelper = NetworkFactory::createNetworkHelper(htElegooLink);
    std::string region = elegooNetworkHelper->getRegion();
    std::string iotUrl = elegooNetworkHelper->getIotUrl();
    ElegooPrinterNetwork::init(region, iotUrl, logLevel);    
}

void NetworkInitializer::uninit() {
    ElegooPrinterNetwork::uninit();
}

} // namespace Slic3r