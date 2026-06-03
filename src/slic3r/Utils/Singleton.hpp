#ifndef slic3r_Singleton_hpp_
#define slic3r_Singleton_hpp_

#include <atomic>
#include <mutex>
#include <memory>

namespace Slic3r {

template <typename T>
class Singleton {
public:
    static T* getInstance() {
        // Double-checked locking: fast path avoids mutex on subsequent calls.
        T* instance = s_instance.load(std::memory_order_acquire);
        if (instance != nullptr) {
            return instance;
        }

        std::lock_guard<std::mutex> lock(s_mutex);
        instance = s_instance.load(std::memory_order_relaxed);
        if (instance == nullptr) {
            instance = new T;
            s_instance.store(instance, std::memory_order_release);
        }
        return instance;
    }

    static void destroy() {
        T* old_instance = s_instance.exchange(nullptr);
        if (old_instance) {
            delete old_instance;
        }
    }

    static bool isInstanceExist() {
        return s_instance.load() != nullptr;
    }

protected:
    Singleton() {}
    virtual ~Singleton() {}

private:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

private:
    static std::mutex s_mutex;
    static std::atomic<T*> s_instance;
};

template <typename T>
std::mutex Singleton<T>::s_mutex;

template <typename T>
std::atomic<T*> Singleton<T>::s_instance{nullptr};

} // namespace Slic3r

#endif 