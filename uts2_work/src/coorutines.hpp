#pragma once

// Подключаем ваш заголовочный файл, где объявлены os::task, os::priority и os::opt
#include "os.h" 

#include <zephyr/kernel.h>
#include <coroutine>
#include <optional>
#include <utility>
#include <cstddef>

namespace os {

// Шаблон неблокирующей задержки на базе k_timer и k_sem
template <typename DispatcherType>
struct DelayAwaiter {
    DispatcherType& dispatcher;
    struct k_sem sem;
    struct k_timer timer;

    DelayAwaiter(DispatcherType& disp, k_timeout_t delay) : dispatcher(disp) {
        k_sem_init(&sem, 0, 1);
        k_timer_init(&timer, [](struct k_timer* t) {
            struct k_sem* s = static_cast<struct k_sem*>(k_timer_user_data_get(t));
            k_sem_give(s);
        }, nullptr);
        k_timer_user_data_set(&timer, &sem);
        k_timer_start(&timer, delay, K_NO_WAIT);
    }

    ~DelayAwaiter() { k_timer_stop(&timer); }

    bool await_ready() { return k_sem_take(&sem, K_NO_WAIT) == 0; }
    void await_suspend(std::coroutine_handle<> h) {
        struct k_poll_event ev;
        // K_POLL_TYPE_SEM_AVAILABLE - стандартный макрос Zephyr для семафоров
        k_poll_event_init(&ev, K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &sem);
        dispatcher.register_event(ev, h);
    }
    void await_resume() { k_sem_take(&sem, K_NO_WAIT); }
};

// Awaiter для очередей сообщений Zephyr (k_msgq)
template <typename MsgType, typename DispatcherType>
struct QueueAwaiter {
    DispatcherType& dispatcher;
    struct k_msgq* msgq;
    MsgType& out_value;

    bool await_ready() { return k_msgq_num_used_get(msgq) > 0; }
    void await_suspend(std::coroutine_handle<> h) {
        struct k_poll_event ev;
        k_poll_event_init(&ev, K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, msgq);
        dispatcher.register_event(ev, h);
    }
    void await_resume() { k_msgq_get(msgq, &out_value, K_NO_WAIT); }
};

// Шаблон планировщика (диспетчера), наследуется от вашего os::task
template <
    int MaxEvents, 
    std::size_t PoolSize, 
    std::size_t StackSize = 1024 + (MaxEvents * sizeof(struct k_poll_event))
>
class Dispatcher : public task<Dispatcher<MaxEvents, PoolSize, StackSize>, StackSize> {
private:
    friend class task<Dispatcher<MaxEvents, PoolSize, StackSize>, StackSize>;

    struct k_heap heap_;
    alignas(alignof(std::max_align_t)) uint8_t heap_mem_[PoolSize];
    
    struct PollRegistration {
        struct k_poll_event event;
        std::coroutine_handle<> handle;
    };
    
    PollRegistration registrations_[MaxEvents];
    volatile int num_registrations_; // volatile против оптимизаций компилятора

    void task_func() {
        // Обычный массив на стеке (БЕЗ static) для безопасности компилятора
        struct k_poll_event poll_events[MaxEvents]; 
        
        while (true) {
            
            if (num_registrations_ == 0) {
                k_msleep(1); 
                continue;
            }

            for (int i = 0; i < num_registrations_; ++i) {
                poll_events[i] = registrations_[i].event;
            }

            k_poll(poll_events, num_registrations_, K_FOREVER); // [4]

            for (int i = 0; i < num_registrations_; ++i) {
                if (poll_events[i].state != K_POLL_STATE_NOT_READY) {
                    auto handle = registrations_[i].handle;

                    for (int j = i; j < num_registrations_ - 1; ++j) {
                        registrations_[j] = registrations_[j + 1];
                    }
                    num_registrations_--;
                    i--;

                    handle.resume();
                    break;
                }
            }
        }
    }

public:
    Dispatcher(const char* name, const priority prio = priority::normal)
        : task<Dispatcher<MaxEvents, PoolSize, StackSize>, StackSize>(name, prio, opt::nostart) 
    {
        num_registrations_ = 0;
        k_heap_init(&heap_, heap_mem_, PoolSize);
    }

    // Запуск потока (вызывается вручную из main)
    void start() {
        k_thread_start(this->tid_);
    }

    // Запрещаем копирование
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void* alloc(std::size_t size) {
        return k_heap_alloc(&heap_, size, K_NO_WAIT);
    }

    void free(void* ptr) {
        k_heap_free(&heap_, ptr);
    }

    void register_event(struct k_poll_event ev, std::coroutine_handle<> h) {
        if (num_registrations_ < MaxEvents) {
            registrations_[num_registrations_++] = {ev, h};
        } else {
            k_panic();
        }
    }

