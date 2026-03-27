#include "PrinterManager.hpp"
#include "PrinterUploadManager.hpp"
#include "IPCClient.hpp"
#include "MultiInstanceCoordinator.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include <wx/app.h>
#include <wx/wx.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "slic3r/Utils/PrintHost.hpp"
#include <boost/beast/core/detail/base64.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <future>
#include <mutex>
#include <algorithm>
#include "PrinterCache.hpp"
#include "PrinterNetworkEvent.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/Utils/Elegoo/PrinterPluginManager.hpp"
#include "slic3r/Utils/Elegoo/UserNetworkManager.hpp"
#include "slic3r/Utils/Elegoo/MultiInstanceCoordinator.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"

// Do not lock mInitializedMutex: close() may join() while std::async workers still call APIs that use this macro.
#define CHECK_INITIALIZED(returnValue) \
    do { \
        if (!mIsInitialized.load(std::memory_order_acquire)) { \
            using ValueType = std::decay_t<decltype(returnValue)>; \
            return PrinterNetworkResult<ValueType>(PrinterNetworkErrorCode::PRINTER_NETWORK_NOT_INITIALIZED, returnValue); \
        } \
    } while (0)

namespace Slic3r {

PrinterManager::PrinterManager()
    : mIsInitialized(false)
    , mMonitoring(false)
{
}

PrinterManager::~PrinterManager() {}

void PrinterManager::init()
{
    BOOST_LOG_TRIVIAL(info) << "PrinterManager::init";
    std::lock_guard<std::mutex> lock(mInitializedMutex);
    if (mIsInitialized) {
        BOOST_LOG_TRIVIAL(info) << "PrinterManager::init: already initialized";
        return;
    }
    PrinterUploadManager::getInstance()->init();

    if(!MultiInstanceCoordinator::getInstance()->isMaster()){
        BOOST_LOG_TRIVIAL(info) << "PrinterManager::init: non-master, skip network stack";
        return;
    }    
     // connect status changed event
     mConnectStatusHandlerId = PrinterNetworkEvent::getInstance()->connectStatusChanged.connect([](const PrinterConnectStatusEvent& event) {
        // only update the connect status when the printer is disconnected
        if (event.status != PRINTER_CONNECT_STATUS_CONNECTED) {
            PrinterCache::getInstance()->updatePrinterConnectStatus(event.printerId, event.status);
        }
    });

    // printer status changed event
    mStatusChangedHandlerId = PrinterNetworkEvent::getInstance()->statusChanged.connect(
        [](const PrinterStatusEvent& event) { PrinterCache::getInstance()->updatePrinterStatus(event.printerId, event.status); });

    // printer print task changed event
    mPrintTaskChangedHandlerId = PrinterNetworkEvent::getInstance()->printTaskChanged.connect([](const PrinterPrintTaskEvent& event) {
        PrinterCache::getInstance()->updatePrinterPrintTask(event.printerId, event.task);
    });

    // printer attributes changed event
    mAttributesChangedHandlerId = PrinterNetworkEvent::getInstance()->attributesChanged.connect([](const PrinterAttributesEvent& event) {
        PrinterCache::getInstance()->updatePrinterAttributesByNotify(event.printerId, event.printerInfo);
    });

    // WAN printer list changed event (async refresh request -> monitor thread)
    mPrinterOnlineListChangedHandlerId = PrinterNetworkEvent::getInstance()->printerOnlineListChanged.connect([](const PrinterOnlineListChangedEvent&) {
        PrinterManager::getInstance()->enqueueWanSyncRequest();
    });

    // Get log level from AppConfig
    std::string logLevel = "info";
    try {
        if (wxGetApp().app_config) {
            #if ELEGOO_INTERNAL_TESTING
                logLevel = "debug";
            #else
                if(wxGetApp().app_config->get_bool("developer_mode")){
                    logLevel = "debug";
                }else{
                    // logLevel = wxGetApp().app_config->get("log_severity_level");
                    // if (logLevel.empty()) {
                    //     logLevel = "info";
                    // }
                }
            #endif
        }
    } catch (...) {
        logLevel = "info";
    }
    
    NetworkInitializer::init(logLevel);
    PrinterPluginManager::getInstance()->init();
    UserNetworkManager::getInstance()->init();

    PrinterCache::getInstance()->loadPrinterList();
    syncOldPresetPrinters();
    mMonitoring = true;

    mConnectionThread = std::thread([this]() { monitorPrinterConnections(); });

    
    mIsInitialized = true;
    BOOST_LOG_TRIVIAL(info) << "PrinterManager::init: complete (master)";
}

void PrinterManager::close()
{
    BOOST_LOG_TRIVIAL(info) << "PrinterManager::close";
    std::lock_guard<std::mutex> lock(mInitializedMutex);
    PrinterUploadManager::getInstance()->close();

    if (!mIsInitialized) {
        BOOST_LOG_TRIVIAL(info) << "PrinterManager::close: master stack not initialized, done";
        return;
    }
    
    mIsInitialized = false;
    mMonitoring = false;

    {
        std::lock_guard<std::mutex> lock(mWanSyncRequestMutex);
        // Ensure worker wakes up and exits promptly during shutdown.
        mWanSyncRequestPending.store(true);
    }
    mWanSyncRequestCv.notify_all();

    if (mConnectStatusHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->connectStatusChanged.disconnect(mConnectStatusHandlerId);
        mConnectStatusHandlerId = 0;
    }
    if (mStatusChangedHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->statusChanged.disconnect(mStatusChangedHandlerId);
        mStatusChangedHandlerId = 0;
    }
    if (mPrintTaskChangedHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->printTaskChanged.disconnect(mPrintTaskChangedHandlerId);
        mPrintTaskChangedHandlerId = 0;
    }
    if (mAttributesChangedHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->attributesChanged.disconnect(mAttributesChangedHandlerId);
        mAttributesChangedHandlerId = 0;
    }
    if (mPrinterOnlineListChangedHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->printerOnlineListChanged.disconnect(mPrinterOnlineListChangedHandlerId);
        mPrinterOnlineListChangedHandlerId = 0;
    }

    // Wait for connection monitor thread to finish
    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }
    // Disconnect all printer networks
    {
        std::lock_guard<std::mutex> lockPrinter(mPrinterNetworkMutex);
        for (const auto& [printerId, network] : mPrinterNetworkConnections) {
            network->disconnectFromPrinter();
        }
        mPrinterNetworkConnections.clear();
    }

    PrinterCache::getInstance()->savePrinterList();

    UserNetworkManager::getInstance()->uninit();
    PrinterPluginManager::getInstance()->uninit();

