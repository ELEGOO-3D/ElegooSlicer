#include "PrinterManager.hpp"
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
#include "ElegooLink.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/Utils/Elegoo/PrinterPluginManager.hpp"
#include "slic3r/Utils/Elegoo/UserNetworkManager.hpp"
#include "slic3r/Utils/Elegoo/MultiInstanceCoordinator.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"

#define CHECK_INITIALIZED(returnValue) \
    { \
        std::lock_guard<std::mutex> __initLock(mInitializedMutex); \
        if(!mIsInitialized.load()) { \
            using ValueType = std::decay_t<decltype(returnValue)>; \
            if(MultiInstanceCoordinator::getInstance()->isMaster()) { \
                return PrinterNetworkResult<ValueType>(PrinterNetworkErrorCode::PRINTER_NETWORK_NOT_INITIALIZED, returnValue); \
            } else { \
                return PrinterNetworkResult<ValueType>(PrinterNetworkErrorCode::NOT_MAIN_CLIENT, returnValue); \
            } \
        } \
    }

namespace Slic3r {

namespace fs = boost::filesystem;

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

PrinterManager::PrinterManager() {}

PrinterManager::~PrinterManager() {}

void PrinterManager::init()
{
    // Only master instance initializes network components
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mInitializedMutex);
    if (mIsInitialized) {
        return;
    }
     
    // connect status changed event
    PrinterNetworkEvent::getInstance()->connectStatusChanged.connect([this](const PrinterConnectStatusEvent& event) {
        // only update the connect status when the printer is disconnected
        if(event.status != PRINTER_CONNECT_STATUS_CONNECTED) {
            PrinterCache::getInstance()->updatePrinterConnectStatus(event.printerId, event.status);
        }
    });

    // printer status changed event
    PrinterNetworkEvent::getInstance()->statusChanged.connect(
        [this](const PrinterStatusEvent& event) { PrinterCache::getInstance()->updatePrinterStatus(event.printerId, event.status); });

    // printer print task changed event
    PrinterNetworkEvent::getInstance()->printTaskChanged.connect(
        [this](const PrinterPrintTaskEvent& event) { PrinterCache::getInstance()->updatePrinterPrintTask(event.printerId, event.task); });

    // printer attributes changed event
    PrinterNetworkEvent::getInstance()->attributesChanged.connect([this](const PrinterAttributesEvent& event) {
        PrinterCache::getInstance()->updatePrinterAttributesByNotify(event.printerId, event.printerInfo);
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

    monitorPrinterConnectionsRunning = true;
    mConnectionThread                = std::thread([this]() { monitorPrinterConnections(); });
    mWanPrinterConnectionThread      = std::thread([this]() { monitorWanPrinterConnections(); });
    mIsInitialized                   = true;
}

void PrinterManager::close()
{
    std::lock_guard<std::mutex> lock(mInitializedMutex);
    if (!mIsInitialized) {
        return;
    }
    mIsInitialized                   = false;
    monitorPrinterConnectionsRunning = false;
    // Disconnect all PrinterNetworkEvent signals
    PrinterNetworkEvent::getInstance()->connectStatusChanged.disconnectAll();
    PrinterNetworkEvent::getInstance()->statusChanged.disconnectAll();
    PrinterNetworkEvent::getInstance()->printTaskChanged.disconnectAll();
    PrinterNetworkEvent::getInstance()->attributesChanged.disconnectAll();

    // Wait for connection monitor thread to finish
    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }
    if (mWanPrinterConnectionThread.joinable()) {
        mWanPrinterConnectionThread.join();
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
}
PrinterNetworkResult<bool> PrinterManager::deletePrinter(const std::string& printerId)
{
    PrinterLock lock(printerId); // lock the printer to prevent the printer info from being modified

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
    PrinterLock lock(printerId); // lock the printer to prevent the printer info from being modified
    auto        printer = PrinterCache::getInstance()->getPrinter(printerId);
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
    PrinterLock                     lock(printerId);
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
    PrinterLock                     lock(printerId);
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
    bool               isUpdatePrinterName = false;
    PrinterNetworkInfo printer             = v.value();
    if (printer.printerName != printerInfo.printerName) {
        isUpdatePrinterName = true;
    }

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
    PrinterNetworkResult<bool> result = connectToPrinter(printer, isUpdatePrinterName);
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
    CHECK_INITIALIZED(false);
    // Use a static mutex to serialize printer addition to prevent race conditions
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

    // bind the printer if it is a WAN printer
    if (printerNetworkInfo.networkType == NETWORK_TYPE_WAN) {
        PrinterNetworkResult<PrinterNetworkInfo> bindResult = UserNetworkManager::getInstance()->bindWANPrinter(printerNetworkInfo);
        if (!bindResult.isSuccess()) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": add printer failed to bind WAN printer %s %s %s: %s") % printerNetworkInfo.host %
                                              printerNetworkInfo.printerName % printerNetworkInfo.printerModel % bindResult.message;
            return PrinterNetworkResult<bool>(bindResult.code, false, bindResult.message);
        }
        PrinterNetworkInfo boundPrinterNetworkInfo = bindResult.data.value();
        // update the printer network info with the bound printer network info
        printerNetworkInfo.printerId = boundPrinterNetworkInfo.printerId;
        printerNetworkInfo.serialNumber = boundPrinterNetworkInfo.serialNumber;
    }
    PrinterNetworkResult<bool> addResult = connectToPrinter(printerNetworkInfo, printerNetworkInfo.isPhysicalPrinter ? true : false);
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
    std::shared_ptr<IPrinterNetwork> network = NetworkFactory::createPrinterNetwork(printerNetworkInfo);
    if (!network) {
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }
    return network->cancelBindPrinter(printerNetworkInfo.serialNumber);
}
PrinterNetworkResult<std::vector<PrinterNetworkInfo>> PrinterManager::discoverPrinter()
{
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
    auto printers = PrinterCache::getInstance()->getPrinters();
    std::sort(printers.begin(), printers.end(),
              [](const PrinterNetworkInfo& a, const PrinterNetworkInfo& b) { return a.addTime < b.addTime; });
    return printers;
}

