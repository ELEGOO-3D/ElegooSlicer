#ifndef slic3r_PrinterNetwork_hpp_
#define slic3r_PrinterNetwork_hpp_

#include <string>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>
#include <map>
#include <mutex>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"

namespace Slic3r {

class IPrinterNetwork
{
public:
    IPrinterNetwork(const PrinterNetworkInfo& printerNetworkInfo) : mPrinterNetworkInfo(printerNetworkInfo) {}
    IPrinterNetwork()                                  = delete;
    IPrinterNetwork(const IPrinterNetwork&)            = delete;
    IPrinterNetwork& operator=(const IPrinterNetwork&) = delete;
    virtual ~IPrinterNetwork()                         = default;

public:
    virtual PrinterNetworkResult<PrinterNetworkInfo>              connectToPrinter()                                        = 0;
    virtual PrinterNetworkResult<bool>                            disconnectFromPrinter()                                   = 0;
    virtual PrinterNetworkResult<bool>                            sendPrintTask(const PrinterNetworkParams& params)         = 0;
    virtual PrinterNetworkResult<bool>                            sendPrintFile(const PrinterNetworkParams& params)         = 0;
    virtual PrinterNetworkResult<std::vector<PrinterNetworkInfo>> discoverPrinters()                                        = 0;
    virtual PrinterNetworkResult<PrinterMmsGroup>                 getPrinterMmsInfo()                                       = 0;
    virtual PrinterNetworkResult<PrinterNetworkInfo>              getPrinterAttributes()                                    = 0;
    virtual PrinterNetworkResult<PrinterNetworkInfo>              getPrinterStatus()                                        = 0;
    virtual PrinterNetworkResult<std::string>                     getPrinterStatusRaw()                                     = 0;
    virtual PrinterNetworkResult<PrinterPrintFileResponse>        getFileList(int pageNumber, int pageSize)                 = 0;
    virtual PrinterNetworkResult<PrinterPrintTaskResponse>        getPrintTaskList(int pageNumber, int pageSize)            = 0;
    virtual PrinterNetworkResult<bool>                            deletePrintTasks(const std::vector<std::string>& taskIds) = 0;
    virtual PrinterNetworkResult<bool>                            sendRtmMessage(const std::string& message)                = 0;
    virtual PrinterNetworkResult<PrinterPrintFileResponse>        getFileDetail(const std::string& fileName)                = 0;
    virtual PrinterNetworkResult<bool>                            updatePrinterName(const std::string& printerName)         = 0;
    virtual PrinterNetworkResult<bool>                            cancelBindPrinter(const std::string& serialNumber)        = 0;

    const PrinterNetworkInfo& getPrinterNetworkInfo() const { return mPrinterNetworkInfo; }

protected:
    PrinterNetworkInfo mPrinterNetworkInfo;
    // if 1 to 1, implement http websocket etc. network management here
    // if ElegooLink, just redirect the interface
};

class IUserNetwork
{
public:
    IUserNetwork(const UserNetworkInfo& userNetworkInfo) : mUserNetworkInfo(userNetworkInfo) {}
    IUserNetwork()                               = delete;
    IUserNetwork(const IUserNetwork&)            = delete;
    IUserNetwork& operator=(const IUserNetwork&) = delete;
    virtual ~IUserNetwork()                      = default;

    virtual PrinterNetworkResult<bool>                            logout()                                                        = 0;
    virtual PrinterNetworkResult<UserNetworkInfo>                 connectToIot(const UserNetworkInfo& userInfo)                   = 0;
    virtual PrinterNetworkResult<UserNetworkInfo>                 getRtcToken()                                                   = 0;
    virtual PrinterNetworkResult<std::vector<PrinterNetworkInfo>> getUserBoundPrinters()                                          = 0;
    virtual PrinterNetworkResult<UserNetworkInfo>                 refreshToken(const UserNetworkInfo& userInfo)                   = 0;
    virtual PrinterNetworkResult<bool>                            setRegion(const std::string& region, const std::string& iotUrl) = 0;

    // WAN
    virtual PrinterNetworkResult<PrinterNetworkInfo> bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo) = 0;
    virtual PrinterNetworkResult<bool>               unbindWANPrinter(const std::string& serialNumber)            = 0;

    UserNetworkInfo getUserNetworkInfo() const
    {
        std::lock_guard<std::mutex> lock(mUserNetworkInfoMutex);
        return mUserNetworkInfo;
    }

    void updateUserNetworkInfo(const UserNetworkInfo& userNetworkInfo)
    {
        std::lock_guard<std::mutex> lock(mUserNetworkInfoMutex);
        mUserNetworkInfo = userNetworkInfo;
    }

protected:
    UserNetworkInfo    mUserNetworkInfo;
    mutable std::mutex mUserNetworkInfoMutex;
};

class IPluginNetwork
{
public:
    IPluginNetwork(const PluginNetworkInfo& pluginNetworkInfo) : mPluginNetworkInfo(pluginNetworkInfo) {}
    IPluginNetwork()                                 = delete;
    IPluginNetwork(const IPluginNetwork&)            = delete;
    IPluginNetwork& operator=(const IPluginNetwork&) = delete;
    virtual ~IPluginNetwork()                        = default;

    virtual PrinterNetworkResult<PluginNetworkInfo>              hasInstalledPlugin()                         = 0;
    virtual PrinterNetworkResult<bool>                           installPlugin(const std::string& pluginPath) = 0;
    virtual PrinterNetworkResult<bool>                           uninstallPlugin()                            = 0;
    virtual PrinterNetworkResult<PluginNetworkInfo>              getPluginLastestVersion()                    = 0;
    virtual PrinterNetworkResult<std::vector<PluginNetworkInfo>> getPluginOldVersions()                       = 0;

    const PluginNetworkInfo& getPluginNetworkInfo() const { return mPluginNetworkInfo; }

protected:
    PluginNetworkInfo mPluginNetworkInfo;
};

class INetworkHelper
{
public:
    INetworkHelper(PrintHostType hostType) : mHostType(hostType) {}
    INetworkHelper()                                 = delete;
    INetworkHelper(const INetworkHelper&)            = delete;
    INetworkHelper& operator=(const INetworkHelper&) = delete;
    virtual ~INetworkHelper()                        = default;
    virtual std::string getLanguage();
    virtual std::string getRegion();

    virtual std::string getOnlineModelsUrl()  = 0;
    virtual std::string getLoginUrl()         = 0;
    virtual std::string getProfileUpdateUrl() = 0;
    virtual std::string getAppUpdateUrl()     = 0;
    virtual std::string getPluginUpdateUrl()  = 0;
    virtual std::string getUserAgent()        = 0;
    virtual std::string getIotUrl()           = 0;

    PrintHostType getHostType() const { return mHostType; }

    static void setTestEnvJson(const nlohmann::json& testObj) { testEnvJson = testObj; }

protected:
    PrintHostType         mHostType;
    static nlohmann::json testEnvJson;
};

class NetworkInitializer
{
public:
    static void init(const std::string& logLevel = "info");
    static void uninit();
};

class NetworkFactory
{
public:
    static std::shared_ptr<IPrinterNetwork> createPrinterNetwork(const PrinterNetworkInfo& printerNetworkInfo);
    static std::shared_ptr<IUserNetwork>    createUserNetwork(const UserNetworkInfo& userNetworkInfo);
    static std::shared_ptr<IPluginNetwork>  createPluginNetwork(const PluginNetworkInfo& pluginNetworkInfo);
    static std::shared_ptr<INetworkHelper>  createNetworkHelper(PrintHostType hostType);
};

} // namespace Slic3r

#endif
