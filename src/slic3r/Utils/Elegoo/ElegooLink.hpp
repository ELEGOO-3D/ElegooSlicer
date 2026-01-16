#ifndef slic3r_ElegooLink_hpp_
#define slic3r_ElegooLink_hpp_

#include "Singleton.hpp"
#include <string>
#include <vector>
#include <mutex>
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"

namespace Slic3r {

class ElegooLink : public Singleton<ElegooLink>
{
    friend class Singleton<ElegooLink>;
public:
    ElegooLink();
    ~ElegooLink();
    void init(const std::string& region, std::string& iotUrl);
    void uninit();

    PrinterNetworkResult<PrinterNetworkInfo>              connectToPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool>                            disconnectFromPrinter(const std::string& printerId);
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> discoverPrinters();
    PrinterNetworkResult<bool>                            sendPrintTask(const PrinterNetworkParams& params);
    PrinterNetworkResult<bool>                            sendPrintFile(const PrinterNetworkParams& params);
    PrinterNetworkResult<PrinterMmsGroup>                 getPrinterMmsInfo(const std::string& printerId);
    PrinterNetworkResult<PrinterNetworkInfo>              getPrinterAttributes(const std::string& printerId);
    PrinterNetworkResult<PrinterNetworkInfo>              getPrinterStatus(const std::string& printerId);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<PrinterPrintTaskResponse> getPrintTaskList(const std::string& printerId, int pageNumber, int pageSize);
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
    PrinterNetworkResult<PrinterNetworkInfo> bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool> unbindWANPrinter(const std::string& serialNumber);
    PrinterNetworkResult<bool> setRegion(const std::string& region, const std::string& iotUrl);
 
private:
    bool isBusy(const std::string& printerId, PrinterStatus& status, int tryCount = 10);
    void doUninstallPlugin();

    std::mutex mMutex;
    bool mIsInitialized{false};
};

} // namespace Slic3r

#endif // slic3r_ElegooLink_hpp_