    // Хелперы для лаконичного синтаксиса co_await
    auto delay(k_timeout_t t) {
        return DelayAwaiter<Dispatcher<MaxEvents, PoolSize, StackSize>>(*this, t);
    }

    template <typename MsgType>
    auto wait_queue(struct k_msgq* msgq, MsgType& out_val) {
        return QueueAwaiter<MsgType, Dispatcher<MaxEvents, PoolSize, StackSize>>(*this, msgq, out_val);
    }
};

// Шаблон Task<T, DispatcherType> для вложенных корутин
template <typename T, typename DispatcherType>
struct [[nodiscard]] Task {
    struct promise_type {
        std::coroutine_handle<> awaiting_coroutine;
        
        // Переименовали переменную из return_value в value_, чтобы избежать конфликта:
        std::optional<T> value_; 

        Task<T, DispatcherType> get_return_object() {
            return Task<T, DispatcherType>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().awaiting_coroutine) {
                    return h.promise().awaiting_coroutine;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        // Метод компилятора сохраняет значение в нашу переименованную переменную value_
        void return_value(T val) { value_.emplace(std::move(val)); }
        
        void unhandled_exception() { k_panic(); }

        template <typename... Args>
        void* operator new(std::size_t size, DispatcherType& disp, Args&&...) {
            constexpr std::size_t header_size = alignof(std::max_align_t);
            void* raw = disp.alloc(size + header_size);
            if (!raw) k_panic();
            *reinterpret_cast<DispatcherType**>(raw) = &disp;
            return static_cast<char*>(raw) + header_size;
        }

        void operator delete(void* ptr) {
            constexpr std::size_t header_size = alignof(std::max_align_t);
            void* raw = static_cast<char*>(ptr) - header_size;
            DispatcherType* disp = *reinterpret_cast<DispatcherType**>(raw);
            disp->free(raw);
        }
    };

    std::coroutine_handle<promise_type> handle;

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    ~Task() { if (handle) handle.destroy(); }

    bool await_ready() { return !handle || handle.done(); }
    void await_suspend(std::coroutine_handle<> awaiting) {
        handle.promise().awaiting_coroutine = awaiting;
        handle.resume();
    }
    
    // При пробуждении забираем значение из нашей переменной value_
    T await_resume() { return std::move(*handle.promise().value_); }
};

// Специализация Task<void, DispatcherType>
template <typename DispatcherType>
struct [[nodiscard]] Task<void, DispatcherType> {
    struct promise_type {
        std::coroutine_handle<> awaiting_coroutine;

        Task<void, DispatcherType> get_return_object() {
            return Task<void, DispatcherType>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().awaiting_coroutine) {
                    return h.promise().awaiting_coroutine;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() {}
        void unhandled_exception() { k_panic(); }

        template <typename... Args>
        void* operator new(std::size_t size, DispatcherType& disp, Args&&...) {
            constexpr std::size_t header_size = alignof(std::max_align_t);
            void* raw = disp.alloc(size + header_size);
            if (!raw) k_panic();
            *reinterpret_cast<DispatcherType**>(raw) = &disp;
            return static_cast<char*>(raw) + header_size;
        }

        void operator delete(void* ptr) {
            constexpr std::size_t header_size = alignof(std::max_align_t);
            void* raw = static_cast<char*>(ptr) - header_size;
            DispatcherType* disp = *reinterpret_cast<DispatcherType**>(raw);
            disp->free(raw);
        }
    };

    std::coroutine_handle<promise_type> handle;

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    ~Task() { if (handle) handle.destroy(); }

    bool await_ready() { return !handle || handle.done(); }
    void await_suspend(std::coroutine_handle<> awaiting) {
        handle.promise().awaiting_coroutine = awaiting;
        handle.resume();
    }
    void await_resume() {}
};

// Шаблон DetachedTask<DispatcherType> для корневых задач
template <typename DispatcherType>
struct DetachedTask {
    struct promise_type {
        DetachedTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { k_panic(); }

        template <typename... Args>
        void* operator new(std::size_t size, DispatcherType& disp, Args&&...) {
            constexpr std::size_t header_size = alignof(std::max_align_t);
            void* raw = disp.alloc(size + header_size);
            if (!raw) k_panic();
            *reinterpret_cast<DispatcherType**>(raw) = &disp;
            return static_cast<char*>(raw) + header_size;
        }

        void operator delete(void* ptr) {
            constexpr std::size_t header_size = alignof(std::max_align_t);
            void* raw = static_cast<char*>(ptr) - header_size;
            DispatcherType* disp = *reinterpret_cast<DispatcherType**>(raw);
            disp->free(raw);
        }
    };
};

} // namespace os