PrinterNetworkInfo PrinterManager::getPrinterNetworkInfo(const std::string& printerId)
{
    auto printer = PrinterCache::getInstance()->getPrinter(printerId);
    if (printer.has_value()) {
        return printer.value();
    }
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, printerId: %s") % printerId;
    return PrinterNetworkInfo();
}

PrinterNetworkResult<bool> PrinterManager::upload(PrinterNetworkParams& params)
{
    auto                       printer = PrinterCache::getInstance()->getPrinter(params.printerId);
    PrinterNetworkResult<bool> result(PrinterNetworkErrorCode::SUCCESS, false);
    bool                       isSendPrintTaskFailed = false;

    do {
        if (!printer.has_value()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not found, file name: %s") % params.fileName;
            result.code = PrinterNetworkErrorCode::PRINTER_NOT_FOUND;
            break;
        }
        if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer not connected, file name: %s") % params.fileName;
            result.code = PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR;
            break;
        }
        if(printer.value().networkType == NETWORK_TYPE_WAN) {
           try {
               boost::filesystem::path filePath(params.filePath);
               boost::uintmax_t fileSize = boost::filesystem::file_size(filePath);
               if(fileSize > 500 * 1024 * 1024) {
                   result = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::FILE_TOO_LARGE, false);
                   break;
               }
           } catch (const boost::filesystem::filesystem_error& e) {
               BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to get file size, path: %s, error: %s") % params.filePath % e.what();
               result = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::FILE_NOT_FOUND, false);
               break;
           }
        }

        std::shared_ptr<IPrinterNetwork> network = getPrinterNetwork(params.printerId);
        if (!network) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": no network connection for printer: %s") % params.printerId;
            result = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
            break;
        }

        // Record request context for WAN error checking
        UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
        result                          = network->sendPrintFile(params);
        if (result.isError()) {
            checkUserAuthStatus(printer.value(), result, requestUserInfo);
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": failed to send print file to printer %s %s %s %s") %
                                            network->getPrinterNetworkInfo().host % network->getPrinterNetworkInfo().printerName %
                                            network->getPrinterNetworkInfo().printerModel % result.message;
            break;
        }
        if (result.isSuccess()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                    << boost::format(": file upload success %s %s %s, file name: %s") % printer.value().host %
                                           printer.value().printerName % printer.value().printerModel % params.fileName;
            if (params.uploadAndStartPrint) {
                result = network->sendPrintTask(params);
                if (result.isError()) {
                    checkUserAuthStatus(printer.value(), result, requestUserInfo);
                    isSendPrintTaskFailed = true;
                }
            }
        }
    } while (0);

    if (result.isError()) {
        if (isSendPrintTaskFailed) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": failed to send print task after file upload %s %s %s, file name: %s, error: %s") %
                                            printer.value().host % printer.value().printerName % printer.value().printerModel %
                                            params.fileName % result.message;
        } else {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                     << boost::format(": failed to send print file %s %s %s, file name: %s, error: %s") %
                                            printer.value().host % printer.value().printerName % printer.value().printerModel %
                                            params.fileName % result.message;
        }
        if (params.errorFn) {
            std::string errorMessage = isSendPrintTaskFailed ? _u8L("Send print task failed") : _u8L("Send print file failed");
            params.errorFn(errorMessage + ", " + result.message);
        }
    }
    return result;
}