    NetworkInitializer::uninit();
    BOOST_LOG_TRIVIAL(info) << "PrinterManager::close: complete";
}
PrinterNetworkResult<bool> PrinterManager::deletePrinter(const std::string& printerId)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->deletePrinter(printerId);
    }
    
    PrinterLock lock(printerId);
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }

    if (printer.value().networkType == NETWORK_TYPE_WAN) {
        // unbind the WAN printer
        auto unbindResult = UserNetworkManager::getInstance()->unbindWANPrinter(printer.value().serialNumber);
        if (!unbindResult.isSuccess()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": delete printer failed to unbind WAN printer %s %s %s: %s") % printer.value().host %
                                            printer.value().printerName % printer.value().printerModel % unbindResult.message;
            return unbindResult;
        }
    }

    std::shared_ptr<IPrinterNetwork> printerNetwork = getPrinterNetwork(printerId);

    if (printerNetwork) {
        printerNetwork->disconnectFromPrinter();
    }
    deletePrinterNetwork(printerId);
    PrinterCache::getInstance()->deletePrinter(printerId);
    PrinterCache::getInstance()->savePrinterList();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": delete printer %s %s %s") % printer.value().host % printer.value().printerName %
                                   printer.value().printerModel;
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}
PrinterNetworkResult<bool> PrinterManager::updatePrinterName(const std::string& printerId, const std::string& printerName)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->updatePrinterName(printerId, printerName);
    }
    
    PrinterLock lock(printerId);
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }
    std::shared_ptr<IPrinterNetwork> printerNetwork = getPrinterNetwork(printerId);
    if (!printerNetwork) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }

    UserNetworkInfo requestUserInfo  = UserNetworkManager::getInstance()->getUserInfo();
    auto            updateNameResult = printerNetwork->updatePrinterName(printerName);
    checkUserAuthStatus(printer.value(), updateNameResult, requestUserInfo);
    if (!updateNameResult.isSuccess()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": update printer name failed, %s %s %s, error: %s") % printer.value().host %
                                          printer.value().printerName % printer.value().printerModel % updateNameResult.message;
        return updateNameResult;
    }
    PrinterCache::getInstance()->updatePrinterName(printerId, printerName);
    PrinterCache::getInstance()->savePrinterList();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": update printer name %s %s %s to %s") % printer.value().host % printer.value().printerName %
                                   printer.value().printerModel % printerName;
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}
PrinterNetworkResult<bool> PrinterManager::updatePrinterHost(const std::string& printerId, const std::string& host)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->updatePrinterHost(printerId, host);
    }
    
    PrinterLock lock(printerId);
    std::vector<PrinterNetworkInfo> printers = PrinterCache::getInstance()->getPrinters();
    for (auto& p : printers) {
        if (!host.empty() && p.host == host && p.printerId != printerId) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": printer already exists, host: %s, printerId: %s") % p.host % printerId;
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_ALREADY_EXISTS, false);
        }
    }
    auto v = PrinterCache::getInstance()->getPrinter(printerId);
    if (!v.has_value()) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }

    PrinterNetworkInfo printer = v.value();
    // if the printer is a WAN printer, not support to update the host
    if (printer.networkType == NETWORK_TYPE_WAN) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_TYPE_NOT_SUPPORTED, false);
    }

    size_t pos = printer.webUrl.find(printer.host);
    if (pos != std::string::npos) {
        printer.webUrl.replace(pos, printer.host.length(), host);
    }
    printer.host = host;

    PrinterNetworkResult<bool> result = connectToPrinter(printer);
    if (result.isSuccess()) {
        PrinterCache::getInstance()->updatePrinterField(printerId, [&printer](PrinterNetworkInfo& cachedPrinter) {
            cachedPrinter.webUrl             = printer.webUrl;
            cachedPrinter.host               = printer.host;
            cachedPrinter.printerName        = printer.printerName;
            cachedPrinter.printCapabilities  = printer.printCapabilities;
            cachedPrinter.systemCapabilities = printer.systemCapabilities;
            cachedPrinter.firmwareVersion    = printer.firmwareVersion;
            cachedPrinter.mainboardId        = printer.mainboardId;
            cachedPrinter.serialNumber       = printer.serialNumber;
            cachedPrinter.webUrl             = printer.webUrl;
            cachedPrinter.connectStatus      = printer.connectStatus;
        });
        return result;
    }
    PrinterCache::getInstance()->updatePrinterField(printerId, [&printer](PrinterNetworkInfo& cachedPrinter) {
        cachedPrinter.connectStatus = printer.connectStatus;
        if (printer.printerStatus == PRINTER_STATUS_ID_NOT_MATCH || printer.printerStatus == PRINTER_STATUS_AUTH_ERROR) {
            cachedPrinter.printerStatus = printer.printerStatus;
        }
    });
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << boost::format(": update printer host failed, %s %s %s, error: %s") % printer.host % printer.printerName %
                                      printer.printerModel % result.message;
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false, result.message);
}
PrinterNetworkResult<bool> PrinterManager::updatePhysicalPrinter(const std::string& printerId, const PrinterNetworkInfo& printerInfo)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->updatePhysicalPrinter(printerId, printerInfo);
    }
    
    PrinterLock lock(printerId);
    std::vector<PrinterNetworkInfo> printers = PrinterCache::getInstance()->getPrinters();
    for (auto& p : printers) {
        if (!printerInfo.host.empty() && p.host == printerInfo.host && p.printerId != printerId) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": printer already exists, host: %s, printerId: %s") % printerInfo.host % printerId;
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_ALREADY_EXISTS, false);
        }
    }

    auto v = PrinterCache::getInstance()->getPrinter(printerId);
    if (!v.has_value()) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }

    PrinterNetworkInfo printer = v.value();

    const bool updateNameOnConnect = !printerInfo.printerName.empty() && printer.printerName != printerInfo.printerName;

    printer.host         = printerInfo.host;
    printer.webUrl       = printerInfo.webUrl;
    printer.hostType     = printerInfo.hostType;
    printer.printerName  = printerInfo.printerName;
    printer.password     = printerInfo.password;
    printer.accessCode   = printerInfo.accessCode;
    printer.serialNumber = printerInfo.serialNumber;
    printer.extraInfo    = printerInfo.extraInfo;
    printer.pinCode      = printerInfo.pinCode;

    PrinterCache::getInstance()->updatePrinterConnectStatus(printerId, PRINTER_CONNECT_STATUS_DISCONNECTED);
    std::shared_ptr<IPrinterNetwork> oldNetwork = getPrinterNetwork(printerId);
    if (oldNetwork) {
        oldNetwork->disconnectFromPrinter();
        deletePrinterNetwork(printerId);
    }
    PrinterNetworkResult<bool> result = connectToPrinter(printer, updateNameOnConnect);
    if (result.isSuccess()) {

        PrinterCache::getInstance()->updatePrinterField(printerId, [&printer](PrinterNetworkInfo& cachedPrinter) {
            cachedPrinter.printerName        = printer.printerName;
            cachedPrinter.hostType           = printer.hostType;
            cachedPrinter.host               = printer.host;
            cachedPrinter.webUrl             = printer.webUrl;
            cachedPrinter.password           = printer.password;
            cachedPrinter.accessCode         = printer.accessCode;
            cachedPrinter.serialNumber       = printer.serialNumber;
            cachedPrinter.extraInfo          = printer.extraInfo;
            cachedPrinter.pinCode            = printer.pinCode;
            cachedPrinter.printCapabilities  = printer.printCapabilities;
            cachedPrinter.systemCapabilities = printer.systemCapabilities;
            cachedPrinter.firmwareVersion    = printer.firmwareVersion;
            cachedPrinter.connectStatus      = printer.connectStatus;
        });
    } else {
        PrinterCache::getInstance()->updatePrinterField(printerId, [&printer](PrinterNetworkInfo& cachedPrinter) {
            cachedPrinter.connectStatus = printer.connectStatus;
            if (printer.printerStatus == PRINTER_STATUS_ID_NOT_MATCH || printer.printerStatus == PRINTER_STATUS_AUTH_ERROR) {
                cachedPrinter.printerStatus = printer.printerStatus;
            }
        });
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": failed to connect to printer, %s %s %s, error: %s") % printer.host %
                                          printer.printerName % printer.printerModel % result.message;
        return result;
    }

    PrinterCache::getInstance()->savePrinterList();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": updated printer %s %s %s") % printer.host % printer.printerName % printer.printerModel;
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}
PrinterNetworkResult<bool> PrinterManager::addPrinter(PrinterNetworkInfo& printerNetworkInfo)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->addPrinter(printerNetworkInfo);
    }
    
    CHECK_INITIALIZED(false);
    
    std::lock_guard<std::mutex> lock(mAddPrinterMutex);

    // only generate a unique id for the printer when adding a printer
    // the printer info is from the UI, the UI info is from the discover device or manual add
    std::vector<PrinterNetworkInfo> printers = PrinterCache::getInstance()->getPrinters();
    for (const auto& localPrinter : printers) {
        if ((!localPrinter.host.empty() && localPrinter.host == printerNetworkInfo.host) ||
            (!localPrinter.serialNumber.empty() && localPrinter.serialNumber == printerNetworkInfo.serialNumber) ||
            (!localPrinter.mainboardId.empty() && localPrinter.mainboardId == printerNetworkInfo.mainboardId) ||
            (!localPrinter.printerId.empty() && localPrinter.printerId == printerNetworkInfo.printerId)) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": printer already exists, name: %s, host: %s, serialNumber: %s, mainboardId: %s") %
                                              localPrinter.printerName % localPrinter.host % localPrinter.serialNumber %
                                              localPrinter.mainboardId;
            std::string errorMessage = getErrorMessage(PrinterNetworkErrorCode::PRINTER_ALREADY_EXISTS);

            if (localPrinter.networkType == NETWORK_TYPE_WAN) {
                errorMessage += _u8L("Name") + ":" + localPrinter.printerName + ", " + _u8L("SN") + ":" + localPrinter.serialNumber;
            } else {
                errorMessage += _u8L("Name") + ":" + localPrinter.printerName + ", " + _u8L("Host/IP/URL") + ":" + localPrinter.host;
            }
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_ALREADY_EXISTS, false, errorMessage);
        }
    }

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    printerNetworkInfo.addTime        = now;
    printerNetworkInfo.modifyTime     = now;
    printerNetworkInfo.lastActiveTime = now;
    if (printerNetworkInfo.printerId.empty() && printerNetworkInfo.networkType != NETWORK_TYPE_WAN) {
        printerNetworkInfo.printerId = generatePrinterId();
    }
    if (!printerNetworkInfo.password.empty()) {
        printerNetworkInfo.authMode = PRINTER_AUTH_MODE_PASSWORD;
    }
    if (printerNetworkInfo.networkType == NETWORK_TYPE_WAN) {
        if (!printerNetworkInfo.pinCode.empty())
            printerNetworkInfo.authMode = PRINTER_AUTH_MODE_PIN_CODE;
    }
    if (printerNetworkInfo.networkType == NETWORK_TYPE_LAN) {
        if (!printerNetworkInfo.accessCode.empty())
            printerNetworkInfo.authMode = PRINTER_AUTH_MODE_ACCESS_CODE;
    }

    const bool updateNameOnConnect = printerNetworkInfo.isPhysicalPrinter && !printerNetworkInfo.printerName.empty();

    // bind the printer if it is a WAN printer
    if (printerNetworkInfo.networkType == NETWORK_TYPE_WAN) {
        PrinterNetworkResult<PrinterNetworkInfo> bindResult = UserNetworkManager::getInstance()->bindWANPrinter(printerNetworkInfo);
        if (!bindResult.isSuccess() || !bindResult.hasData()) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": add printer failed to bind WAN printer %s %s %s: %s") % printerNetworkInfo.host %
                                              printerNetworkInfo.printerName % printerNetworkInfo.printerModel % bindResult.message;
            return PrinterNetworkResult<bool>(bindResult.code, false, bindResult.message);
        }
        PrinterNetworkInfo boundPrinterNetworkInfo = bindResult.data.value();
        // update the printer network info with the bound printer network info
        printerNetworkInfo.printerId = boundPrinterNetworkInfo.printerId;
        printerNetworkInfo.serialNumber = boundPrinterNetworkInfo.serialNumber;
        //manual bind wan device, set to online device  
        printerNetworkInfo.isPhysicalPrinter = false;
    }

    PrinterNetworkResult<bool> addResult  = connectToPrinter(printerNetworkInfo, updateNameOnConnect);

    if (addResult.isSuccess()) {
        PrinterCache::getInstance()->addPrinter(printerNetworkInfo);
        PrinterCache::getInstance()->savePrinterList();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(": added printer %s %s %s") % printerNetworkInfo.host % printerNetworkInfo.printerName %
                                       printerNetworkInfo.printerModel;
        return addResult;
    } else {
        if (printerNetworkInfo.networkType == NETWORK_TYPE_WAN) {
            // if bind WAN printer success, but connect to printer failed, also return success
            PrinterCache::getInstance()->addPrinter(printerNetworkInfo);
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__
                << boost::format(": add printer failed to connect to WAN printer %s %s %s, but bind WAN printer success, return success") %
                       printerNetworkInfo.host % printerNetworkInfo.printerName % printerNetworkInfo.printerModel;
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
        }
    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << boost::format(": failed to add printer %s %s %s: %s") % printerNetworkInfo.host %
                                      printerNetworkInfo.printerName % printerNetworkInfo.printerModel % addResult.message;
    return addResult;
}
PrinterNetworkResult<bool> PrinterManager::cancelBindPrinter(const PrinterNetworkInfo& printerNetworkInfo)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->cancelBindPrinter(printerNetworkInfo);
    }
    
    std::shared_ptr<IPrinterNetwork> network = NetworkFactory::createPrinterNetwork(printerNetworkInfo);
    if (!network) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }
    return network->cancelBindPrinter(printerNetworkInfo.serialNumber);
}
PrinterNetworkResult<std::vector<PrinterNetworkInfo>> PrinterManager::discoverPrinter()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->discoverPrinter();
    }
    
    CHECK_INITIALIZED(std::vector<PrinterNetworkInfo>());
    
    // Use a static mutex to serialize printer discovery to prevent race conditions
    static std::mutex           discoverPrinterMutex;
    std::lock_guard<std::mutex> lock(discoverPrinterMutex);

    std::vector<PrinterNetworkInfo> discoveredPrinters;
    UserNetworkInfo                 requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();

    for (const auto& printerHostType : {htElegooLink, htOctoPrint, htPrusaLink, htPrusaConnect, htDuet, htFlashAir, htAstroBox, htRepetier,
                                        htMKS, htESP3D, htCrealityPrint, htObico, htFlashforge, htSimplyPrint}) {
        PrinterNetworkInfo printerNetworkInfo;
        printerNetworkInfo.hostType              = PrintHost::get_print_host_type_str(printerHostType);
        std::shared_ptr<IPrinterNetwork> network = NetworkFactory::createPrinterNetwork(printerNetworkInfo);
        if (!network) {
            continue;
        }
        PrinterNetworkResult<std::vector<PrinterNetworkInfo>> result = network->discoverPrinters();
        if (result.isSuccess() && result.hasData()) {
            discoveredPrinters.insert(discoveredPrinters.end(), result.data.value().begin(), result.data.value().end());
        } else if (result.isError()) {
            UserNetworkManager::getInstance()->checkUserAuthStatus(requestUserInfo, result.code);
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": failed to discover devices for host type %d: %s") %
                                              static_cast<int>(printerHostType) % result.message;
        }
    }

    std::vector<PrinterNetworkInfo> printerList = PrinterCache::getInstance()->getPrinters();
    std::vector<PrinterNetworkInfo> printersToAdd;
    for (auto& discoveredPrinter : discoveredPrinters) {
        // check if the device is existing
        bool isSamePrinter = false;
        for (auto& p : printerList) {
            if (!p.mainboardId.empty() && (discoveredPrinter.mainboardId == p.mainboardId) && (discoveredPrinter.networkType == p.networkType)) {
                isSamePrinter = true;
            }
            if (!p.serialNumber.empty() && (discoveredPrinter.serialNumber == p.serialNumber) && (discoveredPrinter.networkType == p.networkType)) {
                isSamePrinter = true;
            }
            if (isSamePrinter) {
                discoveredPrinter.isAdded = true;
                break;
            }
        }
        PrinterNetworkInfo printerNetworkInfo = discoveredPrinter;
        if (printerNetworkInfo.printerId.empty()) {
            printerNetworkInfo.printerId = boost::uuids::to_string(boost::uuids::random_generator()());
        }
        // Validate and complete printer info
        validateAndCompletePrinterInfo(printerNetworkInfo);
        printersToAdd.push_back(printerNetworkInfo);
    }
    return PrinterNetworkResult<std::vector<PrinterNetworkInfo>>(PrinterNetworkErrorCode::SUCCESS, printersToAdd);
}
std::vector<PrinterNetworkInfo> PrinterManager::getPrinterList()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getPrinterList();
    }
    
    auto printers = PrinterCache::getInstance()->getPrinters();
    std::sort(printers.begin(), printers.end(),
              [](const PrinterNetworkInfo& a, const PrinterNetworkInfo& b) { return a.addTime < b.addTime; });
    return printers;
}
PrinterNetworkInfo PrinterManager::getPrinterNetworkInfo(const std::string& printerId)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getPrinterNetworkInfo(printerId);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (printer.has_value()) {
        return printer.value();
    }
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
    return PrinterNetworkInfo();
}
PrinterNetworkResult<PrinterMmsGroup> PrinterManager::getPrinterMmsInfo(const std::string& printerId)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getPrinterMmsInfo(printerId);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterMmsGroup>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, PrinterMmsGroup());
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterMmsGroup>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, PrinterMmsGroup());
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<PrinterMmsGroup>(PrinterNetworkErrorCode::NETWORK_ERROR, PrinterMmsGroup());
    }

    UserNetworkInfo                       requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    PrinterNetworkResult<PrinterMmsGroup> result          = network->getPrinterMmsInfo();
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    if (result.isSuccess() && result.hasData()) {
        std::string mmsSystemName = "MMS";
        if (!result.data.value().mmsSystemName.empty()) {
            mmsSystemName = result.data.value().mmsSystemName;
        }
        return PrinterNetworkResult<PrinterMmsGroup>(PrinterNetworkErrorCode::SUCCESS, result.data.value());
    }

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << boost::format(": failed to get printer mms info %s %s %s, error: %s") % printer.value().host %
                                      printer.value().printerName % printer.value().printerModel % result.message;
    return PrinterNetworkResult<PrinterMmsGroup>(result.isSuccess() ? PrinterNetworkErrorCode::PRINTER_INVALID_RESPONSE : result.code,
                                                 PrinterMmsGroup());
}
void PrinterManager::enqueueWanSyncRequest()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        IPCClient::getInstance()->enqueueWanSyncRequest();
        return;
    }

    mWanSyncRequestPending.store(true);
    mWanSyncRequestCv.notify_one();
}
// first get selected printer by modelName and printerId
// if not found, get selected printer by modelName
// if not found, get selected printer by printerId
// if not found, return first printer
PrinterNetworkInfo PrinterManager::getSelectedPrinter(const std::string& printerModel, const std::string& printerId)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getSelectedPrinter(printerModel, printerId);
    }
    
    auto               printers = getPrinterList();
    PrinterNetworkInfo selectedPrinter;
    if (!printerModel.empty() && !printerId.empty()) {
        for (auto& printer : printers) {
            if (printer.printerModel == printerModel && printer.printerId == printerId) {
                selectedPrinter = printer;
                break;
            }
        }
    }
    if (!printerModel.empty() && selectedPrinter.printerId.empty()) {
        for (auto& printer : printers) {
            if (printer.printerModel == printerModel) {
                selectedPrinter = printer;
                break;
            }
        }
    }
    if (!printerId.empty() && selectedPrinter.printerId.empty()) {
        for (auto& printer : printers) {
            if (printer.printerId == printerId) {
                selectedPrinter = printer;
                break;
            }
        }
    }
    if (selectedPrinter.printerId.empty() && !printers.empty()) {
        selectedPrinter = printers[0];
    }
    return selectedPrinter;
}
PrinterNetworkResult<PrinterPrintFileResponse> PrinterManager::getFileList(const std::string& printerId, int pageNumber, int pageSize)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getFileList(printerId, pageNumber, pageSize);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintFileResponse>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, PrinterPrintFileResponse());
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintFileResponse>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, PrinterPrintFileResponse());
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintFileResponse>(PrinterNetworkErrorCode::NETWORK_ERROR, PrinterPrintFileResponse());
    }

    UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = network->getFileList(pageNumber, pageSize);
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}
PrinterNetworkResult<PrinterPrintFileResponse> PrinterManager::getFileDetail(const std::string& printerId, const std::string& fileName)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getFileDetail(printerId, fileName);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintFileResponse>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, PrinterPrintFileResponse());
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintFileResponse>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, PrinterPrintFileResponse());
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintFileResponse>(PrinterNetworkErrorCode::NETWORK_ERROR, PrinterPrintFileResponse());
    }

    UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = network->getFileDetail(fileName);
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}
PrinterNetworkResult<PrinterPrintTaskResponse> PrinterManager::getPrintTaskList(const std::string& printerId, int pageNumber, int pageSize)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getPrintTaskList(printerId, pageNumber, pageSize);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintTaskResponse>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, PrinterPrintTaskResponse());
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintTaskResponse>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, PrinterPrintTaskResponse());
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<PrinterPrintTaskResponse>(PrinterNetworkErrorCode::NETWORK_ERROR, PrinterPrintTaskResponse());
    }

    UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = network->getPrintTaskList(pageNumber, pageSize);
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}
PrinterNetworkResult<bool> PrinterManager::deletePrintTasks(const std::string& printerId, const std::vector<std::string>& taskIds)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->deletePrintTasks(printerId, taskIds);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, false);
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }

    UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = network->deletePrintTasks(taskIds);
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}
PrinterNetworkResult<bool> PrinterManager::sendRtmMessage(const std::string& printerId, const std::string& message)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->sendRtmMessage(printerId, message);
    }
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, false);
    }
    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }

    UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = network->sendRtmMessage(message);
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}

