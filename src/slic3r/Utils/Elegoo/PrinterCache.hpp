#pragma once

#include <map>
#include <mutex>
#include <vector>
#include <optional>
#include <functional>
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "slic3r/Utils/Singleton.hpp"

namespace Slic3r {

/**
 * @brief Printer Cache Layer
 */
class PrinterCache : public Singleton<PrinterCache>
{
    friend class Singleton<PrinterCache>;

public:
    ~PrinterCache();
    PrinterCache(const PrinterCache&) = delete;
    PrinterCache& operator=(const PrinterCache&) = delete;
    
    /**
     * @brief Load printer list from file
     */
    bool loadPrinterList();

    /**
     * @brief Save printer list to file
     */
    bool savePrinterList();

    /**
     * @brief Get single printer by ID
     */
    std::optional<PrinterNetworkInfo> getPrinter(const std::string& printerId) const;
    
    /**
     * @brief Get all printers
     */
    std::vector<PrinterNetworkInfo> getPrinters() const;
    
    /**
     * @brief Add new printer
     */
    bool addPrinter(const PrinterNetworkInfo& printerInfo);

    /**
     * @brief Delete printer by ID
     */
    bool deletePrinter(const std::string& printerId);
    
    /**
     * @brief Update printer connection status
     */
    bool updatePrinterConnectStatus(const std::string& printerId, const PrinterConnectStatus& status);
    
    /**
     * @brief Update printer name
     */
    bool updatePrinterName(const std::string& printerId, const std::string& name);
    
    /**
     * @brief Update printer host
     */
    bool updatePrinterHost(const std::string& printerId, const PrinterNetworkInfo& printerInfo);

    /**
     * @brief Update printer attributes only when the printer is connected
     * @param printerId: printer id
     * @param printerInfo: printer info
     */
    void updatePrinterAttributes(const std::string& printerId, const PrinterNetworkInfo& printerInfo);

    /**
     * @brief Update printer attributes by notify
     * @param printerId: printer id
     * @param printerInfo: printer info
     */
    void updatePrinterAttributesByNotify(const std::string& printerId, const PrinterNetworkInfo& printerInfo);

    /**
     * @brief Update printer status
     */
    void updatePrinterStatus(const std::string& printerId, const PrinterStatus& status,
                             const std::vector<PrinterExceptionDetail>& exceptions = {}, int deviceAssistantStatus = 0);
    /**
     * @brief Update printer print task
     */
    void updatePrinterPrintTask(const std::string& printerId, const PrinterPrintTask& task);

    /**
     * @brief Update printer field by custom updater function
     * @param printerId: printer id
     * @param updater: function to update printer info
     * @return true if printer found and updated
     */
    bool updatePrinterField(const std::string& printerId, std::function<void(PrinterNetworkInfo&)> updater);

private:
    PrinterCache();
    mutable std::mutex mCacheMutex;
    std::map<std::string, PrinterNetworkInfo> mPrinters;
};

} // namespace Slic3r
