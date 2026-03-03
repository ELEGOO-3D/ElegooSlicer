#include "PrinterMmsManager.hpp"
#include "slic3r/Utils/Elegoo/PrinterManager.hpp"
#include <nlohmann/json.hpp>
#include <wx/colour.h>
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <limits>

namespace Slic3r {

struct StandardColor
{
    std::string colorName;
    std::string colorHex;
};


struct LabColor {
    double l, a, b;
    LabColor(double lVal = 0, double aVal = 0, double bVal = 0) : l(lVal), a(aVal), b(bVal) {}
};


struct XyzColor {
    double x, y, z;
    XyzColor(double xVal = 0, double yVal = 0, double zVal = 0) : x(xVal), y(yVal), z(zVal) {}
};


static LabColor xyzToLab(const XyzColor& xyz)
{
    // D65 illuminant
    const double xn = 95.047;
    const double yn = 100.000;
    const double zn = 108.883;
    
    double fx = (xyz.x / xn > 0.008856) ? pow(xyz.x / xn, 1.0/3.0) : (7.787 * xyz.x / xn + 16.0/116.0);
    double fy = (xyz.y / yn > 0.008856) ? pow(xyz.y / yn, 1.0/3.0) : (7.787 * xyz.y / yn + 16.0/116.0);
    double fz = (xyz.z / zn > 0.008856) ? pow(xyz.z / zn, 1.0/3.0) : (7.787 * xyz.z / zn + 16.0/116.0);
    
    double l = 116 * fy - 16;
    double a = 500 * (fx - fy);
    double b = 200 * (fy - fz);
    
    return LabColor(l, a, b);
}
struct StandardColorLab
{
    StandardColor color;
    LabColor lab;
};

struct RgbColor {
    int r, g, b;
    RgbColor(int red = 0, int green = 0, int blue = 0) : r(red), g(green), b(blue) {}
};

static bool isValidHex6(const std::string& h)
{
    if (h.length() != 6) {
        return false;
    }
    for (char c : h) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

static RgbColor hexToRgb(const std::string& hex)
{
    std::string h = hex;
    if (!h.empty() && h[0] == '#') {
        h = h.substr(1);
    }
    
    if (isValidHex6(h)) {
        try {
            int r = std::stoi(h.substr(0, 2), nullptr, 16);
            int g = std::stoi(h.substr(2, 2), nullptr, 16);
            int b = std::stoi(h.substr(4, 2), nullptr, 16);
            return RgbColor(r, g, b);
        } catch (...) {
            // Invalid hex format, return black as fallback
            return RgbColor(0, 0, 0);
        }
    }
    
    // Invalid hex format, return black as fallback
    return RgbColor(0, 0, 0);
}

static XyzColor rgbToXyz(const RgbColor& rgb)
{
    double rNorm = rgb.r / 255.0;
    double gNorm = rgb.g / 255.0;
    double bNorm = rgb.b / 255.0;
    
    // Apply gamma correction
    rNorm = (rNorm > 0.04045) ? pow((rNorm + 0.055) / 1.055, 2.4) : rNorm / 12.92;
    gNorm = (gNorm > 0.04045) ? pow((gNorm + 0.055) / 1.055, 2.4) : gNorm / 12.92;
    bNorm = (bNorm > 0.04045) ? pow((bNorm + 0.055) / 1.055, 2.4) : bNorm / 12.92;
    
    // Convert to XYZ using sRGB matrix
    double x = rNorm * 0.4124564 + gNorm * 0.3575761 + bNorm * 0.1804375;
    double y = rNorm * 0.2126729 + gNorm * 0.7151522 + bNorm * 0.0721750;
    double z = rNorm * 0.0193339 + gNorm * 0.1191920 + bNorm * 0.9503041;
    
    // Scale by 100
    return XyzColor(x * 100, y * 100, z * 100);
}

// Delta E 2000 calculation
static double deltaE2000(const LabColor& lab1, const LabColor& lab2)
{
    // Step 1: Calculate C1, C2
    double c1 = std::sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
    double c2 = std::sqrt(lab2.a * lab2.a + lab2.b * lab2.b);
    double cBar = (c1 + c2) / 2.0;

    // Step 2: G factor
    double cBar7 = std::pow(cBar, 7);
    double g = 0.5 * (1.0 - std::sqrt(cBar7 / (cBar7 + std::pow(25.0, 7))));

    // Step 3: a'1, a'2
    double a1Prime = (1.0 + g) * lab1.a;
    double a2Prime = (1.0 + g) * lab2.a;

    // Step 4: C'1, C'2
    double c1Prime = std::sqrt(a1Prime * a1Prime + lab1.b * lab1.b);
    double c2Prime = std::sqrt(a2Prime * a2Prime + lab2.b * lab2.b);

    // Step 5: h'1, h'2 (in degrees)
    auto calcHuePrime = [](double a, double b) {
        double h = std::atan2(b, a) * 180.0 / M_PI;
        return (h < 0) ? (h + 360.0) : h;
    };

    double h1Prime = calcHuePrime(a1Prime, lab1.b);
    double h2Prime = calcHuePrime(a2Prime, lab2.b);

    // Step 6: ΔL', ΔC'
    double deltaLPrime = lab2.l - lab1.l;
    double deltaCPrime = c2Prime - c1Prime;

    // Step 7: Δh'
    double deltahPrime = 0.0;
    double hDiff = h2Prime - h1Prime;
    if (c1Prime * c2Prime == 0) {
        deltahPrime = 0;
    } else {
        if (std::fabs(hDiff) <= 180.0) {
            deltahPrime = hDiff;
        } else if (hDiff > 180.0) {
            deltahPrime = hDiff - 360.0;
        } else {
            deltahPrime = hDiff + 360.0;
        }
    }

    // Step 8: ΔH'
    double deltaHPrime = 2.0 * std::sqrt(c1Prime * c2Prime) *
                         std::sin((deltahPrime * M_PI) / 360.0);

    // Step 9: L', C', h' averages
    double lBarPrime = (lab1.l + lab2.l) / 2.0;
    double cBarPrime = (c1Prime + c2Prime) / 2.0;
    double hBarPrime = 0.0;
    if (c1Prime * c2Prime == 0) {
        hBarPrime = h1Prime + h2Prime;
    } else {
        if (std::fabs(hDiff) <= 180.0) {
            hBarPrime = (h1Prime + h2Prime) / 2.0;
        } else {
            hBarPrime = (h1Prime + h2Prime + 360.0) / 2.0;
        }
    }
    if (hBarPrime >= 360.0) hBarPrime -= 360.0;

    // Step 10: T
    double T = 1.0 - 0.17 * std::cos((hBarPrime - 30.0) * M_PI / 180.0)
                  + 0.24 * std::cos((2.0 * hBarPrime) * M_PI / 180.0)
                  + 0.32 * std::cos((3.0 * hBarPrime + 6.0) * M_PI / 180.0)
                  - 0.20 * std::cos((4.0 * hBarPrime - 63.0) * M_PI / 180.0);

    // Step 11: Δθ
    double deltaTheta = 30.0 * std::exp(-std::pow((hBarPrime - 275.0) / 25.0, 2.0));

    // Step 12: R_C
    double cBarPrime7 = std::pow(cBarPrime, 7);
    double rC = 2.0 * std::sqrt(cBarPrime7 / (cBarPrime7 + std::pow(25.0, 7)));

    // Step 13: S_L, S_C, S_H
    double sL = 1.0 + (0.015 * std::pow(lBarPrime - 50.0, 2.0)) /
                        std::sqrt(20.0 + std::pow(lBarPrime - 50.0, 2.0));
    double sC = 1.0 + 0.045 * cBarPrime;
    double sH = 1.0 + 0.015 * cBarPrime * T;

    // Step 14: R_T
    double rT = -std::sin((2.0 * deltaTheta) * M_PI / 180.0) * rC;

    // Step 15: ΔE00
    double kL = 1.0, kC = 1.0, kH = 1.0; // Standard observer
    double deltaE = std::sqrt(
        std::pow(deltaLPrime / (kL * sL), 2.0) +
        std::pow(deltaCPrime / (kC * sC), 2.0) +
        std::pow(deltaHPrime / (kH * sH), 2.0) +
        rT * (deltaCPrime / (kC * sC)) * (deltaHPrime / (kH * sH))
    );

    return deltaE;
}

static const std::vector<StandardColor> standardColors = {{"white", "#FFFFFF"},
                                                          {"yellow", "#FFF242"},
                                                          {"light_yellow_green", "#DBF47A"},
                                                          {"green", "#09CC3A"},
                                                          {"dark_green", "#077747"},
                                                          {"blue_green", "#0B6283"},
                                                          {"cyan_green", "#0BE2A0"},
                                                          {"sky_blue", "#74D9F3"},
                                                          {"light_blue", "#48A7FA"},
                                                          {"dark_blue", "#2850DF"},
                                                          {"purple", "#433089"},
                                                          {"light_purple", "#A03BF7"},
                                                          {"pink_purple", "#F32FF8"},
                                                          {"light_pink_purple", "#D4B1DD"},
                                                          {"pink", "#F95D77"},
                                                          {"red", "#F72221"},
                                                          {"brown", "#7C4C00"},
                                                          {"orange", "#F88D36"},
                                                          {"beige", "#FCEBD7"},
                                                          {"light_brown", "#D2C5A3"},
                                                          {"dark_brown", "#AF7832"},
                                                          {"dark_gray", "#898989"},
                                                          {"light_gray", "#BCBCBC"},
                                                          {"black", "#000000"}};

// Pre-compute Lab values for all standard colors to improve performance
static const std::vector<StandardColorLab> standardColorLabs = []() {
    std::vector<StandardColorLab> v;
    v.reserve(standardColors.size());
    for (const auto& sc : standardColors) {
        RgbColor rgb = hexToRgb(sc.colorHex);
        LabColor lab = xyzToLab(rgbToXyz(rgb));
        v.push_back({sc, lab});
    }
    return v;
}();

// ΔE2000 (CIEDE2000) color match
// 0-1 barely noticeable to the human eye
// 1-2 noticeable only when compared closely
// 2-10 noticeable to the average person
// 11-49 noticeably different
// 50+ completely different colors

static StandardColor getStandardColor(const std::string& hex)
{
    // Convert input to Lab
    RgbColor rgb = hexToRgb(hex);
    XyzColor xyz = rgbToXyz(rgb);
    LabColor inputLab = xyzToLab(xyz);
    
    double minDeltaE = std::numeric_limits<double>::max();
    const StandardColor* bestMatch = nullptr;
    
    // Iterate through pre-computed standard color Labs
    for (const auto& sc : standardColorLabs) {
        double dE = deltaE2000(inputLab, sc.lab);
        
        if (dE < minDeltaE) {
            minDeltaE = dE;
            bestMatch = &sc.color;
        }
    }
    
    // Always return the best match, regardless of ΔE value
    // ΔE can be used as confidence indicator:
    // - deltaE < 2: barely noticeable
    // - deltaE < 10: noticeable to average person
    // - deltaE >= 30: obviously different
    // - deltaE >= 50: completely different
    return bestMatch != nullptr ? *bestMatch : StandardColor{"", ""};
}

// standardize filament name for matching: remove vendor/generic prefixes, replace dashes with spaces
static std::string standardizeFilamentName(const std::string& name, const std::string& vendor)
{
    std::string standardized = boost::to_upper_copy(name);
    boost::trim(standardized);
    
    // remove vendor prefix if exists
    std::string vendorUpper = boost::to_upper_copy(vendor);
    boost::trim(vendorUpper);
    if(!vendorUpper.empty() && standardized.find(vendorUpper) != std::string::npos) {
        boost::erase_all(standardized, vendorUpper);
        boost::trim(standardized);
    }
    
    // remove GENERIC prefix if exists
    if(standardized.find("GENERIC") != std::string::npos) {
        boost::erase_all(standardized, "GENERIC");
        boost::trim(standardized);
    }
    
    // remove @ suffix if exists
    size_t atPos = standardized.find('@');
    if(atPos != std::string::npos) {
        standardized = standardized.substr(0, atPos);
        boost::trim(standardized);
    }
    
    // replace all dashes with spaces
    std::replace(standardized.begin(), standardized.end(), '-', ' ');
    boost::trim(standardized);
    
    return standardized;
}

PrinterMmsManager::PrinterMmsManager() {}

PrinterMmsManager::~PrinterMmsManager() {}


// find mms tray filament id by matching standardized filament name
// builds a merged preset map containing vendor specific, vendor generic, and orca generic filaments
// matches by standardized name (removes vendor/generic prefixes, replaces dashes with spaces)
// if multiple candidates found, selects the one with highest priority (lowest priority number)

void PrinterMmsManager::getMmsTrayFilamentId(const PrinterNetworkInfo& printerNetworkInfo, PrinterMmsGroup& mmsGroup)
{
    std::vector<double> currentProjectNozzleDiameters = {0.4};
    try {
        auto& app = GUI::wxGetApp();
        if(app.preset_bundle) {
            auto nozzleDiameterOpt = app.preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
            if(nozzleDiameterOpt && !nozzleDiameterOpt->values.empty()) {
                currentProjectNozzleDiameters = nozzleDiameterOpt->values;
            }
        }
    } catch(...) {
    }
    
    // build merged preset map with all filament types (vendor specific, vendor generic, orca generic)
    auto mergedPresetMap = buildPresetFilamentMap(printerNetworkInfo, currentProjectNozzleDiameters);

    // match filament id in system preset to mms tray by standardized name
    // if multiple candidates found, select the one with highest priority (lowest priority number)
    for(auto& mms : mmsGroup.mmsList) {
        for(auto& tray : mms.trayList) {
            if(!checkTrayIsReady(tray)) {
                continue;
            }
            
            PresetFilamentInfo matchedPreset = matchFilamentPreset(tray.filamentName, mergedPresetMap, printerNetworkInfo, tray.filamentType);
            if(!matchedPreset.filamentName.empty()) {
                tray.filamentId = matchedPreset.filamentId;
                tray.settingId = matchedPreset.settingId;
                tray.filamentPresetName = matchedPreset.filamentName;
                continue;
            }
        }
    }
    
    return;
}

// build preset filament map by standardized name
// loads vendor bundle and orca filament library, processes all filament types and returns merged map
std::map<std::string, std::vector<PrinterMmsManager::PresetFilamentInfo>> PrinterMmsManager::buildPresetFilamentMap(
    const PrinterNetworkInfo& printerNetworkInfo,
    const std::vector<double>& currentProjectNozzleDiameters)
{
    std::map<std::string, std::vector<PresetFilamentInfo>> mergedPresetMap;
    
    // load vendor bundle
    PresetBundle vendorBundle;
    try {
        vendorBundle.load_vendor_configs_from_json((boost::filesystem::path(Slic3r::resources_dir()) / "profiles").string(),
                                                   printerNetworkInfo.vendor, PresetBundle::LoadSystem,
                                                   ForwardCompatibilitySubstitutionRule::EnableSilent, nullptr);                                        

    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "PrinterMmsManager::buildPresetFilamentMap: get vendor configs failed: " << printerNetworkInfo.vendor << " " << e.what();
        return mergedPresetMap;
    }

    
    // load orca filament library
    PresetBundle orcaFilamentLibraryBundle;
    try {
        orcaFilamentLibraryBundle.load_vendor_configs_from_json((boost::filesystem::path(Slic3r::resources_dir()) / "profiles").string(),
                                                   PresetBundle::ORCA_FILAMENT_LIBRARY, PresetBundle::LoadSystem,
                                                   ForwardCompatibilitySubstitutionRule::EnableSilent, nullptr);                                        

    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "PrinterMmsManager::buildPresetFilamentMap: get orca filament library failed: " << e.what();
    }
    // build printer preset map from vendor bundle
    std::map<std::string, PrinterPresetInfo> printerPresetMap;
    for(const auto& printer : vendorBundle.printers) {
        if(!printer.is_system) continue;
        
        PrinterPresetInfo info;
        auto printerModelOpt = printer.config.option<ConfigOptionString>("printer_model");
        if(printerModelOpt) {
            info.printerModel = printerModelOpt->value;
        }
        
        auto nozzleDiameterOpt = printer.config.option<ConfigOptionFloats>("nozzle_diameter");
        if(nozzleDiameterOpt) {
            info.nozzleDiameters = nozzleDiameterOpt->values;
        }
        
        printerPresetMap[printer.name] = info;
    }

    // process vendor specific filaments (priority 1)
    processFilamentsFromBundle(vendorBundle, mergedPresetMap, printerNetworkInfo, printerPresetMap, currentProjectNozzleDiameters, 1, false, true);

    // process vendor generic filaments (priority 2)
    processFilamentsFromBundle(vendorBundle, mergedPresetMap, printerNetworkInfo, printerPresetMap, currentProjectNozzleDiameters, 2, true, true);

    // process orca generic filaments (priority 3)
    processFilamentsFromBundle(orcaFilamentLibraryBundle, mergedPresetMap, printerNetworkInfo, printerPresetMap, currentProjectNozzleDiameters, 3, true, false);
    
    return mergedPresetMap;
}

// process filaments from bundle and add to merged preset map
void PrinterMmsManager::processFilamentsFromBundle(
    const PresetBundle& bundle,
    std::map<std::string, std::vector<PresetFilamentInfo>>& mergedPresetMap,
    const PrinterNetworkInfo& printerNetworkInfo,
    const std::map<std::string, PrinterPresetInfo>& printerPresetMap,
    const std::vector<double>& currentProjectNozzleDiameters,
    int priority,
    bool isGeneric,
    bool checkCompatible)
{
    for (const auto& filament : bundle.filaments) {
        if (!filament.is_system) continue;
        
        auto* filament_type_opt = dynamic_cast<const ConfigOptionStrings*>(filament.config.option("filament_type"));
        if(!filament_type_opt || filament_type_opt->values.empty()) continue;

        if(checkCompatible && !isFilamentCompatible(filament, printerNetworkInfo, printerPresetMap, currentProjectNozzleDiameters)) continue;
        
        std::string name = boost::to_upper_copy(filament.name);
        bool isGenericFilament = (name.find("GENERIC") != std::string::npos);
        if(isGeneric != isGenericFilament) continue;
        
        PresetFilamentInfo info;
        info.filamentId = filament.filament_id;
        info.settingId = filament.setting_id;
        info.filamentAlias = filament.alias;
        info.filamentName = filament.name;
        info.filamentType = filament_type_opt->values[0];
        info.priority = priority;
        
        std::string standardizedName = standardizeFilamentName(filament.alias, printerNetworkInfo.vendor);
        mergedPresetMap[standardizedName].push_back(info);
    }
}

// check filament compatible
bool PrinterMmsManager::isFilamentCompatible(
    const Preset& filament,
    const PrinterNetworkInfo& printerNetworkInfo,
    const std::map<std::string, PrinterPresetInfo>& printerPresetMap,
    const std::vector<double>& currentProjectNozzleDiameters)
{
    const auto compatiblePrinters = filament.config.option<ConfigOptionStrings>("compatible_printers");
    if(!compatiblePrinters) return false;
    
    for (const std::string& printer_name : compatiblePrinters->values) {
        auto it = printerPresetMap.find(printer_name);
        if(it == printerPresetMap.end() || it->second.printerModel != printerNetworkInfo.printerModel) continue;
        
        if(currentProjectNozzleDiameters.empty()) return true;
        
        for(double currentProjectNozzleDiameter : currentProjectNozzleDiameters) {
            for(double presetNozzle : it->second.nozzleDiameters) {
                if(std::abs(currentProjectNozzleDiameter - presetNozzle) < 0.01) return true;
            }
        }
        return false;
    }
    return false;
}



PrinterNetworkResult<PrinterMmsGroup> PrinterMmsManager::getPrinterMmsInfo(const std::string& printerId)
{
    PrinterNetworkInfo printerNetworkInfo = PrinterManager::getInstance()->getPrinterNetworkInfo(printerId);
    if(printerNetworkInfo.printerId.empty()) {
        return PrinterNetworkResult<PrinterMmsGroup>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND, PrinterMmsGroup());
    }
    PrinterNetworkResult<PrinterMmsGroup> mmsGroupResult = PrinterManager::getInstance()->getPrinterMmsInfo(printerId);
    if(!mmsGroupResult.isSuccess()) {
        return mmsGroupResult;
    }
    PrinterMmsGroup mmsGroup = mmsGroupResult.data.value();

    getMmsTrayFilamentId(printerNetworkInfo, mmsGroup);
    
    // match filament id in system preset to mms tray
    // set tray index
    std::vector<std::string> trayIndexList = {"A","B","C","D","E","F","G","H"};
    int mmsIndex = 0;
    for(auto& mms : mmsGroup.mmsList) {
        if(mmsIndex >= trayIndexList.size()) {
            BOOST_LOG_TRIVIAL(error) << "PrinterMmsManager::getPrinterMmsInfo: tray index list is not enough";
            break;
        }
        std::string mmsIndexStr =trayIndexList[mmsIndex];
        int trayIndex = 1;
        mmsIndex++;
        for(auto& tray : mms.trayList) {
            tray.trayName = mmsIndexStr + std::to_string(trayIndex);
            trayIndex++;        
        }
    }
    return PrinterNetworkResult<PrinterMmsGroup>(PrinterNetworkErrorCode::SUCCESS, mmsGroup);
}

bool PrinterMmsManager::checkTrayIsReady(const PrinterMmsTray& tray) {
    if(tray.status != TRAY_STATUS_LOADED && tray.status != TRAY_STATUS_PRELOADED) {
        return false;
    }
    if(tray.filamentType.empty() || tray.filamentName.empty() || tray.filamentColor.empty()) {
        return false;
    }
    return true;
}

// find best match by standardized name, fallback to name equals type
PrinterMmsManager::PresetFilamentInfo PrinterMmsManager::matchFilamentPreset(
    const std::string& filamentName,
    const std::map<std::string, std::vector<PresetFilamentInfo>>& presetMap,
    const PrinterNetworkInfo& printerNetworkInfo,
    const std::string& filamentType)
{
    if(filamentName.empty()) {
        return PresetFilamentInfo();
    }
    
    std::string standardizedName = standardizeFilamentName(filamentName, printerNetworkInfo.vendor);
    std::string standardizedType = "";
    if(!filamentType.empty()) {
        standardizedType = boost::to_upper_copy(filamentType);
        boost::trim(standardizedType);
    }
    
    auto it = presetMap.find(standardizedName);
    if(it != presetMap.end() && !it->second.empty()) {
        // find the candidate with lowest priority number (highest priority)
        PresetFilamentInfo matchedPreset = it->second[0];
        for(const auto& candidate : it->second) {
            if(candidate.priority < matchedPreset.priority) {
                matchedPreset = candidate;
            }
        }
        return matchedPreset;
    }
    
    // fallback: match by name equals type if name matching failed
    if(standardizedType.empty()) {
        return PresetFilamentInfo();
    }
    
    PresetFilamentInfo bestMatch;
    bestMatch.priority = std::numeric_limits<int>::max();
    PresetFilamentInfo fallbackMatch;
    fallbackMatch.priority = std::numeric_limits<int>::max();
    
    for(const auto& entry : presetMap) {
        for(const auto& preset : entry.second) {
            std::string presetStandardizedName = standardizeFilamentName(preset.filamentAlias, printerNetworkInfo.vendor);
            std::string presetType = boost::to_upper_copy(preset.filamentType);
            boost::trim(presetType);
            
            // match by name equals type
            if(standardizedType == presetStandardizedName || standardizedName == presetType) {
                if(bestMatch.priority > preset.priority) {
                    bestMatch = preset;
                }
                if(bestMatch.priority == 1) {
                    return bestMatch;
                }
                continue;
            }
            
            // fallback: match by same filament type
            if(presetType == standardizedType && fallbackMatch.priority > preset.priority) {
                fallbackMatch = preset;
            }
        }
    }
    return bestMatch.filamentName.empty() ? fallbackMatch : bestMatch;
}

void PrinterMmsManager::getFilamentMmsMapping(const PrinterNetworkInfo& printerNetworkInfo, std::vector<PrintFilamentMmsMapping>& printFilamentMmsMapping, const PrinterMmsGroup& mmsGroup)
{
    // Load mapping from JSON file
    nlohmann::json mappingJson = loadFilamentMmsMappingFromFile();
    
    for (auto& printFilament : printFilamentMmsMapping) {
        StandardColor standardColor = getStandardColor(printFilament.filamentColor);
        std::string filamentStandardColor = standardColor.colorHex;
        std::string StandardColorName = standardColor.colorName;
        if(StandardColorName.empty() || filamentStandardColor.empty()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": standard color name or filament standard color is empty, filamentType: %s, filamentAlias: %s, filamentColor: %s")
                                                      % printFilament.filamentType % printFilament.filamentAlias % printFilament.filamentColor;
            continue;
        }
        std::string mmsMappingFilamentType = "";
        std::string mmsMappingFilamentName = "";
        std::string mmsMappingFilamentColor = "";
        bool isMapped = false;
        
        // Check if mapping exists in JSON
        if (mappingJson.contains(printFilament.filamentType) &&
            mappingJson[printFilament.filamentType].contains(printFilament.filamentAlias) &&
            mappingJson[printFilament.filamentType][printFilament.filamentAlias].contains(filamentStandardColor)) {
            
            auto mapping = mappingJson[printFilament.filamentType][printFilament.filamentAlias][filamentStandardColor];
            mmsMappingFilamentType = mapping.value("mappedFilamentType", "");
            mmsMappingFilamentName = mapping.value("mappedFilamentName", "");
            mmsMappingFilamentColor = mapping.value("mappedFilamentColor", "");
        }
        if (!mmsMappingFilamentType.empty() && !mmsMappingFilamentName.empty() && !mmsMappingFilamentColor.empty()) {
            for (auto& mms : mmsGroup.mmsList) {
                for (auto& tray : mms.trayList) {
                    if(!checkTrayIsReady(tray)) {
                        continue;
                    }
                    if (boost::to_upper_copy(tray.filamentType) == boost::to_upper_copy(mmsMappingFilamentType) &&
                        boost::to_upper_copy(tray.filamentName) == boost::to_upper_copy(mmsMappingFilamentName) &&
                        boost::to_upper_copy(tray.filamentColor) == boost::to_upper_copy(mmsMappingFilamentColor)) {
                        printFilament.mappedMmsFilament.trayId           = tray.trayId;
                        printFilament.mappedMmsFilament.mmsId            = tray.mmsId;
                        printFilament.mappedMmsFilament.trayName         = tray.trayName;
                        printFilament.mappedMmsFilament.filamentType     = tray.filamentType;
                        printFilament.mappedMmsFilament.filamentName     = tray.filamentName;
                        printFilament.mappedMmsFilament.filamentColor    = tray.filamentColor;
                        printFilament.mappedMmsFilament.filamentDiameter = tray.filamentDiameter;
                        printFilament.mappedMmsFilament.minNozzleTemp    = tray.minNozzleTemp;
                        printFilament.mappedMmsFilament.maxNozzleTemp    = tray.maxNozzleTemp;
                        printFilament.mappedMmsFilament.minBedTemp       = tray.minBedTemp;
                        printFilament.mappedMmsFilament.maxBedTemp       = tray.maxBedTemp;
                        printFilament.mappedMmsFilament.status           = tray.status;
                        isMapped                                         = true;
                        break;
                    }
                }
            }
        }

        if (isMapped) {
            continue;
        }
        // not mapped or mapped filament not exist
        // build filament preset map from print filament
        std::map<std::string, std::vector<PresetFilamentInfo>> filamentPresetMap;
        PresetFilamentInfo filamentInfo;
        filamentInfo.filamentId = printFilament.filamentId;
        filamentInfo.settingId = printFilament.settingId;
        filamentInfo.filamentAlias = printFilament.filamentAlias;
        filamentInfo.filamentName = printFilament.filamentName;
        filamentInfo.filamentType = printFilament.filamentType;
        filamentInfo.priority = 1; // default priority for user filament
        std::string standardizedName = standardizeFilamentName(printFilament.filamentAlias, printerNetworkInfo.vendor);
        filamentPresetMap[standardizedName].push_back(filamentInfo);

        PrinterMmsTray mappedTray;
        for (auto& mms : mmsGroup.mmsList) {
            for (auto& tray : mms.trayList) {
                if(!checkTrayIsReady(tray)) {
                    continue;
                }
                // check if the filament color is the same as the standard color
                if(boost::to_upper_copy(getStandardColor(tray.filamentColor).colorHex) != boost::to_upper_copy(filamentStandardColor)){
                    continue;
                }
                mappedTray = tray;
                PresetFilamentInfo matchedPreset = matchFilamentPreset(mappedTray.filamentName, filamentPresetMap, printerNetworkInfo, mappedTray.filamentType);
                if(!matchedPreset.filamentName.empty()) {
                    mappedTray.filamentId = matchedPreset.filamentId;
                    mappedTray.settingId = matchedPreset.settingId;
                    mappedTray.filamentPresetName = matchedPreset.filamentName;
                    isMapped = true;
                    break;
                }
            }
            if(isMapped) break;
        }
        if(isMapped) {
            printFilament.mappedMmsFilament.trayName         = mappedTray.trayName;
            printFilament.mappedMmsFilament.trayId           = mappedTray.trayId;
            printFilament.mappedMmsFilament.mmsId            = mappedTray.mmsId;
            printFilament.mappedMmsFilament.filamentName     = mappedTray.filamentName;
            printFilament.mappedMmsFilament.filamentColor    = mappedTray.filamentColor;
            printFilament.mappedMmsFilament.filamentType     = mappedTray.filamentType;
            printFilament.mappedMmsFilament.filamentDiameter = mappedTray.filamentDiameter;
            printFilament.mappedMmsFilament.minNozzleTemp    = mappedTray.minNozzleTemp;
            printFilament.mappedMmsFilament.maxNozzleTemp    = mappedTray.maxNozzleTemp;
            printFilament.mappedMmsFilament.minBedTemp       = mappedTray.minBedTemp;
            printFilament.mappedMmsFilament.maxBedTemp       = mappedTray.maxBedTemp;
            printFilament.mappedMmsFilament.status           = mappedTray.status;
        }
    }
}