PrinterNetworkResult<std::vector<LicenseExpiredDevice>> PrinterManager::getLicenseExpiredDevices()
{
    return UserNetworkManager::getInstance()->getLicenseExpiredDevices();
}

PrinterNetworkResult<bool> PrinterManager::renewLicense(const std::string& serialNumber)
{
    if (serialNumber.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": serialNumber is empty";
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::INVALID_PARAMETER, false);
    }
    return UserNetworkManager::getInstance()->renewLicense(serialNumber);
}

PrinterNetworkResult<bool> PrinterManager::refreshPrinterStatus(const std::string& printerId)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->refreshPrinterStatus(printerId);
    }

    if (printerId.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": printerId is empty";
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::INVALID_PARAMETER, false);
    }
    CHECK_INITIALIZED(false);

    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, false);
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }

    UserNetworkInfo           requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    PrinterNetworkResult<bool> result         = network->refreshPrinterStatus();
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}

PrinterNetworkResult<std::string> PrinterManager::getPrinterStatusRaw(const std::string& printerId)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getPrinterStatusRaw(printerId);
    }
    
    CHECK_INITIALIZED(std::string());
    
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (!printer.has_value()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
        return PrinterNetworkResult<std::string>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, std::string());
    }
    
    
    if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, printerId: %s") % printerId;
        return PrinterNetworkResult<std::string>(PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR, std::string());
    }

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printerId);
    if (!network) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % printerId;
        return PrinterNetworkResult<std::string>(PrinterNetworkErrorCode::NETWORK_ERROR, std::string());
    }

    UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = network->getPrinterStatusRaw();
    checkUserAuthStatus(printer.value(), result, requestUserInfo);
    return result;
}

