#include "ElegooPrinterNetwork.hpp"
#include "ElegooLink.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"
#include <wx/log.h>

namespace Slic3r {

ElegooPrinterNetwork::ElegooPrinterNetwork(const PrinterNetworkInfo& printerNetworkInfo) : IPrinterNetwork(printerNetworkInfo) {}

ElegooPrinterNetwork::~ElegooPrinterNetwork(){


}
void ElegooPrinterNetwork::init(const std::string& region, std::string& iotUrl)
{
    ElegooLink::getInstance()->init(region, iotUrl);
}

void ElegooPrinterNetwork::uninit()
{
    ElegooLink::getInstance()->uninit();
}


PrinterNetworkResult<PrinterNetworkInfo> ElegooPrinterNetwork::connectToPrinter()
{
    return ElegooLink::getInstance()->connectToPrinter(mPrinterNetworkInfo);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::disconnectFromPrinter()
{
    return ElegooLink::getInstance()->disconnectFromPrinter(mPrinterNetworkInfo.printerId);
}
PrinterNetworkResult<std::vector<PrinterNetworkInfo>> ElegooPrinterNetwork::discoverPrinters()   
{
    return ElegooLink::getInstance()->discoverPrinters();
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::sendPrintTask(const PrinterNetworkParams& params)
{
    return ElegooLink::getInstance()->sendPrintTask(params);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::sendPrintFile(const PrinterNetworkParams& params)
{
    return ElegooLink::getInstance()->sendPrintFile(params);
}

PrinterNetworkResult<PrinterPrintFileResponse> ElegooPrinterNetwork::getFileDetail(const std::string& fileName)
{
    return ElegooLink::getInstance()->getFileDetail(mPrinterNetworkInfo.printerId, fileName);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::updatePrinterName(const std::string& printerName)
{
    return ElegooLink::getInstance()->updatePrinterName(mPrinterNetworkInfo.printerId, printerName);
}
PrinterNetworkResult<PrinterMmsGroup> ElegooPrinterNetwork::getPrinterMmsInfo()
{
    return ElegooLink::getInstance()->getPrinterMmsInfo(mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterNetworkInfo> ElegooPrinterNetwork::getPrinterAttributes()
{
    return ElegooLink::getInstance()->getPrinterAttributes(mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterNetworkInfo> ElegooPrinterNetwork::getPrinterStatus()
{
    return ElegooLink::getInstance()->getPrinterStatus(mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<std::string> ElegooPrinterNetwork::getPrinterStatusRaw()
{
    return ElegooLink::getInstance()->getPrinterStatusRaw(mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterPrintFileResponse> ElegooPrinterNetwork::getFileList(int pageNumber, int pageSize)
{
    return ElegooLink::getInstance()->getFileList(mPrinterNetworkInfo.printerId, pageNumber, pageSize);
}

PrinterNetworkResult<PrinterPrintTaskResponse> ElegooPrinterNetwork::getPrintTaskList(int pageNumber, int pageSize)
{
    return ElegooLink::getInstance()->getPrintTaskList(mPrinterNetworkInfo.printerId, pageNumber, pageSize);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::deletePrintTasks(const std::vector<std::string>& taskIds)
{
    return ElegooLink::getInstance()->deletePrintTasks(mPrinterNetworkInfo.printerId, taskIds);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::sendRtmMessage(const std::string& message)
{
    return ElegooLink::getInstance()->sendRtmMessage(mPrinterNetworkInfo.printerId, message);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::cancelBindPrinter(const std::string& serialNumber)
{
    return ElegooLink::getInstance()->cancelBindPrinter(serialNumber);
}
} // namespace Slic3r 