void PrinterMmsManager::saveFilamentMmsMapping(std::vector<PrintFilamentMmsMapping>& printFilamentMmsMapping)
{
    // Load existing mappings first
    nlohmann::json mappingJson = loadFilamentMmsMappingFromFile();
    
    for (auto& printFilament : printFilamentMmsMapping) {
        if (printFilament.mappedMmsFilament.trayName.empty() || 
            printFilament.mappedMmsFilament.trayId.empty() || 
            printFilament.mappedMmsFilament.filamentName.empty() ||
            printFilament.mappedMmsFilament.filamentColor.empty()) {
            continue;
        }
        
        StandardColor mappedStandardColor = getStandardColor(printFilament.mappedMmsFilament.filamentColor);
        std::string mappedFilamentStandardColor = mappedStandardColor.colorHex;
        std::string mappedStandardColorName = mappedStandardColor.colorName;
        
        if(mappedStandardColorName.empty() || mappedFilamentStandardColor.empty()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": color name or filament standard color is empty, filamentType: %s, filamentAlias: %s, filamentColor: %s")
                                                  % printFilament.filamentType % printFilament.filamentAlias % printFilament.filamentColor;
            continue;
        }
        StandardColor standardColor = getStandardColor(printFilament.filamentColor);
        std::string filamentStandardColor = standardColor.colorHex;
        std::string filamentStandardColorName = standardColor.colorName;
        
        if(filamentStandardColorName.empty() || filamentStandardColor.empty()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": color name or filament standard color is empty, filamentType: %s, filamentAlias: %s, filamentColor: %s")
                                                  % printFilament.filamentType % printFilament.filamentAlias % printFilament.filamentColor;
            continue;
        }
        // Create three-level structure: filamentType -> filamentAlias -> filamentStandardColor
        if (!mappingJson.contains(printFilament.filamentType)) {
            mappingJson[printFilament.filamentType] = nlohmann::json::object();
        }
        if (!mappingJson[printFilament.filamentType].contains(printFilament.filamentAlias)) {
            mappingJson[printFilament.filamentType][printFilament.filamentAlias] = nlohmann::json::object();
        }

        // save mapping information, mapped filament info, print filament color converted to standard color 
        // when model multi-color editing, the same standard color will have many color values, unified converted to standard color
        // mapped filament color is the mms filament actual color, but also store the mms filament color corresponding to the standard color
        mappingJson[printFilament.filamentType][printFilament.filamentAlias][filamentStandardColor] = {
            {"mappedFilamentType", printFilament.mappedMmsFilament.filamentType},
            {"mappedFilamentName", printFilament.mappedMmsFilament.filamentName},
            {"mappedFilamentColor", printFilament.mappedMmsFilament.filamentColor},
            {"mappedFilamentStandardColor", mappedFilamentStandardColor},
            {"mappedFilamentStandardColorName", mappedStandardColorName},
            {"filamentStandardColor", filamentStandardColor},
            {"filamentStandardColorName", filamentStandardColorName}
        };
    }
    
    // Save merged mappings to JSON file
    saveFilamentMmsMappingToFile(mappingJson);
}

