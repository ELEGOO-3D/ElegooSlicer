#include "PrinterNetworkEvent.hpp"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

// emit() only pushes here; handlers run on this thread (emit never waits for them).
class AsyncEventDispatch {
public:
    void post(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mQueue.push(std::move(task));
        }
        mCv.notify_one();
    }

    void waitUntilIdle()
    {
        if (std::this_thread::get_id() == mDispatchThreadId)
            return;

        // std::function requires a copyable functor; std::promise is not copyable (MSVC enforces this).
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> fut = done->get_future();
        post([done]() { done->set_value(); });
        fut.wait();
    }

    ~AsyncEventDispatch()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mStop = true;
        }
        mCv.notify_all();
        if (mThread.joinable()) {
            mThread.join();
        }
    }

private:
    void run()
    {
        mDispatchThreadId = std::this_thread::get_id();
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCv.wait(lock, [this] { return mStop || !mQueue.empty(); });
                if (mQueue.empty()) {
                    if (mStop) {
                        return;
                    }
                    continue;
                }
                task = std::move(mQueue.front());
                mQueue.pop();
            }
            if (task) {
                task();
            }
        }
    }

    std::mutex                        mMutex;
    std::condition_variable           mCv;
    std::queue<std::function<void()>> mQueue;
    bool                              mStop{false};
    std::thread::id                   mDispatchThreadId{};
    std::thread                       mThread{[this] { run(); }};
};

static AsyncEventDispatch& async_dispatch()
{
    static AsyncEventDispatch instance;
    return instance;
}

} // namespace

// --- event structs ---

PrinterConnectStatusEvent::PrinterConnectStatusEvent(const std::string& id, const PrinterConnectStatus& s)
    : printerId(id), status(s), timestamp(std::chrono::system_clock::now())
{}

PrinterStatusEvent::PrinterStatusEvent(const std::string& id, const PrinterStatus& s)
    : printerId(id), status(s), timestamp(std::chrono::system_clock::now())
{}

PrinterPrintTaskEvent::PrinterPrintTaskEvent(const std::string& id, const PrinterPrintTask& t)
    : printerId(id), task(t), timestamp(std::chrono::system_clock::now())
{}

PrinterAttributesEvent::PrinterAttributesEvent(const std::string& id, const PrinterNetworkInfo& info)
    : printerId(id), printerInfo(info), timestamp(std::chrono::system_clock::now())
{}

PrinterEventRawEvent::PrinterEventRawEvent(const std::string& id, const std::string& e)
    : printerId(id), event(e), timestamp(std::chrono::system_clock::now())
{}

PrinterOnlineListChangedEvent::PrinterOnlineListChangedEvent()
    : timestamp(std::chrono::system_clock::now())
{}

UserRtcTokenEvent::UserRtcTokenEvent(const UserNetworkInfo& ui)
    : userInfo(ui), timestamp(std::chrono::system_clock::now())
{}

UserRtmMessageEvent::UserRtmMessageEvent(const std::string& id, const std::string& msg)
    : printerId(id), message(msg), timestamp(std::chrono::system_clock::now())
{}

UserLoggedInElsewhereEvent::UserLoggedInElsewhereEvent()
    : timestamp(std::chrono::system_clock::now())
{}

UserOnlineStatusChangedEvent::UserOnlineStatusChangedEvent(bool online)
    : timestamp(std::chrono::system_clock::now()), isOnline(online)
{}

UserInfoChangedEvent::UserInfoChangedEvent()
    : timestamp(std::chrono::system_clock::now())
{}

// --- EventSignal ---

template<typename EventType>
typename EventSignal<EventType>::HandlerId EventSignal<EventType>::connect(Handler handler)
{
    std::lock_guard<std::mutex> lock(mHandlersMutex);
    const HandlerId id = mNextHandlerId++;
    mHandlers.emplace_back(id, std::move(handler));
    return id;
}

template<typename EventType>
bool EventSignal<EventType>::disconnect(HandlerId handlerId)
{
    std::lock_guard<std::mutex> lock(mHandlersMutex);
    const auto oldSize = mHandlers.size();
    mHandlers.erase(std::remove_if(mHandlers.begin(), mHandlers.end(),
                                   [handlerId](const std::pair<HandlerId, Handler>& item) { return item.first == handlerId; }),
                    mHandlers.end());
    return mHandlers.size() != oldSize;
}

template<typename EventType>
void EventSignal<EventType>::disconnectAll()
{
    std::lock_guard<std::mutex> lock(mHandlersMutex);
    mHandlers.clear();
    mNextHandlerId = 1;
}

template<typename EventType>
void EventSignal<EventType>::emit(const EventType& event)
{
    EventType            eventCopy = event;
    std::vector<Handler> handlersSnapshot;
    {
        std::lock_guard<std::mutex> lock(mHandlersMutex);
        handlersSnapshot.reserve(mHandlers.size());
        for (const auto& item : mHandlers) {
            handlersSnapshot.push_back(item.second);
        }
    }
    auto run = [eventCopy = std::move(eventCopy), handlersSnapshot = std::move(handlersSnapshot)]() mutable {
        for (auto& handler : handlersSnapshot) {
            try {
                handler(eventCopy);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "EventSignal::emit handler: " << boost::format("%s") % e.what();
            }
        }
    };
    async_dispatch().post(std::move(run));
}

template<typename EventType>
size_t EventSignal<EventType>::handlerCount() const
{
    std::lock_guard<std::mutex> lock(mHandlersMutex);
    return mHandlers.size();
}

template class EventSignal<PrinterConnectStatusEvent>;
template class EventSignal<PrinterStatusEvent>;
template class EventSignal<PrinterPrintTaskEvent>;
template class EventSignal<PrinterAttributesEvent>;
template class EventSignal<PrinterEventRawEvent>;
template class EventSignal<PrinterOnlineListChangedEvent>;
template class EventSignal<UserRtcTokenEvent>;
template class EventSignal<UserRtmMessageEvent>;
template class EventSignal<UserLoggedInElsewhereEvent>;
template class EventSignal<UserOnlineStatusChangedEvent>;
template class EventSignal<UserInfoChangedEvent>;

void disconnectAllPrinterNetworkEvents()
{
    PrinterNetworkEvent* pne = PrinterNetworkEvent::getInstance();
    pne->connectStatusChanged.disconnectAll();
    pne->statusChanged.disconnectAll();
    pne->printTaskChanged.disconnectAll();
    pne->attributesChanged.disconnectAll();
    pne->eventRawChanged.disconnectAll();
    pne->printerOnlineListChanged.disconnectAll();

    UserNetworkEvent* une = UserNetworkEvent::getInstance();
    une->rtcTokenChanged.disconnectAll();
    une->rtmMessageChanged.disconnectAll();
    une->loggedInElsewhereChanged.disconnectAll();
    une->onlineStatusChanged.disconnectAll();
    une->userInfoChanged.disconnectAll();

    async_dispatch().waitUntilIdle();
}

} // namespace Slic3r
