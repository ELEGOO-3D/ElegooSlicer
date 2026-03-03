#pragma once
#include <map>
#include "slic3r/Utils/Elegoo/PrinterNetwork.hpp"
#include "slic3r/Utils/Singleton.hpp"
#include "slic3r/Utils/Elegoo/PrinterNetwork.hpp"

namespace Slic3r {

class PrinterUploadManager;

// Multi-device management
// Support multiple device types, but need to adapt network communication, implement PrinterNetwork class
// Network layer supports 1-to-1, many-to-1

#define CONFIG_KEY_PATH    "printhost_path"
#define CONFIG_KEY_GROUP   "printhost_group"
#define CONFIG_KEY_STORAGE "printhost_storage"

#define CONFIG_KEY_UPLOADANDPRINT      "printsend_upload_and_print"
#define CONFIG_KEY_TIMELAPSE           "printsend_timelapse"
#define CONFIG_KEY_HEATEDBEDLEVELING   "printsend_heated_bed_leveling"
#define CONFIG_KEY_BEDTYPE             "printsend_bed_type"
#define CONFIG_KEY_AUTO_REFILL         "printsend_auto_refill"
#define CONFIG_KEY_SELECTED_PRINTER_ID "printsend_selected_printer_id"
#define CONFIG_KEY_SWITCH_TO_DEVICE_TAB "printsend_switch_to_device_tab"

#define CONFIG_KEY_MMS_FILAMENT_MAPPING "mms_filament_mapping"

class PrinterManager : public Singleton<PrinterManager>
{
    friend class Singleton<PrinterManager>;
    friend class PrinterUploadManager;
public:
    ~PrinterManager();
    PrinterManager(const PrinterManager&) = delete;
    PrinterManager& operator=(const PrinterManager&) = delete;

    // Printer management
    PrinterNetworkInfo getPrinterNetworkInfo(const std::string& printerId);
    std::vector<PrinterNetworkInfo> getPrinterList();
    PrinterNetworkInfo getSelectedPrinter(const std::string &printerModel, const std::string &printerId);
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> discoverPrinter();
    PrinterNetworkResult<bool> addPrinter(PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool> cancelBindPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool> updatePrinterName(const std::string& printerId, const std::string& name);
    PrinterNetworkResult<bool> updatePrinterHost(const std::string& printerId, const std::string& host);
    PrinterNetworkResult<bool> updatePhysicalPrinter(const std::string& printerId, const PrinterNetworkInfo& printerInfo);
    PrinterNetworkResult<bool> deletePrinter(const std::string& printerId);
    
    // Printer operations
    PrinterNetworkResult<PrinterMmsGroup> getPrinterMmsInfo(const std::string& printerId);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileDetail(const std::string& printerId, const std::string& fileName);
    PrinterNetworkResult<PrinterPrintTaskResponse> getPrintTaskList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<bool> deletePrintTasks(const std::string& printerId, const std::vector<std::string>& taskIds);
    PrinterNetworkResult<bool> sendRtmMessage(const std::string& printerId, const std::string& message);
    PrinterNetworkResult<std::vector<LicenseExpiredDevice>> getLicenseExpiredDevices();
    PrinterNetworkResult<bool> renewLicense(const std::string& serialNumber);
    PrinterNetworkResult<bool> refreshPrinterStatus(const std::string& printerId);
    PrinterNetworkResult<std::string> getPrinterStatusRaw(const std::string& printerId);

    static std::map<std::string, std::map<std::string, DynamicPrintConfig>> getVendorPrinterModelConfig();
    static std::string imageFileToBase64DataURI(const std::string& image_path);

    void init();
    void close();

private:
    PrinterManager();
    std::atomic<bool> mIsInitialized;
    std::mutex mInitializedMutex;
    class PrinterLock
    {
    public:
        PrinterLock(const std::string& printerId);
        ~PrinterLock();
        
    private:
        std::recursive_mutex* mPrinterMutex;
        static std::map<std::string, std::recursive_mutex> sPrinterMutexes;
        static std::mutex sMutex;       
    };
    
    // Printer network management
    std::mutex mAddPrinterMutex;
    std::mutex mPrinterNetworkMutex;
    std::map<std::string, std::shared_ptr<IPrinterNetwork>> mPrinterNetworkConnections;
    PrinterNetworkResult<bool> connectToPrinter(PrinterNetworkInfo& printer, bool updatePrinterName = false);
    bool deletePrinterNetwork(const std::string& printerId);
    std::shared_ptr<IPrinterNetwork> getPrinterNetwork(const std::string& printerId);
    void syncOldPresetPrinters();
    void validateAndCompletePrinterInfo(PrinterNetworkInfo& printerInfo);
    std::string generatePrinterId();

    // Monitor printer connections
    std::atomic<bool> mMonitoring;
    std::thread mConnectionThread;
    std::thread mWanPrinterConnectionThread;
    std::chrono::steady_clock::time_point mLastConnectionLoopTime;
    std::chrono::steady_clock::time_point mLastWanConnectionLoopTime;
    void monitorPrinterConnections();
    void monitorWanPrinterConnections();
    std::mutex mWanPrintersMutex;
    void refreshWanPrinters();
    
    // WAN network authentication
    template<typename T>
    void checkUserAuthStatus(const PrinterNetworkInfo& printerNetworkInfo, const PrinterNetworkResult<T>& result, 
                             const UserNetworkInfo& requestUserInfo);
    

};
} // namespace Slic3r::GUI 