bool PrinterManager::deletePrinterNetwork(const std::string& printerId)
{
    std::lock_guard<std::mutex> lock(mPrinterNetworkMutex);
    auto                        it = mPrinterNetworkConnections.find(printerId);
    if (it == mPrinterNetworkConnections.end()) {
        return false;
    }
    mPrinterNetworkConnections.erase(it);
    return true;
}

std::shared_ptr<IPrinterNetwork> PrinterManager::getPrinterNetwork(const std::string& printerId)
{
    std::lock_guard<std::mutex> lock(mPrinterNetworkMutex);
    auto                        it = mPrinterNetworkConnections.find(printerId);

    if (it != mPrinterNetworkConnections.end()) {
        return it->second;
    }
    return nullptr;
}

std::string PrinterManager::generatePrinterId() { return boost::uuids::to_string(boost::uuids::random_generator{}()); }

void PrinterManager::monitorPrinterConnections()
{
    const int connectLoopIntervalSeconds = 10;
    const int wanSyncIntervalSeconds     = 60 * 3;
    auto      now                        = std::chrono::steady_clock::now();
    auto      lastConnectionLoopTime     = now - std::chrono::seconds(connectLoopIntervalSeconds);
    auto      lastWanSyncScheduleTime    = now - std::chrono::seconds(wanSyncIntervalSeconds);

    while (mMonitoring.load()) {
        std::unique_lock<std::mutex> requestLock(mWanSyncRequestMutex);
        mWanSyncRequestCv.wait_for(requestLock, std::chrono::milliseconds(500), [this]() {
            return !mMonitoring.load() || mWanSyncRequestPending.load();
        });
        const bool requestedImmediate = mWanSyncRequestPending.exchange(false);
        requestLock.unlock();
        if (!mMonitoring.load()) {
            break;
        }

        now = std::chrono::steady_clock::now();
        const bool shouldRunWanSync =
            requestedImmediate ||
            std::chrono::duration_cast<std::chrono::seconds>(now - lastWanSyncScheduleTime).count() >= wanSyncIntervalSeconds;
        const bool shouldRunConnection =
            requestedImmediate ||
            std::chrono::duration_cast<std::chrono::seconds>(now - lastConnectionLoopTime).count() >= connectLoopIntervalSeconds;

        if (!shouldRunWanSync && !shouldRunConnection) {
            continue;
        }

        if (shouldRunWanSync) {
            syncWanPrintersFromCloud();
            lastWanSyncScheduleTime = now;
        }

        if (!shouldRunConnection) {
            continue;
        }

        auto printerList = PrinterCache::getInstance()->getPrinters();
        std::vector<std::future<void>> connectionFutures;
        for (auto& printer : printerList) {
            if (printer.connectStatus == PRINTER_CONNECT_STATUS_CONNECTED) {
                if(getPrinterNetwork(printer.printerId)) {      
                    continue;
                }
                PrinterCache::getInstance()->updatePrinterConnectStatus(printer.printerId, PRINTER_CONNECT_STATUS_DISCONNECTED);
            }
            if (printer.printerStatus == PRINTER_STATUS_ID_NOT_MATCH || printer.printerStatus == PRINTER_STATUS_AUTH_ERROR) {
                continue;
            }
            const auto printerSnapshot = printer;
            auto future = std::async(std::launch::async, [this, printerSnapshot]() {
                PrinterNetworkInfo printer = printerSnapshot;
                PrinterNetworkResult<bool> result = connectToPrinter(printer);
                if (result.isSuccess()) {
                    PrinterCache::getInstance()->updatePrinterField(printer.printerId, [&printer](PrinterNetworkInfo& cachedPrinter) {
                        cachedPrinter.webUrl             = printer.webUrl;
                        cachedPrinter.printCapabilities  = printer.printCapabilities;
                        cachedPrinter.systemCapabilities = printer.systemCapabilities;
                        cachedPrinter.firmwareVersion    = printer.firmwareVersion;
                        cachedPrinter.printerName        = printer.printerName;
                        cachedPrinter.connectStatus      = PRINTER_CONNECT_STATUS_CONNECTED;
                    });
                    // refresh printer status
                    refreshPrinterStatus(printer.printerId);   
                } else {
                    PrinterCache::getInstance()->updatePrinterField(printer.printerId, [&printer](PrinterNetworkInfo& cachedPrinter) {
                        cachedPrinter.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
                        if (printer.printerStatus == PRINTER_STATUS_ID_NOT_MATCH || printer.printerStatus == PRINTER_STATUS_AUTH_ERROR) {
                            cachedPrinter.printerStatus = printer.printerStatus;
                        }
                    });
                }
            });
            connectionFutures.push_back(std::move(future));
        }

        for (auto& future : connectionFutures) {
            future.wait();
        }

        lastConnectionLoopTime = now;
    }
}

