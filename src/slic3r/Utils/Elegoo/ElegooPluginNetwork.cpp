#include "ElegooPluginNetwork.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"


namespace Slic3r {

ElegooPluginNetwork::ElegooPluginNetwork(const PluginNetworkInfo& pluginNetworkInfo) : IPluginNetwork(pluginNetworkInfo) {}

ElegooPluginNetwork::~ElegooPluginNetwork(){


}

PrinterNetworkResult<PluginNetworkInfo> ElegooPluginNetwork::hasInstalledPlugin()
{
    return PrinterNetworkResult<PluginNetworkInfo>(PrinterNetworkErrorCode::SUCCESS, PluginNetworkInfo());
}

PrinterNetworkResult<bool> ElegooPluginNetwork::installPlugin(const std::string& pluginPath)
{
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}   

PrinterNetworkResult<bool> ElegooPluginNetwork::uninstallPlugin()
{
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}

PrinterNetworkResult<PluginNetworkInfo> ElegooPluginNetwork::getPluginLastestVersion()
{
    return PrinterNetworkResult<PluginNetworkInfo>(PrinterNetworkErrorCode::SUCCESS, PluginNetworkInfo());
}

PrinterNetworkResult<std::vector<PluginNetworkInfo>> ElegooPluginNetwork::getPluginOldVersions()
{
    std::vector<PluginNetworkInfo> pluginNetworkInfos;
    return PrinterNetworkResult<std::vector<PluginNetworkInfo>>(PrinterNetworkErrorCode::SUCCESS, pluginNetworkInfos);
}

} // namespace Slic3r 

