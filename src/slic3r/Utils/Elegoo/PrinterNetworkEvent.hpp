#ifndef slic3r_PrinterNetworkEvent_hpp_
#define slic3r_PrinterNetworkEvent_hpp_

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <variant>

#include "libslic3r/PrinterNetworkInfo.hpp"
#include "Singleton.hpp"

namespace Slic3r {

struct PrinterConnectStatusEvent {
    std::string                             printerId;
    PrinterConnectStatus                    status;
    std::chrono::system_clock::time_point   timestamp;
    PrinterConnectStatusEvent(const std::string& id, const PrinterConnectStatus& s);
};

struct PrinterStatusEvent {
    std::string                             printerId;
    PrinterStatus                           status;
    std::chrono::system_clock::time_point   timestamp;
    PrinterStatusEvent(const std::string& id, const PrinterStatus& s);
};

struct PrinterPrintTaskEvent {
    std::string                             printerId;
    PrinterPrintTask                        task;
    std::chrono::system_clock::time_point   timestamp;
    PrinterPrintTaskEvent(const std::string& id, const PrinterPrintTask& t);
};

struct PrinterAttributesEvent {
    std::string                             printerId;
    PrinterNetworkInfo                      printerInfo;
    std::chrono::system_clock::time_point   timestamp;
    PrinterAttributesEvent(const std::string& id, const PrinterNetworkInfo& info);
};

struct PrinterEventRawEvent {
    std::string                             printerId;
    std::string                             event;
    std::chrono::system_clock::time_point   timestamp;
    PrinterEventRawEvent(const std::string& id, const std::string& e);
};

struct PrinterOnlineListChangedEvent {
    std::chrono::system_clock::time_point timestamp;
    PrinterOnlineListChangedEvent();
};

struct UserRtcTokenEvent {
    UserNetworkInfo                         userInfo;
    std::chrono::system_clock::time_point   timestamp;
    explicit UserRtcTokenEvent(const UserNetworkInfo& userInfo);
};

struct UserRtmMessageEvent {
    std::string                             printerId;
    std::string                             message;
    std::chrono::system_clock::time_point   timestamp;
    UserRtmMessageEvent(const std::string& id, const std::string& msg);
};

struct UserLoggedInElsewhereEvent {
    std::chrono::system_clock::time_point timestamp;
    UserLoggedInElsewhereEvent();
};

struct UserOnlineStatusChangedEvent {
    std::chrono::system_clock::time_point timestamp;
    bool                                  isOnline;
    explicit UserOnlineStatusChangedEvent(bool online);
};

struct UserInfoChangedEvent {
    std::chrono::system_clock::time_point timestamp;
    UserInfoChangedEvent();
};

using PrinterEvent = std::variant<PrinterConnectStatusEvent, PrinterStatusEvent, PrinterPrintTaskEvent, PrinterAttributesEvent,
                                  PrinterEventRawEvent, PrinterOnlineListChangedEvent>;
using UserEvent    = std::variant<UserRtcTokenEvent, UserRtmMessageEvent, UserLoggedInElsewhereEvent, UserOnlineStatusChangedEvent,
                                  UserInfoChangedEvent>;

// emit() returns immediately; handlers run on a shared worker thread (PrinterNetworkEvent.cpp). UI code should marshal to the main thread itself if needed.
template<typename EventType>
class EventSignal {
public:
    using HandlerId = uint64_t;
    using Handler   = std::function<void(const EventType&)>;

    HandlerId connect(Handler handler);
    bool      disconnect(HandlerId handlerId);
    void      disconnectAll();
    void      emit(const EventType& event);
    size_t    handlerCount() const;

private:
    std::vector<std::pair<HandlerId, Handler>> mHandlers;
    mutable std::mutex                         mHandlersMutex;
    uint64_t                                   mNextHandlerId{1};
};

class PrinterNetworkEvent : public Singleton<PrinterNetworkEvent> {
    friend class Singleton<PrinterNetworkEvent>;

public:
    PrinterNetworkEvent(const PrinterNetworkEvent&)            = delete;
    PrinterNetworkEvent& operator=(const PrinterNetworkEvent&) = delete;

    EventSignal<PrinterConnectStatusEvent>      connectStatusChanged;
    EventSignal<PrinterStatusEvent>             statusChanged;
    EventSignal<PrinterPrintTaskEvent>          printTaskChanged;
    EventSignal<PrinterAttributesEvent>         attributesChanged;
    EventSignal<PrinterEventRawEvent>           eventRawChanged;
    EventSignal<PrinterOnlineListChangedEvent>  printerOnlineListChanged;

private:
    PrinterNetworkEvent()  = default;
    ~PrinterNetworkEvent() = default;
};

class UserNetworkEvent : public Singleton<UserNetworkEvent> {
    friend class Singleton<UserNetworkEvent>;

public:
    UserNetworkEvent(const UserNetworkEvent&)            = delete;
    UserNetworkEvent& operator=(const UserNetworkEvent&) = delete;

    EventSignal<UserRtcTokenEvent>            rtcTokenChanged;
    EventSignal<UserRtmMessageEvent>          rtmMessageChanged;
    EventSignal<UserLoggedInElsewhereEvent>   loggedInElsewhereChanged;
    EventSignal<UserOnlineStatusChangedEvent> onlineStatusChanged;
    EventSignal<UserInfoChangedEvent>         userInfoChanged;

private:
    UserNetworkEvent()  = default;
    ~UserNetworkEvent() = default;
};

// Clears all handlers on every PrinterNetworkEvent and UserNetworkEvent signal, then blocks until the
// shared emit worker has finished all tasks already queued (including snapshots from before the clear).
// Does not wait for wx CallAfter / GUI work scheduled by handlers.
void disconnectAllPrinterNetworkEvents();

} // namespace Slic3r

#endif