void PrinterManager::syncWanPrintersFromCloud()
{
    std::lock_guard<std::mutex> lock(mAddPrinterMutex);

    auto printersResult = UserNetworkManager::getInstance()->getUserBoundPrinters();

    if (printersResult.isError()) {
        // if user network busy, skip refresh online printers
        if (printersResult.code == PrinterNetworkErrorCode::USER_NETWORK_BUSY) {
            return;
        }
        if (printersResult.code == PrinterNetworkErrorCode::NETWORK_ERROR) {
            // update all wan printers to disconnected
            std::vector<PrinterNetworkInfo> printerList = PrinterCache::getInstance()->getPrinters();
            for (const auto& localPrinter : printerList) {
                if (localPrinter.networkType != NETWORK_TYPE_WAN) {
                    continue;
                }
                PrinterCache::getInstance()->updatePrinterConnectStatus(localPrinter.printerId, PRINTER_CONNECT_STATUS_DISCONNECTED);
            }
            return;
        }
    }
    std::vector<PrinterNetworkInfo> wanPrinters;
    if (printersResult.isSuccess() && printersResult.hasData()) {
        for (const auto& printer : printersResult.data.value()) {
            wanPrinters.push_back(printer);
        }
    }

    std::vector<PrinterNetworkInfo> printerList = PrinterCache::getInstance()->getPrinters();

    std::vector<PrinterNetworkInfo> wanPrintersToAdd;
    for (auto& wanPrinter : wanPrinters) {
        if (wanPrinter.networkType != NETWORK_TYPE_WAN) {
            continue;
        }
        if (wanPrinter.serialNumber.empty() || wanPrinter.printerId.empty()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": WAN printer serial number or printer id is empty, printer name: %s, printer "
                                                      "model: %s, printer id: %s, printer serial number: %s") %
                                            wanPrinter.printerName % wanPrinter.printerModel % wanPrinter.printerId %
                                            wanPrinter.serialNumber;
            continue;
        }
        // Check if printer already exists by serial number
        bool isExisting = false;
        for (const auto& localPrinter : printerList) {
            if (localPrinter.networkType != NETWORK_TYPE_WAN) {
                continue;
            }
            if (localPrinter.serialNumber == wanPrinter.serialNumber || localPrinter.printerId == wanPrinter.printerId ||
                (!localPrinter.mainboardId.empty() && localPrinter.mainboardId == wanPrinter.mainboardId)) {
                isExisting = true;
                // update the printer info if the printer already exists
                PrinterCache::getInstance()->updatePrinterField(localPrinter.printerId, [wanPrinter](PrinterNetworkInfo& cachedPrinter) {
                    if (cachedPrinter.printerName != wanPrinter.printerName) {
                        cachedPrinter.printerName = wanPrinter.printerName;
                    }
                });
                break;
            }
        }
        if (!isExisting) {
            validateAndCompletePrinterInfo(wanPrinter);
            // New WAN printers are added as offline first; monitor loop will reconnect and refresh attributes.
            wanPrinter.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
            wanPrintersToAdd.push_back(wanPrinter);
        }
    }

    // Build set of valid serial numbers for O(1) lookup
    std::set<std::string> validSerialNumbers;
    for (const auto& wanPrinter : wanPrinters) {
        if (!wanPrinter.serialNumber.empty()) {
            validSerialNumbers.insert(wanPrinter.serialNumber);
        }
    }

    for (const auto& localPrinter : printerList) {
        if (localPrinter.networkType == NETWORK_TYPE_WAN && validSerialNumbers.find(localPrinter.serialNumber) == validSerialNumbers.end()) {
            PrinterCache::getInstance()->deletePrinter(localPrinter.printerId);
            deletePrinterNetwork(localPrinter.printerId);
        }
    }

    for (const auto& wanPrinter : wanPrintersToAdd) {
        PrinterCache::getInstance()->addPrinter(wanPrinter);
    }
}
PrinterNetworkResult<bool> PrinterManager::connectToPrinter(PrinterNetworkInfo& printer, bool updatePrinterName)
{
    if (printer.printerId.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                 << boost::format(": connect to printer failed, printer id is empty, printer: %s %s %s") % printer.host %
                                        printer.printerName % printer.printerModel;
        printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, false);
    }
    // lock the printer
    PrinterLock lock(printer.printerId);

    std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(printer.printerId);
    if (!network) {
        network = NetworkFactory::createPrinterNetwork(printer);
        if (!network) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": failed to create network for printer: %s %s %s") % printer.host %
                                            printer.printerName % printer.printerModel;
            printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::CREATE_NETWORK_FOR_HOST_TYPE_FAILED, false);
        }
    } else {
        // disconnect the printer network and delete the printer network connection
        network->disconnectFromPrinter();
        printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
        deletePrinterNetwork(printer.printerId);
    }

    auto connectResult = network->connectToPrinter();

    if (!connectResult.isSuccess()) {
        if (connectResult.code == PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD ||
            connectResult.code == PrinterNetworkErrorCode::INVALID_TOKEN ||
            connectResult.code == PrinterNetworkErrorCode::INVALID_ACCESS_CODE ||
            connectResult.code == PrinterNetworkErrorCode::INVALID_PIN_CODE||
            connectResult.code == PrinterNetworkErrorCode::SERVER_PIN_CODE_MISMATCH) {
            printer.printerStatus = PRINTER_STATUS_AUTH_ERROR;
        }
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                 << boost::format(": failed to connect to printer: %s %s %s, error: %s") % printer.host %
                                        printer.printerName % printer.printerModel % connectResult.message;
        printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
        return PrinterNetworkResult<bool>(connectResult.code, false, connectResult.message);
    }
    if (updatePrinterName) {
        auto updateNameResult = network->updatePrinterName(printer.printerName);
        if (!updateNameResult.isSuccess()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": failed to update printer name: %s %s %s, error: %s") % printer.host %
                                            printer.printerName % printer.printerModel % updateNameResult.message;
        }
    }
    // get the printer attributes
    PrinterNetworkResult<PrinterNetworkInfo> attributes;
    for (int i = 0; i < 3; i++) {
        attributes = network->getPrinterAttributes();
        if (!attributes.isSuccess()) {
            BOOST_LOG_TRIVIAL(error)
                << __FUNCTION__
                << boost::format(": connect to printer failed, failed to get printer attributes for printer: %s %s %s, error: %s") %
                       printer.host % printer.printerName % printer.printerModel % attributes.message;

        } else if (!attributes.hasData()) {
            BOOST_LOG_TRIVIAL(error)
                << __FUNCTION__
                << boost::format(
                       ": connect to printer failed, failed to get printer attributes for printer: %s %s %s attribute data is empty") %
                       printer.host % printer.printerName % printer.printerModel;
        } else {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!attributes.isSuccess() || !attributes.hasData()) {
        network->disconnectFromPrinter();
        printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }
    PrinterNetworkInfo printerAttributes = attributes.data.value();
    if ((!printer.mainboardId.empty() && printer.mainboardId != printerAttributes.mainboardId) ||
        (!printer.serialNumber.empty() && printer.serialNumber != printerAttributes.serialNumber) ||
        (printer.printerId != printerAttributes.printerId)) {
        BOOST_LOG_TRIVIAL(error)
            << __FUNCTION__
            << boost::format(": printer mainboardId or serialNumber or printerId not match, local: %s, %s, %s, remote: %s, %s, %s") %
                   printer.mainboardId % printer.serialNumber % printer.printerId % printerAttributes.mainboardId %
                   printerAttributes.serialNumber % printerAttributes.printerId;
        printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
        printer.printerStatus = PRINTER_STATUS_ID_NOT_MATCH;
        network->disconnectFromPrinter();
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::PRINTER_HOST_NOT_MATCH, false);
    }

    {
        std::lock_guard<std::mutex> lock(mPrinterNetworkMutex);
        mPrinterNetworkConnections[printer.printerId] = network;
    }

    printer.printCapabilities  = printerAttributes.printCapabilities;
    printer.systemCapabilities = printerAttributes.systemCapabilities;
    printer.firmwareVersion    = printerAttributes.firmwareVersion;
    printer.mainboardId        = printerAttributes.mainboardId;
    printer.serialNumber       = printerAttributes.serialNumber;
    printer.webUrl             = printerAttributes.webUrl;
    printer.printerName        = printerAttributes.printerName;
    printer.connectStatus      = PRINTER_CONNECT_STATUS_CONNECTED;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": connected to printer: %s %s %s, firmware version: %s") % printer.host %
                                   printer.printerName % printer.printerModel % printerAttributes.firmwareVersion;
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}

