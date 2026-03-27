#include "PrinterCache.hpp"
#include <algorithm>
#include <stdexcept>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <chrono>
#include <exception>
#include <cstdint>
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r {

namespace fs = boost::filesystem;

PrinterCache::PrinterCache() {
  
}

PrinterCache::~PrinterCache() {
}

bool PrinterCache::loadPrinterList() {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    fs::path printerListPath = fs::path(Slic3r::data_dir()) / "user" / "printer_list.json";
    // read printer list from file
    boost::nowide::ifstream ifs(printerListPath.string());
    if (!ifs.is_open()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to open printer list file for reading: %s")
                                              % printerListPath.string();
        return false;
    }
    
    try {
        nlohmann::json jsonData;
        ifs >> jsonData;      
        mPrinters.clear();
        for (const auto& [printerId, printerJson] : jsonData.items()) {
            PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(printerJson);
            mPrinters[printerId] = printerInfo;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": loaded %1% printers") % mPrinters.size();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to load printer list from JSON: %s") % e.what();
    }
    return true;
}

bool PrinterCache::savePrinterList() {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    fs::path printerListPath = fs::path(Slic3r::data_dir()) / "user" / "printer_list.json";
    nlohmann::json jsonData;
    for (const auto& [printerId, printerInfo] : mPrinters) {
        if(printerInfo.networkType == NETWORK_TYPE_WAN) {
            // wan printer not save to file
            continue;
        }
        nlohmann::json printerJson = convertPrinterNetworkInfoToJson(printerInfo);  
        // Remove runtime status fields that shouldn't be persisted
        printerJson.erase("connectStatus");
        printerJson.erase("printerStatus");
        printerJson.erase("printTask");
        
        jsonData[printerId] = printerJson;
    }
    boost::nowide::ofstream ofs(printerListPath.string());
    ofs << jsonData.dump(4);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": saved %1% printers") % jsonData.size();
    return true;
}

std::optional<PrinterNetworkInfo> PrinterCache::getPrinter(const std::string& printerId) const {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<PrinterNetworkInfo> PrinterCache::getPrinters() const {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    std::vector<PrinterNetworkInfo> result;
    result.reserve(mPrinters.size());
    for (const auto& pair : mPrinters) {
        result.push_back(pair.second);
    }
    return result;
}

bool PrinterCache::addPrinter(const PrinterNetworkInfo& printerInfo) {
    if (printerInfo.printerId.empty()) {
        return false;
    }   
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto result = mPrinters.emplace(printerInfo.printerId, printerInfo);
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    result.first->second.addTime = now;
    result.first->second.modifyTime = now;
    result.first->second.lastActiveTime = now;
    return result.second;
}

bool PrinterCache::deletePrinter(const std::string& printerId) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        mPrinters.erase(it);
        return true;
    }
    return false;
}
bool PrinterCache::updatePrinterName(const std::string& printerId, const std::string& name) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        it->second.printerName = name;
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.modifyTime     = now;
        it->second.lastActiveTime = now;
        return true;
    }
    return false;
}

bool PrinterCache::updatePrinterHost(const std::string& printerId, const PrinterNetworkInfo& printerInfo) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        it->second.host = printerInfo.host;
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.modifyTime     = now;
        it->second.lastActiveTime = now;
        return true;
    }
    return false;
}

bool PrinterCache::updatePrinterConnectStatus(const std::string& printerId, const PrinterConnectStatus& status) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.connectStatus = status;
        it->second.lastActiveTime = now;
        return true;
    }
    return false;
}

void PrinterCache::updatePrinterStatus(const std::string& printerId, const PrinterStatus& status) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        it->second.printerStatus = status;
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.lastActiveTime = now;
        if(status != PrinterStatus::PRINTER_STATUS_PRINTING && status != PrinterStatus::PRINTER_STATUS_PAUSED && status != PrinterStatus::PRINTER_STATUS_PAUSING) {
            it->second.printTask.taskId = "";
            it->second.printTask.fileName = "";
            it->second.printTask.totalTime = 0;
            it->second.printTask.currentTime = 0;
            it->second.printTask.estimatedTime = 0;
            it->second.printTask.progress = 0;
        } 
    }
}

void PrinterCache::updatePrinterPrintTask(const std::string& printerId, const PrinterPrintTask& task) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.lastActiveTime = now;
        if(it->second.printerStatus == PrinterStatus::PRINTER_STATUS_IDLE || 
            it->second.printerStatus == PrinterStatus::PRINTER_STATUS_OFFLINE ||
            it->second.printerStatus == PrinterStatus::PRINTER_STATUS_CANCELED) {
            it->second.printTask.taskId = "";
            it->second.printTask.fileName = "";
            it->second.printTask.totalTime = 0;
            it->second.printTask.currentTime = 0;
            it->second.printTask.estimatedTime = 0;
            it->second.printTask.progress = 0;
        } else {
            it->second.printTask = task;
        }
    }
}

void PrinterCache::updatePrinterAttributes(const std::string& printerId, const PrinterNetworkInfo& printerInfo) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.lastActiveTime = now;
        it->second.firmwareVersion = printerInfo.firmwareVersion;
        it->second.printCapabilities = printerInfo.printCapabilities;
        it->second.systemCapabilities = printerInfo.systemCapabilities;
        if(it->second.mainboardId.empty() && !printerInfo.mainboardId.empty()) {
            it->second.mainboardId = printerInfo.mainboardId;
        }
        if(it->second.serialNumber.empty() && !printerInfo.serialNumber.empty()) {
            it->second.serialNumber = printerInfo.serialNumber;
        }
        if(it->second.webUrl.empty() && !printerInfo.webUrl.empty()) {
            it->second.webUrl = printerInfo.webUrl;
        }
    }
}

void PrinterCache::updatePrinterAttributesByNotify(const std::string& printerId, const PrinterNetworkInfo& printerInfo) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.lastActiveTime = now;
        it->second.firmwareVersion = printerInfo.firmwareVersion;
        it->second.printerName = printerInfo.printerName;
        it->second.printCapabilities = printerInfo.printCapabilities;
        it->second.systemCapabilities = printerInfo.systemCapabilities;
    }
}

bool PrinterCache::updatePrinterField(const std::string& printerId, std::function<void(PrinterNetworkInfo&)> updater) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    auto it = mPrinters.find(printerId);
    if (it != mPrinters.end()) {
        updater(it->second);
        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.modifyTime = now;
        it->second.lastActiveTime = now;
        return true;
    }
    return false;
}

} // namespace Slic3r
