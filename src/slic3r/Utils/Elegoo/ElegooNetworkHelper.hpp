#ifndef slic3r_ElegooNetworkHelper_hpp_ 
#define slic3r_ElegooNetworkHelper_hpp_ 

#include "PrinterNetwork.hpp"
#include "PrintHost.hpp"


namespace Slic3r {

class ElegooNetworkHelper : public INetworkHelper
{
public:
    ElegooNetworkHelper() : INetworkHelper(PrintHostType::htElegooLink) {}
    virtual ~ElegooNetworkHelper() = default;

    std::string getOnlineModelsUrl() override;
    std::string getLoginUrl() override;
    std::string getProfileUpdateUrl() override;
    std::string getAppUpdateUrl() override;
    std::string getPluginUpdateUrl() override;
    std::string getUserAgent() override;
    std::string getIotUrl() override;
    std::string getTelemetryUrl() override;


};

} // namespace 

#endif