void PrinterManager::syncOldPresetPrinters()
{
    // Check if migration has already been completed
    if (wxGetApp().app_config->get("machine_migration_completed") == "1") {
        return;
    }

    try {
        if (!wxGetApp().preset_bundle) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": preset bundle not available, skipping sync");
            return;
        }
        const auto& printerPresets = wxGetApp().preset_bundle->printers;

        for (const auto& preset : printerPresets) {
            if (preset.is_system) {
                continue;
            }
            const auto& config = preset.config;
            if (preset.name.empty() || !config.has("host_type") || !config.has("print_host") || !config.has("printer_model")) {
                continue;
            }

            PrinterNetworkInfo printerInfo;
            printerInfo.isPhysicalPrinter = true;
            printerInfo.printerName       = preset.name;

            auto printerModel = config.option<ConfigOptionString>("printer_model")->value;

            // Find vendor by printer model
            std::string vendorName;
            for (const auto& vendorProfile : wxGetApp().preset_bundle->vendors) {
                for (const auto& vendorModel : vendorProfile.second.models) {
                    if (vendorModel.name == printerModel) {
                        vendorName = vendorProfile.first;
                        break;
                    }
                }
                if (!vendorName.empty())
                    break;
            }
            if (vendorName.empty()) {
                continue;
            }
            printerInfo.vendor       = vendorName;
            printerInfo.printerModel = printerModel;
            auto host                = config.option<ConfigOptionString>("print_host");
            if (host && !host->value.empty()) {
                printerInfo.host = host->value;
            } else {
                continue;
            }
            auto hostType = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
            if (!PrintHost::support_device_list_management(config)) {
                continue;
            }
            if (hostType) {
                std::string hostTypeStr = PrintHost::get_print_host_type_str(hostType->value);
                if (!hostTypeStr.empty()) {
                    printerInfo.hostType = hostTypeStr;
                } else {
                    continue;
                }
            } else {
                continue;
            }
            if (config.has("printhost_port")) {
                auto port = config.option<ConfigOptionString>("printhost_port");
                if (port && !port->value.empty()) {
                    printerInfo.port = std::stoi(port->value);
                }
            }
            if (config.has("print_host_webui")) {
                auto print_host_webui = config.option<ConfigOptionString>("print_host_webui");
                if (print_host_webui && !print_host_webui->value.empty()) {
                    printerInfo.webUrl = print_host_webui->value;
                }
            }
            if (config.has("printhost_apikey")) {
                auto printhost_apikey = config.option<ConfigOptionString>("printhost_apikey");
                if (printhost_apikey && !printhost_apikey->value.empty()) {
                    printerInfo.password = printhost_apikey->value;
                }
            }

            nlohmann::json extraInfo = nlohmann::json();

            if (config.has("printhost_cafile")) {
                auto cafile = config.option<ConfigOptionString>("printhost_cafile");
                if (cafile && !cafile->value.empty()) {
                    extraInfo[PRINTER_NETWORK_EXTRA_INFO_KEY_PORT] = cafile->value;
                }
            }
            if (config.has("printhost_ssl_ignore_revoke")) {
                auto sslIgnoreRevoke = config.option<ConfigOptionString>("printhost_ssl_ignore_revoke");
                if (sslIgnoreRevoke && !sslIgnoreRevoke->value.empty()) {
                    extraInfo[PRINTER_NETWORK_EXTRA_INFO_KEY_IGNORE_CERT_REVOCATION] = sslIgnoreRevoke->value;
                }
            }
            printerInfo.extraInfo = extraInfo.dump();

            bool alreadyExists = false;
            auto printers      = PrinterCache::getInstance()->getPrinters();
            for (const auto& printer : printers) {
                if (printer.host == printerInfo.host) {
                    alreadyExists = true;
                    break;
                }
            }
            if (alreadyExists) {
                continue;
            }
            // Generate unique printerId
            printerInfo.printerId = generatePrinterId();

            // Set timestamps
            auto now                   = std::chrono::system_clock::now();
            auto timestamp             = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            printerInfo.addTime        = timestamp;
            printerInfo.modifyTime     = timestamp;
            printerInfo.lastActiveTime = timestamp;

            PrinterCache::getInstance()->addPrinter(printerInfo);
        }

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": preset printers synced successfully");

        // Mark migration as completed
        wxGetApp().app_config->set("machine_migration_completed", "1");
        wxGetApp().app_config->save();

    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": error syncing preset printers: %s") % e.what();
    }
}


