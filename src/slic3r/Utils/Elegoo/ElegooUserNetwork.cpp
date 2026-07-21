#include "ElegooUserNetwork.hpp"
#include "ElegooLink.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"
#include <boost/log/trivial.hpp>
#include <string>

namespace Slic3r {

namespace {

template<typename T>
PrinterNetworkResult<T> logUserLinkFailure(const char* fn, PrinterNetworkResult<T> result, const std::string& ctx)
{
    if (result.isError()) {
        if (ctx.empty())
            BOOST_LOG_TRIVIAL(warning) << fn << " failed, code=" << static_cast<int>(result.code) << ", message=" << result.message;
        else
            BOOST_LOG_TRIVIAL(warning) << fn << " failed: " << ctx << ", code=" << static_cast<int>(result.code)
                                       << ", message=" << result.message;
    }
    return result;
}

} // namespace

ElegooUserNetwork::ElegooUserNetwork(const UserNetworkInfo& userNetworkInfo) : IUserNetwork(userNetworkInfo) {}

ElegooUserNetwork::~ElegooUserNetwork(){


}

PrinterNetworkResult<UserNetworkInfo> ElegooUserNetwork::connectToIot(const UserNetworkInfo& userInfo)
{
    auto result = ElegooLink::getInstance()->connectToIot(userInfo);
    return logUserLinkFailure(__FUNCTION__, std::move(result), std::string("userId=") + userInfo.userId);
}
PrinterNetworkResult<UserNetworkInfo> ElegooUserNetwork::getRtcToken()
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getRtcToken(),
                             std::string("userId=") + getUserNetworkInfo().userId);
}

PrinterNetworkResult<UserNetworkInfo> ElegooUserNetwork::refreshToken(const UserNetworkInfo& userInfo)
{
    updateUserNetworkInfo(userInfo);
    auto result = ElegooLink::getInstance()->refreshToken(userInfo);
    return logUserLinkFailure(__FUNCTION__, std::move(result), std::string("userId=") + userInfo.userId);
}

PrinterNetworkResult<std::vector<PrinterNetworkInfo>> ElegooUserNetwork::getUserBoundPrinters()
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getUserBoundPrinters(),
                             std::string("userId=") + getUserNetworkInfo().userId);
}

PrinterNetworkResult<bool> ElegooUserNetwork::setRegion(const std::string& region, const std::string& iotUrl)
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->setRegion(region, iotUrl),
                             std::string("userId=") + getUserNetworkInfo().userId + ", region=" + region);
}

PrinterNetworkResult<bool> ElegooUserNetwork::logout()
{
    const UserNetworkInfo userInfo = getUserNetworkInfo();
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->logout(userInfo),
                             std::string("userId=") + userInfo.userId);
}


PrinterNetworkResult<PrinterNetworkInfo> ElegooUserNetwork::bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo)
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->bindWANPrinter(printerNetworkInfo),
                             std::string("userId=") + getUserNetworkInfo().userId + ", serialNumber=" + printerNetworkInfo.serialNumber);
}

PrinterNetworkResult<bool> ElegooUserNetwork::unbindWANPrinter(const std::string& serialNumber)
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->unbindWANPrinter(serialNumber),
                             std::string("userId=") + getUserNetworkInfo().userId + ", serialNumber=" + serialNumber);
}

PrinterNetworkResult<std::vector<LicenseExpiredDevice>> ElegooUserNetwork::getLicenseExpiredDevices()
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getLicenseExpiredDevices(),
                             std::string("userId=") + getUserNetworkInfo().userId);
}

PrinterNetworkResult<bool> ElegooUserNetwork::renewLicense(const std::string& serialNumber)
{
    return logUserLinkFailure(__FUNCTION__, ElegooLink::getInstance()->renewLicense(serialNumber),
                             std::string("userId=") + getUserNetworkInfo().userId + ", serialNumber=" + serialNumber);
}
} // namespace Slic3r 

