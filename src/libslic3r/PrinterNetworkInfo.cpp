#include <string>
#include <functional>

#include <vector>
#include <map>
#include "PrinterNetworkInfo.hpp"

namespace Slic3r {

PrinterNetworkInfo convertJsonToPrinterNetworkInfo(const nlohmann::json& json)
{
    PrinterNetworkInfo printerNetworkInfo;
    try {
        if (json.contains("printerId")) {
            printerNetworkInfo.printerId = json["printerId"];
        }
        if (json.contains("printerName")) {
            printerNetworkInfo.printerName = json["printerName"];
        }
        if (json.contains("host")) {
            printerNetworkInfo.host = json["host"];
        }
        if (json.contains("port")) {
            printerNetworkInfo.port = json["port"].get<int>();
        }
        if (json.contains("vendor")) {
            printerNetworkInfo.vendor = json["vendor"];
        }
        if (json.contains("printerModel")) {
            printerNetworkInfo.printerModel = json["printerModel"];
        }
        if (json.contains("protocolVersion")) {
            printerNetworkInfo.protocolVersion = json["protocolVersion"];
        }
        if (json.contains("firmwareVersion")) {
            printerNetworkInfo.firmwareVersion = json["firmwareVersion"];
        }
        if (json.contains("hostType")) {
            printerNetworkInfo.hostType = json["hostType"];
        }
        if (json.contains("mainboardId")) {
            printerNetworkInfo.mainboardId = json["mainboardId"];
        }
        if (json.contains("serialNumber")) {
            printerNetworkInfo.serialNumber = json["serialNumber"];
        }
        if (json.contains("username")) {
            printerNetworkInfo.username = json["username"];
        }
        if (json.contains("password")) {
            printerNetworkInfo.password = json["password"];
        }
        if (json.contains("authMode")) {
            printerNetworkInfo.authMode = json["authMode"];
        }
        if (json.contains("token")) {
            printerNetworkInfo.token = json["token"];
        }
        if (json.contains("accessCode")) {
            printerNetworkInfo.accessCode = json["accessCode"];
        }
        if (json.contains("networkType")) {
            printerNetworkInfo.networkType = static_cast<NetworkType>(json["networkType"].get<int>());
        }
        if (json.contains("pinCode")) {
            printerNetworkInfo.pinCode = json["pinCode"];
        }
        if (json.contains("webUrl")) {
            printerNetworkInfo.webUrl = json["webUrl"];
        }
        if (json.contains("extraInfo")) {
            printerNetworkInfo.extraInfo = json["extraInfo"].dump();
        } else {
            printerNetworkInfo.extraInfo = "{}";
        }
        if (json.contains("isPhysicalPrinter")) {
            printerNetworkInfo.isPhysicalPrinter = json["isPhysicalPrinter"].get<bool>();
        }
        if (json.contains("addTime")) {
            printerNetworkInfo.addTime = json["addTime"].get<uint64_t>();
        }
        if (json.contains("modifyTime")) {
            printerNetworkInfo.modifyTime = json["modifyTime"].get<uint64_t>();
        }
        if (json.contains("lastActiveTime")) {
            printerNetworkInfo.lastActiveTime = json["lastActiveTime"].get<uint64_t>();
        }
        if (json.contains("connectStatus")) {
            printerNetworkInfo.connectStatus = static_cast<PrinterConnectStatus>(json["connectStatus"].get<int>());
        }
        if (json.contains("printerStatus")) {
            printerNetworkInfo.printerStatus = static_cast<PrinterStatus>(json["printerStatus"].get<int>());
        }
        if (json.contains("printTask")) {
            if (json["printTask"].contains("taskId")) {
                printerNetworkInfo.printTask.taskId = json["printTask"]["taskId"];
            }
            if (json["printTask"].contains("fileName")) {
                printerNetworkInfo.printTask.fileName = json["printTask"]["fileName"];
            }
            if (json["printTask"].contains("totalTime")) {
                printerNetworkInfo.printTask.totalTime = json["printTask"]["totalTime"].get<int>();
            }
            if (json["printTask"].contains("currentTime")) {
                printerNetworkInfo.printTask.currentTime = json["printTask"]["currentTime"].get<int>();
            }
            if (json["printTask"].contains("estimatedTime")) {
                printerNetworkInfo.printTask.estimatedTime = json["printTask"]["estimatedTime"].get<int>();
            }
            if (json["printTask"].contains("progress")) {
                printerNetworkInfo.printTask.progress = json["printTask"]["progress"].get<int>();
            }
        }
        if (json.contains("printCapabilities")) {
            if (json["printCapabilities"].contains("supportsAutoBedLeveling")) {
                printerNetworkInfo.printCapabilities.supportsAutoBedLeveling = json["printCapabilities"]["supportsAutoBedLeveling"]
                                                                                   .get<bool>();
            }
            if (json["printCapabilities"].contains("supportsTimeLapse")) {
                printerNetworkInfo.printCapabilities.supportsTimeLapse = json["printCapabilities"]["supportsTimeLapse"].get<bool>();
            }
            if (json["printCapabilities"].contains("supportsHeatedBedSwitching")) {
                printerNetworkInfo.printCapabilities.supportsHeatedBedSwitching = json["printCapabilities"]["supportsHeatedBedSwitching"]
                                                                                      .get<bool>();
            }
            if (json["printCapabilities"].contains("supportsFilamentMapping")) {
                printerNetworkInfo.printCapabilities.supportsFilamentMapping = json["printCapabilities"]["supportsFilamentMapping"]
                                                                                   .get<bool>();
            }
            if (json["printCapabilities"].contains("supportsAutoRefill")) {
                printerNetworkInfo.printCapabilities.supportsAutoRefill = json["printCapabilities"]["supportsAutoRefill"].get<bool>();
            }
        }
        if (json.contains("systemCapabilities")) {
            if (json["systemCapabilities"].contains("supportsMultiFilament")) {
                printerNetworkInfo.systemCapabilities.supportsMultiFilament = json["systemCapabilities"]["supportsMultiFilament"].get<bool>();
            }
            if (json["systemCapabilities"].contains("canGetDiskInfo")) {
                printerNetworkInfo.systemCapabilities.canGetDiskInfo = json["systemCapabilities"]["canGetDiskInfo"].get<bool>();
            }
            if (json["systemCapabilities"].contains("canSetPrinterName")) {
                printerNetworkInfo.systemCapabilities.canSetPrinterName = json["systemCapabilities"]["canSetPrinterName"].get<bool>();
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to convert json to printer network info: " + std::string(e.what()));
    }
    return printerNetworkInfo;
}
nlohmann::json convertPrinterNetworkInfoToJson(const PrinterNetworkInfo& printerNetworkInfo)
{
    nlohmann::json json;
    json["printerId"]       = printerNetworkInfo.printerId;
    json["printerName"]     = printerNetworkInfo.printerName;
    json["host"]            = printerNetworkInfo.host;
    json["port"]            = printerNetworkInfo.port;
    json["vendor"]          = printerNetworkInfo.vendor;
    json["printerModel"]    = printerNetworkInfo.printerModel;
    json["protocolVersion"] = printerNetworkInfo.protocolVersion;
    json["firmwareVersion"] = printerNetworkInfo.firmwareVersion;
    json["hostType"]        = printerNetworkInfo.hostType;
    json["mainboardId"]     = printerNetworkInfo.mainboardId;
    json["serialNumber"]    = printerNetworkInfo.serialNumber;
    json["username"]        = printerNetworkInfo.username;
    json["password"]        = printerNetworkInfo.password;
    json["authMode"]        = printerNetworkInfo.authMode;
    json["token"]           = printerNetworkInfo.token;
    json["accessCode"]      = printerNetworkInfo.accessCode;
    json["pinCode"]         = printerNetworkInfo.pinCode;
    json["webUrl"]          = printerNetworkInfo.webUrl;
    json["networkType"]     = printerNetworkInfo.networkType;
    if (!printerNetworkInfo.extraInfo.empty()) {
        json["extraInfo"] = nlohmann::json::parse(printerNetworkInfo.extraInfo);
    } else {
        json["extraInfo"] = nlohmann::json::object();
    }
    json["isPhysicalPrinter"] = printerNetworkInfo.isPhysicalPrinter;
    json["addTime"]           = printerNetworkInfo.addTime;
    json["modifyTime"]        = printerNetworkInfo.modifyTime;
    json["lastActiveTime"]    = printerNetworkInfo.lastActiveTime;
    json["connectStatus"]     = printerNetworkInfo.connectStatus;
    json["printerStatus"]     = printerNetworkInfo.printerStatus;
    nlohmann::json printTaskJson;
    printTaskJson["taskId"]        = printerNetworkInfo.printTask.taskId;
    printTaskJson["fileName"]      = printerNetworkInfo.printTask.fileName;
    printTaskJson["totalTime"]     = printerNetworkInfo.printTask.totalTime;
    printTaskJson["currentTime"]   = printerNetworkInfo.printTask.currentTime;
    printTaskJson["estimatedTime"] = printerNetworkInfo.printTask.estimatedTime;
    printTaskJson["progress"]      = printerNetworkInfo.printTask.progress;
    json["printTask"]              = printTaskJson;
    nlohmann::json printCapabilitiesJson;
    printCapabilitiesJson["supportsAutoBedLeveling"]    = printerNetworkInfo.printCapabilities.supportsAutoBedLeveling;
    printCapabilitiesJson["supportsTimeLapse"]          = printerNetworkInfo.printCapabilities.supportsTimeLapse;
    printCapabilitiesJson["supportsHeatedBedSwitching"] = printerNetworkInfo.printCapabilities.supportsHeatedBedSwitching;
    printCapabilitiesJson["supportsFilamentMapping"]    = printerNetworkInfo.printCapabilities.supportsFilamentMapping;
    printCapabilitiesJson["supportsAutoRefill"]         = printerNetworkInfo.printCapabilities.supportsAutoRefill;
    json["printCapabilities"]                           = printCapabilitiesJson;
    nlohmann::json systemCapabilitiesJson;
    systemCapabilitiesJson["supportsMultiFilament"] = printerNetworkInfo.systemCapabilities.supportsMultiFilament;
    systemCapabilitiesJson["canGetDiskInfo"]        = printerNetworkInfo.systemCapabilities.canGetDiskInfo;
    systemCapabilitiesJson["canSetPrinterName"]     = printerNetworkInfo.systemCapabilities.canSetPrinterName;
    json["systemCapabilities"]                      = systemCapabilitiesJson;
    json["isAdded"]                                 = printerNetworkInfo.isAdded;
    return json;
}

PrinterMms convertJsonToPrinterMms(const nlohmann::json& json)
{
    PrinterMms mms;
    try {
        mms.mmsId       = json["mmsId"];
        mms.temperature = json["temperature"];
        mms.humidity    = json["humidity"];
        mms.connected   = json["connected"];
        for (auto& tray : json["trayList"]) {
            mms.trayList.push_back(convertJsonToPrinterMmsTray(tray));
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to convert json to printer mms: " + std::string(e.what()));
    }
    return mms;
}

nlohmann::json convertPrinterMmsToJson(const PrinterMms& mms)
{
    nlohmann::json json     = nlohmann::json::object();
    json["mmsId"]           = mms.mmsId;
    json["temperature"]     = mms.temperature;
    json["humidity"]        = mms.humidity;
    json["connected"]       = mms.connected;
    nlohmann::json trayList = nlohmann::json::array();
    for (auto& tray : mms.trayList) {
        nlohmann::json trayJson = convertPrinterMmsTrayToJson(tray);
        trayList.push_back(trayJson);
    }
    json["trayList"] = trayList;
    return json;
}

nlohmann::json convertPrinterMmsTrayToJson(const PrinterMmsTray& tray)
{
    nlohmann::json json        = nlohmann::json::object();
    json["trayId"]             = tray.trayId;
    json["mmsId"]              = tray.mmsId;
    json["trayName"]           = tray.trayName;
    json["settingId"]          = tray.settingId;
    json["filamentId"]         = tray.filamentId;
    json["from"]               = tray.from;
    json["vendor"]             = tray.vendor;
    json["serialNumber"]       = tray.serialNumber;
    json["filamentType"]       = tray.filamentType;
    json["filamentName"]       = tray.filamentName;
    json["filamentColor"]      = tray.filamentColor;
    json["filamentDiameter"]   = tray.filamentDiameter;
    json["filamentPresetName"] = tray.filamentPresetName;
    json["minNozzleTemp"]      = tray.minNozzleTemp;
    json["maxNozzleTemp"]      = tray.maxNozzleTemp;
    json["minBedTemp"]         = tray.minBedTemp;
    json["maxBedTemp"]         = tray.maxBedTemp;
    json["status"]             = tray.status;
    return json;
}

PrinterMmsTray convertJsonToPrinterMmsTray(const nlohmann::json& json)
{
    PrinterMmsTray tray;
    try {
        tray.trayId           = json["trayId"];
        tray.mmsId            = json["mmsId"];
        tray.trayName         = json["trayName"];
        tray.settingId        = json["settingId"];
        tray.filamentId       = json["filamentId"];
        tray.from             = json["from"];
        tray.vendor           = json["vendor"];
        tray.serialNumber     = json["serialNumber"];
        tray.filamentType     = json["filamentType"];
        tray.filamentName     = json["filamentName"];
        tray.filamentColor    = json["filamentColor"];
        tray.filamentDiameter = json["filamentDiameter"];
        tray.filamentPresetName = json["filamentPresetName"];
        tray.minNozzleTemp    = json["minNozzleTemp"];
        tray.maxNozzleTemp    = json["maxNozzleTemp"];
        tray.minBedTemp       = json["minBedTemp"];
        tray.maxBedTemp       = json["maxBedTemp"];
        tray.status           = json["status"];
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to convert json to printer mms tray: " + std::string(e.what()));
    }
    return tray;
}

nlohmann::json convertPrinterMmsGroupToJson(const PrinterMmsGroup& mmsGroup)
{
    nlohmann::json json    = nlohmann::json::object();
    json["connectNum"]     = mmsGroup.connectNum;
    json["connected"]      = mmsGroup.connected;
    json["activeMmsId"]    = mmsGroup.activeMmsId;
    json["activeTrayId"]   = mmsGroup.activeTrayId;
    json["autoRefill"]     = mmsGroup.autoRefill;
    json["mmsSystemName"]  = mmsGroup.mmsSystemName;
    json["vtTray"]         = convertPrinterMmsTrayToJson(mmsGroup.vtTray);
    nlohmann::json mmsList = nlohmann::json::array();
    for (auto& mms : mmsGroup.mmsList) {
        nlohmann::json mmsJson = convertPrinterMmsToJson(mms);
        mmsList.push_back(mmsJson);
    }
    json["mmsList"] = mmsList;
    return json;
}

PrinterMmsGroup convertJsonToPrinterMmsGroup(const nlohmann::json& json)
{
    PrinterMmsGroup mmsGroup;
    try {
        mmsGroup.connectNum    = json["connectNum"];
        mmsGroup.connected     = json["connected"];
        mmsGroup.activeMmsId   = json["activeMmsId"];
        mmsGroup.activeTrayId  = json["activeTrayId"];
        mmsGroup.autoRefill    = json["autoRefill"];
        mmsGroup.mmsSystemName = json["mmsSystemName"];
        mmsGroup.vtTray        = convertJsonToPrinterMmsTray(json["vtTray"]);
        if (json.contains("mmsList")) {
            for (auto& mms : json["mmsList"]) {
                mmsGroup.mmsList.push_back(convertJsonToPrinterMms(mms));
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to convert json to printer mms group: " + std::string(e.what()));
    }
    return mmsGroup;
}

nlohmann::json convertPrintFilamentMmsMappingToJson(const PrintFilamentMmsMapping& printFilamentMmsMapping)
{
    nlohmann::json json       = nlohmann::json::object();
    json["filamentId"]        = printFilamentMmsMapping.filamentId;
    json["filamentName"]      = printFilamentMmsMapping.filamentName;
    json["filamentAlias"]     = printFilamentMmsMapping.filamentAlias;
    json["filamentColor"]     = printFilamentMmsMapping.filamentColor;
    json["filamentType"]      = printFilamentMmsMapping.filamentType;
    json["filamentWeight"]    = printFilamentMmsMapping.filamentWeight;
    json["filamentDensity"]   = printFilamentMmsMapping.filamentDensity;
    json["index"]             = printFilamentMmsMapping.index;
    json["mappedMmsFilament"] = convertPrinterMmsTrayToJson(printFilamentMmsMapping.mappedMmsFilament);
    return json;
}

PrintFilamentMmsMapping convertJsonToPrintFilamentMmsMapping(const nlohmann::json& json)
{
    PrintFilamentMmsMapping printFilamentMmsMapping;
    try {
        printFilamentMmsMapping.filamentId        = json["filamentId"];
        printFilamentMmsMapping.filamentName      = json["filamentName"];
        printFilamentMmsMapping.filamentAlias     = json["filamentAlias"];
        printFilamentMmsMapping.filamentColor     = json["filamentColor"];
        printFilamentMmsMapping.filamentType      = json["filamentType"];
        printFilamentMmsMapping.filamentWeight    = json["filamentWeight"];
        printFilamentMmsMapping.filamentDensity   = json["filamentDensity"];
        printFilamentMmsMapping.index             = json["index"];
        printFilamentMmsMapping.mappedMmsFilament = convertJsonToPrinterMmsTray(json["mappedMmsFilament"]);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to convert json to print filament mms mapping: " + std::string(e.what()));
    }
    return printFilamentMmsMapping;
}

nlohmann::json convertUserNetworkInfoToJson(const UserNetworkInfo& userNetworkInfo)
{
    nlohmann::json json            = nlohmann::json::object();
    json["userId"]                 = userNetworkInfo.userId;
    json["username"]               = userNetworkInfo.username;
    json["token"]                  = userNetworkInfo.token;
    json["refreshToken"]           = userNetworkInfo.refreshToken;
    json["hostType"]               = userNetworkInfo.hostType;
    json["accessTokenExpireTime"]  = userNetworkInfo.accessTokenExpireTime;
    json["refreshTokenExpireTime"] = userNetworkInfo.refreshTokenExpireTime;
    json["rtcToken"]               = userNetworkInfo.rtcToken;
    json["rtcTokenExpireTime"]     = userNetworkInfo.rtcTokenExpireTime;
    json["nickname"]               = userNetworkInfo.nickname;
    json["email"]                  = userNetworkInfo.email;
    json["avatar"]                 = userNetworkInfo.avatar;
    json["openid"]                 = userNetworkInfo.openid;
    json["phone"]                  = userNetworkInfo.phone;
    json["region"]                = userNetworkInfo.region;
    json["language"]               = userNetworkInfo.language;
    json["createTime"]             = userNetworkInfo.createTime;
    json["loginTime"]              = userNetworkInfo.loginTime;
    json["loginStatus"]            = userNetworkInfo.loginStatus;
    json["lastTokenRefreshTime"]   = userNetworkInfo.lastTokenRefreshTime;
    json["extraInfo"]              = userNetworkInfo.extraInfo;
    json["loginErrorMessage"]      = userNetworkInfo.loginErrorMessage;
    return json;
}
UserNetworkInfo convertJsonToUserNetworkInfo(const nlohmann::json& json)
{
    UserNetworkInfo userNetworkInfo;
    if (json.contains("userId")) {
        userNetworkInfo.userId = json["userId"];
    }
    if (json.contains("username")) {
        userNetworkInfo.username = json["username"];
    }
    if (json.contains("token")) {
        userNetworkInfo.token = json["token"];
    }
    if (json.contains("refreshToken")) {
        userNetworkInfo.refreshToken = json["refreshToken"];
    }
    if (json.contains("hostType")) {
        userNetworkInfo.hostType = json["hostType"];
    }
    if (json.contains("accessTokenExpireTime")) {
        userNetworkInfo.accessTokenExpireTime = json["accessTokenExpireTime"];
    }
    if (json.contains("refreshTokenExpireTime")) {
        userNetworkInfo.refreshTokenExpireTime = json["refreshTokenExpireTime"];
    }
    if (json.contains("rtcToken")) {
        userNetworkInfo.rtcToken = json["rtcToken"];
    }
    if (json.contains("rtcTokenExpireTime")) {
        userNetworkInfo.rtcTokenExpireTime = json["rtcTokenExpireTime"];
    }
    if (json.contains("nickname")) {
        userNetworkInfo.rtcTokenExpireTime = json["rtcTokenExpireTime"];
    }
    if (json.contains("email")) {
        userNetworkInfo.email = json["email"];
    }
    if (json.contains("avatar")) {
        userNetworkInfo.avatar = json["avatar"];
    }
    if (json.contains("nickname")) {
        userNetworkInfo.nickname = json["nickname"];
    }
    if (json.contains("openid")) {
        userNetworkInfo.openid = json["openid"];
    }
    if (json.contains("phone")) {
        userNetworkInfo.phone = json["phone"];
    }
    if (json.contains("region")) {
        userNetworkInfo.region = json["region"];
    }
    if (json.contains("language")) {
        userNetworkInfo.language = json["language"];
    }
    if (json.contains("createTime")) {
        userNetworkInfo.createTime = json["createTime"];
    }
    if (json.contains("loginTime")) {
        userNetworkInfo.loginTime = json["loginTime"];
    }
    if (json.contains("loginStatus")) {
        userNetworkInfo.loginStatus = json["loginStatus"];
    }
    if (json.contains("lastTokenRefreshTime")) {
        userNetworkInfo.lastTokenRefreshTime = json["lastTokenRefreshTime"];
    }
    if (json.contains("extraInfo")) {
        userNetworkInfo.extraInfo = json["extraInfo"];
    }
    if (json.contains("loginErrorMessage")) {
        userNetworkInfo.loginErrorMessage = json["loginErrorMessage"];
    }
    return userNetworkInfo;
}

LoginStatus parseLoginStatusByErrorCode(PrinterNetworkErrorCode resultCode)
{
    switch (resultCode) {
    case PrinterNetworkErrorCode::SUCCESS:
        return LOGIN_STATUS_LOGIN_SUCCESS;   
    case PrinterNetworkErrorCode::SERVER_UNAUTHORIZED:
        // token expired, need to refresh, if refresh failed, need to re-login
        return LOGIN_STATUS_OFFLINE_TOKEN_EXPIRED;
    case PrinterNetworkErrorCode::NETWORK_ERROR:
         return LOGIN_STATUS_OFFLINE;
    default:
        return LOGIN_STATUS_OTHER_NETWORK_ERROR;
    }
}

} // namespace Slic3r
