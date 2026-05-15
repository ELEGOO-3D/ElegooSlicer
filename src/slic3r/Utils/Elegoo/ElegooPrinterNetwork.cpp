#include "ElegooPrinterNetwork.hpp"
#include "ElegooLink.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"
#include <boost/log/trivial.hpp>
#include <string>

namespace Slic3r {

namespace {

template<typename T>
PrinterNetworkResult<T> logPrinterLinkFailure(const char* fn, PrinterNetworkResult<T> result, const std::string& ctx)
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

ElegooPrinterNetwork::ElegooPrinterNetwork(const PrinterNetworkInfo& printerNetworkInfo) : IPrinterNetwork(printerNetworkInfo) {}

ElegooPrinterNetwork::~ElegooPrinterNetwork(){


}
void ElegooPrinterNetwork::init(const std::string& region, std::string& iotUrl, const std::string& logLevel)
{
    BOOST_LOG_TRIVIAL(info) << "ElegooPrinterNetwork::init";
    ElegooLink::getInstance()->init(region, iotUrl, logLevel);
}

void ElegooPrinterNetwork::uninit()
{
    BOOST_LOG_TRIVIAL(info) << "ElegooPrinterNetwork::uninit";
    ElegooLink::getInstance()->uninit();
}


PrinterNetworkResult<PrinterNetworkInfo> ElegooPrinterNetwork::connectToPrinter()
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->connectToPrinter(mPrinterNetworkInfo),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::disconnectFromPrinter()
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->disconnectFromPrinter(mPrinterNetworkInfo.printerId),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}
PrinterNetworkResult<std::vector<PrinterNetworkInfo>> ElegooPrinterNetwork::discoverPrinters()   
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->discoverPrinters(), {});
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::sendPrintTask(const PrinterNetworkParams& params)
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->sendPrintTask(params),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::sendPrintFile(const PrinterNetworkParams& params)
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->sendPrintFile(params),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterPrintFileResponse> ElegooPrinterNetwork::getFileDetail(const std::string& fileName)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->getFileDetail(mPrinterNetworkInfo.printerId, fileName),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId + ", fileName=" + fileName);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::updatePrinterName(const std::string& printerName)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->updatePrinterName(mPrinterNetworkInfo.printerId, printerName),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}
PrinterNetworkResult<PrinterMmsGroup> ElegooPrinterNetwork::getPrinterMmsInfo()
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getPrinterMmsInfo(mPrinterNetworkInfo.printerId),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterNetworkInfo> ElegooPrinterNetwork::getPrinterAttributes()
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getPrinterAttributes(mPrinterNetworkInfo.printerId),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterNetworkInfo> ElegooPrinterNetwork::getPrinterStatus()
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getPrinterStatus(mPrinterNetworkInfo.printerId),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::refreshPrinterStatus()
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->refreshPrinterStatus(mPrinterNetworkInfo.printerId),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<std::string> ElegooPrinterNetwork::getPrinterStatusRaw()
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->getPrinterStatusRaw(mPrinterNetworkInfo.printerId),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<PrinterPrintFileResponse> ElegooPrinterNetwork::getFileList(int pageNumber, int pageSize)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->getFileList(mPrinterNetworkInfo.printerId, pageNumber, pageSize),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId + ", page=" + std::to_string(pageNumber));
}

PrinterNetworkResult<PrinterPrintTaskResponse> ElegooPrinterNetwork::getPrintTaskList(int pageNumber, int pageSize)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->getPrintTaskList(mPrinterNetworkInfo.printerId, pageNumber, pageSize),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId + ", page=" + std::to_string(pageNumber));
}

PrinterNetworkResult<PrinterExceptionResponse> ElegooPrinterNetwork::getExceptionList(int pageNumber, int pageSize)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->getExceptionList(mPrinterNetworkInfo.printerId, pageNumber, pageSize),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId + ", page=" + std::to_string(pageNumber));
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::deletePrintTasks(const std::vector<std::string>& taskIds)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->deletePrintTasks(mPrinterNetworkInfo.printerId, taskIds),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId + ", taskCount=" + std::to_string(taskIds.size()));
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::sendRtmMessage(const std::string& message)
{
    return logPrinterLinkFailure(__FUNCTION__,
                                 ElegooLink::getInstance()->sendRtmMessage(mPrinterNetworkInfo.printerId, message),
                                 std::string("printerId=") + mPrinterNetworkInfo.printerId);
}

PrinterNetworkResult<bool> ElegooPrinterNetwork::cancelBindPrinter(const std::string& serialNumber)
{
    return logPrinterLinkFailure(__FUNCTION__, ElegooLink::getInstance()->cancelBindPrinter(serialNumber),
                                 std::string("serialNumber=") + serialNumber);
}
} // namespace Slic3r 

