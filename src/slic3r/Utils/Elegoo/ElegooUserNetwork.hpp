#ifndef slic3r_ElegooUserNetwork_hpp_
#define slic3r_ElegooUserNetwork_hpp_

#include "PrinterNetwork.hpp"

namespace Slic3r {
    
class ElegooUserNetwork : public IUserNetwork
{
public:
    ElegooUserNetwork(const UserNetworkInfo& userNetworkInfo);
    ElegooUserNetwork()=delete;
    ElegooUserNetwork(const ElegooUserNetwork&)=delete;
    ElegooUserNetwork& operator=(const ElegooUserNetwork&)=delete;
    virtual ~ElegooUserNetwork();

    virtual PrinterNetworkResult<bool> logout() override;
    virtual PrinterNetworkResult<UserNetworkInfo> connectToIot(const UserNetworkInfo& userInfo) override;
    virtual PrinterNetworkResult<UserNetworkInfo> getRtcToken() override;
    virtual PrinterNetworkResult<std::vector<PrinterNetworkInfo>> getUserBoundPrinters() override;
    virtual PrinterNetworkResult<UserNetworkInfo> refreshToken(const UserNetworkInfo& userInfo) override;
    virtual PrinterNetworkResult<bool> setRegion(const std::string& region, const std::string& iotUrl) override;
    virtual PrinterNetworkResult<PrinterNetworkInfo> bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo) override;
    virtual PrinterNetworkResult<bool>               unbindWANPrinter(const std::string& serialNumber)            override;
    virtual PrinterNetworkResult<std::vector<LicenseExpiredDevice>> getLicenseExpiredDevices() override;
    virtual PrinterNetworkResult<bool>                              renewLicense(const std::string& serialNumber) override;
};

} // namespace Slic3r

#endif