nlohmann::json PrinterMmsManager::loadFilamentMmsMappingFromFile()
{
    std::lock_guard<std::mutex> lock(mFilamentMmsMappingMutex);
    try {
        
        std::string filePath = (boost::filesystem::path(Slic3r::data_dir()) / "user" / "filament_mms_mapping.json").string();
        
        if (boost::filesystem::exists(filePath)) {
            boost::nowide::ifstream file(filePath);
            if (file.is_open()) {
                nlohmann::json jsonData;
                file >> jsonData;
                file.close();
                return jsonData;
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to load filament MMS mapping file: %s") % e.what();
    }
    
    return nlohmann::json::object();
}

void PrinterMmsManager::saveFilamentMmsMappingToFile(const nlohmann::json& mappingJson)
{
    std::lock_guard<std::mutex> lock(mFilamentMmsMappingMutex);
    try {
        std::string filePath = (boost::filesystem::path(Slic3r::data_dir()) / "user" / "filament_mms_mapping.json").string();
        
        // Ensure directory exists
        boost::filesystem::path dir = boost::filesystem::path(filePath).parent_path();
        if (!boost::filesystem::exists(dir)) {
            boost::filesystem::create_directories(dir);
        }
        
        boost::nowide::ofstream file(filePath);
        if (file.is_open()) {
            file << mappingJson.dump(4); // Pretty print with 4 spaces indentation
            file.close();
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to save filament MMS mapping file: %s") % e.what();
    }
}

void PrinterMmsManager::removeFilamentMmsMapping(const std::string& filamentType, const std::string& filamentAlias, const std::string& filamentColor)
{
    std::lock_guard<std::mutex> lock(mFilamentMmsMappingMutex);
    try {
        // Load existing mappings
        nlohmann::json mappingJson = loadFilamentMmsMappingFromFile();
        
        std::string filamentStandardColor = getStandardColor(filamentColor).colorHex;
        
        // Remove specific mapping if it exists
        if (mappingJson.contains(filamentType) &&
            mappingJson[filamentType].contains(filamentAlias) &&
            mappingJson[filamentType][filamentAlias].contains(filamentStandardColor)) {
            
            mappingJson[filamentType][filamentAlias].erase(filamentStandardColor);
            
            // Clean up empty objects
            if (mappingJson[filamentType][filamentAlias].empty()) {
                mappingJson[filamentType].erase(filamentAlias);
            }
            if (mappingJson[filamentType].empty()) {
                mappingJson.erase(filamentType);
            }
            
            // Save updated mappings
            saveFilamentMmsMappingToFile(mappingJson);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to remove filament MMS mapping: %s") % e.what();
    }
}

} // namespace Slic3r

