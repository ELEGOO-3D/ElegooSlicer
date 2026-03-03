#ifndef slic3r_PrinterNetworkEvent_hpp_
#define slic3r_PrinterNetworkEvent_hpp_

#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <variant>
#include <string>
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "Singleton.hpp"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

namespace Slic3r {

struct PrinterConnectStatusEvent {
    std::string printerId;
    PrinterConnectStatus status;
    std::chrono::system_clock::time_point timestamp;
    PrinterConnectStatusEvent(const std::string& id, const PrinterConnectStatus& s)
        : printerId(id), status(s), timestamp(std::chrono::system_clock::now()) {}
};

struct PrinterStatusEvent {
    std::string printerId;
    PrinterStatus status;
    std::chrono::system_clock::time_point timestamp;
    PrinterStatusEvent(const std::string& id, const PrinterStatus& s)
        : printerId(id), status(s), timestamp(std::chrono::system_clock::now()) {}
};

struct PrinterPrintTaskEvent {
    std::string printerId;
    PrinterPrintTask task;
    std::chrono::system_clock::time_point timestamp;
    PrinterPrintTaskEvent(const std::string& id, const PrinterPrintTask& t)
        : printerId(id), task(t), timestamp(std::chrono::system_clock::now()) {}
};

struct PrinterAttributesEvent {
    std::string printerId;    
    PrinterNetworkInfo printerInfo;
    std::chrono::system_clock::time_point timestamp;
    PrinterAttributesEvent(const std::string& id, const PrinterNetworkInfo& info)
        : printerId(id), printerInfo(info), timestamp(std::chrono::system_clock::now()) {}
};


struct PrinterEventRawEvent {
    std::string printerId;
    std::string event;
    std::chrono::system_clock::time_point timestamp;
    PrinterEventRawEvent(const std::string& id, const std::string& e)
        : printerId(id), event(e), timestamp(std::chrono::system_clock::now()) {}
};

// User network event(iot)
struct UserRtcTokenEvent {
    UserNetworkInfo userInfo;
    std::chrono::system_clock::time_point timestamp;
    UserRtcTokenEvent(const UserNetworkInfo& userInfo)
        : userInfo(userInfo), timestamp(std::chrono::system_clock::now()) {}
};


struct UserRtmMessageEvent {
    std::string printerId;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    UserRtmMessageEvent(const std::string& id, const std::string& msg)
        : printerId(id), message(msg), timestamp(std::chrono::system_clock::now()) {}
};

struct UserLoggedInElsewhereEvent {   
    std::chrono::system_clock::time_point timestamp;
    UserLoggedInElsewhereEvent()
        : timestamp(std::chrono::system_clock::now()) {}
};


struct UserOnlineStatusChangedEvent {
    std::chrono::system_clock::time_point timestamp;
    bool isOnline;
    UserOnlineStatusChangedEvent(const bool& isOnline)
        : isOnline(isOnline), timestamp(std::chrono::system_clock::now()) {}
};

struct UserInfoChangedEvent {   
    std::chrono::system_clock::time_point timestamp;
    UserInfoChangedEvent()
        : timestamp(std::chrono::system_clock::now()) {}
};

using PrinterEvent = std::variant<PrinterConnectStatusEvent, PrinterStatusEvent, PrinterPrintTaskEvent, PrinterAttributesEvent, PrinterEventRawEvent>;
using UserEvent = std::variant<UserRtcTokenEvent, UserRtmMessageEvent, UserLoggedInElsewhereEvent, UserOnlineStatusChangedEvent, UserInfoChangedEvent>;

template<typename EventType>
class EventSignal {
private:
    std::vector<std::function<void(const EventType&)>> mHandlers;
    std::mutex mHandlersMutex;
    
public:
    // register handler
    void connect(std::function<void(const EventType&)> handler) {
        std::lock_guard<std::mutex> lock(mHandlersMutex);
        mHandlers.push_back(std::move(handler));
    }
    
    // disconnect all handlers
    void disconnectAll() {
        std::lock_guard<std::mutex> lock(mHandlersMutex);
        mHandlers.clear();
    }
    
    // emit event (non-blocking)
    void emit(const EventType& event) {
        std::lock_guard<std::mutex> lock(mHandlersMutex);
        for (auto& handler : mHandlers) {
            try {
                handler(event);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": exception in event handler: %s") % e.what();
            }
        }
    }
    
    // get handler count
    size_t handlerCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mHandlersMutex));
        return mHandlers.size();
    }
};

// printer network event system
class PrinterNetworkEvent : public Singleton<PrinterNetworkEvent> {
    friend class Singleton<PrinterNetworkEvent>;
public:
    PrinterNetworkEvent(const PrinterNetworkEvent&) = delete;
    PrinterNetworkEvent& operator=(const PrinterNetworkEvent&) = delete;
    
    // type-safe signal
    EventSignal<PrinterConnectStatusEvent> connectStatusChanged;
    EventSignal<PrinterStatusEvent> statusChanged;
    EventSignal<PrinterPrintTaskEvent> printTaskChanged;
    EventSignal<PrinterAttributesEvent> attributesChanged;
    EventSignal<PrinterEventRawEvent> eventRawChanged;

private:
    PrinterNetworkEvent() = default;
    ~PrinterNetworkEvent() = default;
};


class UserNetworkEvent : public Singleton<UserNetworkEvent> {
    friend class Singleton<UserNetworkEvent>;
public:
    UserNetworkEvent(const UserNetworkEvent&) = delete;
    UserNetworkEvent& operator=(const UserNetworkEvent&) = delete;
    
    EventSignal<UserRtcTokenEvent> rtcTokenChanged;
    EventSignal<UserRtmMessageEvent> rtmMessageChanged;
    EventSignal<UserLoggedInElsewhereEvent> loggedInElsewhereChanged;
    EventSignal<UserOnlineStatusChangedEvent> onlineStatusChanged;
    EventSignal<UserInfoChangedEvent> userInfoChanged;

private:
    UserNetworkEvent() = default;
    ~UserNetworkEvent() = default;
};
} // namespace Slic3r

#endif 