PrinterNetworkResult<PrinterMmsGroup> PrinterManager::getPrinterMmsInfo(const std::string& printerId)
{
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

void PrinterManager::refreshWanPrinters()
{
    std::lock_guard<std::mutex> lock(mWanPrintersMutex);

    auto printersResult = UserNetworkManager::getInstance()->getUserBoundPrinters();

    if (printersResult.isError()) {
        // if user network busy, skip refresh online printers
        if (printersResult.code == PrinterNetworkErrorCode::USER_NETWORK_BUSY) {
            return;
        }
        if(printersResult.code == PrinterNetworkErrorCode::NETWORK_ERROR) {
            //update all wan printers to disconnected
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
    // After binding succeeds in the add printer interface, online printers will be queried from IoT.
    // Both places executing cache addition will cause issues, so mutex lock is needed.
    std::lock_guard<std::mutex> addPrinterLock(mAddPrinterMutex);

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
                // // update the printer info if the printer is already exists
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

    std::vector<std::future<void>> addWanPrinterFutures;
    for (auto& wanPrinter : wanPrintersToAdd) {
        auto future = std::async(std::launch::async, [this, &wanPrinter]() {
            connectToPrinter(wanPrinter);
            PrinterCache::getInstance()->addPrinter(wanPrinter);
        });
        addWanPrinterFutures.push_back(std::move(future));
    }
    for (auto& future : addWanPrinterFutures) {
        future.wait();
    }
}

// first get selected printer by modelName and printerId
// if not found, get selected printer by modelName
// if not found, get selected printer by printerId
// if not found, return first printer
PrinterNetworkInfo PrinterManager::getSelectedPrinter(const std::string& printerModel, const std::string& printerId)
{
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
    return ElegooLink::getInstance()->getLicenseExpiredDevices();
}

PrinterNetworkResult<bool> PrinterManager::renewLicense(const std::string& serialNumber)
{
    if (serialNumber.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": serialNumber is empty";
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::INVALID_PARAMETER, false);
    }
    return ElegooLink::getInstance()->renewLicense(serialNumber);
}

PrinterNetworkResult<bool> PrinterManager::refreshPrinterStatus(const std::string& printerId)
{
    if (printerId.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": printerId is empty";
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::INVALID_PARAMETER, false);
    }
    return ElegooLink::getInstance()->refreshPrinterStatus(printerId);
}

PrinterNetworkResult<std::string> PrinterManager::getPrinterStatusRaw(const std::string& printerId)
{
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
    int loopIntervalSeconds = 10;
    mLastConnectionLoopTime = std::chrono::steady_clock::now() - std::chrono::seconds(loopIntervalSeconds);
    while (monitorPrinterConnectionsRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - mLastConnectionLoopTime).count();

        if (elapsed < loopIntervalSeconds) {
            continue;
        }
        auto                           printerList = PrinterCache::getInstance()->getPrinters();
        std::vector<std::future<void>> connectionFutures;
        for (auto& printer : printerList) {
            if (printer.networkType == NETWORK_TYPE_WAN) {
                continue;
            }
            if (printer.connectStatus == PRINTER_CONNECT_STATUS_CONNECTED) {
                if(getPrinterNetwork(printer.printerId)) {      
                    continue;
                }
                PrinterCache::getInstance()->updatePrinterConnectStatus(printer.printerId, PRINTER_CONNECT_STATUS_DISCONNECTED);
            }
            if (printer.printerStatus == PRINTER_STATUS_ID_NOT_MATCH || printer.printerStatus == PRINTER_STATUS_AUTH_ERROR) {
                continue;
            }
            auto future = std::async(std::launch::async, [this, &printer]() {
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
        mLastConnectionLoopTime = now;
    }
}

void PrinterManager::monitorWanPrinterConnections()
{
    int loopIntervalSeconds    = 10;
    mLastWanConnectionLoopTime = std::chrono::steady_clock::now() - std::chrono::seconds(loopIntervalSeconds);

    while (monitorPrinterConnectionsRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - mLastWanConnectionLoopTime).count();

        if (elapsed < loopIntervalSeconds) {
            continue;
        }

        refreshWanPrinters();

        auto printerList = PrinterCache::getInstance()->getPrinters();

        std::vector<std::future<void>> connectionFutures;
        for (auto& printer : printerList) {
            if (printer.networkType != NETWORK_TYPE_WAN) {
                continue;
            }
            if (printer.connectStatus == PRINTER_CONNECT_STATUS_CONNECTED) {
                if(getPrinterNetwork(printer.printerId)) {      
                    continue;
                }
                PrinterCache::getInstance()->updatePrinterConnectStatus(printer.printerId, PRINTER_CONNECT_STATUS_DISCONNECTED);
            }
            if (printer.printerStatus == PRINTER_STATUS_ID_NOT_MATCH || printer.printerStatus == PRINTER_STATUS_AUTH_ERROR) {
                continue;
            }
            auto future = std::async(std::launch::async, [this, &printer]() {
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
        mLastWanConnectionLoopTime = now;
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
            network->disconnectFromPrinter();
            printer.connectStatus = PRINTER_CONNECT_STATUS_DISCONNECTED;
            return PrinterNetworkResult<bool>(updateNameResult.code, false, updateNameResult.message);
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
    // network->getPrinterStatus();
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
} // namespace Slic3r
