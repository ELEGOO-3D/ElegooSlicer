#include "PrinterPluginManager.hpp"
#include "PrintHost.hpp"
#include "libslic3r/Utils.hpp"
#include "PrinterNetwork.hpp"
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

namespace Slic3r {

PrinterPluginManager::PrinterPluginManager() {}

PrinterPluginManager::~PrinterPluginManager() { 

}

bool PrinterPluginManager::init() {
    std::lock_guard<std::mutex> lock(mInitMutex);
    if (mInitialized) {
        BOOST_LOG_TRIVIAL(info) << "PrinterPluginManager::init: already initialized";
        return true;
    }
    
    loadPluginList();
    
    std::vector<std::string> supportedPluginList = getSupportedPluginTypeList();
    for(const std::string& hostType : supportedPluginList) {
        if(mPluginList.find(hostType) == mPluginList.end()) {
            continue;
        }
        PluginNetworkInfo plugin = getPlugin(hostType);
        plugin.installed = installPlugin(plugin).isSuccess();
        updatePlugin(plugin);
    }


    mInitialized = true;
    BOOST_LOG_TRIVIAL(info) << "PrinterPluginManager::init: complete, plugin types=" << mPluginList.size();
    return true;
}

bool PrinterPluginManager::uninit() {
    std::lock_guard<std::mutex> lock(mInitMutex);
    if (!mInitialized) {
        BOOST_LOG_TRIVIAL(info) << "PrinterPluginManager::uninit: not initialized, skip";
        return true;
    }  
    
    for(const auto& plugin : mPluginList) {
        uninstallPlugin(plugin.second);
    }

    mPluginList.clear();
    mInitialized = false;
    BOOST_LOG_TRIVIAL(info) << "PrinterPluginManager::uninit: complete";
    return true;
}

PrinterNetworkResult<PluginNetworkInfo> PrinterPluginManager::hasInstalledPlugin(const std::string& hostType) {

    PluginNetworkInfo plugin = getPlugin(hostType);
    std::shared_ptr<IPluginNetwork> network = NetworkFactory::createPluginNetwork(plugin);
    if(network) {
        PrinterNetworkResult<PluginNetworkInfo> result = network->hasInstalledPlugin();
        if(result.isSuccess()) {
            return PrinterNetworkResult<PluginNetworkInfo>(PrinterNetworkErrorCode::SUCCESS, plugin);
        }
    }
    return PrinterNetworkResult<PluginNetworkInfo>(PrinterNetworkErrorCode::HOST_TYPE_NOT_SUPPORTED, plugin);
}

PrinterNetworkResult<bool> PrinterPluginManager::installPlugin(const PluginNetworkInfo& plugin) {  
    std::shared_ptr<IPluginNetwork> network = NetworkFactory::createPluginNetwork(plugin);
    if(network) {
        return network->installPlugin(plugin.pluginPath);
    }
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::HOST_TYPE_NOT_SUPPORTED, false);
}

PrinterNetworkResult<bool> PrinterPluginManager::uninstallPlugin(const PluginNetworkInfo& plugin) {
    std::shared_ptr<IPluginNetwork> network = NetworkFactory::createPluginNetwork(plugin);
    if(network) {
        return network->uninstallPlugin();
    }
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}

std::vector<std::string> PrinterPluginManager::getSupportedPluginTypeList() {
    std::vector<std::string> pluginList;
    // for (const auto& hostType : {htElegooLink}) {    
    //     pluginList.push_back(PrintHost::get_print_host_type_str(hostType));
    // }
    return pluginList;
}

void PrinterPluginManager::loadPluginList() {
    std::lock_guard<std::mutex> lock(mPluginsMutex);
    mPluginList.clear();
    try {
        namespace fs = boost::filesystem;
        fs::path pluginsDir = fs::path(Slic3r::resources_dir()) / "plugins";
        
        if (!fs::exists(pluginsDir) || !fs::is_directory(pluginsDir)) {
            return;
        }

        for (fs::directory_iterator it(pluginsDir); it != fs::directory_iterator(); ++it) {
            if (!fs::is_directory(it->path())) {
                continue;
            }

            std::string hostType = it->path().filename().string();
            PluginNetworkInfo info;
            info.pluginPath    = it->path().string();
            info.hostType      = hostType;
            info.version       = "";
            info.latestVersion = "";
            info.installed     = false;

            mPluginList[hostType] = info;

        }

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": loaded %zu plugins") % mPluginList.size();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to load plugins: %s") % e.what();
    }
}

void PrinterPluginManager::addPlugin(const PluginNetworkInfo& info) {
    std::lock_guard<std::mutex> lock(mPluginsMutex);
    mPluginList[info.hostType] = info;
}

void PrinterPluginManager::updatePlugin(const PluginNetworkInfo& info) {
    std::lock_guard<std::mutex> lock(mPluginsMutex);
    mPluginList[info.hostType] = info;
}

void PrinterPluginManager::deletePlugin(const std::string& hostType) {
    std::lock_guard<std::mutex> lock(mPluginsMutex);
    mPluginList.erase(hostType);
}

PluginNetworkInfo PrinterPluginManager::getPlugin(const std::string& hostType) {
    std::lock_guard<std::mutex> lock(mPluginsMutex);
    return mPluginList[hostType];
}
} // namespace Slic3r
