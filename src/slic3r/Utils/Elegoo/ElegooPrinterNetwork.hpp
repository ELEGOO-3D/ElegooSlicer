#ifndef slic3r_ElegooPrinterNetwork_hpp_
#define slic3r_ElegooPrinterNetwork_hpp_

#include "PrinterNetwork.hpp"

namespace Slic3r {
    
class ElegooPrinterNetwork : public IPrinterNetwork
{
public:
    ElegooPrinterNetwork(const PrinterNetworkInfo& printerNetworkInfo);
    ElegooPrinterNetwork()=delete;
    ElegooPrinterNetwork(const ElegooPrinterNetwork&)=delete;
    ElegooPrinterNetwork& operator=(const ElegooPrinterNetwork&)=delete;
    virtual ~ElegooPrinterNetwork();
    virtual PrinterNetworkResult<PrinterNetworkInfo> connectToPrinter() override;
    virtual PrinterNetworkResult<bool> disconnectFromPrinter() override;
    virtual PrinterNetworkResult<bool> sendPrintTask(const PrinterNetworkParams& params) override;
    virtual PrinterNetworkResult<bool> sendPrintFile(const PrinterNetworkParams& params) override;
    virtual PrinterNetworkResult<std::vector<PrinterNetworkInfo>> discoverPrinters() override;
    virtual PrinterNetworkResult<PrinterMmsGroup> getPrinterMmsInfo() override;
    virtual PrinterNetworkResult<PrinterNetworkInfo> getPrinterAttributes() override;  
    virtual PrinterNetworkResult<PrinterNetworkInfo> getPrinterStatus() override;
    virtual PrinterNetworkResult<std::string> getPrinterStatusRaw() override;
    virtual PrinterNetworkResult<PrinterPrintFileResponse> getFileList(int pageNumber, int pageSize) override;
    virtual PrinterNetworkResult<PrinterPrintTaskResponse> getPrintTaskList(int pageNumber, int pageSize) override;
    virtual PrinterNetworkResult<bool> deletePrintTasks(const std::vector<std::string>& taskIds) override;
    virtual PrinterNetworkResult<bool> sendRtmMessage(const std::string& message) override;
    virtual PrinterNetworkResult<PrinterPrintFileResponse> getFileDetail(const std::string& fileName) override;
    virtual PrinterNetworkResult<bool> updatePrinterName(const std::string& printerName) override;
    virtual PrinterNetworkResult<bool> cancelBindPrinter(const std::string& serialNumber) override;

    static void init(const std::string& region, std::string& iotUrl);
    static void uninit();
};

} // namespace Slic3r

#endif