// Check and handle WAN network error (like token expiration)
template<typename T>
void PrinterManager::checkUserAuthStatus(const PrinterNetworkInfo&      printerNetworkInfo,
                                         const PrinterNetworkResult<T>& result,
                                         const UserNetworkInfo&         requestUserInfo)
{
    if (printerNetworkInfo.networkType != NETWORK_TYPE_WAN) {
        return;
    }
    // Check if error is token-related
    if (result.isSuccess()) {
        return;
    }

    // Check and update user auth status
    UserNetworkManager::getInstance()->checkUserAuthStatus(requestUserInfo, result.code);
}

template void PrinterManager::checkUserAuthStatus<bool>(const PrinterNetworkInfo&      printerNetworkInfo,
                                                         const PrinterNetworkResult<bool>& result,
                                                         const UserNetworkInfo&         requestUserInfo);

// Static member definitions for PrinterLock
std::map<std::string, std::recursive_mutex> PrinterManager::PrinterLock::sPrinterMutexes;
std::mutex                                  PrinterManager::PrinterLock::sMutex;

PrinterManager::PrinterLock::PrinterLock(const std::string& printerId) : mPrinterMutex(nullptr)
{
    {
        std::unique_lock<std::mutex> lock(sMutex);
        auto                         it = sPrinterMutexes.find(printerId);
        if (it == sPrinterMutexes.end()) {
            it = sPrinterMutexes.try_emplace(printerId).first;
        }
        mPrinterMutex = &(it->second);
    }

    // Lock the specific printer's mutex (outside the sMutex lock)
    if (mPrinterMutex) {
        mPrinterMutex->lock();
    }
}

