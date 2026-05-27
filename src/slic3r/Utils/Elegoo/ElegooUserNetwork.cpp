#include "ElegooUserNetwork.hpp"
#include "ElegooLink.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"

namespace Slic3r {

ElegooUserNetwork::ElegooUserNetwork(const UserNetworkInfo& userNetworkInfo) : IUserNetwork(userNetworkInfo) {}

ElegooUserNetwork::~ElegooUserNetwork(){


}

PrinterNetworkResult<UserNetworkInfo> ElegooUserNetwork::connectToIot(const UserNetworkInfo& userInfo)
{
    UserNetworkInfo userNetworkInfo = userInfo;
    auto result = ElegooLink::getInstance()->connectToIot(userInfo);
    return result;
}
PrinterNetworkResult<UserNetworkInfo> ElegooUserNetwork::getRtcToken()
{
    return ElegooLink::getInstance()->getRtcToken();
}

PrinterNetworkResult<UserNetworkInfo> ElegooUserNetwork::refreshToken(const UserNetworkInfo& userInfo)
{
    mUserNetworkInfo = userInfo;
    auto result = ElegooLink::getInstance()->refreshToken(userInfo);
    return result;
}

PrinterNetworkResult<std::vector<PrinterNetworkInfo>> ElegooUserNetwork::getUserBoundPrinters()
{
    return ElegooLink::getInstance()->getUserBoundPrinters();
}

PrinterNetworkResult<bool> ElegooUserNetwork::setRegion(const std::string& region, const std::string& iotUrl)
{
    return ElegooLink::getInstance()->setRegion(region, iotUrl);
}

PrinterNetworkResult<bool> ElegooUserNetwork::logout()
{
    return ElegooLink::getInstance()->logout(mUserNetworkInfo);
}


PrinterNetworkResult<PrinterNetworkInfo> ElegooUserNetwork::bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo)
{
    return ElegooLink::getInstance()->bindWANPrinter(printerNetworkInfo);
}

PrinterNetworkResult<bool> ElegooUserNetwork::unbindWANPrinter(const std::string& serialNumber)
{
    return ElegooLink::getInstance()->unbindWANPrinter(serialNumber);
}

PrinterNetworkResult<std::vector<LicenseExpiredDevice>> ElegooUserNetwork::getLicenseExpiredDevices()
{
    return ElegooLink::getInstance()->getLicenseExpiredDevices();
}

PrinterNetworkResult<bool> ElegooUserNetwork::renewLicense(const std::string& serialNumber)
{
    return ElegooLink::getInstance()->renewLicense(serialNumber);
}
} // namespace Slic3r 

