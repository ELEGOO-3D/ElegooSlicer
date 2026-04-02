#ifndef slic3r_ElegooLink_hpp_
#define slic3r_ElegooLink_hpp_

#include "Singleton.hpp"
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"

namespace Slic3r {

class ElegooNetworkHelper;
class ElegooPrinterNetwork;
class ElegooUserNetwork;
class ElegooPluginNetwork;

// SDK 封装：仅允许下列网络适配类调用实例接口（getInstance() 仍继承自 Singleton，但外部无法调用 private 成员函数）。
class ElegooLink : public Singleton<ElegooLink>
{
    friend class Singleton<ElegooLink>;
    friend class ElegooNetworkHelper;
    friend class ElegooPrinterNetwork;
    friend class ElegooUserNetwork;
    friend class ElegooPluginNetwork;

    ElegooLink();
    ~ElegooLink();

private:
    void init(const std::string& region, std::string& iotUrl, const std::string& logLevel = "info");
    void uninit();

    PrinterNetworkResult<PrinterNetworkInfo>              connectToPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool>                            disconnectFromPrinter(const std::string& printerId);
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> discoverPrinters();
    PrinterNetworkResult<bool>                            sendPrintTask(const PrinterNetworkParams& params);
    PrinterNetworkResult<bool>                            sendPrintFile(const PrinterNetworkParams& params);
    PrinterNetworkResult<PrinterMmsGroup>                 getPrinterMmsInfo(const std::string& printerId);
    PrinterNetworkResult<PrinterNetworkInfo>              getPrinterAttributes(const std::string& printerId);
    PrinterNetworkResult<PrinterNetworkInfo>              getPrinterStatus(const std::string& printerId);
    PrinterNetworkResult<std::string>                     getPrinterStatusRaw(const std::string& printerId);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<PrinterPrintTaskResponse> getPrintTaskList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<PrinterExceptionResponse> getExceptionList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<bool> deletePrintTasks(const std::string& printerId, const std::vector<std::string>& taskIds);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileDetail(const std::string& printerId, const std::string& fileName);
    PrinterNetworkResult<bool> updatePrinterName(const std::string& printerId, const std::string& printerName);
    PrinterNetworkResult<bool> cancelBindPrinter(const std::string& serialNumber);

    PrinterNetworkResult<PluginNetworkInfo> hasInstalledPlugin();
    PrinterNetworkResult<bool> installPlugin(const std::string& pluginPath);
    PrinterNetworkResult<bool> uninstallPlugin();
    PrinterNetworkResult<bool> logout(const UserNetworkInfo& userInfo);
    PrinterNetworkResult<UserNetworkInfo> connectToIot(const UserNetworkInfo& userInfo);
    PrinterNetworkResult<UserNetworkInfo> getRtcToken();
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> getUserBoundPrinters();
    PrinterNetworkResult<UserNetworkInfo> refreshToken(const UserNetworkInfo& userInfo);
    PrinterNetworkResult<bool> sendRtmMessage(const std::string& printerId, const std::string& message);
    PrinterNetworkResult<std::vector<LicenseExpiredDevice>> getLicenseExpiredDevices();
    PrinterNetworkResult<bool> renewLicense(const std::string& serialNumber);
    PrinterNetworkResult<bool> refreshPrinterStatus(const std::string& printerId);
    PrinterNetworkResult<PrinterNetworkInfo> bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool> unbindWANPrinter(const std::string& serialNumber);
    PrinterNetworkResult<bool> setRegion(const std::string& region, const std::string& iotUrl);

    bool isBusy(const std::string& printerId, PrinterStatus& status, int tryCount = 10);
    void doUninstallPlugin();

    std::mutex mMutex;
    std::atomic<bool> mIsInitialized{false};
};

} // namespace Slic3r

#endif // slic3r_ElegooLink_hpp_