PrinterManager::PrinterLock::~PrinterLock()
{
    if (mPrinterMutex) {
        mPrinterMutex->unlock();
    }
}

// Cached preset bundle - load only once
static PresetBundle* getCachedSystemBundle()
{
    static std::unique_ptr<PresetBundle> cachedBundle;
    static std::once_flag                initFlag;

    std::call_once(initFlag, []() {
        cachedBundle = std::make_unique<PresetBundle>();
        cachedBundle->load_system_models_from_json(ForwardCompatibilitySubstitutionRule::EnableSilent);
    });

    return cachedBundle.get();
}

VendorProfile getMachineProfile(const std::string& vendorName, const std::string& machineModel, VendorProfile::PrinterModel& printerModel)
{
    std::string   profile_vendor_name;
    VendorProfile machineProfile;
    PresetBundle* bundle = getCachedSystemBundle();

    for (const auto& vendor : bundle->vendors) {
        const auto& vendor_profile = vendor.second;
        if (boost::to_upper_copy(vendor_profile.name) == boost::to_upper_copy(vendorName)) {
            // find the profile model name from the vendor profile
            // The profile model name may not contain the vendor name, so we need to add the vendor name
            for (const auto& model : vendor_profile.models) {
                std::string profileModelName     = boost::to_upper_copy(model.name);
                std::string discoverMachineModel = boost::to_upper_copy(machineModel);

                if (profileModelName.find(boost::to_upper_copy(vendorName)) == std::string::npos) {
                    profileModelName = boost::to_upper_copy(vendorName) + " " + profileModelName;
                }

                if (discoverMachineModel.find(boost::to_upper_copy(vendorName)) == std::string::npos) {
                    discoverMachineModel = boost::to_upper_copy(vendorName) + " " + discoverMachineModel;
                }

                if (profileModelName == discoverMachineModel) {
                    machineProfile = vendor_profile;
                    printerModel   = model;
                    break;
                }
            }
            break;
        }
    }
    return machineProfile;
}

void PrinterManager::validateAndCompletePrinterInfo(PrinterNetworkInfo& printerInfo)
{
    // Validate and update vendor and model info
    VendorProfile::PrinterModel printerModel;
    VendorProfile               machineProfile = getMachineProfile(printerInfo.vendor, printerInfo.printerModel, printerModel);

    if (machineProfile.name.empty()) {
        return; // No matching profile found
    }

    printerInfo.vendor = machineProfile.name;
    if (printerInfo.printerName.empty()) {
        printerInfo.printerName = printerModel.name;
    }
    printerInfo.printerModel = printerModel.name;

    // Update hostType to keep consistent with system preset
    auto vendorPrinterModelConfigMap = getVendorPrinterModelConfig();
    if (vendorPrinterModelConfigMap.find(printerInfo.vendor) != vendorPrinterModelConfigMap.end()) {
        auto modelConfigMap = vendorPrinterModelConfigMap[printerInfo.vendor];
        if (modelConfigMap.find(printerInfo.printerModel) != modelConfigMap.end()) {
            auto        config      = modelConfigMap[printerInfo.printerModel];
            const auto  opt         = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
            const auto  hostType    = opt != nullptr ? opt->value : htOctoPrint;
            std::string hostTypeStr = PrintHost::get_print_host_type_str(hostType);
            if (!hostTypeStr.empty()) {
                printerInfo.hostType = hostTypeStr;
            }
        }
    }
}

std::map<std::string, std::map<std::string, DynamicPrintConfig>> PrinterManager::getVendorPrinterModelConfig()
{
    // Cache the vendor printer model config - build only once
    static std::map<std::string, std::map<std::string, DynamicPrintConfig>> cachedConfig;
    static std::once_flag                                                   initFlag;

    std::call_once(initFlag, []() {
        PresetBundle* bundle = getCachedSystemBundle();

        for (const auto& vendor : bundle->vendors) {
            const std::string& vendorName = vendor.first;
            PresetBundle       vendorBundle;
            try {
                // load the vendor configs from the resources dir
                vendorBundle.load_vendor_configs_from_json((boost::filesystem::path(Slic3r::resources_dir()) / "profiles").string(),
                                                           vendorName, PresetBundle::LoadMachineOnly,
                                                           ForwardCompatibilitySubstitutionRule::EnableSilent, nullptr);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": load vendor %s error: %s") % vendorName % e.what();
                continue;
            }
            for (const auto& printer : vendorBundle.printers) {
                if (!printer.vendor) {
                    continue;
                }
                auto printerModel = printer.config.option<ConfigOptionString>("printer_model");
                if (!printerModel) {
                    continue;
                }

                std::string modelName = printerModel->value;
                if (PrintHost::support_device_list_management(printer.config)) {
                    cachedConfig[printer.vendor->name][modelName] = printer.config;
                }
            }
        }
    });

    return cachedConfig;
}

std::string PrinterManager::imageFileToBase64DataURI(const std::string& image_path)
{
    std::ifstream ifs(std::filesystem::u8path(image_path), std::ios::binary);
    if (!ifs)
        return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string img_data = oss.str();
    if (img_data.empty())
        return "";
    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(img_data.size()));
    encoded.resize(boost::beast::detail::base64::encode(&encoded[0], img_data.data(), img_data.size()));
    std::string ext = "png";
    size_t      dot = image_path.find_last_of('.');
    if (dot != std::string::npos) {
        ext = image_path.substr(dot + 1);
        boost::algorithm::to_lower(ext);
    }
    return "data:image/" + ext + ";base64," + encoded;
}

} // namespace Slic3r
